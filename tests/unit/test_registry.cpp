#include "doctest.h"

#include "mith/core/registry.h"

#include <cstdint>

using mith::EntityID;
using mith::EntityRegistry;
using mith::ComponentRegistrationPolicy;
using mith::ComponentOrigin;
using mith::RegistrationStatus;

namespace {

struct PosComponent : mith::HotComponent<PosComponent> {
    int x = 0;
    int y = 0;
    PosComponent() noexcept = default;
    PosComponent(int x_, int y_) noexcept : x(x_), y(y_) {}
};

struct HealthComponent : mith::HotComponent<HealthComponent> {
    int value = 100;
    HealthComponent() noexcept = default;
    explicit HealthComponent(int v) noexcept : value(v) {}
};

struct MissionTagComponent : mith::ColdComponent<MissionTagComponent> {
    int tag = 0;
    MissionTagComponent() noexcept = default;
    explicit MissionTagComponent(int t) noexcept : tag(t) {}
};

} // namespace

TEST_CASE("default-constructed registry has no components, is unlocked") {
    EntityRegistry reg;
    CHECK_FALSE(reg.is_locked());
    CHECK(reg.registered_count() == 0u);
    CHECK(reg.policy() == ComponentRegistrationPolicy::LockAfterInit);
}

TEST_CASE("self_id returns SELF_ENTITY (=1), distinct from INVALID_ENTITY") {
    EntityRegistry reg;
    CHECK(reg.self_id() == mith::SELF_ENTITY);
    CHECK(reg.self_id() == EntityID{1});
    CHECK(mith::SELF_ENTITY != mith::INVALID_ENTITY);
}

TEST_CASE("register_component tags origin User") {
    EntityRegistry reg;
    CHECK(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    CHECK(reg.is_registered<PosComponent>());
    CHECK(reg.origin_of<PosComponent>() == ComponentOrigin::User);
    CHECK(reg.registered_count() == 1u);
}

TEST_CASE("register_builtin_component tags origin Built_In") {
    EntityRegistry reg;
    CHECK(reg.register_builtin_component<PosComponent>() == RegistrationStatus::Ok);
    CHECK(reg.origin_of<PosComponent>() == ComponentOrigin::Built_In);
}

TEST_CASE("re-registering same T is idempotent (AlreadyRegistered)") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);

    const auto second = reg.register_component<PosComponent>();
    CHECK(second == RegistrationStatus::AlreadyRegistered);
    CHECK(reg.registered_count() == 1u);
    CHECK(reg.origin_of<PosComponent>() == ComponentOrigin::User);   // unchanged
}

TEST_CASE("emplace then get returns the same value") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);

    CHECK_FALSE(reg.has<PosComponent>(reg.self_id()));

    reg.emplace<PosComponent>(reg.self_id(), PosComponent{42, 99});

    CHECK(reg.has<PosComponent>(reg.self_id()));
    const auto& p = reg.get<PosComponent>(reg.self_id());
    CHECK(p.x == 42);
    CHECK(p.y == 99);
}

TEST_CASE("get returns a mutable reference (writes are visible)") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    reg.emplace<PosComponent>(reg.self_id(), PosComponent{0, 0});

    reg.get<PosComponent>(reg.self_id()).x = 1;
    reg.get<PosComponent>(reg.self_id()).y = 2;

    const auto& q = reg.get<PosComponent>(reg.self_id());
    CHECK(q.x == 1);
    CHECK(q.y == 2);
}

TEST_CASE("remove makes has return false; re-emplace re-enables it") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    reg.emplace<PosComponent>(reg.self_id(), PosComponent{1, 2});
    REQUIRE(reg.has<PosComponent>(reg.self_id()));

    reg.remove<PosComponent>(reg.self_id());
    CHECK_FALSE(reg.has<PosComponent>(reg.self_id()));

    reg.emplace<PosComponent>(reg.self_id(), PosComponent{7, 8});
    CHECK(reg.has<PosComponent>(reg.self_id()));
    CHECK(reg.get<PosComponent>(reg.self_id()).x == 7);
}

TEST_CASE("has returns false for non-self entity IDs (v0.1 N=1)") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    reg.emplace<PosComponent>(reg.self_id(), PosComponent{1, 2});

    CHECK_FALSE(reg.has<PosComponent>(mith::INVALID_ENTITY));
    CHECK_FALSE(reg.has<PosComponent>(EntityID{42}));
    CHECK(reg.has<PosComponent>(reg.self_id()));
}

TEST_CASE("multiple components register and access independently") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    REQUIRE(reg.register_component<HealthComponent>() == RegistrationStatus::Ok);
    REQUIRE(reg.register_component<MissionTagComponent>() == RegistrationStatus::Ok);

    CHECK(reg.registered_count() == 3u);

    reg.emplace<PosComponent>(reg.self_id(), PosComponent{1, 2});
    reg.emplace<HealthComponent>(reg.self_id(), HealthComponent{50});
    reg.emplace<MissionTagComponent>(reg.self_id(), MissionTagComponent{7});

    CHECK(reg.get<PosComponent>(reg.self_id()).x == 1);
    CHECK(reg.get<HealthComponent>(reg.self_id()).value == 50);
    CHECK(reg.get<MissionTagComponent>(reg.self_id()).tag == 7);
}

