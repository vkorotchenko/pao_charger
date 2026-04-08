/*
 * Config.h
 */

#ifndef CONFIG_H_
#define CONFIG_H_

/*
PINS
*/

#define GREEN_PIN 26   // A0 on ESP32 Feather V2
#define ORANGE_PIN 25  // A1 on ESP32 Feather V2
#define RED_PIN 32     // GPIO32 (A2/GPIO34 is ADC input-only on ESP32)
#define SPI_CS_PIN 33  // CAN CS (GPIO5 = SCK on ESP32, can't use as CS)
#define CAN_INT_PIN = 6;
/*
 * SERIAL CONFIGURATION
 */
#define SERIAL_SPEED 115200

// CONFIGURATIONS
#define tcc_incoming_can_id 0x1806E5F4
#define tcc_outgoing_can_id 0x18FF50E5
#define tcc_send_interval 1000
#define ble_interval 1000
#define led_reset_interval 1000
#define FULL_CHARGE_MULTIPLIER 1.05
#define MID_CHARGE_MULTIPLIER 1.04

/*
 * HARD CODED PARAMETERS / DEFAULTS
 */

#define CAN_SPEED 16           // can speed in 1000 kbps
#define NOMINAL_VOLTAGE 1600   // nominal voltage for the battery pack in 1/10th of a volt
#define TARGET_PERCENTAGE 0.95 // target charge percentage (0.0–1.0)
#define MAX_AMPS 100           // max amp for the charger in 1/10 of an AMP
#define MAX_CHARGE_TIME 43200  // time in seconds before shutting off, 0 to disable
#define NOMINAL_MAX_MULT_DEFAULT 1.14f  // max voltage = nominal * this multiplier
#define NOMINAL_MIN_MULT_DEFAULT 0.81f  // min voltage = nominal * this multiplier
#define DISPLAY_NAME "Pao Charger"      // BLE advertised name

/*
    CAN CONFIG BROADCAST / SET IDs (runtime configurable, stored in Preferences)
*/
#define CONFIG_BROADCAST_FRAME1_DEFAULT  0x18FFA0E5UL
#define CONFIG_BROADCAST_FRAME2_DEFAULT  0x18FFA1E5UL
#define CHARGER_CONFIG_BROADCAST3_ID     0x18FFA2E5
#define CONFIG_SET_DEFAULT               0x18FF60F4UL
#define config_broadcast_interval        1000

// Config set command IDs
#define CAN_CMD_SET_MAX_TIME         0x01  // value = seconds (uint16)
#define CAN_CMD_SET_TARGET_PCT       0x02  // value = percentage * 1000 (uint16, e.g. 950 = 95.0%)
#define CAN_CMD_SET_TARGET_AMPS      0x03  // value = 1/10th A (uint16)
#define CAN_CMD_SET_NOMINAL_MAX_MULT 0x05  // value = multiplier * 100 (uint16, e.g. 114 = 1.14)
#define CAN_CMD_SET_NOMINAL_MIN_MULT 0x06  // value = multiplier * 100 (uint16, e.g. 81 = 0.81)

#include "Logger.h"

class Config
{
public:
    static float getMaxVoltage();
    static float getMinVoltage();
    static int getNominalVoltage();
    static int getMaxCurrent();
    static int getCanSpeed();
    static float getTargetPercentage();
    static int getMaxChargeTime();
    static int getTargetVoltage();

    static void printAllValues();
    static float getNominalMaxMultiplier();
    static float getNominalMinMultiplier();
    static void setMaxCurrent(int newValue);
    static void setCanSpeed(int newValue);
    static void setTargetPercentage(float newValue);
    static void setMaxChargeTime(int newValue);
    static void setNominalMaxMultiplier(int valueX100);
    static void setNominalMinMultiplier(int valueX100);
    static void resetToDefaults();
    static void init();

    static unsigned long getConfigBroadcast1Id();
    static unsigned long getConfigBroadcast2Id();
    static unsigned long getConfigSetId();
    static void setConfigBroadcast1Id(unsigned long newId);
    static void setConfigBroadcast2Id(unsigned long newId);
    static void setConfigSetId(unsigned long newId);
};

#endif /* CONFIG_H_ */
