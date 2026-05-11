// ota.cpp — Charger OTA state machine. See ota.h for the protocol contract.
//
// Why no SHA256 verification on the firmware side: implementing sha256 here
// adds code size and complexity for limited safety upside — Update's internal
// CRC catches transmission corruption, and the dual-bank rollback catches a
// bricked image (the new app must call esp_ota_mark_app_valid_cancel_rollback
// inside ota::verify() within one boot, or the bootloader rolls back). Mobile
// is responsible for hashing the binary before transfer; we trust the channel.

#include "ota.h"

#include <Arduino.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ble.h"
#include "Logger.h"

// isCharging is owned by main.cpp (set inside canWrite() based on checkTimer()).
// Pair it with chargerEnabled — together they drive the Elcon TCC frame's
// enableBit (see main.cpp: enableBit = (isCharging && chargerEnabled) ? 0x00 : 0x01).
extern bool isCharging;

namespace ota {

namespace {

constexpr uint32_t kMaxImageSize = 3 * 1024 * 1024;  // matches ota_0/ota_1 slot in partitions.csv

State    g_state = State::IDLE;
uint32_t g_total_size = 0;
uint32_t g_bytes_received = 0;
uint32_t g_chunk_count_in_window = 0;
uint8_t  g_expected_sha256[32] = {0};

// Phase 5 force-disable: remember whether the user had charging enabled when
// OTA started, so abort() can restore it. Untouched by end() — the reboot
// blows away RAM state and the new firmware boots with whatever its default
// chargerEnabled value is.
static bool s_ota_saved_charger_enabled = false;

// Stale-transfer watchdog: last time writeChunk() ran. Used by tickWatchdog()
// to abort a half-stalled session that left chargerEnabled force-disabled.
static uint32_t s_ota_last_chunk_millis = 0;
constexpr uint32_t kWatchdogTimeoutMs = 10000;

// Persisted across reboots — set in end() right before ESP.restart(), cleared
// in verify(). Used by logBootStatus() to log whether the post-reboot run is
// a pending OTA image awaiting verification. The bootloader rollback safety
// net does not depend on this flag — it depends on
// esp_ota_mark_app_valid_cancel_rollback() being called or NOT called.
constexpr const char* kNvsNamespace = "ota";
constexpr const char* kNvsKeyPending = "pending";

void resetSession() {
    g_state = State::IDLE;
    g_total_size = 0;
    g_bytes_received = 0;
    g_chunk_count_in_window = 0;
    memset(g_expected_sha256, 0, sizeof(g_expected_sha256));
}

void notify(uint8_t code, uint32_t bytes) {
    notifyOtaStatus(code, bytes);
}

void setOtaPendingFlag(bool value) {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/false)) {
        Logger::log(LOG_CAT_ERR, "OTA: NVS begin (rw) failed for ota_pending");
        return;
    }
    prefs.putBool(kNvsKeyPending, value);
    prefs.end();
}

bool readOtaPendingFlag() {
    Preferences prefs;
    if (!prefs.begin(kNvsNamespace, /*readOnly=*/true)) {
        // Namespace doesn't exist yet — that's fine, treat as not pending.
        return false;
    }
    bool v = prefs.getBool(kNvsKeyPending, false);
    prefs.end();
    return v;
}

}  // namespace

State currentState() { return g_state; }

