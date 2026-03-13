#include "led.h"

Led::Led(int green, int amber, int red) {
    greenPin = green;
    amberPin = amber;
    redPin = red;
    blinkPin = -1;
    blinkCount = 0;
    blinksDone = 0;
    blinkOn = false;
    lastToggle = 0;
}

void Led::setup()
{
    pinMode(greenPin, OUTPUT);
    pinMode(amberPin, OUTPUT);
    pinMode(redPin, OUTPUT);

    digitalWrite(greenPin, HIGH);
    digitalWrite(amberPin, LOW);
    digitalWrite(redPin, HIGH);
}

void Led::loop(int errorState, int soc) {
    ledHandler(errorState, soc);
}

void Led::ledHandler(int errorState, int soc)
{
    int targetBlinkPin = -1;
    int targetBlinkCount = 0;
    bool solidGreen = false, solidAmber = false, solidRed = false;

    if (errorState != 0) {
        solidRed = true;
        targetBlinkPin = amberPin;
        switch (errorState) {
            case B00000001: targetBlinkCount = 1; break;
            case B00000010: targetBlinkCount = 2; break;
            case B00000100: targetBlinkCount = 3; break;
            case B00001000: targetBlinkCount = 4; break;
            case B00010000: targetBlinkCount = 5; break;
            case B00001100: targetBlinkCount = 6; break;
            default:        targetBlinkCount = 1; break;
        }
    } else {
        switch (soc) {
            case 1:
                targetBlinkPin = redPin;
                targetBlinkCount = 2;
                break;
            case 2:
                solidRed = true;
                targetBlinkPin = amberPin;
                targetBlinkCount = 2;
                break;
            case 3:
                solidRed = true;
                solidAmber = true;
                targetBlinkPin = greenPin;
                targetBlinkCount = 2;
                break;
            case 4:
                solidRed = true;
                solidAmber = true;
                solidGreen = true;
                break;
            default:
                break; // all off
        }
    }

    // Reset sequence if state changed
    if (targetBlinkPin != blinkPin || targetBlinkCount != blinkCount) {
        if (blinkPin >= 0) digitalWrite(blinkPin, LOW);
        blinkPin = targetBlinkPin;
        blinkCount = targetBlinkCount;
        blinksDone = 0;
        blinkOn = false;
        lastToggle = millis();
    }

    // Set solid background pins (skip the blink pin)
    if (blinkPin != greenPin) digitalWrite(greenPin, solidGreen ? HIGH : LOW);
    if (blinkPin != amberPin) digitalWrite(amberPin, solidAmber ? HIGH : LOW);
    if (blinkPin != redPin)   digitalWrite(redPin,   solidRed   ? HIGH : LOW);

    // Handle blink timing
    if (blinkPin >= 0 && millis() - lastToggle >= BLINK_INTERVAL) {
        lastToggle = millis();
        if (!blinkOn) {
            digitalWrite(blinkPin, HIGH);
            blinkOn = true;
        } else {
            digitalWrite(blinkPin, LOW);
            blinkOn = false;
            blinksDone++;
            if (blinksDone >= blinkCount) {
                blinksDone = 0;
            }
        }
    }
}
