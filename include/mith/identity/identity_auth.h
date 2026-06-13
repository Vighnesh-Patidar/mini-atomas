#pragma once

// Cryptographic identity types — see ARCHITECTURE.md §3.3
//
// v0.1 ships *only* the type surface — IdentityKey is a POD holding an
// Ed25519 public key; IdentityVerifier is the abstract interface that
// signed-mode transports will call to verify per-packet signatures;
// NoopIdentityVerifier is the default that accepts everything (unsigned
// mode, v0.1).
//
// The Ed25519 verification implementation + the vendored ChaCha20 CSPRNG
// land in v0.2 behind MITH_ENABLE_AUTH. The interface and key shape are
// frozen now so v0.2 plugs in without API changes.

#include "mith/identity/hierarchical_id.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mith {

// Public key material for one identity. The private key never appears in
// any serialised form and lives in a separate sender-only struct that's
// not part of the wire format (§3.3).
struct IdentityKey {
    static constexpr std::size_t PUBLIC_KEY_LEN = 32;   // Ed25519
    static constexpr std::size_t SIGNATURE_LEN  = 64;   // Ed25519

    std::array<std::uint8_t, PUBLIC_KEY_LEN> public_key{};
};

// Pluggable verifier. Transports in signed mode invoke verify() on every
// inbound packet; the implementation either:
//   - looks up the IdentityKey for `claimed` from a peer table (typical
//     for fleets with pre-shared keys or attested public keys), or
//   - extracts the key from the packet itself (typical for self-signed
//     identities where UnitID = BLAKE3(public_key)[0..16]).
//
// pointer+size on the byte buffers (not std::span — we're C++17).
class IdentityVerifier {
public:
    virtual ~IdentityVerifier() = default;

    virtual bool verify(const HierarchicalID& claimed,
                        const std::uint8_t* payload,   std::size_t payload_size,
                        const std::uint8_t* signature, std::size_t sig_size) const noexcept = 0;
};

// Default verifier for unsigned mode (v0.1) — accepts everything. Suitable
// for sim, research, single-host, or otherwise trusted networks where
// authentication is out of scope.
//
// The real Ed25519 verifier ships in v0.2 behind MITH_ENABLE_AUTH and
// replaces this as the default when that feature is enabled.
class NoopIdentityVerifier : public IdentityVerifier {
public:
    bool verify(const HierarchicalID&,
                const std::uint8_t*, std::size_t,
                const std::uint8_t*, std::size_t) const noexcept override {
        return true;
    }
};

} // namespace mith
