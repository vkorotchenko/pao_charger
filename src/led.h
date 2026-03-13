#ifndef LED_H_
#define LED_H_

#include <Arduino.h>

#define BLINK_INTERVAL 200  // ms per blink phase (on or off)

class Led {
public:
    Led(int green, int amber, int red);
    void setup();
    void loop(int errorState, int soc);
private:
    void ledHandler(int errorState, int soc);
    int greenPin;
    int amberPin;
    int redPin;

    int blinkPin;
    int blinkCount;
    int blinksDone;
    bool blinkOn;
    unsigned long lastToggle;
};

#endif /* LED_H_ */
