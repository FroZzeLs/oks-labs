#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Frame {
    uint8_t sender;
    uint8_t receiver;
    uint64_t timestamp;
    uint8_t seqNumber;
    uint16_t dataLen;

    std::vector<uint8_t> data;
    std::vector<uint8_t> fcs;

    std::vector<uint8_t> create_frame() const;

    void byte_stufing(const std::vector<uint8_t>& inner, const uint8_t START_FLAG, const uint8_t END_FLAG, std::vector<uint8_t>& out) const;

    static bool parse_from_unstuffed(const std::vector<uint8_t>& buf, Frame& outFrame);
    static bool de_byte_stuffing(const std::vector<uint8_t>& raw, Frame& outFrame);
};