#pragma once

// SerialTransport — see ARCHITECTURE.md §7.5
//
// TransportLayer over a POSIX serial fd. Carries beacons + messages as
// udp_wire frames wrapped in serial_framing sync+length packets — so
// the wire schema is identical to UDPMulticastTransport's, only the
// physical transport differs.
//
// Built only when MITH_ENABLE_SERIAL=ON (CMake option). Defines
// MITH_SERIAL_ENABLED.
//
// Two construction paths:
//   open(device, baud) — opens /dev/ttyUSB0 etc., configures termios
//                        raw mode + non-blocking, owns the fd.
//   for_fd(fd, own_fd) — adopts an existing fd. Use for tests via
//                        socketpair() or pipes.
//
// poll_beacons / poll_messages share the same recv-drain pattern as
// UDPMulticastTransport: read all available bytes, feed through the
// framing parser, route fully-decoded frames into per-channel inboxes
// by udp_wire tag.

#include "mith/comms/transport.h"
#include "mith/comms/serial_framing.h"

#include <cstdint>
#include <memory>
#include <string>

namespace mith {

class SerialTransport : public TransportLayer {
public:
    // Open `device` (e.g. "/dev/ttyUSB0") at `baud_rate` and return a
    // ready transport. Returns nullptr on any syscall failure.
    static std::unique_ptr<SerialTransport>
        open(const std::string& device, std::uint32_t baud_rate);

    // Adopt an already-open file descriptor (e.g. a socketpair end for
    // tests). When `own_fd` is true, the destructor closes it.
    static std::unique_ptr<SerialTransport>
        for_fd(int fd, bool own_fd);

    ~SerialTransport() override;

    SerialTransport(const SerialTransport&) = delete;
    SerialTransport& operator=(const SerialTransport&) = delete;

    bool send_beacon(const StateVector& sv)               override;
    bool send_message(const Message& msg)                 override;
    void poll_beacons(std::vector<StateVector>& out)      override;
    void poll_messages(std::vector<Message>& out)         override;
    bool is_healthy() const                               override;

    // Observability.
    int            fd() const noexcept;
    std::uint64_t  bytes_sent() const noexcept;
    std::uint64_t  bytes_received() const noexcept;
    std::uint64_t  frames_decoded() const noexcept;
    std::uint64_t  parse_errors() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit SerialTransport(std::unique_ptr<Impl> impl) noexcept;

    static void drain_fd(Impl& impl);
    static bool write_framed(Impl& impl, const std::uint8_t* payload, std::size_t len);
};

} // namespace mith
