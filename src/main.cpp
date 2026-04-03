#include <Arduino.h>
#include <SimpleTimer.h>
#include "mcp2515_can.h"
#include "Logger.h"
#include "Config.h"
#include "led.h"
#include "ble.h"
#include "SerialConsole.h"
#include <SPI.h>

#if SOFTWARE_SERIAL_AVAILABLE
  #include <SoftwareSerial.h>
#endif



unsigned char len = 8; // Length of received CAN message of either charger
int length = 8; // Length of received CAN message of either charger
unsigned char buf[8];  // Buffer for data from CAN message of either charger
byte ext = 1;

unsigned char voltamp[8] = {highByte(Config::getTargetVoltage()), lowByte(Config::getTargetVoltage()), highByte(Config::getMaxCurrent()), lowByte(Config::getMaxCurrent()), 0x00, 0x00, 0x00, 0x00};

int error_state = 0;
float pv_voltage;
float pv_current;
unsigned long running_time;
bool isCharging = true;
bool chargerEnabled = true;  // controlled via BLE cmd=4; true=charging allowed, false=user stopped

Led *led;
Ble *bt;

#include "mcp2515_can.h"
mcp2515_can CAN(SPI_CS_PIN); // Set CS pin

SimpleTimer timer;
SerialConsole *serialConsole;

unsigned long charge_start_time;

void send_ble_info();

int getSOC()
{
  float minV    = Config::getMinVoltage()    / 10.0f;
  float targetV = Config::getTargetVoltage() / 10.0f;

  // Guard against degenerate range
  if (targetV <= minV) { return 1; }

  float pct = (pv_voltage - minV) / (targetV - minV) * 100.0f;

  if (pct < 20.0f) { return 1; }
  if (pct < 50.0f) { return 2; }
  if (pct < 90.0f) { return 3; }
  return 4;
}

bool checkTimer(){
  running_time = (millis() - charge_start_time) / 1000;
  if( Config::getMaxChargeTime() == 0) {
    return true;
  }
  if (running_time > Config::getMaxChargeTime()) {
    return false;
  }
  return true;
}

void handleConfigSetCommand(unsigned char *data, unsigned char dataLen) {
  if (dataLen < 4) return;
  uint8_t cmd  = data[0];
  uint16_t val = ((uint16_t)data[2] << 8) | data[3];
  Logger::log(LOG_CAT_CAN, "CAN SET cmd=%d val=%d [%02X %02X %02X %02X]", (int)cmd, (int)val,
              data[0], data[1], data[2], data[3]);
  switch (cmd) {
    case CAN_CMD_SET_MAX_TIME: {
      int prev = Config::getMaxChargeTime();
      Config::setMaxChargeTime((int)val);
      Logger::log(LOG_CAT_CAN, "  max_time: %d -> %d s (0=no limit). EEPROM saved.", prev, Config::getMaxChargeTime());
      break;
    }
    case CAN_CMD_SET_TARGET_PCT: {
      int prevPct  = (int)(Config::getTargetPercentage() * 1000);
      int prevTgtV = Config::getTargetVoltage();
      Config::setTargetPercentage((float)val / 1000.0f);
      Logger::log(LOG_CAT_CAN, "  target_pct: %d -> %d (pct*1000). targetV: %d -> %d. EEPROM saved.",
                  prevPct, (int)val, prevTgtV, Config::getTargetVoltage());
      break;
    }
    case CAN_CMD_SET_TARGET_AMPS: {
      int prevAmp   = Config::getMaxCurrent();
      int prevTgtV  = Config::getTargetVoltage();
      Config::setMaxCurrent((int)val);
      Logger::log(LOG_CAT_CAN, "  max_current: %d -> %d (1/10th A). targetV unchanged: %d. EEPROM saved.",
                  prevAmp, Config::getMaxCurrent(), prevTgtV);
      break;
    }
    case CAN_CMD_SET_NOMINAL_VOLTAGE: {
      int prev     = Config::getNominalVoltage();
      int prevTgtV = Config::getTargetVoltage();
      Config::setNominalVoltage((int)val);
      Logger::log(LOG_CAT_CAN, "  nominal_voltage: %d -> %d (1/10th V). targetV: %d -> %d. EEPROM saved.",
                  prev, Config::getNominalVoltage(), prevTgtV, Config::getTargetVoltage());
      break;
    }
    case CAN_CMD_SET_NOMINAL_MAX_MULT: {
      int prev     = (int)(Config::getNominalMaxMultiplier() * 100);
      int prevTgtV = Config::getTargetVoltage();
      Config::setNominalMaxMultiplier((int)val);
      Logger::log(LOG_CAT_CAN, "  max_mult: %d -> %d (/100). targetV: %d -> %d. EEPROM saved.",
                  prev, (int)val, prevTgtV, Config::getTargetVoltage());
      break;
    }
    case CAN_CMD_SET_NOMINAL_MIN_MULT: {
      int prev = (int)(Config::getNominalMinMultiplier() * 100);
      Config::setNominalMinMultiplier((int)val);
      Logger::log(LOG_CAT_CAN, "  min_mult: %d -> %d (/100). EEPROM saved.", prev, (int)val);
      break;
    }
    case CAN_CMD_SET_AUTO_NOMINAL: {
      bool prev = Config::getAutoNominalFromCan();
      Config::setAutoNominalFromCan(val != 0);
      Logger::log(LOG_CAT_CAN, "  auto_nominal: %d -> %d. EEPROM saved.", (int)prev, (int)(val != 0));
      break;
    }
    default:
      Logger::log(LOG_CAT_CAN, "  unknown cmd %d — ignored", (int)cmd);
      break;
  }
}

