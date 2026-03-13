#ifndef LED_H_
#define LED_H_

#include <Arduino.h>

class Led {
public:
    Led(int green, int amber, int red);
    void setup();
    void loop(int errorState, int soc);
private:
    void blinkIndicatorLeds(int count);
    void blinkIndicatorLeds(int pin, int count);
    void ledHandler(int errorState, int soc);
    void setIndicatorLeds(int soc);
    int greenPin;
    int amberPin;
    int redPin;
};

#endif /* LED_H_ */

