#include "mith/identity/ed25519.h"

#include <cstring>
#include <vector>

// TweetNaCl is vendored C. The header chains macros (crypto_sign →
// crypto_sign_ed25519 → crypto_sign_ed25519_tweet) so the .c file emits
// the fully-suffixed symbols. Declare those directly here — we don't
// include tweetnacl.h to avoid pulling its global #define soup into the
// rest of the runtime.
extern "C" {
int crypto_sign_ed25519_tweet_keypair(unsigned char* pk, unsigned char* sk);
int crypto_sign_ed25519_tweet(unsigned char* sm, unsigned long long* smlen,
                              const unsigned char* m, unsigned long long mlen,
                              const unsigned char* sk);
int crypto_sign_ed25519_tweet_open(unsigned char* m, unsigned long long* mlen,
                                    const unsigned char* sm, unsigned long long smlen,
                                    const unsigned char* pk);
}

namespace mith {

IdentityKeyPair generate_identity_keypair() noexcept {
    IdentityKeyPair kp;
    crypto_sign_ed25519_tweet_keypair(kp.public_key.public_key.data(),
                                       kp.private_key.bytes.data());
    return kp;
}

std::array<std::uint8_t, IdentityKey::SIGNATURE_LEN>
sign_payload(const IdentityPrivateKey& sk,
             const std::uint8_t*        payload,
             std::size_t                payload_size) {
    // TweetNaCl's combined-mode sign returns a buffer of size payload_size + 64
    // (signature || payload). We extract the first 64 bytes as the detached
    // signature.
    std::vector<std::uint8_t> sm(payload_size + IdentityKey::SIGNATURE_LEN);
    unsigned long long smlen = 0;
    crypto_sign_ed25519_tweet(sm.data(), &smlen,
                               payload, static_cast<unsigned long long>(payload_size),
                               sk.bytes.data());
    std::array<std::uint8_t, IdentityKey::SIGNATURE_LEN> sig{};
    std::memcpy(sig.data(), sm.data(), IdentityKey::SIGNATURE_LEN);
    return sig;
}

bool verify_signature(const IdentityKey& pk,
                      const std::uint8_t* payload,   std::size_t payload_size,
                      const std::uint8_t* signature, std::size_t signature_size) noexcept {
    if (signature_size != IdentityKey::SIGNATURE_LEN) return false;
    if (payload_size > 0 && payload == nullptr)      return false;
    if (signature == nullptr)                        return false;

    // Reconstruct combined-mode blob: signature || payload.
    std::vector<std::uint8_t> sm(IdentityKey::SIGNATURE_LEN + payload_size);
    std::memcpy(sm.data(), signature, IdentityKey::SIGNATURE_LEN);
    if (payload_size > 0) {
        // GCC -Wstringop-overflow= chokes here computing an absurd upper
        // bound on payload_size; the guard above + sm's pre-sized
        // capacity already cover the actual bound. Suppress the false
        // positive on the one memcpy that trips it.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Wrestrict"
#endif
        std::memcpy(sm.data() + IdentityKey::SIGNATURE_LEN, payload, payload_size);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    }

    std::vector<std::uint8_t> m_out(sm.size());
    unsigned long long mlen = 0;
    const int rc = crypto_sign_ed25519_tweet_open(
        m_out.data(), &mlen,
        sm.data(), static_cast<unsigned long long>(sm.size()),
        pk.public_key.data());
    return rc == 0;
}

// ------------------------------------------------------------------------
// Ed25519IdentityVerifier
// ------------------------------------------------------------------------

bool Ed25519IdentityVerifier::verify(const HierarchicalID& claimed,
                                      const std::uint8_t* payload,   std::size_t payload_size,
                                      const std::uint8_t* signature, std::size_t signature_size) const noexcept {
    for (const auto& p : peers_) {
        if (p.hid == claimed) {
            return verify_signature(p.key, payload, payload_size, signature, signature_size);
        }
    }
    return false;   // unknown peer — reject. Trust policy lives above this.
}

bool Ed25519IdentityVerifier::add_peer(const HierarchicalID& hid, const IdentityKey& key) {
    for (const auto& p : peers_) {
        if (p.hid == hid) {
            // Idempotent if same key; collision if different.
            return p.key.public_key == key.public_key;
        }
    }
    peers_.push_back(PeerEntry{hid, key});
    return true;
}

std::size_t Ed25519IdentityVerifier::known_peer_count() const noexcept {
    return peers_.size();
}

} // namespace mith
