// ota.h — Over-the-air firmware update state machine for the charger.
//
// Phase 5 of the charger OTA plan. Mobile drives the protocol; firmware
// receives chunks on 0xFF26 (WRITE_NR), notifies status on 0xFF27, and
// honours commands 10/11/12/13 on 0xFF05.
//
// SAFETY GATES (firmware-side):
//   1. OTA_BEGIN is rejected with ERR_BUSY when chargerEnabled == true.
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
    STATUS_ERR_BUSY          = 0x10,
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

// Called from cmd dispatcher (cmd=10). Validates payload (36 bytes), checks
// the chargerEnabled safety gate, and calls Update.begin(). Notifies on 0xFF27.
void begin(const uint8_t* payload, size_t len);

// Called from 0xFF26 write callback for each chunk. Calls Update.write().
// Emits an ACK every OTA_ACK_WINDOW_CHUNKS.
void writeChunk(const uint8_t* data, size_t len);

// Called from cmd dispatcher (cmd=11). Verifies size, Update.end(true),
// sets NVS ota_pending, notifies REBOOTING, calls ESP.restart().
void end();

// Called from cmd dispatcher (cmd=12). Update.abort(), notify ABORTED.
void abort();

// Called from cmd dispatcher (cmd=13). If the NVS ota_pending flag is set,
// calls esp_ota_mark_app_valid_cancel_rollback() and clears the flag.
// Notifies VERIFIED (or NOT_PENDING).
void verify();

// Called once at boot, after BLE init. Logs whether we booted into a pending
// image — purely informational. verify() is what actually commits or rolls
// back; this function never mutates state.
void logBootStatus();

// Current state — used by ble.cpp::loop's logging if helpful.
State currentState();

}  // namespace ota

#endif  // OTA_H_
