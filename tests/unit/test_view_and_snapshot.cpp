#include "doctest.h"

#include "mith/core/builtin_components.h"
#include "mith/core/entity_snapshot.h"
#include "mith/core/registry.h"

#include <cstdint>

using mith::EntityID;
using mith::EntityRegistry;
using mith::EntitySnapshot;
using mith::RegistrationStatus;

namespace {

struct PosComponent : mith::HotComponent<PosComponent> {
    int x = 0;
    int y = 0;
    PosComponent() noexcept = default;
    PosComponent(int x_, int y_) noexcept : x(x_), y(y_) {}
};

struct VelComponent : mith::HotComponent<VelComponent> {
    int vx = 0;
    int vy = 0;
    VelComponent() noexcept = default;
    VelComponent(int vx_, int vy_) noexcept : vx(vx_), vy(vy_) {}
};

struct TagComponent : mith::ColdComponent<TagComponent> {
    int tag = 0;
    TagComponent() noexcept = default;
    explicit TagComponent(int t) noexcept : tag(t) {}
};

} // namespace

// ------------------------------------------------------------------------
// ArchetypeView — §4.3
// ------------------------------------------------------------------------

TEST_CASE("view on unregistered component is empty") {
    EntityRegistry reg;
    auto v = reg.view<PosComponent>();

    CHECK(v.empty());
    CHECK_FALSE(v.has_all());
    CHECK(v.size() == 0u);

    int invoked = 0;
    v.for_each([&](EntityID, PosComponent&) { ++invoked; });
    CHECK(invoked == 0);
}

TEST_CASE("view on registered-but-not-emplaced component is empty") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);

    auto v = reg.view<PosComponent>();
    CHECK(v.empty());
    CHECK(v.size() == 0u);

    int invoked = 0;
    v.for_each([&](EntityID, PosComponent&) { ++invoked; });
    CHECK(invoked == 0);
}

TEST_CASE("view yields the self entity when the component is emplaced") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    reg.emplace<PosComponent>(reg.self_id(), PosComponent{3, 4});

    auto v = reg.view<PosComponent>();
    CHECK(v.has_all());
    CHECK_FALSE(v.empty());
    CHECK(v.size() == 1u);

    int invoked = 0;
    v.for_each([&](EntityID id, PosComponent& p) {
        ++invoked;
        CHECK(id == reg.self_id());
        CHECK(p.x == 3);
        CHECK(p.y == 4);
    });
    CHECK(invoked == 1);
}

TEST_CASE("view<T1, T2>: both present → invoked once with both refs") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    REQUIRE(reg.register_component<VelComponent>() == RegistrationStatus::Ok);
    reg.emplace<PosComponent>(reg.self_id(), PosComponent{1, 2});
    reg.emplace<VelComponent>(reg.self_id(), VelComponent{10, 20});

    auto v = reg.view<PosComponent, VelComponent>();
    CHECK(v.has_all());
    CHECK(v.size() == 1u);

    int invoked = 0;
    v.for_each([&](EntityID id, PosComponent& p, VelComponent& vel) {
        ++invoked;
        CHECK(id == reg.self_id());
        CHECK(p.x == 1);
        CHECK(vel.vx == 10);
    });
    CHECK(invoked == 1);
}

TEST_CASE("view<T1, T2>: only one present → empty, for_each not invoked") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    REQUIRE(reg.register_component<VelComponent>() == RegistrationStatus::Ok);
    reg.emplace<PosComponent>(reg.self_id(), PosComponent{1, 2});
    // VelComponent registered but NOT emplaced

    auto v = reg.view<PosComponent, VelComponent>();
    CHECK(v.empty());

    int invoked = 0;
    v.for_each([&](EntityID, PosComponent&, VelComponent&) { ++invoked; });
    CHECK(invoked == 0);
}

TEST_CASE("for_each refs are mutable — writes through the view persist") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    REQUIRE(reg.register_component<VelComponent>() == RegistrationStatus::Ok);
    reg.emplace<PosComponent>(reg.self_id(), PosComponent{0, 0});
    reg.emplace<VelComponent>(reg.self_id(), VelComponent{1, 2});

    reg.view<PosComponent, VelComponent>().for_each(
        [](EntityID, PosComponent& p, VelComponent& vel) {
            p.x += vel.vx;
            p.y += vel.vy;
        });

    CHECK(reg.get<PosComponent>(reg.self_id()).x == 1);
    CHECK(reg.get<PosComponent>(reg.self_id()).y == 2);
}

TEST_CASE("view works for cold components") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<TagComponent>() == RegistrationStatus::Ok);
    reg.emplace<TagComponent>(reg.self_id(), TagComponent{42});

    auto v = reg.view<TagComponent>();
    CHECK(v.size() == 1u);

    int invoked = 0;
    v.for_each([&](EntityID, TagComponent& t) {
        ++invoked;
        CHECK(t.tag == 42);
    });
    CHECK(invoked == 1);
}

TEST_CASE("view mixes hot and cold components seamlessly") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);
    REQUIRE(reg.register_component<TagComponent>() == RegistrationStatus::Ok);
    reg.emplace<PosComponent>(reg.self_id(), PosComponent{7, 8});
    reg.emplace<TagComponent>(reg.self_id(), TagComponent{99});

    int invoked = 0;
    reg.view<PosComponent, TagComponent>().for_each(
        [&](EntityID, PosComponent& p, TagComponent& t) {
            ++invoked;
            CHECK(p.x == 7);
            CHECK(t.tag == 99);
        });
    CHECK(invoked == 1);
}

