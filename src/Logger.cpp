#include "Logger.h"

Logger::LogLevel Logger::logLevel = Logger::Debug;

void Logger::setDebug() {
    logLevel = LogLevel::Debug;
}
void Logger::setInfo() {
    logLevel = LogLevel::Info;
}

boolean Logger::isDebug() {
    return logLevel == Debug;
}

/*
 * Output a log message (called by log(), console())
 *
 * Supports printf() like syntax:
 *
 * %% - outputs a '%' character
 * %s - prints the next parameter as string
 * %d - prints the next parameter as decimal
 * %f - prints the next parameter as double float
 * %x - prints the next parameter as hex value
 * %X - prints the next parameter as hex value with '0x' added before
 * %b - prints the next parameter as binary value
 * %B - prints the next parameter as binary value with '0b' added before
 * %l - prints the next parameter as long
 * %c - prints the next parameter as a character
 * %t - prints the next parameter as boolean ('T' or 'F')
 * %T - prints the next parameter as boolean ('true' or 'false')
 */
void Logger::log(const char *message, ...) {
      if (!Logger::isDebug()) {
        return;
    }
    va_list args;
    va_start(args, message);
    Logger::logMessage(message, args);
    va_end(args);
}

/*
 * Output a log message (called by log(), console())
 *
 * Supports printf() like syntax:
 *
 * %% - outputs a '%' character
 * %s - prints the next parameter as string
 * %d - prints the next parameter as decimal
 * %f - prints the next parameter as double float
 * %x - prints the next parameter as hex value
 * %X - prints the next parameter as hex value with '0x' added before
 * %b - prints the next parameter as binary value
 * %B - prints the next parameter as binary value with '0b' added before
 * %l - prints the next parameter as long
 * %c - prints the next parameter as a character
 * %t - prints the next parameter as boolean ('T' or 'F')
 * %T - prints the next parameter as boolean ('true' or 'false')
 */
void Logger::print(const char *message, ...) {
    va_list args;
    va_start(args, message);
    Logger::logMessage(message, args);
    va_end(args);
}

void Logger::logMessage(const char *format, va_list args) {
    char data[1000];
  sprintf(data, format, args);
  Serial.println(data);

// Logger::logMessage(format, args);

}

void Logger::logIncomingMsg(unsigned long id, byte ext, byte len, float tVolt, float tAmp){
    Logger::log("=== INCOMING: ");
    Logger::log("====== id: %d", id);
    Logger::log("====== ext %X: ", ext);
    Logger::log("====== length %d: ", len);
    Logger::log("====== volt %dV: ", tVolt);
    Logger::log("====== amp: %f", tAmp);
}

void Logger::logOutgoingMsg(unsigned long id, byte ext, byte len, float tVolt, int tAmp){
    Logger::log("=== INCOMING: ");
    Logger::log("====== id: %d", id);
    Logger::log("====== ext %X: ", ext);
    Logger::log("====== length %d: ", len);
    Logger::log("====== volt %dV: ", tVolt);
    Logger::log("====== amp: %d", tAmp);
}


/*
 * Output a log message (called by log(), console())
 *
 * Supports printf() like syntax:
 *
 * %% - outputs a '%' character
 * %s - prints the next parameter as string
 * %d - prints the next parameter as decimal
 * %f - prints the next parameter as double float
 * %x - prints the next parameter as hex value
 * %X - prints the next parameter as hex value with '0x' added before
 * %b - prints the next parameter as binary value
 * %B - prints the next parameter as binary value with '0b' added before
 * %l - prints the next parameter as long
 * %c - prints the next parameter as a character
 * %t - prints the next parameter as boolean ('T' or 'F')
 * %T - prints the next parameter as boolean ('true' or 'false')
 */
// void Logger::logMessage(const char *format, va_list args) {
//     for (; *format != 0; ++format) {
//         if (*format == '%') {
//             ++format;
//             if (*format == '\0')
//                 break;
//             if (*format == '%') {
//                 Serial.println(F(*format));
//                 continue;
//             }
//             if (*format == 's') {
//                 register char *s = (char *) va_arg( args, int );
//                 Serial.println(F(s));
//                 continue;
//             }
//             if (*format == 'd' || *format == 'i') {
                
//                 Serial.println(String(va_arg( args, int ), DEC));
//                 continue;
//             }
//             if (*format == 'f') {
//                 Serial.println(String(va_arg( args, double ), 2));
//                 continue;
//             }
//             if (*format == 'x') {
//                  Serial.println(String(va_arg( args, int ), HEX));
//                 continue;
//             }
//             if (*format == 'X') {
//                 Serial.println(F("0x"));
//                 Serial.println(String(va_arg( args, int ), HEX));
//                 continue;
//             }
//             if (*format == 'b') {
//                 Serial.println(String(va_arg( args, int ), BIN));
//                 continue;
//             }
//             if (*format == 'B') {
//                 Serial.println(F("0b"));
//                 Serial.println(String(va_arg( args, int ), BIN));
//                 continue;
//             }
//             if (*format == 'l') {
//                 Serial.println(String(va_arg( args, long ), DEC));
//                 continue;
//             }

//             if (*format == 'c') {
//                 Serial.println(F(va_arg( args, int )));
//                 continue;
//             }
//             if (*format == 't') {
//                 if (va_arg( args, int ) == 1) {
//                     Serial.println(F("T"));
//                 } else {
//                     Serial.println(F("F"));
//                 }
//                 continue;
//             }
//             if (*format == 'T') {
//                 if (va_arg( args, int ) == 1) {
//                     Serial.println(F("t"));
//                 } else {
//                     Serial.println(F("f"));
//                 }
//                 continue;
//             }

//         }
//         Serial.println(F(*format));
//     }
//     Serial.println(F(""));
// }