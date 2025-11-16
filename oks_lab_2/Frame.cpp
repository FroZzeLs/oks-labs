#include "Frame.h"
#include "HammingBlock.h"
#include <sstream>
#include <iomanip>
#include <cstring>

static const uint8_t ESC = 0x1B;
static const uint8_t START_FLAG = 0x08;
static const uint8_t END_FLAG = 0x7E;

std::vector<uint8_t> Frame::create_frame() const {
    std::vector<uint8_t> out;
    out.push_back(START_FLAG);

    std::vector<uint8_t> inner;
    inner.push_back(sender);
    inner.push_back(receiver);

    for (int i = 0; i < 8; ++i) {
        inner.push_back(static_cast<uint8_t>((timestamp >> (i * 8)) & 0xFF));
    }

    inner.push_back(seqNumber);

    inner.push_back(static_cast<uint8_t>((dataLen >> 8) & 0xFF));
    inner.push_back(static_cast<uint8_t>(dataLen & 0xFF));

    inner.insert(inner.end(), data.begin(), data.end());

    std::vector<uint8_t> fcs_bytes = HammingBlock::generate_fcs(data);
    inner.insert(inner.end(), fcs_bytes.begin(), fcs_bytes.end());

    byte_stufing(inner, START_FLAG, END_FLAG, out);

    out.push_back(END_FLAG);
    return out;
}

void Frame::byte_stufing(const std::vector<uint8_t>& inner, const uint8_t START_FLAG, const uint8_t END_FLAG, std::vector<uint8_t>& out) const {
    for (uint8_t b : inner) {
        if (b == START_FLAG || b == ESC || b == END_FLAG) {
            out.push_back(ESC);
            out.push_back(b ^ 0x20);
        }
        else {
            out.push_back(b);
        }
    }
}

bool Frame::parse_from_unstuffed(const std::vector<uint8_t>& buf, Frame& outFrame) {
    if (buf.size() < 13) {
        return false;
    }
    size_t idx = 0;
    outFrame.sender = buf[idx++];
    outFrame.receiver = buf[idx++];

    outFrame.timestamp = 0;
    for (int i = 0; i < 8; ++i) {
        outFrame.timestamp |= static_cast<uint64_t>(buf[idx++]) << (i * 8);
    }

    outFrame.seqNumber = buf[idx++];

    outFrame.dataLen = (static_cast<uint16_t>(buf[idx]) << 8) | static_cast<uint16_t>(buf[idx + 1]);
    idx += 2;

    int data_bits_count = outFrame.dataLen * 8;
    int p = 0;
    if (data_bits_count > 0) {
        while ((1 << p) < (data_bits_count + p + 1)) {
            p++;
        }
    }
    size_t fcs_size_bytes = (p + 1 + 7) / 8;

    if (idx + outFrame.dataLen + fcs_size_bytes > buf.size()) {
        return false;
    }

    outFrame.data.assign(buf.begin() + idx, buf.begin() + idx + outFrame.dataLen);
    idx += outFrame.dataLen;

    outFrame.fcs.assign(buf.begin() + idx, buf.begin() + idx + fcs_size_bytes);

    return true;
}

bool Frame::de_byte_stuffing(const std::vector<uint8_t>& raw, Frame& outFrame) {
    std::vector<uint8_t> unstuffed;
    if (raw.size() < 2) {
        return false;
    }
    size_t i = 1;
    while (i < raw.size() - 1) {
        uint8_t b = raw[i++];
        if (b == ESC) {
            if (i >= raw.size() - 1) {
                return false;
            }
            b = raw[i++] ^ 0x20;
        }
        unstuffed.push_back(b);
    }
    return parse_from_unstuffed(unstuffed, outFrame);
}