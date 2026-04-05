#include "ble.h"
#include "Config.h"

extern bool chargerEnabled;

Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);
int32_t serviceId;
int32_t tVoltId;
int32_t tAmpId;
int32_t cVoltId;
int32_t cAmpId;
int32_t rTime;

// Config characteristics (Read+Write+Notify 0x1A).
// Mobile writes directly; current values broadcast in slow group (every 5s).
// PROPERTIES=0x1A (includes Notify bit 0x10): the Adafruit nRF51822 AT+GATTADDCHAR
// implementation only sets the SoftDevice attribute WRITE permission correctly when
// the Notify bit is present. Without it, mobile writes fail with WRITE_NOT_PERMITTED.
// Mobile never calls monitor() on these chars → no CCCD is ever written → still
// exactly 6 CCCDs total (within nRF51822 S110 limit).
int32_t cfgAmpId;
int32_t cfgPctId;
int32_t cfgMaxTimeId;
int32_t cfgOnOffId;  // 0xFF06 — on/off: 1-byte (0x00=off, 0x01=on)

// Status characteristics (read + notify)
int32_t chargeStateCharId;
int32_t socCharId;
int32_t errorCharId;

// Battery info characteristics (read + notify)
int32_t nominalVoltCharId;
int32_t maxMultCharId;
int32_t minMultCharId;
int32_t absMaxVCharId;
int32_t absMinVCharId;

static bool wasConnected = false;

static uint16_t pendingAmpEcho  = UINT16_MAX;
static uint16_t pendingPctEcho  = UINT16_MAX;
static uint16_t pendingTimeEcho = UINT16_MAX;

// --- Per-value write callbacks ---
// Each reads [hi, lo] from data[] and updates EEPROM via Config::set*().
// No write-back: 0xFF01/02/03 use PROPERTIES=0x1A (Notify bit present for SoftDevice
// write permission). Mobile never subscribes → no CCCD → no notification pipeline.
// Mobile sees the updated value on the next slow broadcast (every 5s).

void bleAmpCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 2) return;
    uint16_t val = ((uint16_t)data[0] << 8) | data[1];
    int prevAmp    = Config::getMaxCurrent();
    int prevTargetV = Config::getTargetVoltage();
    Logger::log(LOG_CAT_BLE, "BLE SET max_current: [%02X %02X] %d -> %d (1/10th A)", data[0], data[1], prevAmp, (int)val);
    Config::setMaxCurrent((int)val);
    Logger::log(LOG_CAT_BLE, "  EEPROM saved. next canWrite: targetV=%d amp=%d  (was: targetV=%d amp=%d)",
                Config::getTargetVoltage(), Config::getMaxCurrent(), prevTargetV, prevAmp);
    pendingAmpEcho = val;
}

void blePctCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 2) return;
    uint16_t val = ((uint16_t)data[0] << 8) | data[1];
    int prevPctX1000 = (int)(Config::getTargetPercentage() * 1000);
    int prevTargetV  = Config::getTargetVoltage();
    Logger::log(LOG_CAT_BLE, "BLE SET target_pct: [%02X %02X] %d -> %d (pct*1000)", data[0], data[1], prevPctX1000, (int)val);
    Config::setTargetPercentage((float)val / 1000.0f);
    Logger::log(LOG_CAT_BLE, "  EEPROM saved. targetV: %d -> %d  (amp unchanged: %d)",
                prevTargetV, Config::getTargetVoltage(), Config::getMaxCurrent());
    pendingPctEcho = val;
}

void bleMaxTimeCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 2) return;
    uint16_t val = ((uint16_t)data[0] << 8) | data[1];
    int prevTime = Config::getMaxChargeTime();
    Logger::log(LOG_CAT_BLE, "BLE SET max_time: [%02X %02X] %d -> %d s (0=no limit)", data[0], data[1], prevTime, (int)val);
    Config::setMaxChargeTime((int)val);
    Logger::log(LOG_CAT_BLE, "  EEPROM saved. timer limit now: %d s", Config::getMaxChargeTime());
    pendingTimeEcho = val;
}


