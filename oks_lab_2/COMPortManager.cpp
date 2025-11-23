#include "COMPortManager.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <random>
#include <cmath>
#include <iomanip>

static const uint8_t FRAME_START_FLAG = 0x08;
static const uint8_t FRAME_END_FLAG = 0x7E;

COMPortManager::COMPortManager() :
    hSendPort(INVALID_HANDLE_VALUE),
    hReceivePort(INVALID_HANDLE_VALUE),
    currentSendPort(""),
    currentReceivePort(""),
    currentBaudRate(9600),
    stopReceiverThread(false),
    receiverStateInFrame(false) {
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

bool COMPortManager::openPort(const std::string& portName, HANDLE& hPort) {
    std::string fullPortName = "\\\\.\\" + portName;
    hPort = CreateFileA(fullPortName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hPort == INVALID_HANDLE_VALUE) return false;

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
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 10;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 10;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hPort, &timeouts);

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
    if (openPort(portName, hSendPort)) {
        currentSendPort = portName;
        return true;
    }
    return false;
}

bool COMPortManager::setReceivePort(const std::string& portName) {
    stopReceiverThread = true;
    if (receiverThread.joinable()) receiverThread.join();
    if (hReceivePort != INVALID_HANDLE_VALUE) CloseHandle(hReceivePort);

    if (openPort(portName, hReceivePort)) {
        currentReceivePort = portName;
        stopReceiverThread = false;
        receiverThread = std::thread(&COMPortManager::receiverThreadFunc, this);
        return true;
    }
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

CSMA::Stats COMPortManager::getGlobalStats() const { return globalStats; }
CSMA::Stats COMPortManager::getLastSessionStats() const { return lastSessionStats; }
void COMPortManager::resetGlobalStats() { globalStats = CSMA::Stats(); }

void COMPortManager::closePorts() {
    stopReceiverThread = true;
    if (receiverThread.joinable()) receiverThread.join();

    if (hSendPort != INVALID_HANDLE_VALUE) { CloseHandle(hSendPort); hSendPort = INVALID_HANDLE_VALUE; }
    if (hReceivePort != INVALID_HANDLE_VALUE) { CloseHandle(hReceivePort); hReceivePort = INVALID_HANDLE_VALUE; }
}

bool COMPortManager::writeByte(HANDLE hPort, uint8_t byte) {
    DWORD bw = 0;
    return WriteFile(hPort, &byte, 1, &bw, NULL) && bw == 1;
}

bool COMPortManager::readByte(HANDLE hPort, uint8_t& byte) {
    DWORD br = 0;
    if (ReadFile(hPort, &byte, 1, &br, NULL) && br == 1) return true;
    return false;
}

void COMPortManager::sendJamSignal() {
    for (int i = 0; i < CSMA::JAM_LENGTH; ++i) {
        writeByte(hSendPort, CSMA::JAM);
    }
    std::lock_guard<std::mutex> lock(outputMutex);
    std::cout << "Отправка JAM-сигнала..." << std::endl;
}

// --- ФУНКЦИЯ ИСКАЖЕНИЯ (Восстановлена) ---
void COMPortManager::distort_payload(std::vector<uint8_t>& payload) {
    if (payload.empty()) {
        return;
    }

    static thread_local std::mt19937 rng(std::random_device{}());

    std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
    double p = prob_dist(rng);

    // Вероятность: 85% - 1 бит, 15% - 2 бита (как было у вас)
    int bits_to_flip = (p < 0.85) ? 1 : 2;

    size_t total_bits = payload.size() * 8;
    std::uniform_int_distribution<size_t> bit_pos_dist(0, total_bits - 1);

    size_t pos1 = bit_pos_dist(rng);

    // Искажение первого бита
    payload[pos1 / 8] ^= (1 << (pos1 % 8));

    if (bits_to_flip == 2) {
        size_t pos2;
        do {
            pos2 = bit_pos_dist(rng);
        } while (pos1 == pos2);

        // Искажение второго бита
        payload[pos2 / 8] ^= (1 << (pos2 % 8));
    }
}

bool COMPortManager::sendMessage(const std::string& message, DWORD* bytesWrittenPtr) {
    if (hSendPort == INVALID_HANDLE_VALUE) return false;

    lastSessionStats = CSMA::Stats();

    const size_t maxDataPerFrame = 32;
    size_t totalWritten = 0;
    uint8_t seq = 1;

    for (size_t offset = 0; offset < message.size(); offset += maxDataPerFrame) {
        size_t len = std::min<size_t>(maxDataPerFrame, message.size() - offset);
        Frame frame;
        fill_frame(frame, seq, message, offset, len);
        std::vector<uint8_t> raw = frame.create_frame();
        lastSentRawFrame = raw;

        int attempts = 0;
        bool frameSent = false;

        while (attempts < CSMA::MAX_ATTEMPTS) {
            lastSessionStats.total_attempts++;
            globalStats.total_attempts++;

            // 1. Прослушивание
            {
                std::lock_guard<std::mutex> lock(outputMutex);
                std::cout << "Прослушивание канала..." << std::endl;
            }

            PurgeComm(hSendPort, PURGE_RXCLEAR);
            writeByte(hSendPort, CSMA::ENQ);

            uint8_t response = 0;
            bool channelFree = false;

            auto startWait = std::chrono::steady_clock::now();
            while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startWait).count() < CSMA::SLOT_TIME_MS) {
                if (readByte(hSendPort, response)) {
                    if (response == CSMA::ACK) {
                        channelFree = true;
                        break;
                    }
                }
            }

            if (!channelFree) {
                lastSessionStats.busy_events++;
                globalStats.busy_events++;

                std::lock_guard<std::mutex> lock(outputMutex);
                std::cout << "Канал занят. Ожидание..." << std::endl;
                Sleep(CSMA::SLOT_TIME_MS);
                continue;
            }
            else {
                std::lock_guard<std::mutex> lock(outputMutex);
                std::cout << "Канал свободен. Передача данных..." << std::endl;
            }

            // 2. Передача
            bool collisionDetected = false;
            for (uint8_t b : raw) {
                writeByte(hSendPort, b);

                uint8_t signal = 0;
                Sleep(2);
                if (readByte(hSendPort, signal)) {
                    if (signal == CSMA::COL) {
                        collisionDetected = true;
                        break;
                    }
                }
            }

            if (collisionDetected) {
                lastSessionStats.collisions++;
                globalStats.collisions++;

                attempts++;
                sendJamSignal();

                lastSessionStats.jam_sent++;
                globalStats.jam_sent++;

                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    std::cout << "Обнаружена коллизия! Попытка: " << attempts << std::endl;
                }

                if (attempts >= CSMA::MAX_ATTEMPTS) break;

                int k = std::min<int>(attempts, CSMA::MAX_BACKOFF_LIMIT);
                int max_r = (1 << k);
                int r = rand() % (max_r + 1);
                int delay = r * CSMA::SLOT_TIME_MS;

                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    std::cout << "Задержка: " << delay << " мс" << std::endl;
                }
                Sleep(delay);
            }
            else {
                frameSent = true;
                lastSessionStats.packets_sent++;
                globalStats.packets_sent++;

                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    std::cout << "Кадр передан успешно." << std::endl;
                }
                break;
            }
        }

        if (!frameSent) {
            std::lock_guard<std::mutex> lock(outputMutex);
            std::cout << "Ошибка: превышено число попыток отправки." << std::endl;
            return false;
        }
        totalWritten += raw.size();
    }

    if (bytesWrittenPtr) *bytesWrittenPtr = static_cast<DWORD>(totalWritten);
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

