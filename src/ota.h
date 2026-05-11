// ota.h — Over-the-air firmware update state machine for the charger.
//
// Phase 5 of the charger OTA plan. Mobile drives the protocol; firmware
// receives chunks on 0xFF26 (WRITE_NR), notifies status on 0xFF27, and
// honours commands 10/11/12/13 on 0xFF05.
//
// SAFETY GATES (firmware-side):
//   1. OTA_BEGIN never rejects a busy charger. It saves chargerEnabled, force-
//      disables it, waits ~1.5 s for at least one TCC frame to go out with the
//      enable bit cleared, then proceeds with Update.begin(). On any abort
//      path (Update.begin failure, cmd=12, BLE disconnect, stale-transfer
//      watchdog) the saved chargerEnabled value is restored. While OTA is
//      not IDLE, BLE writes to 0xFF06 (charger on/off) are silently rejected
//      so the user can't defeat the force-disable mid-flash.
//   2. Update.end(true) is called only after bytes_received == total_size.
//   3. esp_ota_mark_app_valid_cancel_rollback() is called only from verify(),
//      which is mobile-driven. If verify() never fires, the bootloader rolls
//      back to the previous image automatically on the next reboot.
//
// SHA256: mobile verifies the binary before transfer; firmware does NOT
// re-verify the hash. The 32 hash bytes from OTA_BEGIN are kept for logging
// only. Image integrity comes from Update's internal CRC + dual-bank rollback.
#ifndef OTA_H_
#define OTA_H_

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

namespace ota {

// 0xFF27 status codes — mirror what the mobile side expects on the wire.
enum StatusCode : uint8_t {
    STATUS_IDLE              = 0x00,
    STATUS_READY             = 0x01,
    STATUS_ACK               = 0x02,
    STATUS_COMMITTING        = 0x03,
    STATUS_REBOOTING         = 0x04,
    STATUS_VERIFIED          = 0x05,
    STATUS_ERR_BUSY          = 0x10,  // reserved for protocol stability — no longer emitted (Phase 5 force-disable replaces busy rejection)
    STATUS_ERR_BEGIN_FAILED  = 0x11,
    STATUS_ERR_WRITE_FAILED  = 0x12,
    STATUS_ERR_SIZE_MISMATCH = 0x13,
    STATUS_ERR_END_FAILED    = 0x14,
    STATUS_ERR_BAD_PAYLOAD   = 0x15,
    STATUS_ABORTED           = 0x16,
    STATUS_NOT_PENDING       = 0x17,
};

enum class State {
    IDLE,
    READY,
    RECEIVING,
    COMMITTING,
    REBOOTING,
};

// One ACK every N chunks. Tunable. 16 keeps the BLE write queue from
// overflowing with the default ESP32 NimBLE buffer depth at MTU 517.
#ifndef OTA_ACK_WINDOW_CHUNKS
#define OTA_ACK_WINDOW_CHUNKS 16
#endif

// Called from cmd dispatcher (cmd=10). Validates payload (36 bytes), saves the
// current chargerEnabled value and force-disables it, waits ~1.5 s for at least
// one TCC frame to ship with the enable bit cleared, then calls Update.begin().
// Notifies on 0xFF27. If Update.begin() fails the saved chargerEnabled value is
// restored before emitting ERR_BEGIN_FAILED.
void begin(const uint8_t* payload, size_t len);

// Called from 0xFF26 write callback for each chunk. Calls Update.write().
// Emits an ACK every OTA_ACK_WINDOW_CHUNKS.
void writeChunk(const uint8_t* data, size_t len);

// Called from cmd dispatcher (cmd=11). Verifies size, Update.end(true),
// sets NVS ota_pending, notifies REBOOTING, calls ESP.restart().
void end();

// Called from cmd dispatcher (cmd=12), the BLE disconnect callback, and the
// stale-transfer watchdog. Update.abort(), restore the saved chargerEnabled
// value (so we never leave the charger force-disabled), notify ABORTED.
void abort();

// Called from main loop. If currentState() == RECEIVING and no chunks have
// arrived for >10 s, calls abort() to free Update state and restore
// chargerEnabled. Cheap to call; no-op outside RECEIVING.
void tickWatchdog();

// Called from cmd dispatcher (cmd=13). If the NVS ota_pending flag is set,
// calls esp_ota_mark_app_valid_cancel_rollback() and clears the flag.
// Notifies VERIFIED (or NOT_PENDING).
void verify();

// Called once at boot, after BLE init. Logs whether we booted into a pending
// image — purely informational. verify() is what actually commits or rolls
// back; this function never mutates state.
void logBootStatus();

// Manual NVS-based rollback. Called as the very first action in setup() after
// Serial.begin(), BEFORE any subsystem (Config, Led, Ble) that could panic.
//
// WHY: Arduino-ESP32's Update.end(true) marks the new partition VALID directly
// (it does not put it in PENDING_VERIFY), so the IDF bootloader's automatic
// rollback never fires. Instead we count boot attempts of a pending image in
// a dedicated NVS namespace ("ota_recovery") and, after 3 failed attempts,
// swap the boot partition back to the image that was running when the OTA
// was committed.
//
// Contract:
//   - Idempotent. Safe to call exactly once per boot. No-op if no OTA pending.
//   - May call ESP.restart() and never return (after a rollback swap).
//   - Touches its own NVS namespace only — does not interact with Config NVS
//     or the "ota" namespace used by setOtaPendingFlag().
//
// Lifecycle:
//   end()    -> writes pending=1, attempts=0, prev_part=<running subtype>
//   boot 1   -> attempts becomes 1, proceed (may panic and reboot)
//   boot 2   -> attempts becomes 2, proceed
//   boot 3   -> attempts becomes 3, swap to prev_part, clear state, reboot
//   boot 4   -> in safe partition, no state, no-op
//   verify() -> clears all keys; future boots are no-ops
void checkBootRecovery();

// Current state — used by ble.cpp::loop's logging if helpful.
State currentState();

}  // namespace ota

#endif  // OTA_H_
