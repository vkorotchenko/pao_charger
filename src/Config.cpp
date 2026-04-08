#include "Config.h"
#include <Preferences.h>

static Preferences prefs;

void Config::init() {
    prefs.begin("charger", false);
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

float Config::getMaxVoltage() {
    return Config::getNominalVoltage() * Config::getNominalMaxMultiplier();
}

float Config::getMinVoltage() {
    return Config::getNominalVoltage() * Config::getNominalMinMultiplier();
}

int Config::getNominalVoltage() {
    return NOMINAL_VOLTAGE;
}

float Config::getNominalMaxMultiplier() {
    return prefs.getFloat("nomMaxMult", NOMINAL_MAX_MULT_DEFAULT);
}

float Config::getNominalMinMultiplier() {
    return prefs.getFloat("nomMinMult", NOMINAL_MIN_MULT_DEFAULT);
}

int Config::getMaxCurrent() {
    return prefs.getInt("maxCurrent", MAX_AMPS);
}

int Config::getCanSpeed() {
    return prefs.getInt("canSpeed", CAN_SPEED);
}

float Config::getTargetPercentage() {
    return prefs.getFloat("targetPct", TARGET_PERCENTAGE);
}

int Config::getMaxChargeTime() {
    return prefs.getInt("maxChgTime", MAX_CHARGE_TIME);
}

int Config::getTargetVoltage() {
    float minV = Config::getMinVoltage();
    float maxV = Config::getMaxVoltage();
    return round(minV + Config::getTargetPercentage() * (maxV - minV));
}

unsigned long Config::getConfigBroadcast1Id() {
    return prefs.getULong("cfgBcast1", CONFIG_BROADCAST_FRAME1_DEFAULT);
}

unsigned long Config::getConfigBroadcast2Id() {
    return prefs.getULong("cfgBcast2", CONFIG_BROADCAST_FRAME2_DEFAULT);
}

unsigned long Config::getConfigSetId() {
    return prefs.getULong("cfgSetId", CONFIG_SET_DEFAULT);
}

// ---------------------------------------------------------------------------
// Setters
// ---------------------------------------------------------------------------

void Config::setMaxCurrent(int newValue) {
    prefs.putInt("maxCurrent", newValue);
}

void Config::setCanSpeed(int newValue) {
    prefs.putInt("canSpeed", newValue);
}

void Config::setTargetPercentage(float newValue) {
    prefs.putFloat("targetPct", newValue);
}

void Config::setMaxChargeTime(int newValue) {
    prefs.putInt("maxChgTime", newValue);
}

void Config::setNominalMaxMultiplier(int valueX100) {
    prefs.putFloat("nomMaxMult", valueX100 / 100.0f);
}

void Config::setNominalMinMultiplier(int valueX100) {
    prefs.putFloat("nomMinMult", valueX100 / 100.0f);
}

void Config::setConfigBroadcast1Id(unsigned long newId) {
    prefs.putULong("cfgBcast1", newId);
}

void Config::setConfigBroadcast2Id(unsigned long newId) {
    prefs.putULong("cfgBcast2", newId);
}

void Config::setConfigSetId(unsigned long newId) {
    prefs.putULong("cfgSetId", newId);
}

void Config::resetToDefaults() {
    prefs.clear();
}

void Config::printAllValues() {
    Logger::print("=== Config values ===");
    Logger::print("Nominal Voltage: %.1f V", Config::getNominalVoltage() / 10.0f);
    Logger::print("Max Current: %.1f A", Config::getMaxCurrent() / 10.0f);
    Logger::print("Can speed: %d", Config::getCanSpeed());
    Logger::print("Percentage target: %.1f %%", Config::getTargetPercentage() * 100.0f);
    Logger::print("Max Charge time: %d s", Config::getMaxChargeTime());
}
