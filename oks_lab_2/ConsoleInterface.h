#pragma once
#include "COMPortManager.h"
#include <vector>
#include <string>

class ConsoleInterface {
private:
    COMPortManager portManager;

    struct PortPair {
        std::string sendPort;
        std::string receivePort;
    };

    std::vector<PortPair> availablePortPairs;
    std::vector<DWORD> baudRates;

    void showMainMenu();
    std::string prettyPrint(const std::vector<uint8_t>& stuffed) const;
    void setupPorts();
    void sendMessageMenu();
    void receiveMessageMenu();
    void changeBaudRate();
    void viewLastSentFrame();

public:
    ConsoleInterface();
    void run();
};
