#include "Frame.h"
#include <sstream>
#include <iomanip>

static const uint8_t ESC = 0x1B;

std::vector<uint8_t> Frame::create_frame() const {

    const uint8_t START_FLAG = 0x08;
    const uint8_t END_FLAG = 0x7E;
    std::vector<uint8_t> out;
    out.push_back(START_FLAG);

    std::vector<uint8_t> inner;
    inner.push_back(sender);
    inner.push_back(receiver);

    for (int i = 0; i < 8; ++i)
        inner.push_back((timestamp >> (i * 8)) & 0xFF);

    inner.push_back(seqNumber);
    inner.insert(inner.end(), data.begin(), data.end());

    byte_stufing(inner, START_FLAG, END_FLAG, out);


    out.push_back(END_FLAG);
    return out;
}

void Frame::byte_stufing(std::vector<uint8_t>& inner, const uint8_t START_FLAG, const uint8_t END_FLAG, std::vector<uint8_t>& out) const
{
    for (uint8_t b : inner) {
        if (b == START_FLAG || b == ESC || b == END_FLAG) {
            out.push_back(ESC);
            out.push_back(b ^ 0x20);
        }
        out.push_back(b);
    }
}

bool Frame::parse_from_unstuffed(const std::vector<uint8_t>& buf, Frame& outFrame) {
    size_t idx = 0;
    outFrame.sender = buf[idx++];
    outFrame.receiver = buf[idx++];

    outFrame.timestamp = 0;
    for (int i = 0; i < 8; ++i)
        outFrame.timestamp |= static_cast<uint64_t>(buf[idx++]) << (i * 8);

    outFrame.seqNumber = buf[idx++];
    outFrame.data.assign(buf.begin() + idx, buf.end());
    return true;
}

bool Frame::de_byte_stuffing(const std::vector<uint8_t>& raw, Frame& outFrame) {
    std::vector<uint8_t> unstuffed;
    size_t i = 1;
    while (i < raw.size() - 1) {
        uint8_t b = raw[i++];
        if (b == ESC) {
            if (i >= raw.size() - 1)
                return false;
            b = raw[i++] ^ 0x20;
        }
        unstuffed.push_back(b);
    }
    return parse_from_unstuffed(unstuffed, outFrame);
}
