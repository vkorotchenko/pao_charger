#ifndef BLE_H_
#define BLE_H_

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "Logger.h"
#include "Config.h"

extern bool chargerEnabled;

class Ble {
public:
    void setup();
    void poll();  // no-op: NimBLE runs on its own FreeRTOS task
    void loop(int tVolt, int tAmp, int cVolt, int cAmp,
              unsigned long running_time, bool isCharging, int soc, int error_state);
    void seedReadableChars();

private:
    NimBLECharacteristic* pTVolt    = nullptr;  // 0x2A1B  target voltage    READ
    NimBLECharacteristic* pTAmp     = nullptr;  // 0x2A1A  target amps       READ
    NimBLECharacteristic* pCVolt    = nullptr;  // 0x2BED  current voltage   NOTIFY
    NimBLECharacteristic* pCAmp     = nullptr;  // 0x2BF0  current amps      NOTIFY
    NimBLECharacteristic* pRTime    = nullptr;  // 0x2BEE  running time      NOTIFY
    NimBLECharacteristic* pCfgAmp   = nullptr;  // 0xFF01  cfg max current   READ|WRITE|NOTIFY
    NimBLECharacteristic* pCfgPct   = nullptr;  // 0xFF02  cfg target pct    READ|WRITE|NOTIFY
    NimBLECharacteristic* pCfgTime  = nullptr;  // 0xFF03  cfg max time      READ|WRITE|NOTIFY
    NimBLECharacteristic* pOnOff    = nullptr;  // 0xFF06  on/off            READ|WRITE|NOTIFY
    NimBLECharacteristic* pChgState = nullptr;  // 0xFF10  charge state      READ|NOTIFY
    NimBLECharacteristic* pSOC      = nullptr;  // 0xFF11  SOC percent       READ|NOTIFY
    NimBLECharacteristic* pError    = nullptr;  // 0xFF12  error state       READ|NOTIFY
    NimBLECharacteristic* pNomV     = nullptr;  // 0xFF20  nominal voltage   READ
    NimBLECharacteristic* pMaxMult  = nullptr;  // 0xFF21  max multiplier    READ
    NimBLECharacteristic* pMinMult  = nullptr;  // 0xFF22  min multiplier    READ
    NimBLECharacteristic* pAbsMaxV  = nullptr;  // 0xFF23  abs max voltage   READ
    NimBLECharacteristic* pAbsMinV  = nullptr;  // 0xFF24  abs min voltage   READ

    static void setU16(NimBLECharacteristic* c, uint16_t v, bool notify = false);
    static void setU8 (NimBLECharacteristic* c, uint8_t  v, bool notify = false);
    static void setU32(NimBLECharacteristic* c, uint32_t v, bool notify = false);
};

#endif /* BLE_H_ */