void begin(const uint8_t* payload, size_t len) {
    Logger::log(LOG_CAT_BLE, "OTA BEGIN: payload_len=%u state=%d (isChg=%d on=%d)",
                (unsigned)len, (int)g_state,
                (int)isCharging, (int)chargerEnabled);

    if (len != 36) {
        Logger::log(LOG_CAT_ERR, "OTA: bad payload length %u (want 36)", (unsigned)len);
        notify(STATUS_ERR_BAD_PAYLOAD, 0);
        return;
    }

    // If a previous session was in flight, abandon it before starting a new one.
    // abort() restores any previously saved chargerEnabled before we overwrite it
    // with the current value below.
    if (g_state == State::RECEIVING || g_state == State::READY) {
        Logger::log(LOG_CAT_BLE, "OTA: dropping previous session before new BEGIN");
        Update.abort();
        chargerEnabled = s_ota_saved_charger_enabled;
        resetSession();
    }

    // Parse: 4-byte LE total_size, 32 bytes sha256.
    uint32_t total = (uint32_t)payload[0]
                   | ((uint32_t)payload[1] << 8)
                   | ((uint32_t)payload[2] << 16)
                   | ((uint32_t)payload[3] << 24);

    if (total == 0 || total > kMaxImageSize) {
        Logger::log(LOG_CAT_ERR, "OTA: rejected total_size=%u (max=%u)",
                    (unsigned)total, (unsigned)kMaxImageSize);
        notify(STATUS_ERR_BEGIN_FAILED, 0);
        return;
    }

    // Phase 5 force-disable: instead of rejecting on busy, save the user's
    // chargerEnabled, force it false, and wait long enough for at least one
    // 1 Hz TCC frame (tcc_send_interval = 1000 ms in config.h) to ship with the
    // enable bit cleared. The Elcon's comm-timeout is 5 s, so 1.5 s is well
    // within margin. We sleep BEFORE Update.begin() because flash writes are
    // the unsafe part — once the TCC frame goes out clean, the Elcon will stop
    // delivering power and we can flash safely.
    s_ota_saved_charger_enabled = chargerEnabled;
    chargerEnabled = false;
    Logger::log(LOG_CAT_BLE, "OTA: force-disabling charger (saved=%d), waiting 1500 ms for TCC frame",
                (int)s_ota_saved_charger_enabled);
    vTaskDelay(pdMS_TO_TICKS(1500));

    g_total_size = total;
    g_bytes_received = 0;
    g_chunk_count_in_window = 0;
    memcpy(g_expected_sha256, payload + 4, sizeof(g_expected_sha256));

    // Update.begin() picks the inactive ota_X slot via esp_ota_get_next_update_partition.
    if (!Update.begin(g_total_size, U_FLASH)) {
        Logger::log(LOG_CAT_ERR, "OTA: Update.begin failed (err=%d, total=%u) — restoring chargerEnabled",
                    (int)Update.getError(), (unsigned)g_total_size);
        // Critical: we already force-disabled. If Update.begin() failed the OTA
        // never started, so we must restore the user's prior chargerEnabled
        // before notifying — otherwise a failed BEGIN leaves the charger
        // force-disabled until reboot.
        chargerEnabled = s_ota_saved_charger_enabled;
        notify(STATUS_ERR_BEGIN_FAILED, 0);
        resetSession();
        return;
    }

    g_state = State::READY;
    // Reset the watchdog so a slow first chunk doesn't trip it instantly.
    s_ota_last_chunk_millis = millis();
    Logger::log(LOG_CAT_BLE, "OTA: READY — expecting %u bytes, ack window=%d chunks",
                (unsigned)g_total_size, OTA_ACK_WINDOW_CHUNKS);
    notify(STATUS_READY, 0);
}

void writeChunk(const uint8_t* data, size_t len) {
    if (len == 0) return;

    if (g_state != State::READY && g_state != State::RECEIVING) {
        // Stray chunk before BEGIN or after END/ABORT — silently drop.
        // Don't notify; chunk arrival is high-frequency and we don't want to
        // amplify any state mismatch into a notify storm.
        return;
    }

    g_state = State::RECEIVING;
    s_ota_last_chunk_millis = millis();

    size_t written = Update.write(const_cast<uint8_t*>(data), len);
    if (written != len) {
        Logger::log(LOG_CAT_ERR, "OTA: Update.write short (%u of %u, err=%d) at offset=%u — restoring chargerEnabled",
                    (unsigned)written, (unsigned)len, (int)Update.getError(),
                    (unsigned)g_bytes_received);
        notify(STATUS_ERR_WRITE_FAILED, g_bytes_received);
        Update.abort();
        // Restore chargerEnabled — the OTA failed mid-flight, give the user
        // back whatever charging state they had before BEGIN.
        chargerEnabled = s_ota_saved_charger_enabled;
        resetSession();
        return;
    }

    g_bytes_received += (uint32_t)len;
    g_chunk_count_in_window++;

    // Emit ACK when a full window has been received OR when the transfer is
    // complete. Without the transfer-complete branch, a partial final window
    // (e.g. 14.6 chunks at the tail of a 663344-byte image with 244-byte
    // chunks) would never be acknowledged and mobile would time out waiting
    // for the last ACK before sending END.
    bool window_full = (g_chunk_count_in_window >= OTA_ACK_WINDOW_CHUNKS);
    bool transfer_complete = (g_bytes_received >= g_total_size);

    if (window_full || transfer_complete) {
        g_chunk_count_in_window = 0;
        Logger::log(LOG_CAT_BLE, "OTA: ACK at %u / %u%s",
                    (unsigned)g_bytes_received, (unsigned)g_total_size,
                    transfer_complete ? " (final)" : "");
        notify(STATUS_ACK, g_bytes_received);
    }
}

