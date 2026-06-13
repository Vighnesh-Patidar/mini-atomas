#pragma once

// Ed25519 signing primitives — see ARCHITECTURE.md §3.3
//
// v0.2 first crypto slice. Backed by vendored TweetNaCl (public domain,
// authored by Bernstein/Lange/Schwabe/etc.) under third_party/tweetnacl/.
// Gated by MITH_ENABLE_AUTH — when OFF, this header still compiles but
// the API symbols are unresolved at link time.
//
// API:
//   generate_identity_keypair()   produces a fresh Ed25519 keypair.
//   sign_payload(sk, ptr, size)   detached 64-byte signature.
//   verify_signature(pk, ...)     returns true iff the signature is valid
//                                  for the (payload, pubkey) pair.
//
// All operations use TweetNaCl's combined-mode sign internally and present
// a detached-signature API to the caller. The private key is the 64-byte
// expanded form (32-byte seed + 32-byte pubkey) per Ed25519 convention;
// callers do not need to know the layout.
//
// The companion Ed25519IdentityVerifier ties this into the IdentityVerifier
// (§3.3) interface — drops into the transport layer's signed-mode path
// once a key-resolution side table is in place (v0.2 BeaconSystem update).

#include "mith/identity/identity_auth.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mith {

struct IdentityPrivateKey {
    // 64-byte expanded form: 32-byte seed || 32-byte derived public key.
    // NEVER serialised to the wire. Caller-owned, lives in secure
    // storage outside the runtime.
    std::array<std::uint8_t, 64> bytes{};
};

struct IdentityKeyPair {
    IdentityKey        public_key;
    IdentityPrivateKey private_key;
};

// Generate a fresh Ed25519 keypair. Entropy from std::random_device via
// the randombytes() shim. The ChaCha20 CSPRNG (§3.3 final design) will
// replace the entropy backing in a follow-up.
IdentityKeyPair generate_identity_keypair() noexcept;

// Produce a detached 64-byte signature over the payload.
std::array<std::uint8_t, IdentityKey::SIGNATURE_LEN>
sign_payload(const IdentityPrivateKey& sk,
             const std::uint8_t*        payload,
             std::size_t                payload_size);

// Verify a detached signature. Returns true iff valid for (payload, pk).
bool verify_signature(const IdentityKey& pk,
                      const std::uint8_t* payload,   std::size_t payload_size,
                      const std::uint8_t* signature, std::size_t signature_size) noexcept;

// Concrete verifier backed by Ed25519. The base class verify() looks up
// the IdentityKey for `claimed` in an internal table (populated by the
// transport / BeaconSystem when v0.2 wires it up). For v0.2 first slice
// the verifier accepts a single peer registration via add_peer() — the
// fuller key-resolution path lands alongside signed-mode beacons.
class Ed25519IdentityVerifier : public IdentityVerifier {
public:
    Ed25519IdentityVerifier() = default;

    bool verify(const HierarchicalID& claimed,
                const std::uint8_t* payload,   std::size_t payload_size,
                const std::uint8_t* signature, std::size_t signature_size) const noexcept override;

    // Register a peer's public key. Returns true if newly added; false if
    // the HID already had a key registered (idempotent: same key) or if
    // a different key is now claimed for the same HID (collision — caller
    // should treat this as a security event).
    bool add_peer(const HierarchicalID& hid, const IdentityKey& key);

    std::size_t known_peer_count() const noexcept;

private:
    // Tiny linear table for v0.2 first slice. Swap for an unordered_map
    // when the swarm sizes warrant — for ~10s of neighbours, linear scan
    // is fine and cache-friendlier.
    struct PeerEntry {
        HierarchicalID hid;
        IdentityKey    key;
    };
    std::vector<PeerEntry> peers_;
};

} // namespace mith
