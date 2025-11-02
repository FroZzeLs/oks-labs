#include "COMPortManager.h"
#include <iostream>
#include <chrono>
#include <algorithm>

#define ESC 0x1B

COMPortManager::COMPortManager()
    : hSendPort(INVALID_HANDLE_VALUE),
    hReceivePort(INVALID_HANDLE_VALUE),
    currentSendPort(""),
    currentReceivePort(""),
    currentBaudRate(9600) {
}

COMPortManager::~COMPortManager() { closePorts(); }

uint8_t COMPortManager::extractPortNumber(const std::string& portName) {
    uint8_t num = 0;
    for (char c : portName) if (std::isdigit(c)) num = static_cast<uint8_t>(num * 10 + (c - '0'));
    return num;
}

bool COMPortManager::openPort(const std::string& portName, HANDLE& hPort, bool forReading) {
    std::string fullPortName = "\\\\.\\" + portName;
    hPort = CreateFileA(fullPortName.c_str(),
        forReading ? GENERIC_READ : GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hPort == INVALID_HANDLE_VALUE) {
        std::cerr << "Ошибка открытия порта " << portName << ". Код: " << GetLastError() << std::endl;
        return false;
    }

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hPort, &dcbSerialParams)) { CloseHandle(hPort); return false; }
    dcbSerialParams.BaudRate = currentBaudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hPort, &dcbSerialParams)) { CloseHandle(hPort); return false; }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hPort, &timeouts);

    std::cout << "Порт " << portName << " успешно открыт" << std::endl;
    return true;
}

void COMPortManager::reconfigurePort(HANDLE hPort) {
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (GetCommState(hPort, &dcbSerialParams)) {
        dcbSerialParams.BaudRate = currentBaudRate;
        SetCommState(hPort, &dcbSerialParams);
    }
}

bool COMPortManager::setSendPort(const std::string& portName) {
    if (hSendPort != INVALID_HANDLE_VALUE) CloseHandle(hSendPort);
    if (openPort(portName, hSendPort, false)) { currentSendPort = portName; return true; }
    return false;
}

bool COMPortManager::setReceivePort(const std::string& portName) {
    if (hReceivePort != INVALID_HANDLE_VALUE) CloseHandle(hReceivePort);
    if (openPort(portName, hReceivePort, true)) { currentReceivePort = portName; return true; }
    return false;
}

bool COMPortManager::setBaudRate(DWORD baudRate) {
    currentBaudRate = baudRate;
    if (hSendPort != INVALID_HANDLE_VALUE) reconfigurePort(hSendPort);
    if (hReceivePort != INVALID_HANDLE_VALUE) reconfigurePort(hReceivePort);
    return true;
}

const std::string& COMPortManager::getCurrentSendPort() const { return currentSendPort; }
const std::string& COMPortManager::getCurrentReceivePort() const { return currentReceivePort; }
DWORD COMPortManager::getCurrentBaudRate() const { return currentBaudRate; }
const std::vector<uint8_t>& COMPortManager::getLastSentRawFrame() const { return lastSentRawFrame; }

bool COMPortManager::sendMessage(const std::string& message, DWORD* bytesWrittenPtr) {
    if (hSendPort == INVALID_HANDLE_VALUE) return false;
    const size_t MAX_FRAME_SIZE = 64;
    size_t headerSize = 1 + 1 + 1 + 8 + 1; // flag+sender+receiver+seq+timestamp+flag
    size_t maxDataPerFrame = MAX_FRAME_SIZE - headerSize;
    size_t totalWritten = 0;
    uint8_t seq = 1;
    for (size_t offset = 0; offset < message.size(); offset += maxDataPerFrame) {
        size_t len = std::min<size_t>(maxDataPerFrame, message.size() - offset);
        Frame frame;
        fill_frame(frame, seq, message, offset, len);

        std::vector<uint8_t> raw = frame.create_frame();
        lastSentRawFrame = raw;

        DWORD written = 0;
        const uint8_t* dataPtr = raw.data();
        size_t toWrite = raw.size();
        size_t writtenSoFar = 0;
        while (writtenSoFar < toWrite) {
            DWORD bw = 0;
            if (!WriteFile(hSendPort, dataPtr + writtenSoFar, toWrite - writtenSoFar, &bw, NULL))
                return false;
            if (bw == 0) return false;
            writtenSoFar += bw;
            totalWritten += bw;
        }
    }
    if (bytesWrittenPtr) *bytesWrittenPtr = static_cast<DWORD>(totalWritten);
    return true;
}

void COMPortManager::fill_frame(Frame& frame, uint8_t& seq, const std::string& message, size_t offset, size_t len)
{
    frame.sender = extractPortNumber(currentSendPort);
    frame.receiver = extractPortNumber(currentReceivePort);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    frame.timestamp = static_cast<uint64_t>(now);
    frame.seqNumber = seq++;
    frame.data.assign(message.begin() + offset, message.begin() + offset + len);
}

std::vector<Frame> COMPortManager::receiveAllFrames() {
    std::vector<Frame> frames;
    if (hReceivePort == INVALID_HANDLE_VALUE) return frames;
    while (true) {
        std::vector<uint8_t> raw;
        uint8_t b;
        DWORD bytesRead = 0;
        if (!ReadFile(hReceivePort, &b, 1, &bytesRead, NULL) || bytesRead < 1) break;
        uint8_t frameFlag = b;
        raw.push_back(frameFlag);
        bool esc = false;
        while (true) {
            if (!ReadFile(hReceivePort, &b, 1, &bytesRead, NULL) || bytesRead < 1) break;
            raw.push_back(b);
            if (esc) { esc = false; continue; }
            if (b == ESC) { esc = true; continue; }
            if (b == frameFlag) break;
        }
        Frame parsed;
        if (Frame::de_byte_stuffing(raw, parsed)) frames.push_back(parsed);
    }
    return frames;
}

void COMPortManager::closePorts() {
    if (hSendPort != INVALID_HANDLE_VALUE) { CloseHandle(hSendPort); hSendPort = INVALID_HANDLE_VALUE; }
    if (hReceivePort != INVALID_HANDLE_VALUE) { CloseHandle(hReceivePort); hReceivePort = INVALID_HANDLE_VALUE; }
}