// 0xFF06 — ON/OFF CMD: 1-byte write. 0x00 = charger off, 0x01 = charger on.
// NOTE: chargerEnabled is a runtime bool, NOT persisted to EEPROM.
// Firmware reboots reset it to on regardless of the last BLE command.
// Future improvement: persist to EEPROM so the off state survives resets.
void bleOnOffCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 1) return;
    bool prev = chargerEnabled;
    chargerEnabled = (data[0] != 0);
    Logger::log(LOG_CAT_BLE, "BLE SET charger on/off: %d -> %d", (int)prev, (int)chargerEnabled);
}

void Ble::setup() {

  if ( !ble.begin(VERBOSE_MODE) )
  {
    Logger::log(LOG_CAT_ERR, "Couldn't find Bluefruit, make sure it's in CoMmanD mode & check wiring?");
  }

  /* Perform a factory reset to make sure everything is in a known state */
  Logger::log(LOG_CAT_BLE, "Performing a factory reset: ");
  if (! ble.factoryReset() ){
       Logger::log(LOG_CAT_ERR, "Couldn't factory reset");
  }

  /* Disable command echo from Bluefruit */
  ble.echo(false);

  Logger::log(LOG_CAT_BLE, "Requesting Bluefruit info:");
  /* Print Bluefruit information */
  ble.info();

  /* Change the device name to make it easier to find */
  Logger::log(LOG_CAT_BLE, "Setting device name to %s': ", DISPLAY_NAME);

  if (! ble.sendCommandCheckOK(F("AT+GAPDEVNAME=" DISPLAY_NAME)) ) {
    Logger::log(LOG_CAT_ERR, "Could not set device name?");
  }

  /* Add the Service definition */
  /* Service ID should be 1 */
  // CCCD budget: nRF51822 S110 softdevice supports ~8 CCCDs per connection.
  // We use exactly 6 notify characteristics (6 CCCDs):
  //   cVolt (0x2BED), cAmp (0x2BF0), rTime (0x2BEE),
  //   chargeState (0xFF10), SOC (0xFF11), error (0xFF12)
  // The following 7 characteristics are PROPERTIES=0x02 (Read-only, no CCCD):
  //   tVolt (0x2A1B), tAmp (0x2A1A), nomV (0xFF20), maxMult (0xFF21),
  //   minMult (0xFF22), absMaxV (0xFF23), absMinV (0xFF24)
  // Config chars (0xFF01/02/03) use PROPERTIES=0x1A (Read+Write+Notify). The Notify
  // bit is required for write permission to work on the nRF51822 SoftDevice. Mobile
  // never subscribes → no CCCD written → still 6 CCCDs total.
  // On/off char (0xFF06) likewise uses PROPERTIES=0x1A for the same SoftDevice
  // write-permission reason. Mobile never subscribes → no 7th CCCD.
  Logger::log(LOG_CAT_BLE, "Adding the Service definition (UUID = 0x27B0): ");
  bool success = ble.sendCommandWithIntReply( F("AT+GATTADDSERVICE=UUID=0x27B0"), &serviceId);
  if (! success) {
    Logger::log(LOG_CAT_ERR, "Could not add service");
  }

  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2A1B, PROPERTIES=0x02, MIN_LEN=1, MAX_LEN=5, VALUE=0"), &tVoltId);
  if (! success) {
    Logger::log(LOG_CAT_ERR, "Could not add char1");
  }
  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2A1A, PROPERTIES=0x02, MIN_LEN=1, MAX_LEN=5, VALUE=0"), &tAmpId);
  if (! success) {
    Logger::log(LOG_CAT_ERR, "Could not add char2");
  }
  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2BED, PROPERTIES=0x10, MIN_LEN=1, MAX_LEN=5, VALUE=0"), &cVoltId);
  if (! success) {
    Logger::log(LOG_CAT_ERR, "Could not add char3");
  }
  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2BF0, PROPERTIES=0x10, MIN_LEN=1, MAX_LEN=5, VALUE=0"), &cAmpId);
  if (! success) {
    Logger::log(LOG_CAT_ERR, "Could not add char4");
  }
  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2BEE, PROPERTIES=0x10, MIN_LEN=1, MAX_LEN=10, VALUE=0"), &rTime);
  if (! success) {
    Logger::log(LOG_CAT_ERR, "Could not add char5");
  }

  // Writable config characteristics (Read+Write+Notify, PROPERTIES=0x1A).
  // Mobile writes directly; values broadcast in slow group (every 5s).
  // PROPERTIES=0x1A: Notify bit (0x10) MUST be present for the nRF51822 SoftDevice
  // to set the attribute WRITE permission correctly. Without it (0x0A), mobile writes
  // fail with WRITE_NOT_PERMITTED. Mobile never subscribes to these → no CCCD written
  // → still exactly 6 CCCDs total (within the S110 per-connection limit).
  // Seeded with VALUE=0 here; the seeding block after SW reset populates real values.
  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF01,PROPERTIES=0x1A,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &cfgAmpId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add cfg amp char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF02,PROPERTIES=0x1A,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &cfgPctId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add cfg pct char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF03,PROPERTIES=0x1A,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &cfgMaxTimeId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add cfg max time char");


  // 0xFF06: ON/OFF CMD — 1-byte (0x00=off, 0x01=on). Default: on.
  // PROPERTIES=0x1A (Read+Write+Notify): Notify bit required for SoftDevice write
  // permission. Mobile never subscribes → no CCCD → still 6 CCCDs total.
  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF06,PROPERTIES=0x1A,MIN_LEN=1,MAX_LEN=1,VALUE=01"), &cfgOnOffId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add on/off char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF10,PROPERTIES=0x12,MIN_LEN=1,MAX_LEN=1,VALUE=00"), &chargeStateCharId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add charge state char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF11,PROPERTIES=0x12,MIN_LEN=1,MAX_LEN=1,VALUE=00"), &socCharId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add soc char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF12,PROPERTIES=0x12,MIN_LEN=1,MAX_LEN=2,VALUE=00"), &errorCharId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add error char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF20,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &nominalVoltCharId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add nominal volt char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF21,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=2,VALUE=00"), &maxMultCharId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add max mult char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF22,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=2,VALUE=00"), &minMultCharId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add min mult char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF23,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &absMaxVCharId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add abs max volt char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF24,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &absMinVCharId);
  if (!success) Logger::log(LOG_CAT_ERR, "Could not add abs min volt char");

  ble.sendCommandCheckOK( F("AT+GAPSETADVDATA=02-01-06-05-02-0d-18-0a-18") );

  /* Reset the device for the new service setting changes to take effect */
  Serial.print(F("Performing a SW reset (service changes require a reset): "));
  ble.reset();

  // Register callbacks AFTER reset — ble.reset() sends ATZ which clears the
  // AT+EVENTENABLE settings on the nRF51822. Registering before reset means the
  // chip never signals CONNECT/DISCONNECT/GATT-write events, so bleConnected
  // would stay false permanently.
  ble.setBleGattRxCallback(cfgAmpId,     bleAmpCallback);
  ble.setBleGattRxCallback(cfgPctId,     blePctCallback);
  ble.setBleGattRxCallback(cfgMaxTimeId, bleMaxTimeCallback);
  ble.setBleGattRxCallback(cfgOnOffId,   bleOnOffCallback);  // on/off


  // Seed GATT characteristics with EEPROM values immediately after reset.
  // mobile's readInitialState() fires on connect before the 1-second BLE timer loop
  // runs, so without this seeding it would read 0 for all values.
  // Config::get*() functions are safe here — they check EEPROM validity and fall back
  // to compile-time defaults if EEPROM is uninitialized.
  // Seed GATT characteristics using the same ble.println(val, HEX) / waitForOK() pattern
  // as the BLE loop. Using snprintf + sendCommandCheckOK produces identical AT+GATTCHAR
  // commands on the wire but goes through a different code path; using print/println
  // guarantees byte-for-byte identical encoding so mobile decodeCharValue sees the same
  // ASCII hex format from both seed reads and live notifications.
  Logger::log(LOG_CAT_BLE, "BLE: seeding GATT table from EEPROM");

  ble.print(F("AT+GATTCHAR=")); ble.print(tVoltId);          ble.print(F(",")); ble.println(Config::getTargetVoltage(),                      HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(tAmpId);           ble.print(F(",")); ble.println(Config::getMaxCurrent(),                          HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(nominalVoltCharId); ble.print(F(",")); ble.println((uint16_t)Config::getNominalVoltage(),            HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(maxMultCharId);    ble.print(F(",")); ble.println((uint8_t)(Config::getNominalMaxMultiplier()*100),  HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(minMultCharId);    ble.print(F(",")); ble.println((uint8_t)(Config::getNominalMinMultiplier()*100),  HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(absMaxVCharId);    ble.print(F(",")); ble.println((uint16_t)Config::getMaxVoltage(),                HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(absMinVCharId);    ble.print(F(",")); ble.println((uint16_t)Config::getMinVoltage(),                HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(cfgAmpId);         ble.print(F(",")); ble.println(Config::getMaxCurrent(),                          HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(cfgPctId);         ble.print(F(",")); ble.println((int)(Config::getTargetPercentage()*1000),        HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(cfgMaxTimeId);     ble.print(F(",")); ble.println(Config::getMaxChargeTime(),                       HEX); ble.waitForOK();
  // Seed on/off char (0xFF06) — charger starts on by default
  ble.print(F("AT+GATTCHAR=")); ble.print(cfgOnOffId);       ble.print(F(",")); ble.println(1,                                                HEX); ble.waitForOK();
  
}

void Ble::poll() {
  ble.update(10);  // process incoming GATT write events
  bool connected = ble.isConnected();
  if (wasConnected && !connected) {
    Logger::log(LOG_CAT_BLE, "BLE disconnected, restarting advertising");
    // Use ble.println() not sendCommandCheckOK(): waitForOK() would consume a queued
    // CONNECT event if mobile reconnects quickly, leaving isConnected() returning false
    // permanently. println() is fire-and-forget; the "OK" is consumed by the next
    // ble.update(10) call as an unrecognized event (silently skipped).
    ble.println(F("AT+GAPSTARTADV"));
  }
  wasConnected = connected;
}

void Ble::loop(int tVolt, int tAmp, int cVolt, int cAmp, unsigned long running_time, bool isCharging, int soc, int error_state){
  // Use AT+GAPGETCONN for reliable connection state — avoids the AT+EVENTENABLE
  // mask-replacement problem where registering both connect and disconnect callbacks
  // leaves only one enabled. Also drain pending GATT write events before AT commands.
  bool connected = ble.isConnected();
  ble.update(0);

  Logger::log(LOG_CAT_BLE, "BLE loop: conn=%d cV=%d cA=%d tV=%d isChg=%d on=%d soc=%d err=%d",
              (int)connected, cVolt, cAmp, tVolt, (int)isCharging,
              (int)chargerEnabled, soc, error_state);
  if (!connected) return;

  // Flush any pending write-back echoes set by BLE write callbacks.
  // Done here (not in callbacks) to avoid SPI conflicts inside ble.update().
  if (pendingAmpEcho != UINT16_MAX) {
    ble.print(F("AT+GATTCHAR=")); ble.print(cfgAmpId);     ble.print(F(",")); ble.println(pendingAmpEcho,  HEX); ble.waitForOK();
    pendingAmpEcho  = UINT16_MAX;
  }
  if (pendingPctEcho != UINT16_MAX) {
    ble.print(F("AT+GATTCHAR=")); ble.print(cfgPctId);     ble.print(F(",")); ble.println(pendingPctEcho,  HEX); ble.waitForOK();
    pendingPctEcho  = UINT16_MAX;
  }
  if (pendingTimeEcho != UINT16_MAX) {
    ble.print(F("AT+GATTCHAR=")); ble.print(cfgMaxTimeId); ble.print(F(",")); ble.println(pendingTimeEcho, HEX); ble.waitForOK();
    pendingTimeEcho = UINT16_MAX;
  }

  // loopCount drives the fast/slow split:
  //   Fast group (every call, ~1s): live telemetry + status
  //   Slow group (every 5 calls, ~5s): config & battery-info values
  // NOTE: A CAN config update (handleConfigSetCommand) takes up to 5s to propagate
  // to mobile for config values. Direct BLE writes get immediate write-back confirmation.
  static uint8_t loopCount = 0;
  loopCount++;

  // --- Fast group: live telemetry — every call (1s) ---
  // Guards between each write: if a DISCONNECT event queued up during waitForOK(),
  // ble.update(0) at the top may have missed it. ble.isConnected() issues AT+GAPGETCONN
  // which flushes any pending event and gives us the true connection state.
  // Without these guards, waitForOK() can consume a queued DISCONNECT response and
  // leave bleConnected=true permanently, causing AT+GATTCHAR writes into the void.
  ble.print(F("AT+GATTCHAR=")); ble.print(tVoltId); ble.print(F(",")); ble.println(tVolt, HEX); ble.waitForOK();
  if (!ble.isConnected()) return;
  ble.print(F("AT+GATTCHAR=")); ble.print(tAmpId);  ble.print(F(",")); ble.println(tAmp, HEX);  ble.waitForOK();
  if (!ble.isConnected()) return;
  ble.print(F("AT+GATTCHAR=")); ble.print(cVoltId); ble.print(F(",")); ble.println(cVolt, HEX); ble.waitForOK();
  if (!ble.isConnected()) return;
  ble.print(F("AT+GATTCHAR=")); ble.print(cAmpId);  ble.print(F(",")); ble.println(cAmp, HEX);  ble.waitForOK();
  if (!ble.isConnected()) return;
  ble.print(F("AT+GATTCHAR=")); ble.print(rTime);   ble.print(F(",")); ble.println(running_time, HEX); ble.waitForOK();
  if (!ble.isConnected()) return;

  // Charge state: 0=charger on and charging, 1=charger off (mirrors enableBit in CAN msg 0x1806E5F4)
  uint8_t chargeStateVal = (isCharging && chargerEnabled) ? 0 : 1;

  // SOC percent: rough 25% steps from getSOC() levels 0-4
  uint8_t socPct = (uint8_t)min(100, soc * 25);

  // Error state: low byte of error_state
  uint8_t errVal = (uint8_t)(error_state & 0xFF);

  Logger::log(LOG_CAT_BLE, "BLE chargeState=%d socPct=%d err=%d", (int)chargeStateVal, (int)socPct, (int)errVal);

  ble.print(F("AT+GATTCHAR=")); ble.print(chargeStateCharId); ble.print(F(",")); ble.println(chargeStateVal, HEX); ble.waitForOK();
  if (!ble.isConnected()) return;
  ble.print(F("AT+GATTCHAR=")); ble.print(socCharId);         ble.print(F(",")); ble.println(socPct, HEX);         ble.waitForOK();
  if (!ble.isConnected()) return;
  ble.print(F("AT+GATTCHAR=")); ble.print(errorCharId);       ble.print(F(",")); ble.println(errVal, HEX);         ble.waitForOK();
  if (!ble.isConnected()) return;

  // --- Slow group: config/battery info — every 5 calls (~5s) ---
  if (loopCount % 5 == 0) {
    uint16_t nomV = (uint16_t)Config::getNominalVoltage();
    ble.print(F("AT+GATTCHAR=")); ble.print(nominalVoltCharId); ble.print(F(",")); ble.println(nomV, HEX); ble.waitForOK();

    uint8_t maxMult = (uint8_t)(Config::getNominalMaxMultiplier() * 100);
    ble.print(F("AT+GATTCHAR=")); ble.print(maxMultCharId); ble.print(F(",")); ble.println(maxMult, HEX); ble.waitForOK();

    uint8_t minMult = (uint8_t)(Config::getNominalMinMultiplier() * 100);
    ble.print(F("AT+GATTCHAR=")); ble.print(minMultCharId); ble.print(F(",")); ble.println(minMult, HEX); ble.waitForOK();

    uint16_t absMaxV = (uint16_t)Config::getMaxVoltage();
    ble.print(F("AT+GATTCHAR=")); ble.print(absMaxVCharId); ble.print(F(",")); ble.println(absMaxV, HEX); ble.waitForOK();

    uint16_t absMinV = (uint16_t)Config::getMinVoltage();
    ble.print(F("AT+GATTCHAR=")); ble.print(absMinVCharId); ble.print(F(",")); ble.println(absMinV, HEX); ble.waitForOK();

    int maxCurrent = Config::getMaxCurrent();
    ble.print(F("AT+GATTCHAR=")); ble.print(cfgAmpId);     ble.print(F(",")); ble.println(maxCurrent, HEX); ble.waitForOK();

    int targetPct  = (int)(Config::getTargetPercentage() * 1000);
    ble.print(F("AT+GATTCHAR=")); ble.print(cfgPctId);     ble.print(F(",")); ble.println(targetPct, HEX);  ble.waitForOK();

    int maxTime    = Config::getMaxChargeTime();
    ble.print(F("AT+GATTCHAR=")); ble.print(cfgMaxTimeId); ble.print(F(",")); ble.println(maxTime, HEX);    ble.waitForOK();
  }
}
