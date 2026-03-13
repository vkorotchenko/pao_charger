/*
 * Logger.h
 *
 Copyright (c) 2013 Collin Kidder, Michael Neuweiler, Charles Galpin

 Permission is hereby granted, free of charge, to any person obtaining
 a copy of this software and associated documentation files (the
 "Software"), to deal in the Software without restriction, including
 without limitation the rights to use, copy, modify, merge, publish,
 distribute, sublicense, and/or sell copies of the Software, and to
 permit persons to whom the Software is furnished to do so, subject to
 the following conditions:

 The above copyright notice and this permission notice shall be included
 in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#ifndef LOGGER_H_
#define LOGGER_H_

#include <Arduino.h>
#include <SPI.h>

class Logger {
public:
    enum LogLevel {
        Debug = 1, Info = 0
    };
    static void setDebug();
    static void setInfo();
    static boolean isDebug();
    static void log(const char *, ...);
    static void print(const char *, ...);
    // static void logMessage(const char *, ...);
    static void logIncomingMsg(unsigned long id, byte ext, byte len, float tVolt, float tAmp);
    static void logOutgoingMsg(unsigned long id, byte ext, byte len, float tVolt, int tAmp);
private:
    static LogLevel logLevel;
    static uint32_t lastLogTime;

    static void logMessage(const char *format, va_list args);
};

#endif /* LOGGER_H_ */


