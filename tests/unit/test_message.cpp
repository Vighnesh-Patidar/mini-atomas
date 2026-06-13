#include "doctest.h"

#include "mith/comms/message.h"
#include "mith/core/builtin_components.h"
#include "mith/core/registry.h"

#include <cstdint>
#include <type_traits>

using mith::CommBufferComponent;
using mith::EntityRegistry;
using mith::HierarchicalID;
using mith::Message;
using mith::MessageTypeID;
using mith::RegistrationStatus;
using mith::SwarmID;

// ------------------------------------------------------------------------
// Message POD + BROADCAST_ID — §7.3
// ------------------------------------------------------------------------

TEST_CASE("default-constructed Message: nil sender, broadcast recipient") {
    Message m;
    CHECK(m.sender.swarm_id == 0u);
    CHECK(m.sender.unit_id.is_nil());
    CHECK(m.recipient == mith::BROADCAST_ID);
    CHECK(m.type == mith::messages::CUSTOM);
    CHECK(m.seq == 0u);
    CHECK(m.timestamp_s == 0.0f);
    for (auto b : m.payload) CHECK(b == 0u);
}

TEST_CASE("Message fields are individually settable") {
    Message m;
    m.sender    = HierarchicalID::generate(SwarmID{1});
    m.recipient = HierarchicalID::generate(SwarmID{2});
    m.type      = mith::messages::TASK_BID;
    m.seq       = 42;
    m.timestamp_s = 1.234f;
    m.payload[0]  = 0xDE;
    m.payload[127] = 0xAD;

    CHECK(m.sender.swarm_id == 1u);
    CHECK(m.recipient.swarm_id == 2u);
    CHECK(m.type == mith::messages::TASK_BID);
    CHECK(m.seq == 42u);
    CHECK(m.timestamp_s == 1.234f);
    CHECK(m.payload[0]   == 0xDEu);
    CHECK(m.payload[127] == 0xADu);
}

TEST_CASE("Message capacity constant matches the v0.1 spec") {
    static_assert(Message::PAYLOAD_SIZE == 128u);
}

TEST_CASE("MessageTypeID built-in constants are stable and distinct") {
    static_assert(mith::messages::TASK_BID      == 1u);
    static_assert(mith::messages::TASK_ASSIGN   == 2u);
    static_assert(mith::messages::FAULT_ALERT   == 3u);
    static_assert(mith::messages::FORMATION_CMD == 4u);
    static_assert(mith::messages::CUSTOM        == 0x1000u);
    static_assert(mith::messages::TASK_BID < mith::messages::CUSTOM);
}

TEST_CASE("BROADCAST_ID is {SwarmID{0}, nil UUID}") {
    static_assert(mith::BROADCAST_ID.swarm_id == 0u);
    CHECK(mith::BROADCAST_ID.unit_id.is_nil());
}

TEST_CASE("is_broadcast: nil UUID → true, generated UUID → false") {
    CHECK(mith::is_broadcast(mith::BROADCAST_ID));

    // Any HID with nil unit_id is a broadcast, regardless of swarm.
    HierarchicalID nil_in_swarm1{SwarmID{1}, mith::UUID{}};
    CHECK(mith::is_broadcast(nil_in_swarm1));

    const auto real = HierarchicalID::generate(SwarmID{1});
    CHECK_FALSE(mith::is_broadcast(real));
}

TEST_CASE("Message is suitable for BoundedQueue (move/copy traits)") {
    static_assert(std::is_default_constructible_v<Message>);
    static_assert(std::is_nothrow_move_constructible_v<Message>);
    static_assert(std::is_nothrow_move_assignable_v<Message>);
}

// ------------------------------------------------------------------------
// CommBufferComponent — §4.4
// ------------------------------------------------------------------------

TEST_CASE("CommBufferComponent: default queue empty, capacity 16") {
    CommBufferComponent c;
    CHECK(c.queue.empty());
    CHECK(c.queue.capacity() == CommBufferComponent::CAPACITY);
    static_assert(CommBufferComponent::CAPACITY == 16u);
    static_assert(decltype(CommBufferComponent::queue)::POLICY == mith::OverflowPolicy::DropOldest);
}

TEST_CASE("CommBufferComponent: push and pop messages in FIFO order") {
    CommBufferComponent c;
    Message a; a.type = mith::messages::TASK_BID;    a.seq = 1;
    Message b; b.type = mith::messages::FAULT_ALERT; b.seq = 2;

    REQUIRE(c.queue.push(std::move(a)));
    REQUIRE(c.queue.push(std::move(b)));
    CHECK(c.queue.size() == 2u);

    auto first = c.queue.pop();
    REQUIRE(first.has_value());
    CHECK(first->seq == 1u);
    CHECK(first->type == mith::messages::TASK_BID);

    auto second = c.queue.pop();
    REQUIRE(second.has_value());
    CHECK(second->seq == 2u);
    CHECK(second->type == mith::messages::FAULT_ALERT);
}

TEST_CASE("CommBufferComponent: 17th push to a full queue evicts the oldest (DropOldest)") {
    CommBufferComponent c;
    for (std::uint32_t i = 0; i < CommBufferComponent::CAPACITY; ++i) {
        Message m; m.seq = i;
        REQUIRE(c.queue.push(std::move(m)));
    }
    REQUIRE(c.queue.full());

    Message overflow_msg; overflow_msg.seq = 999;
    // DropOldest: push returns true, oldest is evicted, new is enqueued.
    REQUIRE(c.queue.push(std::move(overflow_msg)));
    CHECK(c.queue.dropped_count() == 1u);

    // The original seq=0 was evicted; the new oldest is seq=1.
    auto next = c.queue.pop();
    REQUIRE(next.has_value());
    CHECK(next->seq == 1u);
}

TEST_CASE("CommBufferComponent: registers and round-trips via EntityRegistry") {
    EntityRegistry reg;
    REQUIRE(reg.register_builtin_component<CommBufferComponent>() == RegistrationStatus::Ok);

    CommBufferComponent initial;
    Message msg;
    msg.sender    = HierarchicalID::generate(SwarmID{5});
    msg.type      = mith::messages::FORMATION_CMD;
    msg.seq       = 99;
    msg.payload[0] = 0xAA;
    REQUIRE(initial.queue.push(std::move(msg)));

    reg.emplace<CommBufferComponent>(reg.self_id(), std::move(initial));

    auto& c = reg.get<CommBufferComponent>(reg.self_id());
    REQUIRE(c.queue.size() == 1u);

    auto received = c.queue.pop();
    REQUIRE(received.has_value());
    CHECK(received->sender.swarm_id == 5u);
    CHECK(received->type == mith::messages::FORMATION_CMD);
    CHECK(received->seq == 99u);
    CHECK(received->payload[0] == 0xAAu);
}

TEST_CASE("CommBufferComponent: broadcast recipient is the default for default-constructed Message") {
    CommBufferComponent c;
    Message msg;   // recipient defaults to BROADCAST_ID
    REQUIRE(c.queue.push(std::move(msg)));

    auto received = c.queue.pop();
    REQUIRE(received.has_value());
    CHECK(mith::is_broadcast(received->recipient));
}
