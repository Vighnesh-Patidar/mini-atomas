#pragma once

// BinaryTraceSink — see ARCHITECTURE.md §14.4, §16 v0.3
//
// Allocation-free, fixed-capacity ring buffer sink for microcontroller-
// tier targets where JsonTraceSink's per-emit std::string allocations are
// too expensive. Frames are fixed-size (FRAME_BYTES = 256 by default);
// the ring holds N frames pre-allocated at construction. Overflow drops
// the oldest frame, increments a counter, and continues — emit() never
// blocks and never allocates.
//
// Wire layout per frame (little-endian, byte-packed):
//
//   offset  size  field
//   0       8     wall_seq           monotonic sequence number per emit
//   8       1     level              TraceLevel enum
//   9       1     field_count        number of TraceFields in this frame
//   10      2     event_len          length of event-name string
//   12      E     event_name         E = min(event_len, MAX_NAME_LEN)
//   12+E    F*16  fields[]           F = field_count; each = TraceField
//   ...     pad   zero-fill to FRAME_BYTES
//
// Each field on the wire (16 bytes):
//   0       1     kind (TraceField::Kind)
//   1       1     key_len
//   2       6     key bytes  (truncated; not null-terminated)
//   8       8     value      (one of i64/u64/f64/bool — strings are
//                              stored elsewhere or truncated to 6 bytes
//                              by reusing the key slot at offset 2)
//
// String values larger than 6 bytes are truncated. This is acceptable
// for the embedded-tier target — operators decode frames offline with a
// helper that lifts numeric values cleanly and surfaces truncated
// strings as such. Anything that needs full strings should use
// JsonTraceSink on the SoC tier.

#include "mith/core/trace_sink.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mith {

class BinaryTraceSink : public TraceSink {
public:
    static constexpr std::size_t FRAME_BYTES   = 256;
    static constexpr std::size_t MAX_NAME_LEN  = 64;
    static constexpr std::size_t FIELD_BYTES   = 16;
    static constexpr std::size_t MAX_KEY_BYTES = 6;

    // Construct a sink holding `capacity` frames (capacity * FRAME_BYTES
    // bytes total). One-shot allocation here; emit() never allocates.
    // capacity must be a power of two for cheap modulo via mask;
    // non-power-of-two callers get the next-lower power.
    explicit BinaryTraceSink(std::size_t capacity = 256);

    using TraceSink::emit;   // bring convenience overloads back

    void emit(TraceLevel level,
              std::string_view event,
              const TraceField* fields,
              std::size_t       field_count) noexcept override;

    // Number of frames currently held (≤ capacity). After overflow this
    // saturates at capacity.
    std::size_t size() const noexcept;

    // Capacity in frames.
    std::size_t capacity() const noexcept { return capacity_; }

    // Bytes occupied by the ring buffer (capacity * FRAME_BYTES).
    std::size_t buffer_bytes() const noexcept { return buffer_.size(); }

    // Number of frames dropped because the ring filled up. Monotonic
    // from construction.
    std::uint64_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    // Total emits since construction. dropped() + size() ≤ this.
    std::uint64_t total_emits() const noexcept { return total_emits_.load(std::memory_order_relaxed); }

    // Direct read access to the oldest frame in the ring. Returns nullptr
    // if the sink is empty. Used by the offline decoder.
    const std::uint8_t* peek_oldest() const noexcept;

    // Drop the oldest frame. Returns false if the sink is empty.
    bool pop_oldest() noexcept;

    // Reset the ring. Counters preserved.
    void clear() noexcept;

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t               capacity_;     // frames
    std::size_t               mask_;         // capacity_ - 1
    std::size_t               head_         = 0;   // next slot to write
    std::size_t               size_         = 0;   // frames held
    std::atomic<std::uint64_t> wall_seq_    {0};
    std::atomic<std::uint64_t> dropped_     {0};
    std::atomic<std::uint64_t> total_emits_ {0};
};

} // namespace mith
