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

// Config characteristics (Read+Write 0x0A — no Notify, no CCCD).
// Mobile writes directly; current values broadcast in slow group (every 5s).
// PROPERTIES must stay 0x0A: 0x1A adds CCCDs that overflow the nRF51822 attribute table.
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

void bleConnectCallback(void) {
    bleConnected = true;
    Logger::log("BLE connected");
}

void bleDisconnectCallback(void) {
    if (!bleConnected) return;  // duplicate event — already handled
    bleConnected = false;
    needsRestartAdv = true;
    Logger::log("BLE disconnected");
}

// --- Per-value write callbacks ---
// Each reads [hi, lo] from data[] and updates EEPROM via Config::set*().
// No write-back: 0xFF01/02/03 use PROPERTIES=0x0A (no Notify), so there is
// no CCCD and no AT+GATTCHAR confirm needed. Mobile sees the updated value
// on the next slow broadcast (every 5s).

void bleAmpCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 2) return;
    uint16_t val = ((uint16_t)data[0] << 8) | data[1];
    Config::setMaxCurrent((int)val);
    Logger::log("BLE write: max current -> %d (1/10th A)", (int)val);
}

void blePctCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 2) return;
    uint16_t val = ((uint16_t)data[0] << 8) | data[1];
    Config::setTargetPercentage((float)val / 1000.0f);
    Logger::log("BLE write: target pct -> %d/1000", (int)val);
}

void bleMaxTimeCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 2) return;
    uint16_t val = ((uint16_t)data[0] << 8) | data[1];
    Config::setMaxChargeTime((int)val);
    Logger::log("BLE write: max charge time -> %d s", (int)val);
}

// 0xFF05 — ENABLE_CMD: start/stop only (cmd=4).
// All config writes (cmds 1-3) now go directly to 0xFF01/02/03 — send them
// there or they will be ignored here.
void bleCmdCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 4) return;
    uint8_t  cmd = data[0];
    uint16_t val = ((uint16_t)data[2] << 8) | data[3];
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
  // Config chars (0xFF01/02/03) use PROPERTIES=0x0A (Read+Write, no CCCD).
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

  // Writable config characteristics (Read+Write, PROPERTIES=0x0A — no Notify, no CCCD).
  // Mobile writes directly; values broadcast in slow group (every 5s).
  // MUST stay 0x0A: 0x1A adds CCCDs and overflows the nRF51822 attribute table.
  char cmd[80];
  int maxCurrent = Config::getMaxCurrent();
  int targetPct  = (int)(Config::getTargetPercentage() * 1000);
  int maxTime    = Config::getMaxChargeTime();

  snprintf(cmd, sizeof(cmd), "AT+GATTADDCHAR=UUID=0xFF01,PROPERTIES=0x0A,MIN_LEN=2,MAX_LEN=2,VALUE=0x%02X-0x%02X",
           (maxCurrent >> 8) & 0xFF, maxCurrent & 0xFF);
  success = ble.sendCommandWithIntReply(cmd, &cfgAmpId);
  if (!success) Logger::log("Could not add cfg amp char");

  snprintf(cmd, sizeof(cmd), "AT+GATTADDCHAR=UUID=0xFF02,PROPERTIES=0x0A,MIN_LEN=2,MAX_LEN=2,VALUE=0x%02X-0x%02X",
           (targetPct >> 8) & 0xFF, targetPct & 0xFF);
  success = ble.sendCommandWithIntReply(cmd, &cfgPctId);
  if (!success) Logger::log("Could not add cfg pct char");

  snprintf(cmd, sizeof(cmd), "AT+GATTADDCHAR=UUID=0xFF03,PROPERTIES=0x0A,MIN_LEN=2,MAX_LEN=2,VALUE=0x%02X-0x%02X",
           (maxTime >> 8) & 0xFF, maxTime & 0xFF);
  success = ble.sendCommandWithIntReply(cmd, &cfgMaxTimeId);
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

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF20,PROPERTIES=0x02,MIN_LEN=2,MAX_LEN=2,VALUE=00-00"), &nominalVoltCharId);
  if (!success) Logger::log("Could not add nominal volt char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF21,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=1,VALUE=00"), &maxMultCharId);
  if (!success) Logger::log("Could not add max mult char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF22,PROPERTIES=0x02,MIN_LEN=1,MAX_LEN=1,VALUE=00"), &minMultCharId);
  if (!success) Logger::log("Could not add min mult char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF23,PROPERTIES=0x02,MIN_LEN=2,MAX_LEN=2,VALUE=00-00"), &absMaxVCharId);
  if (!success) Logger::log("Could not add abs max volt char");

  success = ble.sendCommandWithIntReply(F("AT+GATTADDCHAR=UUID=0xFF24,PROPERTIES=0x02,MIN_LEN=2,MAX_LEN=2,VALUE=00-00"), &absMinVCharId);
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
}

