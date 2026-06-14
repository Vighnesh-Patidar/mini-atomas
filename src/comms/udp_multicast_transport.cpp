#include "mith/comms/udp_multicast_transport.h"

#include "mith/comms/udp_wire.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <utility>

namespace mith {

struct UDPMulticastTransport::Impl {
    Config                config;
    int                   fd        = -1;
    sockaddr_in           group_sa  = {};
    std::atomic<bool>     healthy   {false};
    std::atomic<std::uint64_t> bytes_sent      {0};
    std::atomic<std::uint64_t> bytes_received  {0};
    std::atomic<std::uint64_t> parse_errors    {0};

    // Per-channel inboxes. Both polls drain the socket into these; the
    // returning poll moves out the relevant inbox. Lets poll_beacons and
    // poll_messages within the same tick both see the packets that
    // arrived since the previous drain.
    std::vector<StateVector> inbox_beacons;
    std::vector<Message>     inbox_messages;

    ~Impl() {
        if (fd >= 0) {
            // Drop multicast membership before close — polite, not strictly
            // required by the kernel.
            ip_mreq mreq{};
            mreq.imr_multiaddr.s_addr = group_sa.sin_addr.s_addr;
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            (void) setsockopt(fd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                              &mreq, sizeof mreq);
            ::close(fd);
        }
    }
};

namespace {

bool set_nonblocking(int fd) noexcept {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace

std::unique_ptr<UDPMulticastTransport>
UDPMulticastTransport::open(Config cfg) {
    auto impl = std::make_unique<Impl>();
    impl->config = std::move(cfg);

    // 1. Open the socket.
    impl->fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (impl->fd < 0) return nullptr;

    // 2. SO_REUSEADDR + SO_REUSEPORT — lets several Worlds share the
    //    bind. Both options needed on Linux (REUSEPORT for tight
    //    load-balanced binds; REUSEADDR for multicast rebind safety).
    const int yes = 1;
    if (::setsockopt(impl->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0
     || ::setsockopt(impl->fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof yes) < 0) {
        return nullptr;
    }

    // 3. Bind to interface_address:port.
    sockaddr_in bind_sa{};
    bind_sa.sin_family      = AF_INET;
    bind_sa.sin_port        = htons(impl->config.port);
    if (::inet_pton(AF_INET, impl->config.interface_address.c_str(),
                    &bind_sa.sin_addr) <= 0) {
        return nullptr;
    }
    if (::bind(impl->fd,
               reinterpret_cast<const sockaddr*>(&bind_sa),
               sizeof bind_sa) < 0) {
        return nullptr;
    }

    // 4. Join the multicast group.
    impl->group_sa.sin_family = AF_INET;
    impl->group_sa.sin_port   = htons(impl->config.port);
    if (::inet_pton(AF_INET, impl->config.group_address.c_str(),
                    &impl->group_sa.sin_addr) <= 0) {
        return nullptr;
    }
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = impl->group_sa.sin_addr.s_addr;
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (::setsockopt(impl->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     &mreq, sizeof mreq) < 0) {
        return nullptr;
    }

    // 5. TTL + loopback for local testing.
    const std::uint8_t ttl  = impl->config.multicast_ttl;
    const std::uint8_t loop = 1;
    (void) ::setsockopt(impl->fd, IPPROTO_IP, IP_MULTICAST_TTL,
                        &ttl, sizeof ttl);
    (void) ::setsockopt(impl->fd, IPPROTO_IP, IP_MULTICAST_LOOP,
                        &loop, sizeof loop);

    // 6. Non-blocking — poll_*() must not block the tick loop.
    if (!set_nonblocking(impl->fd)) return nullptr;

    impl->healthy.store(true);
    return std::unique_ptr<UDPMulticastTransport>(
        new UDPMulticastTransport(std::move(impl)));
}

UDPMulticastTransport::UDPMulticastTransport(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

UDPMulticastTransport::~UDPMulticastTransport() = default;

bool UDPMulticastTransport::send_beacon(const StateVector& sv) {
    std::uint8_t buf[udp_wire::MAX_FRAME_BYTES];
    const std::size_t n = udp_wire::encode_beacon(sv, buf, sizeof buf);
    if (n == 0) return false;

    const ssize_t sent = ::sendto(impl_->fd, buf, n, 0,
                                  reinterpret_cast<const sockaddr*>(&impl_->group_sa),
                                  sizeof impl_->group_sa);
    if (sent < 0) { impl_->healthy.store(false); return false; }
    impl_->bytes_sent.fetch_add(static_cast<std::uint64_t>(sent));
    return true;
}

bool UDPMulticastTransport::send_message(const Message& msg) {
    std::uint8_t buf[udp_wire::MAX_FRAME_BYTES];
    const std::size_t n = udp_wire::encode_message(msg, buf, sizeof buf);
    if (n == 0) return false;

    const ssize_t sent = ::sendto(impl_->fd, buf, n, 0,
                                  reinterpret_cast<const sockaddr*>(&impl_->group_sa),
                                  sizeof impl_->group_sa);
    if (sent < 0) { impl_->healthy.store(false); return false; }
    impl_->bytes_sent.fetch_add(static_cast<std::uint64_t>(sent));
    return true;
}

namespace {

} // namespace

void UDPMulticastTransport::drain_socket(Impl& impl) noexcept {
    std::uint8_t buf[udp_wire::MAX_FRAME_BYTES];
    for (;;) {
        const ssize_t n = ::recv(impl.fd, buf, sizeof buf, 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            impl.healthy.store(false);
            break;
        }
        if (n == 0) break;
        impl.bytes_received.fetch_add(static_cast<std::uint64_t>(n));

        const std::uint8_t tag = udp_wire::peek_tag(
            buf, static_cast<std::size_t>(n));
        if (tag == udp_wire::TAG_BEACON) {
            auto sv = udp_wire::decode_beacon(buf, static_cast<std::size_t>(n));
            if (!sv) { impl.parse_errors.fetch_add(1); continue; }
            if (sv->id == impl.config.self_id) continue;
            impl.inbox_beacons.push_back(*sv);
        } else if (tag == udp_wire::TAG_MESSAGE) {
            auto msg = udp_wire::decode_message(buf, static_cast<std::size_t>(n));
            if (!msg) { impl.parse_errors.fetch_add(1); continue; }
            if (msg->sender == impl.config.self_id) continue;
            impl.inbox_messages.push_back(*msg);
        } else {
            impl.parse_errors.fetch_add(1);
        }
    }
}

void UDPMulticastTransport::poll_beacons(std::vector<StateVector>& out) {
    drain_socket(*impl_);
    out = std::move(impl_->inbox_beacons);
    impl_->inbox_beacons.clear();
}

void UDPMulticastTransport::poll_messages(std::vector<Message>& out) {
    drain_socket(*impl_);
    out = std::move(impl_->inbox_messages);
    impl_->inbox_messages.clear();
}

bool UDPMulticastTransport::is_healthy() const {
    return impl_->healthy.load();
}

const UDPMulticastTransport::Config& UDPMulticastTransport::config() const noexcept {
    return impl_->config;
}

int UDPMulticastTransport::socket_fd() const noexcept {
    return impl_->fd;
}

std::uint64_t UDPMulticastTransport::bytes_sent() const noexcept {
    return impl_->bytes_sent.load();
}

std::uint64_t UDPMulticastTransport::bytes_received() const noexcept {
    return impl_->bytes_received.load();
}

std::uint64_t UDPMulticastTransport::parse_errors() const noexcept {
    return impl_->parse_errors.load();
}

} // namespace mith
