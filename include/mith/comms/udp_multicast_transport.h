#pragma once

// UDPMulticastTransport — see ARCHITECTURE.md §7.5
//
// Real-network TransportLayer impl. Carries beacons and messages over
// IPv4 UDP multicast — one socket, one group address, two channel
// methods routed via the udp_wire tag discriminator.
//
// Built only when MITH_ENABLE_UDP=ON (CMake option, default ON; see §11).
// Linux POSIX sockets via <sys/socket.h>. Self-filtering relies on the
// caller setting Config::self_id before any beacons fly — beacons whose
// sender HID matches Config::self_id are silently dropped on poll.
//
// Open paths (Config::open()):
//   1. socket(AF_INET, SOCK_DGRAM, 0)
//   2. SO_REUSEADDR + SO_REUSEPORT (so multiple Worlds can run on one host)
//   3. bind() to (interface_address, port)
//   4. IP_ADD_MEMBERSHIP for group_address
//   5. IP_MULTICAST_TTL = ttl
//   6. IP_MULTICAST_LOOP = 1 (so loopback / single-host setups work)
//   7. O_NONBLOCK on the file descriptor — poll_*() never blocks
//
// open() returns nullptr on any failure. Inspect errno via syscalls if
// needed; the transport itself surfaces health via is_healthy() (false
// once an unrecoverable socket error is observed).

#include "mith/comms/transport.h"
#include "mith/identity/hierarchical_id.h"

#include <cstdint>
#include <memory>
#include <string>

namespace mith {

class UDPMulticastTransport : public TransportLayer {
public:
    struct Config {
        std::string   group_address      = "239.10.20.30"; // IPv4 admin-scoped
        std::uint16_t port                = 47474;
        std::string   interface_address   = "0.0.0.0";     // any iface
        std::uint8_t  multicast_ttl       = 1;             // local subnet
        std::size_t   recv_buffer_bytes   = 64 * 1024;
        // Set by SimBus / mission code before BeaconSystem ticks — used to
        // filter our own multicast echoes out of poll_beacons.
        HierarchicalID self_id{};
    };

    // Factory. Returns nullptr if any socket call fails (no exceptions).
    static std::unique_ptr<UDPMulticastTransport> open(Config cfg);

    ~UDPMulticastTransport() override;

    UDPMulticastTransport(const UDPMulticastTransport&) = delete;
    UDPMulticastTransport& operator=(const UDPMulticastTransport&) = delete;

    bool send_beacon(const StateVector& sv)               override;
    bool send_message(const Message& msg)                 override;
    void poll_beacons(std::vector<StateVector>& out)      override;
    void poll_messages(std::vector<Message>& out)         override;
    bool is_healthy() const                               override;

    // Test / observability accessors.
    const Config&  config() const noexcept;
    int            socket_fd() const noexcept;
    std::uint64_t  bytes_sent() const noexcept;
    std::uint64_t  bytes_received() const noexcept;
    std::uint64_t  parse_errors() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit UDPMulticastTransport(std::unique_ptr<Impl> impl) noexcept;

    // Drain everything currently readable on the socket into impl_'s
    // per-channel inboxes. Tagged frames route to the matching inbox;
    // untagged or malformed datagrams bump parse_errors. EAGAIN /
    // EWOULDBLOCK is the expected exit (no more packets).
    static void drain_socket(Impl& impl) noexcept;
};

} // namespace mith
