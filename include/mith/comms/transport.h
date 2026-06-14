#pragma once

// TransportLayer — see ARCHITECTURE.md §7.5
//
// Channel-aware split per the §16 v0.2 plan. Two channel interfaces and
// one combined convenience class:
//
//   BeaconTransport  — periodic broadcast (lossy-medium friendly).
//                      send_beacon() + poll_beacons() + is_healthy().
//
//   MessageTransport — directed / broadcast packets (typically higher
//                      priority and lower volume).
//                      send_message() + poll_messages() + is_healthy().
//
//   TransportLayer   — both channels in one impl. SimTransport (§9.1)
//                      and the future UDPMulticastTransport implement
//                      this when they cover both channels via the same
//                      backend.
//
// World accepts either a combined TransportLayer or two split transports.
// Field deployments that want beacons-on-LoRa + messages-on-WiFi plug
// two separate implementations into World; sim and single-radio
// deployments use the combined path.
//
// `is_healthy()` is shared across both channel interfaces via virtual
// inheritance from a tiny TransportBase — implementations override it
// once, and the same function answers for either channel.

#include "mith/api_stability.h"
#include "mith/comms/message.h"
#include "mith/comms/state_vector.h"

#include <vector>

namespace mith {

class MITH_STABLE_API TransportBase {
public:
    virtual ~TransportBase() = default;

    // False if the transport is in an unrecoverable state (link down,
    // hardware fault). Read by FaultMonitorSystem in v0.2+.
    virtual bool is_healthy() const = 0;
};

class MITH_STABLE_API BeaconTransport : public virtual TransportBase {
public:
    virtual bool send_beacon(const StateVector& sv)               = 0;
    virtual void poll_beacons(std::vector<StateVector>& out)      = 0;

    // Capability flag. False for impls that exist only to carry messages
    // (e.g., a TCP-only sidecar). Lets BeaconSystem skip channel work
    // it can't perform.
    virtual bool supports_beacons() const noexcept { return true; }
};

class MITH_STABLE_API MessageTransport : public virtual TransportBase {
public:
    virtual bool send_message(const Message& msg)                 = 0;
    virtual void poll_messages(std::vector<Message>& out)         = 0;

    virtual bool supports_messages() const noexcept { return true; }
};

// Combined transport — carries both channels via a single backend.
// SimTransport, UDPMulticastTransport (v0.2), SerialTransport (v0.3).
class MITH_STABLE_API TransportLayer : public BeaconTransport, public MessageTransport {
    // No new methods. Virtual inheritance from TransportBase collapses
    // is_healthy() into a single slot so concrete impls override once.
};

} // namespace mith
