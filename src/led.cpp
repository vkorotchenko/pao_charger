#include "led.h"

Led:: Led(int green, int amber, int red) {
    greenPin = green;
    amberPin = amber;
    redPin = red;
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

void Led::loop(int errorState, int soc){
    ledHandler(errorState, soc);
}



void Led::ledHandler(int errorState, int soc)
{
  switch (errorState)
  { // Read out error byte

  case B00000001:
    blinkIndicatorLeds(1);
    break;
  case B00000010:
    blinkIndicatorLeds(2);
    break;
  case B00000100:
    blinkIndicatorLeds(3);
    break;
  case B00001000:
    blinkIndicatorLeds(4);
    break;
  case B00010000:
    blinkIndicatorLeds(5);
    break;
  case B00001100:
    blinkIndicatorLeds(6);
    break;
  default:
    setIndicatorLeds(soc);
    break;
  }
}

void Led::setIndicatorLeds(int soc)
{
  digitalWrite(greenPin, LOW);
  digitalWrite(amberPin, LOW);
  digitalWrite(redPin, LOW);

  if (soc == 1)
  {
    Led::blinkIndicatorLeds(redPin, 2);
  }
  if (soc == 2)
  {
    digitalWrite(redPin, HIGH);
    Led::blinkIndicatorLeds(amberPin, 2);
  }
  if (soc == 3)
  {
    digitalWrite(redPin, HIGH);
    digitalWrite(amberPin, HIGH);
    Led::blinkIndicatorLeds(greenPin, 2);
  }
  if (soc == 4)
  {
    digitalWrite(redPin, HIGH);
    digitalWrite(amberPin, HIGH);
    digitalWrite(greenPin, HIGH);
  }
}

void Led::blinkIndicatorLeds(int count)
{

  digitalWrite(greenPin, LOW);
  digitalWrite(amberPin, LOW);
  digitalWrite(redPin, HIGH);

  for (int i = 0; i < count; i++)
  {
    digitalWrite(amberPin, HIGH);
    delay(200);
    digitalWrite(amberPin, LOW);
    delay(200);
  }
}

void Led::blinkIndicatorLeds(int pin, int count)
{
  for (int i = 0; i < count; i++)
  {
    digitalWrite(pin, HIGH);
    delay(200);
    digitalWrite(pin, LOW);
    delay(200);
  }
}