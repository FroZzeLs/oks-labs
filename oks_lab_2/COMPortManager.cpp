#include "COMPortManager.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <random>
#include <cstdlib>

static const uint8_t START_FLAG = 0x08;
static const uint8_t END_FLAG = 0x7E;
static const uint8_t ESC = 0x1B;

COMPortManager::COMPortManager() :
    hSendPort(INVALID_HANDLE_VALUE),
    hReceivePort(INVALID_HANDLE_VALUE),
    currentSendPort(""),
    currentReceivePort(""),
    currentBaudRate(9600) {
}

COMPortManager::~COMPortManager() {
    closePorts();
}

uint8_t COMPortManager::extractPortNumber(const std::string& portName) {
    uint8_t num = 0;
    for (char c : portName) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            num = static_cast<uint8_t>(num * 10 + (c - '0'));
        }
    }
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
    if (!GetCommState(hPort, &dcbSerialParams)) {
        CloseHandle(hPort);
        return false;
    }
    dcbSerialParams.BaudRate = currentBaudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hPort, &dcbSerialParams)) {
        CloseHandle(hPort);
        return false;
    }

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
    if (hSendPort != INVALID_HANDLE_VALUE) {
        CloseHandle(hSendPort);
    }
    if (openPort(portName, hSendPort, false)) {
        currentSendPort = portName;
        return true;
    }
    return false;
}

bool COMPortManager::setReceivePort(const std::string& portName) {
    if (hReceivePort != INVALID_HANDLE_VALUE) {
        CloseHandle(hReceivePort);
    }
    if (openPort(portName, hReceivePort, true)) {
        currentReceivePort = portName;
        return true;
    }
    return false;
}

bool COMPortManager::setBaudRate(DWORD baudRate) {
    currentBaudRate = baudRate;
    if (hSendPort != INVALID_HANDLE_VALUE) {
        reconfigurePort(hSendPort);
    }
    if (hReceivePort != INVALID_HANDLE_VALUE) {
        reconfigurePort(hReceivePort);
    }
    return true;
}

const std::string& COMPortManager::getCurrentSendPort() const { return currentSendPort; }
const std::string& COMPortManager::getCurrentReceivePort() const { return currentReceivePort; }
DWORD COMPortManager::getCurrentBaudRate() const { return currentBaudRate; }
const std::vector<uint8_t>& COMPortManager::getLastSentRawFrame() const { return lastSentRawFrame; }

bool COMPortManager::sendMessage(const std::string& message, DWORD* bytesWrittenPtr) {
    if (hSendPort == INVALID_HANDLE_VALUE) {
        return false;
    }

    const size_t maxDataPerFrame = 32;
    size_t totalWritten = 0;
    uint8_t seq = 1;

    for (size_t offset = 0; offset < message.size(); offset += maxDataPerFrame) {
        size_t len = std::min<size_t>(maxDataPerFrame, message.size() - offset);
        Frame frame;
        fill_frame(frame, seq, message, offset, len);

        std::vector<uint8_t> raw = frame.create_frame();
        lastSentRawFrame = raw;

        const uint8_t* dataPtr = raw.data();
        size_t toWrite = raw.size();
        size_t writtenSoFar = 0;
        while (writtenSoFar < toWrite) {
            DWORD bw = 0;
            if (!WriteFile(hSendPort, dataPtr + writtenSoFar, (DWORD)(toWrite - writtenSoFar), &bw, NULL)) {
                return false;
            }
            if (bw == 0) {
                return false;
            }
            writtenSoFar += bw;
            totalWritten += bw;
        }
    }
    if (bytesWrittenPtr) {
        *bytesWrittenPtr = static_cast<DWORD>(totalWritten);
    }
    return true;
}

void COMPortManager::fill_frame(Frame& frame, uint8_t& seq, const std::string& message, size_t offset, size_t len) {
    frame.sender = extractPortNumber(currentSendPort);
    frame.receiver = extractPortNumber(currentReceivePort);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    frame.timestamp = static_cast<uint64_t>(now);
    frame.seqNumber = seq++;
    frame.dataLen = static_cast<uint16_t>(len);
    frame.data.assign(message.begin() + offset, message.begin() + offset + len);
}

void COMPortManager::distort_payload(std::vector<uint8_t>& payload) {
    if (payload.empty()) {
        return;
    }

    static thread_local std::mt19937 rng(std::random_device{}());

    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    double p = prob_dist(rng);

    int bits_to_flip = (p < 0.85) ? 1 : 2; 

    size_t total_bits = payload.size() * 8;
    std::uniform_int_distribution<size_t> bit_pos_dist(0, total_bits - 1);

    size_t pos1 = bit_pos_dist(rng);

    if (bits_to_flip == 1) {
        payload[pos1 / 8] ^= (1 << (pos1 % 8));
    }   
    else { // bits_to_flip == 2
        size_t pos2;
        do {
            pos2 = bit_pos_dist(rng);
        } while (pos1 == pos2);

        // Искажаем оба бита
        payload[pos1 / 8] ^= (1 << (pos1 % 8));
        payload[pos2 / 8] ^= (1 << (pos2 % 8));
    }
}
// --- КОНЕЦ НОВОЙ ВЕРСИИ ---

std::vector<Frame> COMPortManager::receiveAllFrames() {
    std::vector<Frame> frames;
    if (hReceivePort == INVALID_HANDLE_VALUE) {
        return frames;
    }
    while (true) {
        std::vector<uint8_t> raw;
        uint8_t b;
        DWORD bytesRead = 0;

        do {
            if (!ReadFile(hReceivePort, &b, 1, &bytesRead, NULL) || bytesRead < 1) {
                return frames;
            }
        } while (b != START_FLAG);

        raw.push_back(b);

        bool is_esc = false;
        while (true) {
            if (!ReadFile(hReceivePort, &b, 1, &bytesRead, NULL) || bytesRead < 1) {
                break;
            }
            raw.push_back(b);

            if (b == END_FLAG && !is_esc) {
                break;
            }
            is_esc = (b == ESC && !is_esc);
        }

        Frame parsed;
        if (Frame::de_byte_stuffing(raw, parsed)) {
            // Искажаем данные ПОСЛЕ успешного парсинга кадра
            distort_payload(parsed.data);
            frames.push_back(parsed);
        }
    }
    return frames;
}

void COMPortManager::closePorts() {
    if (hSendPort != INVALID_HANDLE_VALUE) {
        CloseHandle(hSendPort);
        hSendPort = INVALID_HANDLE_VALUE;
    }
    if (hReceivePort != INVALID_HANDLE_VALUE) {
        CloseHandle(hReceivePort);
        hReceivePort = INVALID_HANDLE_VALUE;
    }
}