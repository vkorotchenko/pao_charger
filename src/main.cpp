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
  }
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
}

void send_ble_info(){
  bt->loop(Config::getTargetVoltage(), Config::getMaxCurrent(), ceil(pv_voltage * 10), ceil(pv_current * 10), running_time );
}

void loop()
{
  timer.run();

	serialConsole->loop();
  led->loop(error_state, getSOC());
  canRead();
}
