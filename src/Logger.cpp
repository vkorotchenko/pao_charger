#include "Logger.h"

Logger::LogLevel Logger::logLevel = Logger::Debug;
uint8_t Logger::logMask = LOG_CAT_BLE;

void Logger::setDebug() {
    logLevel = LogLevel::Debug;
}
void Logger::setInfo() {
    logLevel = LogLevel::Info;
}

boolean Logger::isDebug() {
    return logLevel == Debug;
}

void Logger::setLogMask(uint8_t mask) {
    logMask = mask;
}

uint8_t Logger::getLogMask() {
    return logMask;
}

void Logger::log(uint8_t category, const char *format, ...) {
    if (!Logger::isDebug()) return;
    if (!(logMask & category)) return;
    va_list args;
    va_start(args, format);
    Logger::logMessage(format, args);
    va_end(args);
}

void Logger::log(const char *format, ...) {
    if (!Logger::isDebug()) return;
    if (!(logMask & LOG_CAT_SYS)) return;
    va_list args;
    va_start(args, format);
    Logger::logMessage(format, args);
    va_end(args);
}

void Logger::print(const char *format, ...) {
    va_list args;
    va_start(args, format);
    Logger::logMessage(format, args);
    va_end(args);
}

void Logger::logMessage(const char *format, va_list args) {
    char data[200];
    vsnprintf(data, sizeof(data), format, args);
    Serial.println(data);
}

void Logger::logIncomingMsg(unsigned long id, byte ext, byte len, float tVolt, float tAmp){
    Logger::log(LOG_CAT_CAN, "=== INCOMING: ");
    Logger::log(LOG_CAT_CAN, "====== id: %d", id);
    Logger::log(LOG_CAT_CAN, "====== ext %X: ", ext);
    Logger::log(LOG_CAT_CAN, "====== length %d: ", len);
    Logger::log(LOG_CAT_CAN, "====== volt %dV: ", tVolt);
    Logger::log(LOG_CAT_CAN, "====== amp: %f", tAmp);
}

void Logger::logOutgoingMsg(unsigned long id, byte ext, byte len, float tVolt, int tAmp){
    Logger::log(LOG_CAT_CAN, "=== OUTGOING: ");
    Logger::log(LOG_CAT_CAN, "====== id dec: %d  hex: 0x%x", id, id);
    Logger::log(LOG_CAT_CAN, "====== volt dec: %.1f  hex: 0x%x", tVolt / 10.0, (int)tVolt);
    Logger::log(LOG_CAT_CAN, "====== amp dec: %.1f  hex: 0x%x", tAmp / 10.0, tAmp);
}