void canRead()
{
  if (CAN_MSGAVAIL == CAN.checkReceive())
  {
    CAN.readMsgBuf(&len, buf);
    unsigned long receiveId = CAN.getCanId();

    Logger::log(LOG_CAT_CAN, "== INCOMING CAN -- id: %d", receiveId);

    if (receiveId == tcc_outgoing_can_id)
    {

      pv_current = (((float)buf[2] * 256.0) + ((float)buf[3])) / 10.0; // highByte/lowByte + offset
      pv_voltage = (((float)buf[0] * 256.0) + ((float)buf[1])) / 10.0; // highByte/lowByte + offset

      //Logger::logIncomingMsg(receiveId, ext, length, pv_voltage, pv_current);

      // Check status flags using bitwise AND (multiple bits can be set simultaneously)
      if (buf[4] == 0) {
        error_state = 0;
      } else {
        error_state = buf[4];
        if (buf[4] & B00000001) Logger::log(LOG_CAT_ERR, "Error: hardware failure");
        if (buf[4] & B00000010) Logger::log(LOG_CAT_ERR, "Error: overheating");
        if (buf[4] & B00000100) Logger::log(LOG_CAT_ERR, "Error: input voltage wrong");
        if (buf[4] & B00001000) Logger::log(LOG_CAT_ERR, "Error: charger off (reverse polarity protection)");
        if (buf[4] & B00010000) Logger::log(LOG_CAT_ERR, "Error: communication timeout");
      }
    }
    else if (receiveId == Config::getConfigSetId())
    {
      handleConfigSetCommand(buf, len);
    }
    else if (receiveId == DMOC_BAT_VOLTAGE_ID && Config::getAutoNominalFromCan())
    {
      // Bat_Voltage: bytes 0-1, big-endian, 1/10th V (DMOC 0x650)
      uint16_t rawVoltage = ((uint16_t)buf[0] << 8) | buf[1];
      Config::setNominalVoltage((int)rawVoltage);
      Logger::log(LOG_CAT_CAN, "Auto nominal: set from DMOC 0x650 -> %d (1/10th V)", (int)rawVoltage);
    }
  }
}