TEST_CASE("hot and cold components share the same registration API") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    REQUIRE(reg.register_component<MissionTagComponent>() == RegistrationStatus::Ok);

    CHECK(mith::is_hot_component_v<PosComponent>);
    CHECK_FALSE(mith::is_cold_component_v<PosComponent>);
    CHECK(mith::is_cold_component_v<MissionTagComponent>);
    CHECK_FALSE(mith::is_hot_component_v<MissionTagComponent>);
    CHECK(mith::is_component_v<PosComponent>);
    CHECK(mith::is_component_v<MissionTagComponent>);
}

TEST_CASE("LockAfterInit: post-lock registration returns PolicyLocked") {
    EntityRegistry reg(ComponentRegistrationPolicy::LockAfterInit);
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);

    reg.lock();
    CHECK(reg.is_locked());

    CHECK(reg.register_component<HealthComponent>() == RegistrationStatus::PolicyLocked);
    CHECK_FALSE(reg.is_registered<HealthComponent>());
    CHECK(reg.registered_count() == 1u);

    // Built-in registration also blocked post-lock — init has happened.
    CHECK(reg.register_builtin_component<HealthComponent>() == RegistrationStatus::PolicyLocked);
}

TEST_CASE("Open policy: registration allowed after lock()") {
    EntityRegistry reg(ComponentRegistrationPolicy::Open);
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);

    reg.lock();
    CHECK(reg.is_locked());

    CHECK(reg.register_component<HealthComponent>() == RegistrationStatus::Ok);
    CHECK(reg.registered_count() == 2u);
}

TEST_CASE("BuiltInOnly: User registration hard-rejected; Built_In still accepted") {
    EntityRegistry reg(ComponentRegistrationPolicy::BuiltInOnly);

    CHECK(reg.register_component<PosComponent>() == RegistrationStatus::PolicyForbidsUser);
    CHECK_FALSE(reg.is_registered<PosComponent>());

    CHECK(reg.register_builtin_component<PosComponent>() == RegistrationStatus::Ok);
    CHECK(reg.origin_of<PosComponent>() == ComponentOrigin::Built_In);
    CHECK(reg.registered_count() == 1u);
}

TEST_CASE("BuiltInOnly survives lock() — privileged path is not gated by locking") {
    EntityRegistry reg(ComponentRegistrationPolicy::BuiltInOnly);
    REQUIRE(reg.register_builtin_component<PosComponent>() == RegistrationStatus::Ok);

    reg.lock();
    // BuiltInOnly is the active policy, not LockAfterInit — post-lock builtin
    // registration still works.
    CHECK(reg.register_builtin_component<HealthComponent>() == RegistrationStatus::Ok);
}

TEST_CASE("origin_of reflects which entry point did the registration") {
    EntityRegistry reg;
    REQUIRE(reg.register_builtin_component<PosComponent>() == RegistrationStatus::Ok);
    REQUIRE(reg.register_component<HealthComponent>() == RegistrationStatus::Ok);

    CHECK(reg.origin_of<PosComponent>() == ComponentOrigin::Built_In);
    CHECK(reg.origin_of<HealthComponent>() == ComponentOrigin::User);
}

TEST_CASE("component_id<T> is stable and distinct between types") {
    constexpr auto a = mith::component_id<PosComponent>();
    constexpr auto b = mith::component_id<PosComponent>();
    constexpr auto c = mith::component_id<HealthComponent>();
    constexpr auto d = mith::component_id<MissionTagComponent>();

    static_assert(a == b);
    static_assert(a != c);
    static_assert(a != d);
    static_assert(c != d);
}

TEST_CASE("is_registered: false until registered, true after, false for siblings") {
    EntityRegistry reg;
    CHECK_FALSE(reg.is_registered<PosComponent>());

    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    CHECK(reg.is_registered<PosComponent>());
    CHECK_FALSE(reg.is_registered<HealthComponent>());
    CHECK_FALSE(reg.is_registered<MissionTagComponent>());
}

TEST_CASE("remove on unregistered / unemplaced / wrong-id is a no-op") {
    EntityRegistry reg;

    // No registration → no-op (no abort).
    reg.remove<PosComponent>(reg.self_id());
    CHECK(reg.registered_count() == 0u);

    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);

    // Registered but not emplaced → no-op.
    reg.remove<PosComponent>(reg.self_id());
    CHECK_FALSE(reg.has<PosComponent>(reg.self_id()));

    // Wrong entity id → no-op (does not touch the actual self entity).
    reg.emplace<PosComponent>(reg.self_id(), PosComponent{1, 2});
    reg.remove<PosComponent>(EntityID{42});
    CHECK(reg.has<PosComponent>(reg.self_id()));
}

TEST_CASE("type_name produces non-empty distinct names per type") {
    constexpr auto pos = mith::type_name<PosComponent>();
    constexpr auto health = mith::type_name<HealthComponent>();

    CHECK(pos.size() > 0);
    CHECK(health.size() > 0);
    CHECK(pos != health);
}
