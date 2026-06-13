// randombytes() implementation TweetNaCl requires the caller to supply.
//
// v0.2 first crypto slice uses std::random_device directly. The ChaCha20
// CSPRNG that §3.3 promises (seeded once from random_device) lands in
// the follow-up — this file is the seam where that swap will happen.
//
// std::random_device on the platforms in §15 (Linux ≥ 5.3, recent
// glibc/musl) is CSPRNG-backed (/dev/urandom equivalent). On platforms
// where it isn't, the runtime should not produce identity material —
// std::random_device may throw, and that throw will propagate.

#include <random>

extern "C" {

void randombytes(unsigned char* x, unsigned long long xlen) {
    thread_local std::random_device rd;
    // result_type is 32-bit; pull 4 bytes at a time.
    using rd_t = std::random_device::result_type;
    static_assert(sizeof(rd_t) >= 4, "random_device::result_type must be >= 32 bits");

    unsigned long long filled = 0;
    while (filled < xlen) {
        const rd_t v = rd();
        const unsigned long long take =
            (sizeof(rd_t) < (xlen - filled)) ? sizeof(rd_t) : (xlen - filled);
        for (unsigned long long i = 0; i < take; ++i) {
            x[filled + i] = static_cast<unsigned char>((v >> (i * 8)) & 0xFFu);
        }
        filled += take;
    }
}

} // extern "C"
