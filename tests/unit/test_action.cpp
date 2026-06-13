#include "doctest.h"

#include "mith/behaviour/action.h"
#include "mith/behaviour/action_type.h"
#include "mith/core/builtin_components.h"
#include "mith/core/registry.h"

#include <cstdint>

using mith::Action;
using mith::ActionQueueComponent;
using mith::ActionTypeID;
using mith::EntityRegistry;
using mith::RegistrationStatus;

// ------------------------------------------------------------------------
// Action POD — §6.1
// ------------------------------------------------------------------------

TEST_CASE("default-constructed Action is IDLE with zero priority and no target") {
    Action a;
    CHECK(a.type == mith::actions::IDLE);
    CHECK(a.priority == 0.0f);
    CHECK(a.modifies_count == 0u);
    CHECK(a.target.swarm_id == 0u);
    CHECK(a.target.unit_id.is_nil());
    for (auto b : a.params) CHECK(b == 0u);
}

TEST_CASE("Action fields are individually settable") {
    Action a;
    a.type = mith::actions::MOVE;
    a.priority = 0.75f;
    a.params[0] = 0xAB;
    a.params[63] = 0xCD;
    a.modifies[0] = mith::component_id<int>();
    a.modifies_count = 1;

    CHECK(a.type == mith::actions::MOVE);
    CHECK(a.priority == 0.75f);
    CHECK(a.params[0] == 0xABu);
    CHECK(a.params[63] == 0xCDu);
    CHECK(a.modifies[0] == mith::component_id<int>());
    CHECK(a.modifies_count == 1u);
}

TEST_CASE("Action capacity constants match the v0.1 spec") {
    static_assert(Action::MAX_MODIFIES == 4u);
    static_assert(Action::PARAMS_SIZE  == 64u);
}

TEST_CASE("Action is trivially copyable / movable for BoundedQueue use") {
    static_assert(std::is_default_constructible_v<Action>);
    static_assert(std::is_nothrow_move_constructible_v<Action>);
    static_assert(std::is_nothrow_move_assignable_v<Action>);
    static_assert(std::is_copy_constructible_v<Action>);
}

// ------------------------------------------------------------------------
// ActionQueueComponent — §4.4
// ------------------------------------------------------------------------

TEST_CASE("ActionQueueComponent: default queue empty + counters zero") {
    ActionQueueComponent c;
    CHECK(c.queue.empty());
    CHECK(c.queue.size() == 0u);
    CHECK(c.queue.capacity() == ActionQueueComponent::CAPACITY);
    CHECK(c.permission_rejections_total == 0u);
    CHECK(c.last_rejection_tick == 0u);
}

TEST_CASE("ActionQueueComponent: push and pop preserve FIFO order") {
    ActionQueueComponent c;
    Action a; a.type = mith::actions::MOVE;     a.priority = 0.5f;
    Action b; b.type = mith::actions::HOVER;    b.priority = 0.3f;

    REQUIRE(c.queue.push(std::move(a)));
    REQUIRE(c.queue.push(std::move(b)));
    CHECK(c.queue.size() == 2u);

    auto first = c.queue.pop();
    REQUIRE(first.has_value());
    CHECK(first->type == mith::actions::MOVE);
    CHECK(first->priority == 0.5f);

    auto second = c.queue.pop();
    REQUIRE(second.has_value());
    CHECK(second->type == mith::actions::HOVER);

    CHECK(c.queue.empty());
}

TEST_CASE("ActionQueueComponent: 9th push to a full queue is rejected (DropNewest)") {
    static_assert(ActionQueueComponent::CAPACITY == 8u);
    static_assert(decltype(ActionQueueComponent::queue)::POLICY == mith::OverflowPolicy::DropNewest);

    ActionQueueComponent c;
    for (int i = 0; i < 8; ++i) {
        Action a; a.type = mith::actions::MOVE; a.priority = static_cast<float>(i);
        REQUIRE(c.queue.push(std::move(a)));
    }
    REQUIRE(c.queue.full());

    Action overflow_a; overflow_a.type = mith::actions::REGROUP;
    CHECK_FALSE(c.queue.push(std::move(overflow_a)));
    CHECK(c.queue.dropped_count() == 1u);

    // First action is still the original priority-0 MOVE — in-flight intent preserved.
    auto first = c.queue.pop();
    REQUIRE(first.has_value());
    CHECK(first->type == mith::actions::MOVE);
    CHECK(first->priority == 0.0f);
}

TEST_CASE("ActionQueueComponent: registers + emplaces + retrieves via EntityRegistry") {
    EntityRegistry reg;
    REQUIRE(reg.register_builtin_component<ActionQueueComponent>() == RegistrationStatus::Ok);

    ActionQueueComponent initial;
    Action move; move.type = mith::actions::MOVE; move.priority = 1.0f;
    REQUIRE(initial.queue.push(std::move(move)));
    reg.emplace<ActionQueueComponent>(reg.self_id(), std::move(initial));

    auto& c = reg.get<ActionQueueComponent>(reg.self_id());
    REQUIRE(c.queue.size() == 1u);

    // ActionValidatorSystem (§6.4) updates the counters; simulate that here.
    c.permission_rejections_total = 3;
    c.last_rejection_tick = 42;

    CHECK(reg.get<ActionQueueComponent>(reg.self_id()).permission_rejections_total == 3u);
    CHECK(reg.get<ActionQueueComponent>(reg.self_id()).last_rejection_tick == 42u);
}
