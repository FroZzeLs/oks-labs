#include "HammingBlock.h"
#include <numeric>
#include <algorithm>
#include <stdexcept>

// --- Вспомогательные функции для работы с битами ---
namespace {
    bool get_bit(const std::vector<uint8_t>& bytes, int bit_position) {
        if (bit_position < 0) {
            return false;
        }
        int byte_index = bit_position / 8;
        int bit_in_byte = bit_position % 8;
        if (byte_index >= bytes.size()) {
            return false;
        }
        return (bytes[byte_index] >> bit_in_byte) & 1;
    }

    void flip_bit(std::vector<uint8_t>& bytes, int bit_position) {
        if (bit_position < 0) {
            return;
        }
        int byte_index = bit_position / 8;
        int bit_in_byte = bit_position % 8;
        if (byte_index >= bytes.size()) {
            return;
        }
        bytes[byte_index] ^= (1 << bit_in_byte);
    }

    // Вспомогательная функция, чтобы узнать, является ли число степенью двойки
    bool is_power_of_two(int n) {
        return (n > 0) && ((n & (n - 1)) == 0);
    }
}

namespace HammingBlock {

    // --- ГЕНЕРАЦИЯ СИСТЕМАТИЧЕСКОГО КОДА ---
    std::vector<uint8_t> generate_fcs(const std::vector<uint8_t>& data) {
        int data_bits_count = data.size() * 8;
        if (data_bits_count == 0) {
            return {};
        }

        int p = 0; // Количество битов четности Хэмминга
        while ((1 << p) < (data_bits_count + p + 1)) {
            p++;
        }

        std::vector<bool> parity_bits(p, false);

        // 1. Вычисляем биты четности Хэмминга
        int data_bit_idx = 0;
        for (int i = 1; i <= data_bits_count + p; ++i) {
            if (is_power_of_two(i)) {
                continue; // Пропускаем позиции для битов четности
            }

            if (get_bit(data, data_bit_idx)) {
                for (int j = 0; j < p; ++j) {
                    if ((i >> j) & 1) {
                        parity_bits[j] = !parity_bits[j];
                    }
                }
            }
            data_bit_idx++;
        }

        // 2. Вычисляем общий бит четности (для SECDED)
        bool overall_parity = false;
        for (int i = 0; i < data_bits_count; ++i) {
            if (get_bit(data, i)) {
                overall_parity = !overall_parity;
            }
        }
        for (int i = 0; i < p; ++i) {
            if (parity_bits[i]) {
                overall_parity = !overall_parity;
            }
        }

        // 3. Упаковываем p битов Хэмминга и 1 общий бит в байтовый вектор FCS
        size_t fcs_total_bits = p + 1;
        std::vector<uint8_t> fcs((fcs_total_bits + 7) / 8, 0);
        int fcs_bit_idx = 0;
        for (int i = 0; i < p; ++i) {
            if (parity_bits[i]) {
                fcs[fcs_bit_idx / 8] |= (1 << (fcs_bit_idx % 8));
            }
            fcs_bit_idx++;
        }
        if (overall_parity) {
            fcs[fcs_bit_idx / 8] |= (1 << (fcs_bit_idx % 8));
        }

        return fcs;
    }


    // --- ДЕКОДИРОВАНИЕ СИСТЕМАТИЧЕСКОГО КОДА ---
    HammingBlockResult decode_and_correct(const std::vector<uint8_t>& received_data, const std::vector<uint8_t>& received_fcs) {
        HammingBlockResult result;
        result.corrected_data = received_data;
        result.single_error_corrected = false;
        result.double_error_detected = false;

        int data_bits_count = received_data.size() * 8;
        if (data_bits_count == 0) {
            return result;
        }

        int p = 0;
        while ((1 << p) < (data_bits_count + p + 1)) {
            p++;
        }

        if (received_fcs.size() * 8 < p + 1) {
            result.double_error_detected = true;
            return result;
        }

        // 1. Пересчитываем FCS на основе полученных данных
        std::vector<uint8_t> recalculated_fcs_vec = generate_fcs(received_data);

        // 2. Вычисляем синдром (XOR полученного и пересчитанного FCS)
        int syndrome = 0;
        for (int i = 0; i < p; ++i) {
            if (get_bit(received_fcs, i) != get_bit(recalculated_fcs_vec, i)) {
                syndrome |= (1 << i);
            }
        }

        // 3. Сравниваем общую четность
        bool received_overall_parity = get_bit(received_fcs, p);
        bool recalculated_overall_parity = get_bit(recalculated_fcs_vec, p);
        bool parity_match = (received_overall_parity == recalculated_overall_parity);

        // 4. Принимаем решение
        if (syndrome == 0) {
            if (!parity_match) {
                result.single_error_corrected = true; // Ошибка в общем бите четности
            }
            // else: Ошибок нет
        }
        else {
            if (!parity_match) {
                // Одиночная ошибка
                result.single_error_corrected = true;
                int error_pos = syndrome;

                if (!is_power_of_two(error_pos)) {
                    // Ошибка в бите данных, ищем его индекс
                    int data_bit_to_flip = -1;
                    int current_data_idx = 0;
                    for (int i = 1; i < error_pos; ++i) {
                        if (!is_power_of_two(i)) {
                            current_data_idx++;
                        }
                    }
                    data_bit_to_flip = current_data_idx;

                    flip_bit(result.corrected_data, data_bit_to_flip);
                }
                // Если ошибка в бите четности, данные не трогаем
            }
            else {
                // Двойная ошибка
                result.double_error_detected = true;
            }
        }

        return result;
    }

} // namespace HammingBlock