void end() {
    Logger::log(LOG_CAT_BLE, "OTA END: state=%d bytes=%u total=%u",
                (int)g_state, (unsigned)g_bytes_received, (unsigned)g_total_size);

    if (g_state != State::RECEIVING && g_state != State::READY) {
        Logger::log(LOG_CAT_ERR, "OTA: END in unexpected state %d", (int)g_state);
        notify(STATUS_ERR_END_FAILED, g_bytes_received);
        return;
    }

    if (g_bytes_received != g_total_size) {
        Logger::log(LOG_CAT_ERR, "OTA: size mismatch — got %u, expected %u — restoring chargerEnabled",
                    (unsigned)g_bytes_received, (unsigned)g_total_size);
        Update.abort();
        // OTA never committed; restore the saved chargerEnabled.
        chargerEnabled = s_ota_saved_charger_enabled;
        notify(STATUS_ERR_SIZE_MISMATCH, g_bytes_received);
        resetSession();
        return;
    }

    g_state = State::COMMITTING;
    notify(STATUS_COMMITTING, g_bytes_received);

    // true = set the new partition as the boot partition. Update.end() returns
    // true on full success (CRC valid, partition marked).
    if (!Update.end(true)) {
        Logger::log(LOG_CAT_ERR, "OTA: Update.end failed (err=%d) — restoring chargerEnabled",
                    (int)Update.getError());
        // OTA never committed; restore the saved chargerEnabled.
        chargerEnabled = s_ota_saved_charger_enabled;
        notify(STATUS_ERR_END_FAILED, g_bytes_received);
        resetSession();
        return;
    }

    setOtaPendingFlag(true);

    Logger::log(LOG_CAT_SYS, "OTA: image committed, rebooting in 100ms");
    notify(STATUS_REBOOTING, g_bytes_received);

    g_state = State::REBOOTING;

    // Give NimBLE a moment to push the REBOOTING notification before we vanish.
    delay(100);
    ESP.restart();
}

void abort() {
    Logger::log(LOG_CAT_BLE, "OTA ABORT: state=%d bytes=%u",
                (int)g_state, (unsigned)g_bytes_received);
    bool wasActive = (g_state == State::RECEIVING || g_state == State::READY);
    if (wasActive) {
        Update.abort();
    }
    // Restore the saved chargerEnabled. Only meaningful if we were actively
    // running an OTA (otherwise s_ota_saved_charger_enabled is stale/default
    // and chargerEnabled is whatever the user has set since). If the state
    // was already IDLE, this abort() is a no-op call from a redundant cmd=12
    // or a watchdog fire after another path already cleaned up — leave
    // chargerEnabled alone in that case.
    if (wasActive) {
        chargerEnabled = s_ota_saved_charger_enabled;
        Logger::log(LOG_CAT_BLE, "OTA: restored chargerEnabled=%d", (int)chargerEnabled);
    }
    resetSession();
    notify(STATUS_ABORTED, 0);
}

void tickWatchdog() {
    // Only fires while we're actively receiving chunks. READY is intentionally
    // excluded — it's a transient state right after begin(); the first chunk
    // sets s_ota_last_chunk_millis again. If chunks never arrive, the user can
    // disconnect (which auto-aborts) or send cmd=12.
    if (g_state != State::RECEIVING) return;
    uint32_t now = millis();
    if ((now - s_ota_last_chunk_millis) > kWatchdogTimeoutMs) {
        Logger::log(LOG_CAT_ERR, "OTA: watchdog — no chunk for >%u ms, aborting",
                    (unsigned)kWatchdogTimeoutMs);
        abort();
    }
}

void verify() {
    if (!readOtaPendingFlag()) {
        Logger::log(LOG_CAT_BLE, "OTA VERIFY: no pending flag — no-op");
        notify(STATUS_NOT_PENDING, 0);
        return;
    }

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err != ESP_OK) {
        // Rare. If this fails the bootloader will roll back on the next boot,
        // which is the safe outcome. Surface the error to mobile so the user
        // knows verification didn't take.
        Logger::log(LOG_CAT_ERR, "OTA: mark_app_valid_cancel_rollback err=%d", (int)err);
        notify(STATUS_ERR_END_FAILED, 0);
        return;
    }

    setOtaPendingFlag(false);
    Logger::log(LOG_CAT_SYS, "OTA: image verified — rollback cancelled");
    notify(STATUS_VERIFIED, 0);
}

void logBootStatus() {
    bool pending = readOtaPendingFlag();
    const esp_partition_t* running = esp_ota_get_running_partition();
    Logger::log(LOG_CAT_SYS, "OTA boot: running=%s pending_flag=%d",
                running ? running->label : "?", (int)pending);
    if (pending) {
        Logger::log(LOG_CAT_SYS, "OTA: booted into pending image — awaiting verify() from mobile");
    }
}

}  // namespace ota
