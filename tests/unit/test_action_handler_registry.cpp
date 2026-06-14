#include "doctest.h"

#include "mith/behaviour/action.h"
#include "mith/behaviour/action_type.h"
#include "mith/behaviour/action_validator_system.h"
#include "mith/behaviour/builtin_action_handlers.h"
#include "mith/comms/message.h"
#include "mith/comms/state_vector.h"
#include "mith/comms/transport.h"
#include "mith/core/builtin_components.h"
#include "mith/core/registry.h"
#include "mith/core/world.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using mith::Action;
using mith::ActionQueueComponent;
using mith::ActionValidatorSystem;
using mith::HoverActionHandler;
using mith::Message;
using mith::MoveActionHandler;
using mith::PermissionMaskComponent;
using mith::StateVector;
using mith::SwarmID;
using mith::TransmitActionHandler;
using mith::TransportLayer;
using mith::VelocityComponent;
using mith::World;
using mith::WorldConfig;
namespace actions = mith::actions;

namespace {

// Inline transport stub for the Transmit handler tests.
class RecordingTransport : public TransportLayer {
public:
    std::vector<Message> sent_messages;
    bool send_beacon(const StateVector&) override { return true; }
    bool send_message(const Message& m) override { sent_messages.push_back(m); return true; }
    void poll_beacons(std::vector<StateVector>& out) override { out.clear(); }
    void poll_messages(std::vector<Message>& out) override    { out.clear(); }
    bool is_healthy() const override { return true; }
};

void put_f32_le(std::uint8_t* p, float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    p[0] = static_cast<std::uint8_t>( bits        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((bits >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((bits >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((bits >> 24) & 0xFFu);
}
void put_u32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>( v        & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >>  8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

Action make_move(float vx, float vy, float vz) {
    Action a; a.type = actions::MOVE;
    put_f32_le(a.params.data() + 0, vx);
    put_f32_le(a.params.data() + 4, vy);
    put_f32_le(a.params.data() + 8, vz);
    return a;
}

Action make_transmit(std::uint32_t type, std::uint32_t seq, std::uint8_t payload_byte) {
    Action a; a.type = actions::TRANSMIT;
    put_u32_le(a.params.data() + 0, type);
    put_u32_le(a.params.data() + 4, seq);
    a.params[8] = payload_byte;
    return a;
}

} // namespace

TEST_CASE("ActionValidatorSystem: drains the queue into validated[] when allowed") {
    World w(WorldConfig{});
    w.init();
    ActionValidatorSystem v(w);

    auto& aq = w.registry().get<ActionQueueComponent>(w.self_id());
    aq.queue.push(make_move(1, 2, 3));
    aq.queue.push(make_move(4, 5, 6));
    REQUIRE(aq.queue.size() == 2u);

    v.tick(w.registry(), w.context(), 0.1f);
    CHECK(aq.queue.size() == 0u);
    CHECK(aq.validated_count == 2u);
    CHECK(aq.permission_rejections_total == 0u);
}

TEST_CASE("ActionValidatorSystem: rejects disallowed actions; bumps counters; queue still drained") {
    World w(WorldConfig{});
    w.init();
    auto& mask = w.registry().get<PermissionMaskComponent>(w.self_id());
    // Disallow MOVE (clear bit 1).
    mask.allowed_builtins &= ~(1u << actions::MOVE);

    ActionValidatorSystem v(w);
    auto& aq = w.registry().get<ActionQueueComponent>(w.self_id());
    aq.queue.push(make_move(1, 0, 0));
    aq.queue.push(make_move(0, 1, 0));

    v.tick(w.registry(), w.context(), 0.1f);
    CHECK(aq.queue.size() == 0u);
    CHECK(aq.validated_count == 0u);
    CHECK(aq.permission_rejections_total == 2u);
    CHECK(aq.last_rejection_tick == w.context().tick_count);
}

TEST_CASE("ActionValidatorSystem: clears the validated buffer at the start of each tick") {
    World w(WorldConfig{});
    w.init();
    ActionValidatorSystem v(w);

    auto& aq = w.registry().get<ActionQueueComponent>(w.self_id());
    aq.queue.push(make_move(1, 0, 0));
    v.tick(w.registry(), w.context(), 0.1f);
    REQUIRE(aq.validated_count == 1u);

    // No new actions enqueued → next tick must reset the buffer.
    v.tick(w.registry(), w.context(), 0.1f);
    CHECK(aq.validated_count == 0u);
}

TEST_CASE("MoveActionHandler: writes VelocityComponent from MOVE actions, ignores other types") {
    World w(WorldConfig{});
    w.init();
    auto& aq  = w.registry().get<ActionQueueComponent>(w.self_id());
    auto& vel = w.registry().get<VelocityComponent>(w.self_id());

    // Synthesise a validated buffer the validator would have produced.
    aq.validated[0] = make_move(1.5f, -2.5f, 7.0f);
    aq.validated[1] = Action{};                   // IDLE — handler ignores
    aq.validated[2] = make_move(0.25f, 0.25f, 0.25f);   // last one wins (handler overwrites)
    aq.validated_count = 3;

    MoveActionHandler h(w);
    h.tick(w.registry(), w.context(), 0.1f);

    CHECK(vel.vx == doctest::Approx(0.25f));
    CHECK(vel.vy == doctest::Approx(0.25f));
    CHECK(vel.vz == doctest::Approx(0.25f));
    CHECK(h.handled() == 2u);   // two MOVE actions counted
}

TEST_CASE("TransmitActionHandler: enqueues a Message via the message transport") {
    World w(WorldConfig{}, std::make_unique<RecordingTransport>());
    w.init();
    auto* rec = static_cast<RecordingTransport*>(w.transport());

    auto& aq = w.registry().get<ActionQueueComponent>(w.self_id());
    aq.validated[0] = make_transmit(/*type=*/0x42, /*seq=*/7u, /*payload=*/0xAB);
    aq.validated[0].target = mith::HierarchicalID::generate(SwarmID{1});
    aq.validated_count = 1;

    TransmitActionHandler h(w);
    h.tick(w.registry(), w.context(), 0.1f);

    REQUIRE(rec->sent_messages.size() == 1u);
    const auto& m = rec->sent_messages[0];
    CHECK(m.sender == w.identity());
    CHECK(m.recipient == aq.validated[0].target);
    CHECK(m.type == 0x42u);
    CHECK(m.seq  == 7u);
    CHECK(m.payload[0] == 0xABu);
    CHECK(h.handled() == 1u);
}

TEST_CASE("HoverActionHandler: counts IDLE + HOVER, ignores everything else") {
    World w(WorldConfig{});
    w.init();
    auto& aq = w.registry().get<ActionQueueComponent>(w.self_id());

    Action idle;  idle.type  = actions::IDLE;
    Action hover; hover.type = actions::HOVER;
    Action move = make_move(1, 0, 0);

    aq.validated[0] = idle;
    aq.validated[1] = hover;
    aq.validated[2] = move;
    aq.validated_count = 3;

    HoverActionHandler h(w);
    h.tick(w.registry(), w.context(), 0.1f);

    CHECK(h.handled() == 2u);   // idle + hover
}

TEST_CASE("Validator + handlers integrate through register_system / SchedulerStatus") {
    World w(WorldConfig{}, std::make_unique<RecordingTransport>());
    REQUIRE(w.register_system(std::make_unique<ActionValidatorSystem>(w))
            == mith::SchedulerStatus::Ok);
    REQUIRE(w.register_system(std::make_unique<MoveActionHandler>(w))
            == mith::SchedulerStatus::Ok);
    REQUIRE(w.register_system(std::make_unique<TransmitActionHandler>(w))
            == mith::SchedulerStatus::Ok);
    REQUIRE(w.register_system(std::make_unique<HoverActionHandler>(w))
            == mith::SchedulerStatus::Ok);
    w.init();

    auto& aq = w.registry().get<ActionQueueComponent>(w.self_id());
    aq.queue.push(make_move(2.0f, 0, 0));
    aq.queue.push(make_transmit(7, 1, 0xCD));

    w.tick();

    auto& vel = w.registry().get<VelocityComponent>(w.self_id());
    CHECK(vel.vx == doctest::Approx(2.0f));

    auto* rec = static_cast<RecordingTransport*>(w.transport());
    CHECK(rec->sent_messages.size() == 1u);
    CHECK(rec->sent_messages[0].type == 7u);
    // Queue drained; nothing left.
    CHECK(aq.queue.size() == 0u);
    CHECK(aq.validated_count == 2u);
}

TEST_CASE("SystemDescriptors: validator writes ActionQueue; handlers read it; writes are disjoint") {
    World w(WorldConfig{}, std::make_unique<RecordingTransport>());
    w.init();
    const auto vd = ActionValidatorSystem(w).describe();
    const auto md = MoveActionHandler(w).describe();
    const auto td = TransmitActionHandler(w).describe();
    const auto hd = HoverActionHandler(w).describe();

    auto has_comp = [](const std::vector<mith::ComponentTypeID>& v, mith::ComponentTypeID id) {
        for (auto x : v) if (x == id) return true;
        return false;
    };
    auto has_res = [](const std::vector<mith::ResourceID>& v, mith::ResourceID r) {
        for (auto x : v) if (x == r) return true;
        return false;
    };

    CHECK(has_comp(vd.writes_components, mith::component_id<ActionQueueComponent>()));
    CHECK(has_comp(md.reads_components,  mith::component_id<ActionQueueComponent>()));
    CHECK(has_comp(td.reads_components,  mith::component_id<ActionQueueComponent>()));
    CHECK(has_comp(hd.reads_components,  mith::component_id<ActionQueueComponent>()));

    // Move writes VelocityComponent; Transmit writes TransportTx — disjoint
    // so they could run concurrently under Parallel scheduler mode.
    CHECK(has_comp(md.writes_components, mith::component_id<VelocityComponent>()));
    CHECK(has_res(td.writes_resources,   mith::ResourceID::TransportTx));
}
