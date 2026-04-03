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
int32_t cfgCmdId;  // 0xFF05 — start/stop only (cmd=4)

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

static bool bleConnected = false;
static bool needsRestartAdv = false;

static uint16_t pendingAmpEcho  = UINT16_MAX;
static uint16_t pendingPctEcho  = UINT16_MAX;
static uint16_t pendingTimeEcho = UINT16_MAX;

void bleConnectCallback(void) {
    bleConnected = true;
    Logger::log("BLE CONNECT: bleConnected=true");
    Logger::log("BLE connected");
}

void bleDisconnectCallback(void) {
    if (!bleConnected) return;  // duplicate event — already handled
    bleConnected = false;
    needsRestartAdv = true;
    Logger::log("BLE DISCONNECT: bleConnected=false");
    Logger::log("BLE disconnected");
}

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
    Logger::log("BLE SET max_current: [%02X %02X] %d -> %d (1/10th A)", data[0], data[1], prevAmp, (int)val);
    Config::setMaxCurrent((int)val);
    Logger::log("  EEPROM saved. next canWrite: targetV=%d amp=%d  (was: targetV=%d amp=%d)",
                Config::getTargetVoltage(), Config::getMaxCurrent(), prevTargetV, prevAmp);
    pendingAmpEcho = val;
}

void blePctCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 2) return;
    uint16_t val = ((uint16_t)data[0] << 8) | data[1];
    int prevPctX1000 = (int)(Config::getTargetPercentage() * 1000);
    int prevTargetV  = Config::getTargetVoltage();
    Logger::log("BLE SET target_pct: [%02X %02X] %d -> %d (pct*1000)", data[0], data[1], prevPctX1000, (int)val);
    Config::setTargetPercentage((float)val / 1000.0f);
    Logger::log("  EEPROM saved. targetV: %d -> %d  (amp unchanged: %d)",
                prevTargetV, Config::getTargetVoltage(), Config::getMaxCurrent());
    pendingPctEcho = val;
}

void bleMaxTimeCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 2) return;
    uint16_t val = ((uint16_t)data[0] << 8) | data[1];
    int prevTime = Config::getMaxChargeTime();
    Logger::log("BLE SET max_time: [%02X %02X] %d -> %d s (0=no limit)", data[0], data[1], prevTime, (int)val);
    Config::setMaxChargeTime((int)val);
    Logger::log("  EEPROM saved. timer limit now: %d s", Config::getMaxChargeTime());
    pendingTimeEcho = val;
}

// 0xFF05 — ENABLE_CMD: start/stop only (cmd=4).
// All config writes (cmds 1-3) now go directly to 0xFF01/02/03 — send them
// there or they will be ignored here.
void bleCmdCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 4) return;
    uint8_t  cmd = data[0];
    uint16_t val = ((uint16_t)data[2] << 8) | data[3];
    Logger::log("BLE CMD: cmd=%d val=%d", (int)cmd, (int)val);
    switch (cmd) {
        case 4:
            chargerEnabled = (val != 0);
            // NOTE: chargerEnabled is a runtime bool, NOT persisted to EEPROM.
            // If firmware reboots it resets to true regardless of the last BLE command.
            // Future improvement: persist to EEPROM so the stopped state survives resets.
            Logger::log("BLE cmd: charger %s", chargerEnabled ? "enabled" : "stopped");
            break;
        case 5:
            Config::resetToDefaults();
            Logger::log("BLE cmd: EEPROM reset to defaults");
            break;
        default:
            Logger::log("BLE cmd: unknown cmd %d (use direct char writes 0xFF01/02/03 for config)", (int)cmd);
            break;
    }
}

