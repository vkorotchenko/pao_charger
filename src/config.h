/*
 * Config.h
 */

#ifndef CONFIG_H_
#define CONFIG_H_

/*
PINS
*/

#define GREEN_PIN A0
#define ORANGE_PIN A1
#define RED_PIN A2
#define SPI_CS_PIN 5 // CS Pins
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

#define FULL_CHARGE_MULTIPLIER 1.046875
#define MID_CHARGE_MULTIPLIER 1.040625
#define NOMINAL_MIN_MULTIPLIER 0.78125
#define NOMINAL_MAX_MULTIPLIER 1.140625

#define DEFAULT_EEPROM_VAL 0xFFFF

/*
 * HARD CODED PARAMETERS
 */

#define CAN_SPEED 16          // can speed in 1000 kbps
#define NOMINAL_VOLTAGE 3200  // nominal volatage for the battery pack in 1/10th of a volt
#define TARGET_PERCENTAGE 0.95 // if we are limiting charging to x percente for battery life protection in thenth of a percent  1 to disable
#define MAX_AMPS 200          // Max amp for the charger in 1/10 of an AMP
#define MAX_CHARGE_TIME 43200   // time in seconds before shutting off, 0 to disable 
#define DISPLAY_NAME "Pao Charger" //ble name

/*
    EEPROM ADDRESSES
*/
#define EEPROM_CAN_SPEED 12
#define EEPROM_NOMINAL_VOLTAGE 14
#define EEPROM_MAX_AMPS 16
#define EEPROM_MAX_CHARGE_TIME 18
#define EEPROM_TARGET_PERCENTAGE 20
// CAN ID EEPROM addresses (4 bytes each, 32-bit IDs)
#define EEPROM_CONFIG_BROADCAST1_ID 22
#define EEPROM_CONFIG_BROADCAST2_ID 26
#define EEPROM_CONFIG_SET_ID        30

/*
    CAN CONFIG BROADCAST / SET IDs (runtime configurable, stored in EEPROM)
*/
#define CONFIG_BROADCAST_FRAME1_DEFAULT  0x18FFA0E5UL
#define CONFIG_BROADCAST_FRAME2_DEFAULT  0x18FFA1E5UL
#define CONFIG_SET_DEFAULT               0x18FF60F4UL
#define config_broadcast_interval        5000

// Config set command IDs
#define CAN_CMD_SET_MAX_TIME    0x01  // value = seconds (uint16)
#define CAN_CMD_SET_TARGET_PCT  0x02  // value = percentage * 1000 (uint16, e.g. 950 = 95.0%)
#define CAN_CMD_SET_TARGET_AMPS 0x03  // value = 1/10th A (uint16)

#include <FlashAsEEPROM.h>
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
    static void setNominalVoltage(int newValue);
    static void setMaxCurrent(int newValue);
    static void setCanSpeed(int newValue);
    static void setTargetPercentage(float newValue);
    static void setMaxChargeTime(int newValue);

    static unsigned long getConfigBroadcast1Id();
    static unsigned long getConfigBroadcast2Id();
    static unsigned long getConfigSetId();
    static void setConfigBroadcast1Id(unsigned long newId);
    static void setConfigBroadcast2Id(unsigned long newId);
    static void setConfigSetId(unsigned long newId);

private:

    static int getValueFromEEPROM(int def, int addr);
    static unsigned long getULFromEEPROM(unsigned long def, int addr);
};

#endif /* CONFIG_H_ */