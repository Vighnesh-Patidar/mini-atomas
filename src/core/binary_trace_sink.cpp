#include "mith/core/binary_trace_sink.h"

#include <algorithm>
#include <cstring>

namespace mith {

namespace {

// Round `n` down to the nearest power of two (≥ 1).
std::size_t floor_pow2(std::size_t n) noexcept {
    if (n == 0) return 1;
    std::size_t p = 1;
    while ((p << 1) > p && (p << 1) <= n) p <<= 1;
    return p;
}

inline void put_u16_le(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}
inline void put_u64_le(std::uint8_t* p, std::uint64_t v) noexcept {
    for (std::size_t i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
    }
}

} // namespace

BinaryTraceSink::BinaryTraceSink(std::size_t capacity)
    : capacity_(floor_pow2(capacity == 0 ? 1u : capacity))
    , mask_(capacity_ - 1) {
    buffer_.assign(capacity_ * FRAME_BYTES, std::uint8_t{0});
}

void BinaryTraceSink::emit(TraceLevel level,
                            std::string_view event,
                            const TraceField* fields,
                            std::size_t       field_count) noexcept {
    if (level == TraceLevel::Off) return;
    total_emits_.fetch_add(1, std::memory_order_relaxed);

    // Allocate the slot. If we're full, evict the oldest (DropOldest)
    // and bump dropped_.
    if (size_ == capacity_) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        // head_ already points at oldest (= overwrite target). size_
        // stays at capacity_.
    } else {
        ++size_;
    }
    std::uint8_t* frame = buffer_.data() + (head_ & mask_) * FRAME_BYTES;
    head_ = (head_ + 1) & mask_;
    std::memset(frame, 0, FRAME_BYTES);

    // Header.
    const std::uint64_t seq = wall_seq_.fetch_add(1, std::memory_order_relaxed);
    put_u64_le(frame + 0, seq);
    frame[8] = static_cast<std::uint8_t>(level);

    // Cap field count to what fits in the frame body.
    const std::size_t header_bytes = 12;
    const std::size_t name_room    = std::min<std::size_t>(event.size(), MAX_NAME_LEN);
    const std::size_t fields_room  = FRAME_BYTES - header_bytes - name_room;
    const std::size_t max_fields   = fields_room / FIELD_BYTES;
    const std::size_t f_count      = std::min(field_count, max_fields);

    frame[9] = static_cast<std::uint8_t>(f_count);
    put_u16_le(frame + 10, static_cast<std::uint16_t>(name_room));

    // Event name.
    if (name_room > 0) {
        std::memcpy(frame + 12, event.data(), name_room);
    }

    // Fields.
    std::uint8_t* field_p = frame + 12 + name_room;
    for (std::size_t i = 0; i < f_count; ++i) {
        const TraceField& f = fields[i];
        field_p[0] = static_cast<std::uint8_t>(f.kind);

        const std::size_t key_len = std::min(f.key.size(), MAX_KEY_BYTES);
        field_p[1] = static_cast<std::uint8_t>(key_len);
        if (key_len > 0) {
            std::memcpy(field_p + 2, f.key.data(), key_len);
        }

        // Value (offset 8..16). Numeric kinds map to 8-byte LE; strings
        // are truncated into the same 8 bytes.
        switch (f.kind) {
            case TraceField::Kind::I64:
                put_u64_le(field_p + 8, static_cast<std::uint64_t>(f.i64_val));
                break;
            case TraceField::Kind::U64:
                put_u64_le(field_p + 8, f.u64_val);
                break;
            case TraceField::Kind::F64: {
                std::uint64_t bits;
                std::memcpy(&bits, &f.f64_val, sizeof bits);
                put_u64_le(field_p + 8, bits);
                break;
            }
            case TraceField::Kind::Bool:
                field_p[8] = f.bool_val ? std::uint8_t{1} : std::uint8_t{0};
                break;
            case TraceField::Kind::Str: {
                const std::size_t copy = std::min<std::size_t>(f.str_val.size(), 8u);
                if (copy > 0) {
                    std::memcpy(field_p + 8, f.str_val.data(), copy);
                }
                break;
            }
        }
        field_p += FIELD_BYTES;
    }
}

std::size_t BinaryTraceSink::size() const noexcept {
    return size_;
}

const std::uint8_t* BinaryTraceSink::peek_oldest() const noexcept {
    if (size_ == 0) return nullptr;
    // Oldest = head_ - size_ (mod capacity_).
    const std::size_t oldest = (head_ + capacity_ - size_) & mask_;
    return buffer_.data() + oldest * FRAME_BYTES;
}

bool BinaryTraceSink::pop_oldest() noexcept {
    if (size_ == 0) return false;
    --size_;
    return true;
}

void BinaryTraceSink::clear() noexcept {
    head_ = 0;
    size_ = 0;
}

} // namespace mith
