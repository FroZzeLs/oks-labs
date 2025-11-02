#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Frame {
    uint8_t sender;      // Отправитель
    uint8_t receiver;    // Получатель
    uint64_t timestamp;  // Время отправления
    uint8_t seqNumber;   // Порядковый номер кадра
    std::vector<uint8_t> data; // Данные

    // сериализация кадра с байт-стаффингом
    std::vector<uint8_t> create_frame() const;

    void byte_stufing(std::vector<uint8_t>& inner, const uint8_t START_FLAG, const uint8_t END_FLAG, std::vector<uint8_t>& out) const;

    // распаковка кадра из deframed буфера
    static bool parse_from_unstuffed(const std::vector<uint8_t>& buf, Frame& outFrame);

    // распарсить полностью принятый поток с флагами и ESC
    static bool de_byte_stuffing(const std::vector<uint8_t>& raw, Frame& outFrame);
};
