
#ifndef SERIALCONSOLE_H_
#define SERIALCONSOLE_H_

#include "Config.h"
#include "Logger.h"

class SerialConsole {
public:
    SerialConsole();
    void loop();
    void printMenu();

protected:
    enum CONSOLE_STATE
    {
        STATE_ROOT_MENU
    };

private:
    bool handlingEvent;
    char cmdBuffer[80];
    int ptrBuffer;
    int state;
    int loopcount;
    bool cancel;


    void init();
    void serialEvent();
    void handleConsoleCmd();
    void handleShortCmd();
    void handleConfigCmd();
    void resetWiReachMini();
    void getResponse();
    void printHelpMenu();
};

#endif /* SERIALCONSOLE_H_ */