void Ble::poll() {
  ble.update(10);  // process incoming BLE writes and connection events first
  // Defer AT+GAPSTARTADV until after ble.update() so any queued CONNECT event
  // is processed before we restart advertising. If mobile reconnected quickly,
  // bleConnectCallback will have set bleConnected=true above — skip the restart.
  if (needsRestartAdv && !bleConnected) {
    needsRestartAdv = false;
    Logger::log("BLE restarting advertising");
    ble.sendCommandCheckOK(F("AT+GAPSTARTADV"));
  } else if (needsRestartAdv) {
    needsRestartAdv = false;  // mobile already reconnected — no restart needed
  }
}

void Ble::loop(int tVolt, int tAmp, int cVolt, int cAmp, unsigned long running_time, bool isCharging, int soc, int error_state){
  if (!bleConnected) return;

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

  // Charge state: 0=NOT_CHARGING, 1=CHARGING, 2=COMPLETE, 3=STOPPED_BY_USER
  uint8_t chargeStateVal = 0;
  if (!chargerEnabled) { chargeStateVal = 3; }
  else if (isCharging)  { chargeStateVal = (soc >= 4) ? 2 : 1; }

  // SOC percent: rough 25% steps from getSOC() levels 0-4
  uint8_t socPct = (uint8_t)min(100, soc * 25);

  // Error state: low byte of error_state
  uint8_t errVal = (uint8_t)(error_state & 0xFF);

  ble.print(F("AT+GATTCHAR=")); ble.print(chargeStateCharId); ble.print(F(",")); ble.println(chargeStateVal, HEX); ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(socCharId);         ble.print(F(",")); ble.println(socPct, HEX);         ble.waitForOK();
  ble.print(F("AT+GATTCHAR=")); ble.print(errorCharId);       ble.print(F(",")); ble.println(errVal, HEX);         ble.waitForOK();

  // --- Slow group: config/battery info — every 5 calls (~5s) ---
  if (loopCount % 5 == 0) {
    char hexBuf[10];

    uint16_t nomV = (uint16_t)Config::getNominalVoltage();
    snprintf(hexBuf, sizeof(hexBuf), "0x%02X-0x%02X", (uint8_t)(nomV >> 8), (uint8_t)(nomV & 0xFF));
    ble.print(F("AT+GATTCHAR=")); ble.print(nominalVoltCharId); ble.print(F(",")); ble.println(hexBuf); ble.waitForOK();

    uint8_t maxMult = (uint8_t)(Config::getNominalMaxMultiplier() * 100);
    ble.print(F("AT+GATTCHAR=")); ble.print(maxMultCharId); ble.print(F(",")); ble.println(maxMult, HEX); ble.waitForOK();

    uint8_t minMult = (uint8_t)(Config::getNominalMinMultiplier() * 100);
    ble.print(F("AT+GATTCHAR=")); ble.print(minMultCharId); ble.print(F(",")); ble.println(minMult, HEX); ble.waitForOK();

    uint16_t absMaxV = (uint16_t)Config::getMaxVoltage();
    snprintf(hexBuf, sizeof(hexBuf), "0x%02X-0x%02X", (uint8_t)(absMaxV >> 8), (uint8_t)(absMaxV & 0xFF));
    ble.print(F("AT+GATTCHAR=")); ble.print(absMaxVCharId); ble.print(F(",")); ble.println(hexBuf); ble.waitForOK();

    uint16_t absMinV = (uint16_t)Config::getMinVoltage();
    snprintf(hexBuf, sizeof(hexBuf), "0x%02X-0x%02X", (uint8_t)(absMinV >> 8), (uint8_t)(absMinV & 0xFF));
    ble.print(F("AT+GATTCHAR=")); ble.print(absMinVCharId); ble.print(F(",")); ble.println(hexBuf); ble.waitForOK();

    char cfgBuf[10];

    int maxCurrent = Config::getMaxCurrent();
    snprintf(cfgBuf, sizeof(cfgBuf), "0x%02X-0x%02X", (uint8_t)(maxCurrent >> 8), (uint8_t)(maxCurrent & 0xFF));
    ble.print(F("AT+GATTCHAR=")); ble.print(cfgAmpId); ble.print(F(",")); ble.println(cfgBuf); ble.waitForOK();

    int targetPct = (int)(Config::getTargetPercentage() * 1000);
    snprintf(cfgBuf, sizeof(cfgBuf), "0x%02X-0x%02X", (uint8_t)(targetPct >> 8), (uint8_t)(targetPct & 0xFF));
    ble.print(F("AT+GATTCHAR=")); ble.print(cfgPctId); ble.print(F(",")); ble.println(cfgBuf); ble.waitForOK();

    int maxTime = Config::getMaxChargeTime();
    snprintf(cfgBuf, sizeof(cfgBuf), "0x%02X-0x%02X", (uint8_t)(maxTime >> 8), (uint8_t)(maxTime & 0xFF));
    ble.print(F("AT+GATTCHAR=")); ble.print(cfgMaxTimeId); ble.print(F(",")); ble.println(cfgBuf); ble.waitForOK();
  }
}
