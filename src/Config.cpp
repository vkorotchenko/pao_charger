#include "Config.h"

float Config::getMaxVoltage()
{
    return Config::getNominalVoltage() * Config::getNominalMaxMultiplier();
}

float Config::getMinVoltage()
{
    return Config::getNominalVoltage() * Config::getNominalMinMultiplier();
}

float Config::getNominalMaxMultiplier()
{
    return NOMINAL_MAX_MULT_DEFAULT / 100.0f;
}

float Config::getNominalMinMultiplier()
{
    return NOMINAL_MIN_MULT_DEFAULT / 100.0f;
}

bool Config::getAutoNominalFromCan()
{
    return Config::getValueFromEEPROM(AUTO_NOMINAL_FROM_CAN_DEFAULT, EEPROM_AUTO_NOMINAL_FROM_CAN) != 0;
}

void Config::setNominalMaxMultiplier(int valueX100)
{
    if (EEPROM.isValid() && Config::getValueFromEEPROM(NOMINAL_MAX_MULT_DEFAULT, EEPROM_NOMINAL_MAX_MULT) == valueX100) return;
    EEPROM.update(EEPROM_NOMINAL_MAX_MULT,     (uint8_t)(valueX100 >> 8));
    EEPROM.update(EEPROM_NOMINAL_MAX_MULT + 1, (uint8_t)(valueX100 & 0xFF));
    EEPROM.commit();
}

void Config::setNominalMinMultiplier(int valueX100)
{
    if (EEPROM.isValid() && Config::getValueFromEEPROM(NOMINAL_MIN_MULT_DEFAULT, EEPROM_NOMINAL_MIN_MULT) == valueX100) return;
    EEPROM.update(EEPROM_NOMINAL_MIN_MULT,     (uint8_t)(valueX100 >> 8));
    EEPROM.update(EEPROM_NOMINAL_MIN_MULT + 1, (uint8_t)(valueX100 & 0xFF));
    EEPROM.commit();
}

void Config::setAutoNominalFromCan(bool enable)
{
    int val = enable ? 1 : 0;
    if (EEPROM.isValid() && Config::getValueFromEEPROM(AUTO_NOMINAL_FROM_CAN_DEFAULT, EEPROM_AUTO_NOMINAL_FROM_CAN) == val) return;
    EEPROM.update(EEPROM_AUTO_NOMINAL_FROM_CAN,     (uint8_t)(val >> 8));
    EEPROM.update(EEPROM_AUTO_NOMINAL_FROM_CAN + 1, (uint8_t)(val & 0xFF));
    EEPROM.commit();
}

int Config::getNominalVoltage(){
    return Config::getValueFromEEPROM(NOMINAL_VOLTAGE, EEPROM_NOMINAL_VOLTAGE);
}

int Config::getMaxCurrent()
{
    return Config::getValueFromEEPROM(MAX_AMPS, EEPROM_MAX_AMPS);
}

int Config::getCanSpeed()
{
    return Config::getValueFromEEPROM(CAN_SPEED, EEPROM_CAN_SPEED);
}

float Config::getTargetPercentage()
{
    return Config::getValueFromEEPROM((int)(TARGET_PERCENTAGE * 1000), EEPROM_TARGET_PERCENTAGE) / 1000.0f;
}

int Config::getMaxChargeTime()
{
    return Config::getValueFromEEPROM(MAX_CHARGE_TIME, EEPROM_MAX_CHARGE_TIME);
}

int Config::getValueFromEEPROM(int def, int addr) {
    if (!EEPROM.isValid()) return def;
    uint8_t hi = EEPROM.read(addr);
    uint8_t lo = EEPROM.read(addr + 1);
    int val = ((int)hi << 8) | lo;
    if (val == (int)DEFAULT_EEPROM_VAL) return def;
    return val;
}

unsigned long Config::getULFromEEPROM(unsigned long def, int addr) {
    if (!EEPROM.isValid()) return def;
    unsigned long val = ((unsigned long)EEPROM.read(addr)     << 24)
                      | ((unsigned long)EEPROM.read(addr + 1) << 16)
                      | ((unsigned long)EEPROM.read(addr + 2) <<  8)
                      |  (unsigned long)EEPROM.read(addr + 3);
    if (val == 0xFFFFFFFFUL) return def;
    return val;
}

int Config::getTargetVoltage() {
    float minV = Config::getMinVoltage();
    float maxV = Config::getMaxVoltage();
    return round(minV + Config::getTargetPercentage() * (maxV - minV));
}

void Config::printAllValues()
{
    Logger::print("=== EEPROM values ===");
    Logger::print("Nominal Voltage: %d  V", Config::getNominalVoltage() / 10.0);
    Logger::print("Max Current: %d A", Config::getMaxCurrent() / 10.0);
    Logger::print("Can speed: %d", Config::getCanSpeed());
    Logger::print("Percentage target: %d  %", Config::getTargetPercentage());
    Logger::print("Max Charge time: %d s", Config::getMaxChargeTime());
}