TEST_CASE("size and empty reflect state changes through emplace / remove") {
    EntityRegistry reg;
    REQUIRE(reg.register_component<PosComponent>() == RegistrationStatus::Ok);

    auto v = reg.view<PosComponent>();
    CHECK(v.empty());

    reg.emplace<PosComponent>(reg.self_id(), PosComponent{0, 0});
    CHECK(v.size() == 1u);   // same view sees updated state

    reg.remove<PosComponent>(reg.self_id());
    CHECK(v.empty());
}

// ------------------------------------------------------------------------
// EntitySnapshot — §6.2
// ------------------------------------------------------------------------

TEST_CASE("snapshot on empty registry returns default-initialized fields") {
    EntityRegistry reg;
    const auto snap = reg.snapshot(reg.self_id());

    CHECK(snap.id == reg.self_id());
    CHECK(snap.hid.swarm_id == 0u);
    CHECK(snap.hid.unit_id.is_nil());
    CHECK(snap.position.x == 0.0f);
    CHECK(snap.velocity.vx == 0.0f);
    CHECK(snap.orientation.qw == 1.0f);    // identity quaternion default
    CHECK(snap.health.value == 100u);
    CHECK(snap.role.role == 0u);
    CHECK(snap.state.state == 0u);
}

TEST_CASE("snapshot reflects all emplaced built-in components") {
    EntityRegistry reg;

    REQUIRE(reg.register_builtin_component<mith::IdentityComponent>()        == RegistrationStatus::Ok);
    REQUIRE(reg.register_builtin_component<mith::PositionComponent>()        == RegistrationStatus::Ok);
    REQUIRE(reg.register_builtin_component<mith::VelocityComponent>()        == RegistrationStatus::Ok);
    REQUIRE(reg.register_builtin_component<mith::OrientationComponent>()     == RegistrationStatus::Ok);
    REQUIRE(reg.register_builtin_component<mith::HealthComponent>()          == RegistrationStatus::Ok);
    REQUIRE(reg.register_builtin_component<mith::RoleComponent>()            == RegistrationStatus::Ok);
    REQUIRE(reg.register_builtin_component<mith::BehaviourStateComponent>()  == RegistrationStatus::Ok);

    const auto hid = mith::HierarchicalID::generate(mith::SwarmID{0xAB});
    const auto self = reg.self_id();

    reg.emplace<mith::IdentityComponent>      (self, mith::IdentityComponent{hid});
    reg.emplace<mith::PositionComponent>      (self, mith::PositionComponent{1.0f, 2.0f, 3.0f});
    reg.emplace<mith::VelocityComponent>      (self, mith::VelocityComponent{0.1f, 0.2f, 0.3f});
    reg.emplace<mith::OrientationComponent>   (self, mith::OrientationComponent{0.5f, 0.5f, 0.5f, 0.5f});
    reg.emplace<mith::HealthComponent>        (self, mith::HealthComponent{42u});
    reg.emplace<mith::RoleComponent>          (self, mith::RoleComponent{7u});
    reg.emplace<mith::BehaviourStateComponent>(self, mith::BehaviourStateComponent{3u});

    const auto snap = reg.snapshot(self);

    CHECK(snap.id == self);
    CHECK(snap.hid == hid);
    CHECK(snap.position.x == 1.0f);
    CHECK(snap.position.z == 3.0f);
    CHECK(snap.velocity.vy == doctest::Approx(0.2f));
    CHECK(snap.orientation.qw == 0.5f);
    CHECK(snap.health.value == 42u);
    CHECK(snap.role.role == 7u);
    CHECK(snap.state.state == 3u);
}

TEST_CASE("snapshot is a copy — mutating it does not affect the registry") {
    EntityRegistry reg;
    REQUIRE(reg.register_builtin_component<mith::PositionComponent>() == RegistrationStatus::Ok);
    reg.emplace<mith::PositionComponent>(reg.self_id(), mith::PositionComponent{1.0f, 2.0f, 3.0f});

    auto snap = reg.snapshot(reg.self_id());
    snap.position.x = 999.0f;

    // Local snapshot mutated; registry value untouched.
    CHECK(snap.position.x == 999.0f);
    CHECK(reg.get<mith::PositionComponent>(reg.self_id()).x == 1.0f);
}

TEST_CASE("snapshot fills only the components that are emplaced") {
    EntityRegistry reg;

    REQUIRE(reg.register_builtin_component<mith::PositionComponent>() == RegistrationStatus::Ok);
    REQUIRE(reg.register_builtin_component<mith::HealthComponent>()   == RegistrationStatus::Ok);

    // Only Position is emplaced; Health is registered but not emplaced.
    reg.emplace<mith::PositionComponent>(reg.self_id(), mith::PositionComponent{5.0f, 6.0f, 7.0f});

    const auto snap = reg.snapshot(reg.self_id());
    CHECK(snap.position.x == 5.0f);
    CHECK(snap.position.z == 7.0f);
    CHECK(snap.health.value == 100u);     // default — not emplaced
    CHECK(snap.role.role == 0u);          // not registered → default
    CHECK(snap.velocity.vx == 0.0f);      // not registered → default
}

TEST_CASE("snapshot id field matches the entity passed in") {
    EntityRegistry reg;
    const auto snap = reg.snapshot(reg.self_id());
    CHECK(snap.id == reg.self_id());
    CHECK(snap.id == mith::SELF_ENTITY);
}

TEST_CASE("snapshot via const reference (read-only path)") {
    EntityRegistry reg;
    REQUIRE(reg.register_builtin_component<mith::HealthComponent>() == RegistrationStatus::Ok);
    reg.emplace<mith::HealthComponent>(reg.self_id(), mith::HealthComponent{50u});

    const EntityRegistry& cref = reg;
    const auto snap = cref.snapshot(cref.self_id());
    CHECK(snap.health.value == 50u);
}