void Ble::setup() {

  if ( !ble.begin(VERBOSE_MODE) )
  {
    Logger::log("Couldn't find Bluefruit, make sure it's in CoMmanD mode & check wiring?");
  }

  /* Perform a factory reset to make sure everything is in a known state */
  Logger::log("Performing a factory reset: ");
  if (! ble.factoryReset() ){
       Logger::log("Couldn't factory reset");
  }

  /* Disable command echo from Bluefruit */
  ble.echo(false);

  Logger::log("Requesting Bluefruit info:");
  /* Print Bluefruit information */
  ble.info();

  /* Change the device name to make it easier to find */
  Logger::log("Setting device name to %s': ", DISPLAY_NAME);

  if (! ble.sendCommandCheckOK(F("AT+GAPDEVNAME=" DISPLAY_NAME)) ) {
    Logger::log("Could not set device name?");
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
  Logger::log("Adding the Service definition (UUID = 0x27B0): ");
  bool success = ble.sendCommandWithIntReply( F("AT+GATTADDSERVICE=UUID=0x27B0"), &serviceId);
  if (! success) {
    Logger::log("Could not add service");
  }

  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2A1B, PROPERTIES=0x02, MIN_LEN=1, MAX_LEN=5, VALUE=0"), &tVoltId);
  if (! success) {
    Logger::log("Could not add char1");
  }
  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2A1A, PROPERTIES=0x02, MIN_LEN=1, MAX_LEN=5, VALUE=0"), &tAmpId);
  if (! success) {
    Logger::log("Could not add char2");
  }
  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2BED, PROPERTIES=0x10, MIN_LEN=1, MAX_LEN=5, VALUE=0"), &cVoltId);
  if (! success) {
    Logger::log("Could not add char3");
  }
  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2BF0, PROPERTIES=0x10, MIN_LEN=1, MAX_LEN=5, VALUE=0"), &cAmpId);
  if (! success) {
    Logger::log("Could not add char4");
  }
  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2BEE, PROPERTIES=0x10, MIN_LEN=1, MAX_LEN=10, VALUE=0"), &rTime);
  if (! success) {
    Logger::log("Could not add char5");
  }

  // Writable config characteristics (Read+Write+Notify, PROPERTIES=0x1A).
  // Mobile writes directly; values broadcast in slow group (every 5s).
  // PROPERTIES=0x1A: Notify bit (0x10) MUST be present for the nRF51822 SoftDevice
  // to set the attribute WRITE permission correctly. Without it (0x0A), mobile writes
  // fail with WRITE_NOT_PERMITTED. Mobile never subscribes to these → no CCCD written
  // → still exactly 6 CCCDs total (within the S110 per-connection limit).
  // Seeded with VALUE=0 here; the seeding block after SW reset populates real values.
  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF01,PROPERTIES=0x1A,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &cfgAmpId);
  if (!success) Logger::log("Could not add cfg amp char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF02,PROPERTIES=0x1A,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &cfgPctId);
  if (!success) Logger::log("Could not add cfg pct char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF03,PROPERTIES=0x1A,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &cfgMaxTimeId);
  if (!success) Logger::log("Could not add cfg max time char");

  // 0xFF05: ENABLE_CMD — start/stop only (cmd=4), 4-byte [cmd, 0, hi, lo]
  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF05,PROPERTIES=0x0A,MIN_LEN=4,MAX_LEN=4,VALUE=00-00-00-00"), &cfgCmdId);
  if (!success) Logger::log("Could not add cfg cmd char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF10,PROPERTIES=0x12,MIN_LEN=1,MAX_LEN=1,VALUE=00"), &chargeStateCharId);
  if (!success) Logger::log("Could not add charge state char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF11,PROPERTIES=0x12,MIN_LEN=1,MAX_LEN=1,VALUE=00"), &socCharId);
  if (!success) Logger::log("Could not add soc char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF12,PROPERTIES=0x12,MIN_LEN=1,MAX_LEN=1,VALUE=00"), &errorCharId);
  if (!success) Logger::log("Could not add error char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF20,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &nominalVoltCharId);
  if (!success) Logger::log("Could not add nominal volt char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF21,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=1,VALUE=00"), &maxMultCharId);
  if (!success) Logger::log("Could not add max mult char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF22,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=1,VALUE=00"), &minMultCharId);
  if (!success) Logger::log("Could not add min mult char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF23,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &absMaxVCharId);
  if (!success) Logger::log("Could not add abs max volt char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF24,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=5,VALUE=0"), &absMinVCharId);
  if (!success) Logger::log("Could not add abs min volt char");

  // Register per-value write callbacks for direct config characteristics
  ble.setBleGattRxCallback(cfgAmpId,     bleAmpCallback);
  ble.setBleGattRxCallback(cfgPctId,     blePctCallback);
  ble.setBleGattRxCallback(cfgMaxTimeId, bleMaxTimeCallback);
  ble.setBleGattRxCallback(cfgCmdId,     bleCmdCallback);  // start/stop only

  ble.setConnectCallback(bleConnectCallback);
  ble.setDisconnectCallback(bleDisconnectCallback);

  ble.sendCommandCheckOK( F("AT+GAPSETADVDATA=02-01-06-05-02-0d-18-0a-18") );

  /* Reset the device for the new service setting changes to take effect */
  Serial.print(F("Performing a SW reset (service changes require a reset): "));
  ble.reset();

  // Seed GATT characteristics with EEPROM values immediately after reset.
  // mobile's readInitialState() fires on connect before the 1-second BLE timer loop
  // runs, so without this seeding it would read 0 for all values.
  // Config::get*() functions are safe here — they check EEPROM validity and fall back
  // to compile-time defaults if EEPROM is uninitialized.
  Logger::log("BLE: seeding GATT table from EEPROM");
  {
    char initBuf[50];

    int tVoltVal = Config::getTargetVoltage();
    snprintf(initBuf, sizeof(initBuf), "AT+GATTCHAR=%d,%X", (int)tVoltId, tVoltVal);
    ble.sendCommandCheckOK(initBuf);

    int tAmpVal = Config::getMaxCurrent();
    snprintf(initBuf, sizeof(initBuf), "AT+GATTCHAR=%d,%X", (int)tAmpId, tAmpVal);
    ble.sendCommandCheckOK(initBuf);

    uint16_t nomV = (uint16_t)Config::getNominalVoltage();
    snprintf(initBuf, sizeof(initBuf), "AT+GATTCHAR=%d,%X", (int)nominalVoltCharId, (int)nomV);
    ble.sendCommandCheckOK(initBuf);

    uint8_t maxMultVal = (uint8_t)(Config::getNominalMaxMultiplier() * 100);
    snprintf(initBuf, sizeof(initBuf), "AT+GATTCHAR=%d,%X", (int)maxMultCharId, maxMultVal);
    ble.sendCommandCheckOK(initBuf);

    uint8_t minMultVal = (uint8_t)(Config::getNominalMinMultiplier() * 100);
    snprintf(initBuf, sizeof(initBuf), "AT+GATTCHAR=%d,%X", (int)minMultCharId, minMultVal);
    ble.sendCommandCheckOK(initBuf);

    uint16_t absMaxV = (uint16_t)Config::getMaxVoltage();
    snprintf(initBuf, sizeof(initBuf), "AT+GATTCHAR=%d,%X", (int)absMaxVCharId, (int)absMaxV);
    ble.sendCommandCheckOK(initBuf);

    uint16_t absMinV = (uint16_t)Config::getMinVoltage();
    snprintf(initBuf, sizeof(initBuf), "AT+GATTCHAR=%d,%X", (int)absMinVCharId, (int)absMinV);
    ble.sendCommandCheckOK(initBuf);

    int cfgAmpVal  = Config::getMaxCurrent();
    snprintf(initBuf, sizeof(initBuf), "AT+GATTCHAR=%d,%X", (int)cfgAmpId, cfgAmpVal);
    ble.sendCommandCheckOK(initBuf);

    int cfgPctVal  = (int)(Config::getTargetPercentage() * 1000);
    snprintf(initBuf, sizeof(initBuf), "AT+GATTCHAR=%d,%X", (int)cfgPctId, cfgPctVal);
    ble.sendCommandCheckOK(initBuf);

    int cfgTimeVal = Config::getMaxChargeTime();
    snprintf(initBuf, sizeof(initBuf), "AT+GATTCHAR=%d,%X", (int)cfgMaxTimeId, cfgTimeVal);
    ble.sendCommandCheckOK(initBuf);
  }
}

