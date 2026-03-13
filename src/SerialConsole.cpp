
#include "SerialConsole.h"
template <class T>
inline Print &operator<<(Print &obj, T arg)
{
    obj.print(F(arg));
    return obj;
}

uint8_t systype;

SerialConsole::SerialConsole()
{
    init();
}

void SerialConsole::init()
{
    handlingEvent = false;

    // State variables for serial console
    ptrBuffer = 0;
    state = STATE_ROOT_MENU;
    loopcount = 0;
    cancel = false;
}

void SerialConsole::loop()
{
    if (handlingEvent == false)
    {
        if (Serial.available())
        {
            serialEvent();
        }
    }
}

void SerialConsole::printMenu()
{
    Logger::print("===Welcome to TC pao charger ===");
    Logger::print("for available commands enter h or H or ?");
    Logger::print("for current values enter p or P");
}

/*	There is a help menu (press H or h or ?)

 This is no longer going to be a simple single character console.
 Now the system can handle up to 80 input characters. Commands are submitted
 by sending line ending (LF, CR, or both)
 */
void SerialConsole::serialEvent()
{
    int incoming;
    incoming = Serial.read();
    if (incoming == -1)
    { // false alarm....
        return;
    }

    if (incoming == 10 || incoming == 13)
    { // command done. Parse it.
        handleConsoleCmd();
        ptrBuffer = 0; // reset line counter once the line has been processed
    }
    else
    {
        cmdBuffer[ptrBuffer++] = (unsigned char)incoming;
        if (ptrBuffer > 79)
            ptrBuffer = 79;
    }
}

void SerialConsole::handleConsoleCmd()
{
    handlingEvent = true;

    if (state == STATE_ROOT_MENU)
    {
        if (ptrBuffer == 1)
        { // command is a single ascii character
            handleShortCmd();
        }
        else
        { // if cmd over 1 char then assume (for now) that it is a config line
            handleConfigCmd();
        }
    }
    handlingEvent = false;
}

/*For simplicity the configuration setting code uses four characters for each configuration choice. This makes things easier for
 comparison purposes.
 */
void SerialConsole::handleConfigCmd()
{
    int i;
    int newValue;
    bool updateWifi = true;

    // Logger::debug("Cmd size: %i", ptrBuffer);
    if (ptrBuffer < 6)
        return;               // 4 digit command, =, value is at least 6 characters
    cmdBuffer[ptrBuffer] = 0; // make sure to null terminate
    String cmdString = String();
    unsigned char whichEntry = '0';
    i = 0;

    while (cmdBuffer[i] != '=' && i < ptrBuffer)
    {
        cmdString.concat(String(cmdBuffer[i++]));
    }
    i++; // skip the =
    if (i >= ptrBuffer)
    {
        Logger::log("Command needs a value..ie VOLT=3000");
        return;
    }

    // strtol() is able to parse also hex values (e.g. a string "0xCAFE"), useful for enable/disable by device id
    newValue = strtol((char *)(cmdBuffer + i), NULL, 0);

    cmdString.toUpperCase();
    if (cmdString == String("VOLT"))
    {
        if (newValue < 1 || newValue > 10000)
        {
            Logger::print("Please enter a value above 0 and below 10000");
        }
        Logger::print("Setting nominal voltage to  %s V", (float)newValue / 10.0);
        Config::setNominalVoltage(newValue);
    }
    else if (cmdString == String("AMP"))
    {
        if (newValue < 1 || newValue > 5000)
        {
            Logger::print("Please enter a value above 0 and below 5000");
        }
        Logger::print("Setting max charge current to  %s A", newValue);
        Config::setMaxCurrent(newValue);
    }
    else if (cmdString == String("CAN_SPEED"))
    {        
        if (newValue < 0 || newValue > 20)
        {
            Logger::print("Please enter a value between 0 and 19");
        }
        Logger::print("Setting can bus speed:   %s", newValue);
        Config::setCanSpeed(newValue);
    }
    else if (cmdString == String("DEBUG"))
    {
        if (newValue < 0 || newValue > 1)
        {
            Logger::print("Please enter a value between 0 and 1");
        } else  if ( newValue == 0) {
            Logger::setInfo();
        } else {
            Logger::setDebug();
        }

        Logger::print("Setting debug mode to %t", newValue);
    }
    else if (cmdString == String("TARGET"))
    {
        if (newValue < 0 || newValue > 2000)
        {
            Logger::print("Please enter a value between 0 and 2000");
        }

        Logger::print("Setting target charging percentage: %s  %", newValue);
        Config::setTargetPercentage(newValue);
    }
    else if (cmdString == String("MAX_TIME"))
    {if (newValue < 0 )
        {
            Logger::print("Please enter a value above 0");
        }
        Logger::print("Setting max charging time: %s seconds", newValue);
        Config::setMaxChargeTime(newValue);
    }
}

void SerialConsole::handleShortCmd()
{
    uint8_t val;

    switch (cmdBuffer[0])
    {
    case 'h':
    case '?':
    case 'H':
        SerialConsole::printHelpMenu();
        break;
    case 'p':
    case 'P':
        Config::printAllValues();
        break;
    default:
        SerialConsole::printMenu();
    }
}

void SerialConsole::printHelpMenu()
{
    Logger::print("CAN_SPEED : Set can bus speed 500kbps: `CAN_SPEED=16`");
    Logger::print("0- CAN_NOBPS");
    Logger::print("1- CAN_5KBPS");
    Logger::print("2- CAN_10KBPS");
    Logger::print("3- CAN_20KBPS");
    Logger::print("4- CAN_25KBPS");
    Logger::print("5- CAN_31K25BPS");
    Logger::print("6- CAN_33KBPS  ");
    Logger::print("7- CAN_40KBPS  ");
    Logger::print("8- CAN_50KBPS  ");
    Logger::print("9- CAN_80KBPS  ");
    Logger::print("10- CAN_83K3BPS ");
    Logger::print("11- CAN_95KBPS  ");
    Logger::print("12- CAN_100KBPS ");
    Logger::print("13- CAN_125KBPS ");
    Logger::print("14- CAN_200KBPS ");
    Logger::print("15- CAN_250KBPS ");
    Logger::print("16- CAN_500KBPS ");
    Logger::print("17- CAN_666KBPS ");
    Logger::print("18- CAN_800KBPS ");
    Logger::print("19- CAN_1000KBPS");

    Logger::print("VOLT : Setting nominal voltage (tenth of a volt) 320.1 V: `VOLT=3201`");
    Logger::print("AMP : Setting max charge current (tenth of an Amp) 20.1 Amp: `AMP=201`");
    Logger::print("DEBUG : Set debug mode, value is not persisted default to off on boot: on: `DEBUG=1` off: DEBUG=0");
    Logger::print("TARGET : set target charging percentage. 90.5% 0 t0 disable : `TARGET=905`");
    Logger::print("MAX_TIME : Set max charging time (seconds) 0 to disable: 23h 26m 13s: `MAX_TIME=84373`");
}