#include "mith/core/registry.h"

#include <cstdio>
#include <cstdlib>

namespace mith {

namespace detail {

[[noreturn]] void registry_assert_fail(const char* msg) noexcept {
    // §15 prohibits exceptions; we terminate with a structured message on
    // the stderr stream that EW deployments (§13.5) and CI tests can capture.
    std::fprintf(stderr, "mith::EntityRegistry assertion failed: %s\n", msg);
    std::abort();
}

} // namespace detail

EntityRegistry::EntityRegistry(ComponentRegistrationPolicy policy) noexcept
    : policy_(policy) {}

void EntityRegistry::lock() noexcept {
    locked_ = true;
}

bool EntityRegistry::is_locked() const noexcept {
    return locked_;
}

std::size_t EntityRegistry::registered_count() const noexcept {
    return stores_.size();
}

ComponentRegistrationPolicy EntityRegistry::policy() const noexcept {
    return policy_;
}

} // namespace mith
