#pragma once
#include <vector>
#include <cstdint>

// Структура для результата декодирования
struct HammingBlockResult {
    std::vector<uint8_t> corrected_data;
    bool single_error_corrected;
    bool double_error_detected;
};

namespace HammingBlock {
    // Генерирует FCS для всего блока данных.
    std::vector<uint8_t> generate_fcs(const std::vector<uint8_t>& data);

    // Декодирует и исправляет данные, используя полученный FCS.
    HammingBlockResult decode_and_correct(
        const std::vector<uint8_t>& received_data,
        const std::vector<uint8_t>& received_fcs
    );
}