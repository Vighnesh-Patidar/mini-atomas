#include "doctest.h"

#include "mith/identity/ed25519.h"
#include "mith/identity/hierarchical_id.h"

#include <array>
#include <cstdint>

#ifdef MITH_AUTH_ENABLED

using mith::Ed25519IdentityVerifier;
using mith::generate_identity_keypair;
using mith::HierarchicalID;
using mith::IdentityKey;
using mith::IdentityKeyPair;
using mith::IdentityPrivateKey;
using mith::sign_payload;
using mith::SwarmID;
using mith::verify_signature;

TEST_CASE("generate_identity_keypair produces non-nil public key") {
    const auto kp = generate_identity_keypair();
    bool any_nonzero = false;
    for (auto b : kp.public_key.public_key) {
        if (b != 0u) { any_nonzero = true; break; }
    }
    CHECK(any_nonzero);

    bool sk_any_nonzero = false;
    for (auto b : kp.private_key.bytes) {
        if (b != 0u) { sk_any_nonzero = true; break; }
    }
    CHECK(sk_any_nonzero);
}

TEST_CASE("generate_identity_keypair returns distinct keypairs across calls") {
    const auto a = generate_identity_keypair();
    const auto b = generate_identity_keypair();
    CHECK(a.public_key.public_key != b.public_key.public_key);
}

TEST_CASE("sign then verify the same payload returns true") {
    const auto kp = generate_identity_keypair();
    const std::uint8_t payload[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const auto sig = sign_payload(kp.private_key, payload, sizeof(payload));

    CHECK(verify_signature(kp.public_key,
                            payload,   sizeof(payload),
                            sig.data(), sig.size()));
}

TEST_CASE("verify with the wrong public key returns false") {
    const auto signer   = generate_identity_keypair();
    const auto attacker = generate_identity_keypair();
    const std::uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};

    const auto sig = sign_payload(signer.private_key, payload, sizeof(payload));

    CHECK(verify_signature(signer.public_key,
                            payload, sizeof(payload),
                            sig.data(), sig.size()));
    CHECK_FALSE(verify_signature(attacker.public_key,
                                  payload, sizeof(payload),
                                  sig.data(), sig.size()));
}

TEST_CASE("tampered payload fails verification") {
    const auto kp = generate_identity_keypair();
    std::uint8_t payload[] = {10, 20, 30, 40, 50};
    const auto sig = sign_payload(kp.private_key, payload, sizeof(payload));

    payload[2] ^= 0x01;   // flip one bit

    CHECK_FALSE(verify_signature(kp.public_key,
                                  payload, sizeof(payload),
                                  sig.data(), sig.size()));
}

TEST_CASE("tampered signature fails verification") {
    const auto kp = generate_identity_keypair();
    const std::uint8_t payload[] = {0, 1, 2, 3};
    auto sig = sign_payload(kp.private_key, payload, sizeof(payload));

    sig[0] ^= 0x01;   // flip one bit

    CHECK_FALSE(verify_signature(kp.public_key,
                                  payload, sizeof(payload),
                                  sig.data(), sig.size()));
}

TEST_CASE("empty payload signs and verifies correctly") {
    const auto kp = generate_identity_keypair();
    const auto sig = sign_payload(kp.private_key, nullptr, 0);

    CHECK(verify_signature(kp.public_key,
                            nullptr, 0,
                            sig.data(), sig.size()));
}

TEST_CASE("verify rejects signatures of the wrong length") {
    const auto kp = generate_identity_keypair();
    const std::uint8_t payload[] = {1, 2, 3};
    auto sig = sign_payload(kp.private_key, payload, sizeof(payload));

    // Truncated signature.
    CHECK_FALSE(verify_signature(kp.public_key,
                                  payload, sizeof(payload),
                                  sig.data(), 63u));
}

TEST_CASE("verify rejects null signature pointer") {
    const auto kp = generate_identity_keypair();
    const std::uint8_t payload[] = {1};
    CHECK_FALSE(verify_signature(kp.public_key,
                                  payload, sizeof(payload),
                                  nullptr, IdentityKey::SIGNATURE_LEN));
}

// ------------------------------------------------------------------------
// Ed25519IdentityVerifier
// ------------------------------------------------------------------------

