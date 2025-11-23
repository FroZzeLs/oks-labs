#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include "Frame.h"
#include "CsmaConfig.h"

class COMPortManager {
private:
    HANDLE hSendPort;
    HANDLE hReceivePort;
    std::string currentSendPort;
    std::string currentReceivePort;
    DWORD currentBaudRate;

    std::vector<uint8_t> lastSentRawFrame;

    std::atomic<bool> stopReceiverThread;
    std::thread receiverThread;

    mutable std::mutex outputMutex;
    std::mutex queueMutex;

    std::queue<Frame> receivedFrameQueue;

    CSMA::Stats globalStats;
    CSMA::Stats lastSessionStats;

    // ¡ÛÙÂ ÔËÂÏÌËÍ‡
    std::vector<uint8_t> receiverBuffer;
    bool receiverStateInFrame;

    bool openPort(const std::string& portName, HANDLE& hPort);
    void reconfigurePort(HANDLE hPort);
    static uint8_t extractPortNumber(const std::string& portName);

    void receiverThreadFunc();
    bool writeByte(HANDLE hPort, uint8_t byte);
    bool readByte(HANDLE hPort, uint8_t& byte);
    void sendJamSignal();

    // --- ¬≈–Õ”À ¬¿ÿ” ‘”Õ ÷»ﬁ »— ¿∆≈Õ»ﬂ ---
    void distort_payload(std::vector<uint8_t>& payload);

public:
    COMPortManager();
    ~COMPortManager();

    bool setSendPort(const std::string& portName);
    bool setReceivePort(const std::string& portName);
    bool setBaudRate(DWORD baudRate);

    bool sendMessage(const std::string& message, DWORD* bytesWrittenPtr = nullptr);
    void fill_frame(Frame& frame, uint8_t& seq, const std::string& message, size_t offset, size_t len);

    std::vector<Frame> receiveAllFrames();

    void closePorts();

    const std::string& getCurrentSendPort() const;
    const std::string& getCurrentReceivePort() const;
    DWORD getCurrentBaudRate() const;
    const std::vector<uint8_t>& getLastSentRawFrame() const;

    CSMA::Stats getGlobalStats() const;
    CSMA::Stats getLastSessionStats() const;
    void resetGlobalStats();
};