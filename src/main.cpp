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
  if ( pv_voltage < Config::getTargetVoltage() * MID_CHARGE_MULTIPLIER) {
    return 1;
  }
  if ( pv_voltage < Config::getNominalVoltage() * FULL_CHARGE_MULTIPLIER) {
    return 2;
  }
  if ( pv_voltage >= Config::getTargetVoltage() * FULL_CHARGE_MULTIPLIER  * .90  && ( pv_voltage < Config::getTargetVoltage() * FULL_CHARGE_MULTIPLIER  * .98)) {
    return 3;
  }
  if ( pv_voltage >= Config::getTargetVoltage() * FULL_CHARGE_MULTIPLIER  * .98) {
    return 4;
  }
  return 0;
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
  switch (cmd) {
    case CAN_CMD_SET_MAX_TIME:
      Config::setMaxChargeTime((int)val);
      Logger::log("CAN config: max charge time -> %d s", (int)val);
      break;
    case CAN_CMD_SET_TARGET_PCT:
      Config::setTargetPercentage((float)val / 1000.0f);
      Logger::log("CAN config: target pct -> %d/1000", (int)val);
      break;
    case CAN_CMD_SET_TARGET_AMPS:
      Config::setMaxCurrent((int)val);
      Logger::log("CAN config: target amps -> %d (1/10th A)", (int)val);
      break;
    case CAN_CMD_SET_NOMINAL_VOLTAGE:
      Config::setNominalVoltage((int)val);
      Logger::log("CAN config: nominal voltage -> %d (1/10th V)", (int)val);
      break;
    case CAN_CMD_SET_NOMINAL_MAX_MULT:
      Config::setNominalMaxMultiplier((int)val);
      Logger::log("CAN config: nominal max multiplier -> %d/100", (int)val);
      break;
    case CAN_CMD_SET_NOMINAL_MIN_MULT:
      Config::setNominalMinMultiplier((int)val);
      Logger::log("CAN config: nominal min multiplier -> %d/100", (int)val);
      break;
    case CAN_CMD_SET_AUTO_NOMINAL:
      Config::setAutoNominalFromCan(val != 0);
      Logger::log("CAN config: auto nominal from CAN -> %d", (int)val);
      break;
    default:
      Logger::log("CAN config: unknown cmd %d", (int)cmd);
      break;
  }
}

void canRead()
{
  if (CAN_MSGAVAIL == CAN.checkReceive())
  {
    CAN.readMsgBuf(&len, buf);
    unsigned long receiveId = CAN.getCanId();

    Logger::log("== INCOMING CAN -- id: %d");

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
        if (buf[4] & B00000001) Logger::log("Error: hardware failure");
        if (buf[4] & B00000010) Logger::log("Error: overheating");
        if (buf[4] & B00000100) Logger::log("Error: input voltage wrong");
        if (buf[4] & B00001000) Logger::log("Error: charger off (reverse polarity protection)");
        if (buf[4] & B00010000) Logger::log("Error: communication timeout");
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
      Logger::log("Auto nominal: set from DMOC 0x650 -> %d (1/10th V)", (int)rawVoltage);
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
  char enableBit = isCharging ? 0x00 :0x01;

  unsigned char data[length] = {highByte(Config::getTargetVoltage()), lowByte(Config::getTargetVoltage()), highByte(Config::getMaxCurrent()), lowByte(Config::getMaxCurrent()), enableBit, 0x00, 0x00, 0x00};


  Logger::logOutgoingMsg(tcc_incoming_can_id ,ext, length, Config::getTargetVoltage(), Config::getMaxCurrent());
  byte sendStatus = CAN.MCP_CAN::sendMsgBuf(tcc_incoming_can_id, ext, length, data);

  if (sendStatus == CAN_OK)
  { // Status byte for transmission
    Logger::log("canWrite result: CAN message sent successfully to charger");
    error_state = 0;
  }
  else
  {
    Logger::log("canWrite result: Error during message transmission to charger error: %s", sendStatus);
    error_state = B00010000;
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
    Logger::log("waiting for CAN to intialize");
    delay(200);
  }

  charge_start_time = millis();

  Logger::log("CAN initialization successful");
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
