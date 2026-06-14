#pragma once

// serial_framing — see ARCHITECTURE.md §7.5
//
// Byte-stream framing for SerialTransport (UART / LoRa / XBee / RFM).
// UDP gives us packet boundaries for free; serial does not — bytes
// arrive piecewise and may interleave with line noise, so we frame
// every outbound payload with:
//
//   0       1     SYNC_BYTE_1     0xA5
//   1       1     SYNC_BYTE_2     0x5A
//   2       2     length (LE)     payload byte count
//   4       N     payload         caller's bytes (typically a udp_wire frame)
//   4+N     0..   next frame starts here
//
// Decoder is a small state machine — feed bytes as they arrive,
// completed frames invoke a callback. Resyncs on bad sync bytes by
// dropping until the next 0xA5 0x5A pair is seen. No CRC in v0.3
// first slice; line-noise robustness lands in a follow-up.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mith {

namespace serial_framing {

inline constexpr std::uint8_t SYNC_BYTE_1     = 0xA5;
inline constexpr std::uint8_t SYNC_BYTE_2     = 0x5A;
inline constexpr std::size_t  FRAME_HEADER_BYTES = 4;    // 2 sync + 2 length
inline constexpr std::size_t  MAX_PAYLOAD_BYTES  = 1500; // safe LoRa upper bound

// Encode `payload[0..len-1]` into `out` as a complete framed packet.
// Returns the total bytes written, or 0 if `cap` is too small or `len`
// exceeds MAX_PAYLOAD_BYTES.
std::size_t encode(const std::uint8_t* payload, std::size_t len,
                   std::uint8_t* out,           std::size_t cap) noexcept;

// Stream decoder.
class Parser {
public:
    using OnFrame = std::function<void(const std::uint8_t*, std::size_t)>;

    explicit Parser(std::size_t max_payload = MAX_PAYLOAD_BYTES);

    // Feed `n` bytes from `bytes`. For each fully-received frame,
    // `cb` is invoked synchronously with a pointer into Parser's
    // internal buffer — the pointer is valid only for the duration
    // of the callback.
    void feed(const std::uint8_t* bytes, std::size_t n, const OnFrame& cb);

    // Observability — total successfully-decoded frames and total bytes
    // discarded due to sync errors / oversize length fields.
    std::uint64_t frames_decoded() const noexcept { return frames_decoded_; }
    std::uint64_t bytes_dropped()  const noexcept { return bytes_dropped_;  }

    // Reset to WAIT_SYNC1 state. Doesn't clear counters.
    void reset() noexcept;

private:
    enum class State : std::uint8_t {
        WaitSync1,
        WaitSync2,
        WaitLenLo,
        WaitLenHi,
        ReadPayload,
    };

    State                       state_ = State::WaitSync1;
    std::uint16_t               expected_len_ = 0;
    std::uint16_t               filled_       = 0;
    std::vector<std::uint8_t>   buffer_;
    std::uint64_t               frames_decoded_ = 0;
    std::uint64_t               bytes_dropped_  = 0;
};

} // namespace serial_framing
} // namespace mith