void Ble::poll() {
  ble.update(10);  // process incoming BLE writes and connection events first
  // Defer AT+GAPSTARTADV until after ble.update() so any queued CONNECT event
  // is processed before we restart advertising. If mobile reconnected quickly,
  // bleConnectCallback will have set bleConnected=true above — skip the restart.
  if (needsRestartAdv && !bleConnected) {
    needsRestartAdv = false;
    Logger::log("BLE restarting advertising");
    // Use ble.println() instead of sendCommandCheckOK() here.
    // sendCommandCheckOK() calls waitForOK() which reads SPI bytes until it sees "OK".
    // If mobile reconnects quickly, the nRF51822 queues a CONNECT event on SPI before
    // the "OK" for AT+GAPSTARTADV. waitForOK() would consume and discard that CONNECT
    // event, leaving bleConnected=false permanently despite the hardware being connected.
    // ble.println() sends the command fire-and-forget; the "OK" is consumed by the next
    // ble.update(10) call as an unrecognized event (silently skipped), and any CONNECT
    // event arriving after is correctly dispatched to bleConnectCallback.
    ble.println(F("AT+GAPSTARTADV"));
  } else if (needsRestartAdv) {
    needsRestartAdv = false;  // mobile already reconnected — no restart needed
  }
}

void Ble::loop(int tVolt, int tAmp, int cVolt, int cAmp, unsigned long running_time, bool isCharging, int soc, int error_state){
  Logger::log("BLE loop: conn=%d cV=%d cA=%d tV=%d isChg=%d enabled=%d soc=%d err=%d",
              (int)bleConnected, cVolt, cAmp, tVolt, (int)isCharging,
              (int)chargerEnabled, soc, error_state);
  if (!bleConnected) return;

  // Drain any pending BLE events (WRITE callbacks, etc.) before AT commands block them.
  // waitForOK() consumes events; process them first so bleAmpCallback etc. fire correctly.
  ble.update(0);
  Logger::log("BLE drained events");

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
  ble.print(F("AT+GATTCHAR=")); ble.print(tVoltId); ble.print(F(",")); ble.println(tVolt, HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(tAmpId);  ble.print(F(",")); ble.println(tAmp, HEX);  ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(cVoltId); ble.print(F(",")); ble.println(cVolt, HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(cAmpId);  ble.print(F(",")); ble.println(cAmp, HEX);  ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(rTime);   ble.print(F(",")); ble.println(running_time, HEX); ble.waitForOK();

  // Charge state: 0=STOPPED, 1=ENABLED_IDLE, 2=CHARGING, 3=COMPLETE
  uint8_t chargeStateVal;
  if (!chargerEnabled)         { chargeStateVal = 0; }  // STOPPED
  else if (!isCharging)        { chargeStateVal = 1; }  // ENABLED_IDLE
  else if (soc >= 4)           { chargeStateVal = 3; }  // COMPLETE
  else                         { chargeStateVal = 2; }  // CHARGING

  // SOC percent: rough 25% steps from getSOC() levels 0-4
  uint8_t socPct = (uint8_t)min(100, soc * 25);

  // Error state: low byte of error_state
  uint8_t errVal = (uint8_t)(error_state & 0xFF);

  Logger::log("BLE chargeState=%d socPct=%d err=%d", (int)chargeStateVal, (int)socPct, (int)errVal);

  ble.print(F("AT+GATTCHAR=")); ble.print(chargeStateCharId); ble.print(F(",")); ble.println(chargeStateVal, HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(socCharId);         ble.print(F(",")); ble.println(socPct, HEX);         ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(errorCharId);       ble.print(F(",")); ble.println(errVal, HEX);         ble.waitForOK();

  // Guard: if mobile disconnected during the fast group sends (e.g. a CONNECT event was
  // queued and will be picked up on the next poll()), exit now before the slow group
  // fires 5+ more waitForOK() calls that would consume the queued CONNECT event.
  if (!bleConnected) return;

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