void COMPortManager::receiverThreadFunc() {
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    receiverBuffer.clear();
    receiverStateInFrame = false;
    bool jamSequenceActive = false;

    while (!stopReceiverThread) {
        if (hReceivePort == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        uint8_t byte;
        if (readByte(hReceivePort, byte)) {

            if (byte == CSMA::ENQ) {
                double roll = dist(rng);
                if (roll < CSMA::PROB_CHANNEL_BUSY) {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    std::cout << "Канал занят (ENQ)." << std::endl; // Убрано слово "Среда"
                }
                else {
                    writeByte(hReceivePort, CSMA::ACK);
                }
                jamSequenceActive = false;
                continue;
            }

            if (byte == CSMA::JAM) {
                if (!jamSequenceActive) {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    std::cout << "Получен JAM-сигнал." << std::endl;
                    jamSequenceActive = true;
                }
                receiverBuffer.clear();
                receiverStateInFrame = false;
                continue;
            }

            jamSequenceActive = false;

            double collisionRoll = dist(rng);
            if (collisionRoll < CSMA::PROB_COLLISION) {
                std::lock_guard<std::mutex> lock(outputMutex);
                std::cout << "Коллизия!" << std::endl; // Убрано слово "Среда"

                writeByte(hReceivePort, CSMA::COL);
                receiverBuffer.clear();
                receiverStateInFrame = false;
                continue;
            }

            if (byte == FRAME_START_FLAG) {
                receiverBuffer.clear();
                receiverStateInFrame = true;
                receiverBuffer.push_back(byte);
                continue;
            }

            if (receiverStateInFrame) {
                receiverBuffer.push_back(byte);
                if (byte == FRAME_END_FLAG) {
                    Frame parsed;
                    if (Frame::de_byte_stuffing(receiverBuffer, parsed)) {

                        // ПРИМЕНЯЕМ ИСКАЖЕНИЕ ПЕРЕД СОХРАНЕНИЕМ
                        distort_payload(parsed.data);

                        std::lock_guard<std::mutex> qLock(queueMutex);
                        receivedFrameQueue.push(parsed);
                    }
                    receiverBuffer.clear();
                    receiverStateInFrame = false;
                }
            }
        }
    }
}

std::vector<Frame> COMPortManager::receiveAllFrames() {
    std::vector<Frame> frames;
    std::lock_guard<std::mutex> lock(queueMutex);
    while (!receivedFrameQueue.empty()) {
        frames.push_back(receivedFrameQueue.front());
        receivedFrameQueue.pop();
    }
    return frames;
}