TEST_CASE("Ed25519IdentityVerifier rejects unknown peers") {
    Ed25519IdentityVerifier v;
    CHECK(v.known_peer_count() == 0u);

    const auto kp  = generate_identity_keypair();
    const auto hid = HierarchicalID::generate(SwarmID{1});
    const std::uint8_t payload[] = {1, 2, 3};
    const auto sig = sign_payload(kp.private_key, payload, sizeof(payload));

    CHECK_FALSE(v.verify(hid, payload, sizeof(payload), sig.data(), sig.size()));
}

TEST_CASE("Ed25519IdentityVerifier: add_peer + verify accepts the registered peer") {
    Ed25519IdentityVerifier v;
    const auto kp  = generate_identity_keypair();
    const auto hid = HierarchicalID::generate(SwarmID{1});

    CHECK(v.add_peer(hid, kp.public_key));
    CHECK(v.known_peer_count() == 1u);

    const std::uint8_t payload[] = {0xAB, 0xCD, 0xEF};
    const auto sig = sign_payload(kp.private_key, payload, sizeof(payload));
    CHECK(v.verify(hid, payload, sizeof(payload), sig.data(), sig.size()));
}

TEST_CASE("Ed25519IdentityVerifier: add_peer is idempotent for the same key") {
    Ed25519IdentityVerifier v;
    const auto kp  = generate_identity_keypair();
    const auto hid = HierarchicalID::generate(SwarmID{1});

    CHECK(v.add_peer(hid, kp.public_key));        // first registration → true
    CHECK(v.add_peer(hid, kp.public_key));        // same key again → true (idempotent)
    CHECK(v.known_peer_count() == 1u);
}

TEST_CASE("Ed25519IdentityVerifier: add_peer returns false on key collision (same HID, different key)") {
    Ed25519IdentityVerifier v;
    const auto kp_a = generate_identity_keypair();
    const auto kp_b = generate_identity_keypair();
    const auto hid  = HierarchicalID::generate(SwarmID{1});

    CHECK(v.add_peer(hid, kp_a.public_key));
    CHECK_FALSE(v.add_peer(hid, kp_b.public_key));   // collision — security event
    CHECK(v.known_peer_count() == 1u);

    // Original key still works; the impersonation attempt does not.
    const std::uint8_t payload[] = {1};
    const auto sig_a = sign_payload(kp_a.private_key, payload, sizeof(payload));
    const auto sig_b = sign_payload(kp_b.private_key, payload, sizeof(payload));
    CHECK(v.verify(hid, payload, sizeof(payload), sig_a.data(), sig_a.size()));
    CHECK_FALSE(v.verify(hid, payload, sizeof(payload), sig_b.data(), sig_b.size()));
}

TEST_CASE("Ed25519IdentityVerifier: multiple peers each verify independently") {
    Ed25519IdentityVerifier v;
    constexpr int N = 5;
    std::array<IdentityKeyPair,  N> kps;
    std::array<HierarchicalID,   N> hids;

    for (int i = 0; i < N; ++i) {
        kps[i]  = generate_identity_keypair();
        hids[i] = HierarchicalID::generate(SwarmID{static_cast<std::uint16_t>(i + 1)});
        REQUIRE(v.add_peer(hids[i], kps[i].public_key));
    }
    CHECK(v.known_peer_count() == static_cast<std::size_t>(N));

    const std::uint8_t payload[] = {0xAA, 0xBB};
    for (int signer = 0; signer < N; ++signer) {
        const auto sig = sign_payload(kps[signer].private_key, payload, sizeof(payload));
        for (int verifier = 0; verifier < N; ++verifier) {
            const bool same = (signer == verifier);
            const bool ok = v.verify(hids[verifier], payload, sizeof(payload),
                                      sig.data(), sig.size());
            CHECK(ok == same);
        }
    }
}

TEST_CASE("Ed25519 signature length matches IdentityKey::SIGNATURE_LEN") {
    static_assert(IdentityKey::SIGNATURE_LEN == 64u);
    const auto kp = generate_identity_keypair();
    const std::uint8_t payload[] = {1};
    const auto sig = sign_payload(kp.private_key, payload, sizeof(payload));
    CHECK(sig.size() == IdentityKey::SIGNATURE_LEN);
}

#endif // MITH_AUTH_ENABLED
