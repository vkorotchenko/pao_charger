#include "ble.h"
#include "Config.h"

Adafruit_BluefruitLE_SPI ble(BLUEFRUIT_SPI_CS, BLUEFRUIT_SPI_IRQ, BLUEFRUIT_SPI_RST);
int32_t serviceId;
int32_t tVoltId;
int32_t tAmpId;
int32_t cVoltId;
int32_t cAmpId;
int32_t rTime;

// Writable config characteristics
int32_t cfgAmpId;
int32_t cfgPctId;
int32_t cfgMaxTimeId;

void bleConfigCallback(int32_t chars_id, uint8_t data[], uint16_t len) {
    if (len < 2) return;
    int val = ((int)data[0] << 8) | (int)data[1];

    if (chars_id == cfgAmpId) {
        Config::setMaxCurrent(val);
        Logger::log("BLE config: target amps -> %d (1/10th A)", val);
    } else if (chars_id == cfgPctId) {
        Config::setTargetPercentage((float)val / 1000.0f);
        Logger::log("BLE config: target pct -> %d/1000", val);
    } else if (chars_id == cfgMaxTimeId) {
        Config::setMaxChargeTime(val);
        Logger::log("BLE config: max charge time -> %d s", val);
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
  Logger::log("Adding the Service definition (UUID = 0x27B0): ");
  bool success = ble.sendCommandWithIntReply( F("AT+GATTADDSERVICE=UUID=0x27B0"), &serviceId);
  if (! success) {
    Logger::log("Could not add service");
  }

  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2A1B, PROPERTIES=0x10, MIN_LEN=1, MAX_LEN=5, VALUE=0"), &tVoltId);
  if (! success) {
    Logger::log("Could not add char1");
  }
  success = ble.sendCommandWithIntReply( F("AT+GATTADDCHAR=UUID=0x2A1A, PROPERTIES=0x10, MIN_LEN=1, MAX_LEN=5, VALUE=0"), &tAmpId);
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

  // Writable config characteristics (BLE central can write to update config)
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

  ble.setBleGattRxCallback(cfgAmpId,     bleConfigCallback);
  ble.setBleGattRxCallback(cfgPctId,     bleConfigCallback);
  ble.setBleGattRxCallback(cfgMaxTimeId, bleConfigCallback);

  ble.sendCommandCheckOK( F("AT+GAPSETADVDATA=02-01-06-05-02-0d-18-0a-18") );

  /* Reset the device for the new service setting changes to take effect */
  Serial.print(F("Performing a SW reset (service changes require a reset): "));
  ble.reset();
}

void Ble::loop(int tVolt, int tAmp, int cVolt, int cAmp, unsigned long running_time){
  ble.update(10);  // process incoming BLE writes (fires bleConfigCallback if a central wrote a config char)

  ble.print( F("AT+GATTCHAR=") );
  ble.print( tVoltId );
  ble.print( F(",") );
  ble.println(tVolt, HEX);
  ble.waitForOK();

  ble.print( F("AT+GATTCHAR=") );
  ble.print( tAmpId );
  ble.print( F(",") );
  ble.println(tAmp, HEX);
  ble.waitForOK();

  ble.print( F("AT+GATTCHAR=") );
  ble.print( cVoltId );
  ble.print( F(",") );
  ble.println(cVolt, HEX);
  ble.waitForOK();

  ble.print( F("AT+GATTCHAR=") );
  ble.print( cAmpId );
  ble.print( F(",") );
  ble.println(cAmp, HEX);
  ble.waitForOK();

  ble.print( F("AT+GATTCHAR=") );
  ble.print( rTime );
  ble.print( F(",") );
  ble.println(running_time, HEX);
  ble.waitForOK();
}
