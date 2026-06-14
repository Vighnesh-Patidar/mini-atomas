#include "mith/comms/serial_transport.h"

#include "mith/comms/serial_framing.h"
#include "mith/comms/udp_wire.h"

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <utility>
#include <vector>

namespace mith {

struct SerialTransport::Impl {
    int                          fd       = -1;
    bool                         own_fd   = false;
    serial_framing::Parser       parser;
    std::atomic<bool>            healthy  {true};
    std::atomic<std::uint64_t>   bytes_sent      {0};
    std::atomic<std::uint64_t>   bytes_received  {0};
    std::atomic<std::uint64_t>   parse_errors    {0};

    std::vector<StateVector>     inbox_beacons;
    std::vector<Message>         inbox_messages;

    ~Impl() {
        if (own_fd && fd >= 0) ::close(fd);
    }
};

namespace {

bool set_nonblocking(int fd) noexcept {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// Map a baud-rate integer to the termios speed_t constant. Returns 0
// for unsupported rates (caller should treat as error).
speed_t baud_to_speed(std::uint32_t baud) noexcept {
    switch (baud) {
        case 9600:    return B9600;
        case 19200:   return B19200;
        case 38400:   return B38400;
        case 57600:   return B57600;
        case 115200:  return B115200;
        case 230400:  return B230400;
        default:      return 0;
    }
}

bool configure_termios(int fd, std::uint32_t baud_rate) noexcept {
    const speed_t speed = baud_to_speed(baud_rate);
    if (speed == 0) return false;

    termios tio{};
    if (tcgetattr(fd, &tio) < 0) return false;

    cfmakeraw(&tio);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);

    tio.c_cflag |= (CLOCAL | CREAD);   // ignore modem, enable receiver
    tio.c_cflag &= ~CRTSCTS;           // no flow control
    tio.c_cc[VMIN]  = 0;               // non-blocking via O_NONBLOCK
    tio.c_cc[VTIME] = 0;

    return tcsetattr(fd, TCSANOW, &tio) == 0;
}

} // namespace

std::unique_ptr<SerialTransport>
SerialTransport::open(const std::string& device, std::uint32_t baud_rate) {
    const int fd = ::open(device.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) return nullptr;
    if (!configure_termios(fd, baud_rate)) { ::close(fd); return nullptr; }
    if (!set_nonblocking(fd))               { ::close(fd); return nullptr; }

    auto impl = std::make_unique<Impl>();
    impl->fd     = fd;
    impl->own_fd = true;
    return std::unique_ptr<SerialTransport>(new SerialTransport(std::move(impl)));
}

std::unique_ptr<SerialTransport>
SerialTransport::for_fd(int fd, bool own_fd) {
    if (fd < 0) return nullptr;
    if (!set_nonblocking(fd)) return nullptr;
    auto impl = std::make_unique<Impl>();
    impl->fd     = fd;
    impl->own_fd = own_fd;
    return std::unique_ptr<SerialTransport>(new SerialTransport(std::move(impl)));
}

SerialTransport::SerialTransport(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

SerialTransport::~SerialTransport() = default;

bool SerialTransport::write_framed(Impl& impl,
                                    const std::uint8_t* payload,
                                    std::size_t len) {
    std::uint8_t buf[serial_framing::FRAME_HEADER_BYTES + udp_wire::MAX_FRAME_BYTES];
    const std::size_t framed_len =
        serial_framing::encode(payload, len, buf, sizeof buf);
    if (framed_len == 0) return false;

    std::size_t written = 0;
    while (written < framed_len) {
        const ssize_t n = ::write(impl.fd,
                                   buf + written,
                                   framed_len - written);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            impl.healthy.store(false);
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    impl.bytes_sent.fetch_add(framed_len);
    return true;
}

bool SerialTransport::send_beacon(const StateVector& sv) {
    std::uint8_t inner[udp_wire::MAX_FRAME_BYTES];
    const std::size_t n = udp_wire::encode_beacon(sv, inner, sizeof inner);
    if (n == 0) return false;
    return write_framed(*impl_, inner, n);
}

bool SerialTransport::send_message(const Message& msg) {
    std::uint8_t inner[udp_wire::MAX_FRAME_BYTES];
    const std::size_t n = udp_wire::encode_message(msg, inner, sizeof inner);
    if (n == 0) return false;
    return write_framed(*impl_, inner, n);
}

void SerialTransport::drain_fd(Impl& impl) {
    std::uint8_t buf[1024];
    for (;;) {
        const ssize_t n = ::read(impl.fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            impl.healthy.store(false);
            break;
        }
        if (n == 0) break;
        impl.bytes_received.fetch_add(static_cast<std::uint64_t>(n));

        impl.parser.feed(buf, static_cast<std::size_t>(n),
            [&impl](const std::uint8_t* frame, std::size_t flen) {
                const std::uint8_t tag = udp_wire::peek_tag(frame, flen);
                if (tag == udp_wire::TAG_BEACON) {
                    auto sv = udp_wire::decode_beacon(frame, flen);
                    if (sv) impl.inbox_beacons.push_back(*sv);
                    else    impl.parse_errors.fetch_add(1);
                } else if (tag == udp_wire::TAG_MESSAGE) {
                    auto m = udp_wire::decode_message(frame, flen);
                    if (m) impl.inbox_messages.push_back(*m);
                    else   impl.parse_errors.fetch_add(1);
                } else {
                    impl.parse_errors.fetch_add(1);
                }
            });
    }
}

void SerialTransport::poll_beacons(std::vector<StateVector>& out) {
    drain_fd(*impl_);
    out = std::move(impl_->inbox_beacons);
    impl_->inbox_beacons.clear();
}

void SerialTransport::poll_messages(std::vector<Message>& out) {
    drain_fd(*impl_);
    out = std::move(impl_->inbox_messages);
    impl_->inbox_messages.clear();
}

bool SerialTransport::is_healthy() const { return impl_->healthy.load(); }

int           SerialTransport::fd() const noexcept             { return impl_->fd; }
std::uint64_t SerialTransport::bytes_sent() const noexcept     { return impl_->bytes_sent.load(); }
std::uint64_t SerialTransport::bytes_received() const noexcept { return impl_->bytes_received.load(); }
std::uint64_t SerialTransport::frames_decoded() const noexcept { return impl_->parser.frames_decoded(); }
std::uint64_t SerialTransport::parse_errors() const noexcept   { return impl_->parse_errors.load(); }

} // namespace mith