void canWriteConfig()
{
  int maxCurrent = Config::getMaxCurrent();
  int nomVoltage = Config::getNominalVoltage();
  int tgtVoltage = Config::getTargetVoltage();
  int maxMult    = (int)(Config::getNominalMaxMultiplier() * 100.0f);

  unsigned char frame1[8] = {
    highByte(maxCurrent), lowByte(maxCurrent),
    highByte(maxMult),    lowByte(maxMult),
    highByte(nomVoltage), lowByte(nomVoltage),
    highByte(tgtVoltage), lowByte(tgtVoltage)
  };
  CAN.MCP_CAN::sendMsgBuf(Config::getConfigBroadcast1Id(), ext, length, frame1);

  uint16_t chgTime = (uint16_t)running_time;
  uint16_t maxTime = (uint16_t)Config::getMaxChargeTime();
  uint8_t  minMult = (uint8_t)(Config::getNominalMinMultiplier() * 100.0f);
  uint8_t  autoNom = Config::getAutoNominalFromCan() ? 1 : 0;
  unsigned char frame2[8] = {
    (uint8_t)(Config::getTargetPercentage() * 100),
    (uint8_t)error_state,
    highByte(chgTime), lowByte(chgTime),
    highByte(maxTime), lowByte(maxTime),
    minMult, autoNom
  };
  CAN.MCP_CAN::sendMsgBuf(Config::getConfigBroadcast2Id(), ext, length, frame2);

  // Frame 3: Absolute voltage limits
  uint8_t frame3[8] = {0};
  uint16_t absMax = (uint16_t)Config::getMaxVoltage();
  uint16_t absMin = (uint16_t)Config::getMinVoltage();
  frame3[0] = highByte(absMax);
  frame3[1] = lowByte(absMax);
  frame3[2] = highByte(absMin);
  frame3[3] = lowByte(absMin);
  CAN.MCP_CAN::sendMsgBuf(CHARGER_CONFIG_BROADCAST3_ID, ext, length, frame3);
}

void canWrite()
{
  isCharging = checkTimer();
  char enableBit = (isCharging && chargerEnabled) ? 0x00 : 0x01;

  // Read config once — same values go into the log and the CAN frame.
  int tgtV = Config::getTargetVoltage();
  int maxA = Config::getMaxCurrent();

  Logger::log(LOG_CAT_CAN, "canWrite -> elcon: targetV=%d maxA=%d enableBit=%d (runtime=%lu s)",
              tgtV, maxA, (int)enableBit, running_time);

  unsigned char data[length] = {highByte(tgtV), lowByte(tgtV), highByte(maxA), lowByte(maxA), enableBit, 0x00, 0x00, 0x00};

  Logger::logOutgoingMsg(tcc_incoming_can_id, ext, length, tgtV, maxA);
  byte sendStatus = CAN.MCP_CAN::sendMsgBuf(tcc_incoming_can_id, ext, length, data);

  if (sendStatus == CAN_OK)
  { // Status byte for transmission
    Logger::log(LOG_CAT_CAN, "canWrite result: CAN message sent successfully to charger");
    error_state &= ~B00010000;  // clear comm-timeout bit only; preserve fault bits from canRead()
  }
  else
  {
    Logger::log(LOG_CAT_ERR, "canWrite result: Error during message transmission to charger error: %d", (int)sendStatus);
    error_state |= B00010000;  // set comm-timeout bit; preserve any existing fault bits
  }
}

void setup()
{
  Serial.begin(SERIAL_SPEED);

  //serialConsole = new SerialConsole();
  led = new Led(GREEN_PIN, ORANGE_PIN, RED_PIN);
  led->setup();

  bt = new Ble();
  bt->setup();

  while (CAN_OK != CAN.begin(Config::getCanSpeed()))
  {
    Logger::log(LOG_CAT_SYS, "waiting for CAN to intialize");
    delay(200);
  }

  charge_start_time = millis();

  Logger::log(LOG_CAT_SYS, "CAN initialization successful");
  timer.setInterval(tcc_send_interval, canWrite);
  timer.setInterval(ble_interval, send_ble_info);
  timer.setInterval(config_broadcast_interval, canWriteConfig);
}

void send_ble_info(){
  bt->loop(Config::getTargetVoltage(), Config::getMaxCurrent(), round(pv_voltage * 10), round(pv_current * 10), running_time, isCharging, getSOC(), error_state);
}

void loop()
{
  bt->poll();      // poll BLE for incoming writes every iteration — ensures bleConfigCallback
                   // fires before canWrite(), not one cycle (1 s) later
  timer.run();

	// serialConsole->loop();
  led->loop(error_state, getSOC());
  canRead();
}
