#ifndef BLE_H_
#define BLE_H_

#include <Arduino.h>
#include <SPI.h>
#include "Adafruit_BLE.h"
#include "Adafruit_BluefruitLE_SPI.h"
#include "BluefruitConfig.h"
#include "Logger.h"
#include "config.h"

#if SOFTWARE_SERIAL_AVAILABLE
  #include <SoftwareSerial.h>
#endif

extern int32_t minMultCharId;
extern int32_t absMaxVCharId;
extern int32_t absMinVCharId;

// Per-value BLE write callbacks (registered in Ble::setup)
void bleAmpCallback(int32_t chars_id, uint8_t data[], uint16_t len);
void blePctCallback(int32_t chars_id, uint8_t data[], uint16_t len);
void bleMaxTimeCallback(int32_t chars_id, uint8_t data[], uint16_t len);
// 0xFF06 — on/off: 1-byte write (0x00 = off, 0x01 = on)
void bleOnOffCallback(int32_t chars_id, uint8_t data[], uint16_t len);

class Ble {
public:
    void setup();
    void poll();  // call every loop() iteration to process incoming BLE writes immediately
    void loop(int tVolt, int tAmp, int cVolt, int cAmp, unsigned long running_time, bool isCharging, int soc, int error_state);
private:
};

#endif /* BLE_H_ */