void Config::setNominalVoltage(int newValue)
{
    if (EEPROM.isValid() && Config::getNominalVoltage() == newValue) return;
    EEPROM.update(EEPROM_NOMINAL_VOLTAGE,     (uint8_t)(newValue >> 8));
    EEPROM.update(EEPROM_NOMINAL_VOLTAGE + 1, (uint8_t)(newValue & 0xFF));
    EEPROM.commit();
}

void Config::setMaxCurrent(int newValue)
{
    if (EEPROM.isValid() && Config::getMaxCurrent() == newValue) return;
    EEPROM.update(EEPROM_MAX_AMPS,     (uint8_t)(newValue >> 8));
    EEPROM.update(EEPROM_MAX_AMPS + 1, (uint8_t)(newValue & 0xFF));
    EEPROM.commit();
}

void Config::setCanSpeed(int newValue)
{
    if (Config::getCanSpeed() == newValue) return;
    EEPROM.update(EEPROM_CAN_SPEED,     (uint8_t)(newValue >> 8));
    EEPROM.update(EEPROM_CAN_SPEED + 1, (uint8_t)(newValue & 0xFF));
    EEPROM.commit();
}

void Config::setTargetPercentage(float newValue)
{
    int stored = (int)(newValue * 1000);
    if (EEPROM.isValid() && (int)(Config::getTargetPercentage() * 1000) == stored) return;
    EEPROM.update(EEPROM_TARGET_PERCENTAGE,     (uint8_t)(stored >> 8));
    EEPROM.update(EEPROM_TARGET_PERCENTAGE + 1, (uint8_t)(stored & 0xFF));
    EEPROM.commit();
}

void Config::setMaxChargeTime(int newValue)
{
    if (EEPROM.isValid() && Config::getMaxChargeTime() == newValue) return;
    EEPROM.update(EEPROM_MAX_CHARGE_TIME,     (uint8_t)(newValue >> 8));
    EEPROM.update(EEPROM_MAX_CHARGE_TIME + 1, (uint8_t)(newValue & 0xFF));
    EEPROM.commit();
}

void Config::resetToDefaults()
{
    Config::setNominalVoltage(NOMINAL_VOLTAGE);
    Config::setMaxCurrent(MAX_AMPS);
    Config::setMaxChargeTime(MAX_CHARGE_TIME);
    Config::setTargetPercentage(TARGET_PERCENTAGE);
    Config::setNominalMaxMultiplier(NOMINAL_MAX_MULT_DEFAULT);
    Config::setNominalMinMultiplier(NOMINAL_MIN_MULT_DEFAULT);
    Config::setAutoNominalFromCan(false);
}

unsigned long Config::getConfigBroadcast1Id() {
    return Config::getULFromEEPROM(CONFIG_BROADCAST_FRAME1_DEFAULT, EEPROM_CONFIG_BROADCAST1_ID);
}

unsigned long Config::getConfigBroadcast2Id() {
    return Config::getULFromEEPROM(CONFIG_BROADCAST_FRAME2_DEFAULT, EEPROM_CONFIG_BROADCAST2_ID);
}

unsigned long Config::getConfigSetId() {
    return Config::getULFromEEPROM(CONFIG_SET_DEFAULT, EEPROM_CONFIG_SET_ID);
}

void Config::setConfigBroadcast1Id(unsigned long newId) {
    if (Config::getConfigBroadcast1Id() == newId) return;
    EEPROM.update(EEPROM_CONFIG_BROADCAST1_ID,     (uint8_t)(newId >> 24));
    EEPROM.update(EEPROM_CONFIG_BROADCAST1_ID + 1, (uint8_t)(newId >> 16));
    EEPROM.update(EEPROM_CONFIG_BROADCAST1_ID + 2, (uint8_t)(newId >>  8));
    EEPROM.update(EEPROM_CONFIG_BROADCAST1_ID + 3, (uint8_t)(newId      ));
    EEPROM.commit();
}

void Config::setConfigBroadcast2Id(unsigned long newId) {
    if (Config::getConfigBroadcast2Id() == newId) return;
    EEPROM.update(EEPROM_CONFIG_BROADCAST2_ID,     (uint8_t)(newId >> 24));
    EEPROM.update(EEPROM_CONFIG_BROADCAST2_ID + 1, (uint8_t)(newId >> 16));
    EEPROM.update(EEPROM_CONFIG_BROADCAST2_ID + 2, (uint8_t)(newId >>  8));
    EEPROM.update(EEPROM_CONFIG_BROADCAST2_ID + 3, (uint8_t)(newId      ));
    EEPROM.commit();
}

void Config::setConfigSetId(unsigned long newId) {
    if (Config::getConfigSetId() == newId) return;
    EEPROM.update(EEPROM_CONFIG_SET_ID,     (uint8_t)(newId >> 24));
    EEPROM.update(EEPROM_CONFIG_SET_ID + 1, (uint8_t)(newId >> 16));
    EEPROM.update(EEPROM_CONFIG_SET_ID + 2, (uint8_t)(newId >>  8));
    EEPROM.update(EEPROM_CONFIG_SET_ID + 3, (uint8_t)(newId      ));
    EEPROM.commit();
}
