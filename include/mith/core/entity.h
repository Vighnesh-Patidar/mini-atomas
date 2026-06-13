#pragma once

// EntityID — see ARCHITECTURE.md §3.2
//
// At v0.1 each World holds exactly one entity (the robot itself). EntityID
// is shaped for the eventual N>1 extension (v0.5, behind WorldConfig::
// multi_entity); the type and the INVALID_ENTITY sentinel are stable across
// that transition.

#include <cstdint>

namespace mith {

using EntityID = std::uint32_t;

inline constexpr EntityID INVALID_ENTITY = 0;

// The one self entity ID for v0.1. Exposed for World::self_id() and for
// callers writing tests against the registry directly.
inline constexpr EntityID SELF_ENTITY = 1;

} // namespace mith
