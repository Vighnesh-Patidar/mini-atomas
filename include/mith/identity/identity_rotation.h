#pragma once

// Identity rotation policy — see ARCHITECTURE.md §3.4
//
// Selects how (and when) a robot's UnitID changes during a deployment.
// v0.1 ships only PERMANENT as functional; the other policies' enum
// values exist for API stability and are honoured starting in v0.2
// when cryptographic identity (§3.3) lands.
//
//   PERMANENT     UnitID generated once at first boot; never changes.
//                 Default. Suitable for research, sim, fleet management,
//                 industrial deployments where stable IDs matter for
//                 logging, audit, debugging.
//
//   PER_MISSION   Rotate on SwarmID change or mission boundary. Useful
//                 for mission compartmentalisation. Signed mode only
//                 (rotation in unsigned mode just leaks metadata
//                 without buying security).
//
//   PERIODIC      Rotate every WorldConfig::identity_rotation_period_s
//                 seconds. For privacy / anti-tracking in long-running
//                 deployments. Signed mode only.
//
//   EVENT_DRIVEN  Application calls World::rotate_identity() to trigger.
//                 For compromise response. Signed mode only.
//
// In signed mode (v0.2), rotations issue an IdentityCertificate signed by
// the previous key (continuity proof) so neighbours can correlate
// old→new identity without losing reputation / task history. Pseudonymous
// rotation (no continuity) lands as a separate option.

#include <cstdint>

namespace mith {

enum class IdentityRotationPolicy : std::uint8_t {
    PERMANENT     = 0,
    PER_MISSION   = 1,
    PERIODIC      = 2,
    EVENT_DRIVEN  = 3,
};

} // namespace mith
