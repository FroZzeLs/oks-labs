#pragma once
#include <cstdint>

namespace CSMA {
    // Управляющие байты
    const uint8_t ENQ = 0x05;
    const uint8_t ACK = 0x06;
    const uint8_t COL = 0x15;
    const uint8_t JAM = 0x18;

    // Параметры
    const int SLOT_TIME_MS = 30;
    const int MAX_ATTEMPTS = 16;
    const int MAX_BACKOFF_LIMIT = 10;
    const int JAM_LENGTH = 4;

    // Вероятности
    const double PROB_CHANNEL_BUSY = 0.75;
    const double PROB_COLLISION = 0.1;

    // Обновленная структура статистики
    struct Stats {
        int packets_sent = 0;       // Успешно переданные кадры
        int busy_events = 0;        // Сколько раз канал был занят
        int collisions = 0;         // Сколько раз была коллизия
        int jam_sent = 0;           // Сколько раз отправлен JAM
        int total_attempts = 0;     // Общее число попыток захвата канала
    };
}