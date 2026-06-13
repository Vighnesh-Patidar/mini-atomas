#pragma once

// TransportLayer — see ARCHITECTURE.md §7.5
//
// Pluggable interface for inter-robot communication. Each concrete
// TransportLayer carries this robot's outbound beacons and messages
// outward, and surfaces inbound packets back to BeaconSystem (§5.3)
// via poll() each tick.
//
// Implementations:
//   SimTransport (§9.1)        — in-process loopback via SimBus (v0.1)
//   UDPMulticastTransport (v0.2) — production network transport
//   SerialTransport (v0.3)       — wire/radio transport for hardware
//
// All transports must declare their TX-queue OverflowPolicy somewhere
// in their public type signature (§7.5). DropOldest is the recommended
// default for state-vector traffic.

#include "mith/comms/message.h"
#include "mith/comms/state_vector.h"

#include <vector>

namespace mith {

class TransportLayer {
public:
    virtual ~TransportLayer() = default;

    // Submit a StateVector beacon for broadcast. Returns true on success
    // (queued for delivery; not necessarily delivered yet).
    virtual bool send_beacon(const StateVector& sv) = 0;

    // Submit a directed or broadcast Message. Returns true on success.
    virtual bool send_message(const Message& msg) = 0;

    // Drain inbound packets into the caller-supplied vectors. The output
    // vectors are CLEARED and refilled. BeaconSystem (§5.3, v0.2) calls
    // this once per tick.
    virtual void poll(std::vector<StateVector>& beacons_out,
                      std::vector<Message>&     messages_out) = 0;

    // Health check. False if the transport is in an unrecoverable state
    // (link down, hardware fault). Read by FaultMonitorSystem (§13.1, v0.2).
    virtual bool is_healthy() const = 0;
};

} // namespace mith
