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

private:
    
    static int getValueFromEEPROM(int def,int addr);
};

#endif /* CONFIG_H_ */