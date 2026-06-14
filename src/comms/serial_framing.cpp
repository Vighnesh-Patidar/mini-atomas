#include "mith/comms/serial_framing.h"

#include <cstring>

namespace mith::serial_framing {

std::size_t encode(const std::uint8_t* payload, std::size_t len,
                   std::uint8_t* out,           std::size_t cap) noexcept {
    if (len > MAX_PAYLOAD_BYTES) return 0;
    const std::size_t total = FRAME_HEADER_BYTES + len;
    if (cap < total) return 0;

    out[0] = SYNC_BYTE_1;
    out[1] = SYNC_BYTE_2;
    out[2] = static_cast<std::uint8_t>(len & 0xFFu);
    out[3] = static_cast<std::uint8_t>((len >> 8) & 0xFFu);
    if (len > 0) std::memcpy(out + 4, payload, len);
    return total;
}

Parser::Parser(std::size_t max_payload)
    : buffer_(max_payload, std::uint8_t{0}) {}

void Parser::reset() noexcept {
    state_        = State::WaitSync1;
    expected_len_ = 0;
    filled_       = 0;
}

void Parser::feed(const std::uint8_t* bytes, std::size_t n, const OnFrame& cb) {
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t b = bytes[i];
        switch (state_) {
            case State::WaitSync1:
                if (b == SYNC_BYTE_1) {
                    state_ = State::WaitSync2;
                } else {
                    ++bytes_dropped_;
                }
                break;
            case State::WaitSync2:
                if (b == SYNC_BYTE_2) {
                    state_ = State::WaitLenLo;
                } else if (b == SYNC_BYTE_1) {
                    // Could be the start of a new frame; stay open.
                    ++bytes_dropped_;
                } else {
                    state_ = State::WaitSync1;
                    bytes_dropped_ += 2;   // drop sync1 + this byte
                }
                break;
            case State::WaitLenLo:
                expected_len_ = b;
                state_ = State::WaitLenHi;
                break;
            case State::WaitLenHi:
                expected_len_ |= static_cast<std::uint16_t>(b) << 8;
                if (expected_len_ > buffer_.size()) {
                    // Oversize — drop the frame header and resync.
                    bytes_dropped_ += FRAME_HEADER_BYTES;
                    reset();
                } else if (expected_len_ == 0) {
                    // Zero-length frame — emit immediately.
                    cb(buffer_.data(), 0);
                    ++frames_decoded_;
                    reset();
                } else {
                    filled_ = 0;
                    state_  = State::ReadPayload;
                }
                break;
            case State::ReadPayload:
                buffer_[filled_++] = b;
                if (filled_ == expected_len_) {
                    cb(buffer_.data(), filled_);
                    ++frames_decoded_;
                    reset();
                }
                break;
        }
    }
}

} // namespace mith::serial_framing
