# MithAtomas SemVer policy

Effective from **v1.0.0**. Pre-v1.0 releases reserved the right to break
anything between minor versions; v1.0 inherits a stable contract on
its public API surface and downgrades change to predictable axes.

## The three tiers

Every public type, function, and macro lands in exactly one of three
stability tiers. The tier is marked at the declaration site with one
of:

```cpp
#include "mith/api_stability.h"

class MITH_STABLE_API World { ... };
class MITH_EXPERIMENTAL_API SerialTransport { ... };
namespace detail { class MITH_INTERNAL Foo { ... }; }
```

The macros are no-ops at compile time. They drive Doxygen's
"grouped by stability tier" landing page and serve as a single anchor
for future static-analysis tools to enforce the contract.

### `MITH_STABLE_API`

The default for v1.0-released APIs. The signature is locked.

| Change | Allowed in version bump |
|---|---|
| Add a new public method | minor |
| Add a new overload (no ambiguity) | minor |
| Add a new public field | minor (if struct-style) / never (if class with invariants) |
| Add a default-argument'd parameter | minor (source-compat; ABI-break) |
| Change a parameter type, return type, or remove a method | major |
| Remove a public field | major |
| Change observable behaviour for an existing call | major |
| Tighten a documented precondition | major |
| Loosen a documented postcondition | major |
| Fix a bug | patch (if it doesn't break existing valid callers) |

### `MITH_EXPERIMENTAL_API`

Public surface that hasn't reached the stability promise yet. May
change in any release — minor, patch, or major. Use at your own risk;
pin to a specific version if you depend on it.

The tier is the default for:

- Anything behind a CMake gate that's OFF by default (e.g.
  `MITH_ENABLE_SERIAL` → `SerialTransport`). The gate flipping ON by
  default is the trigger for promotion to `MITH_STABLE_API`.
- Features explicitly marked "v0.x" in the ARCHITECTURE.md roadmap
  that have shipped but not yet been validated against the v1.0
  contract.
- New systems / components added between major versions, until they
  appear in two successive minor releases without churn.

### `MITH_INTERNAL`

Implementation detail. Removal or signature change in any release.

Three places this applies:
1. Anything in a `mith::detail::` namespace (implicit — no marker
   needed but tolerated for readability).
2. Helpers that must live outside `detail::` (CRTP bases, template
   metaprogramming utilities visible from public headers).
3. Symbols exported only because the linker requires them (private
   member functions exposed because templates instantiate in user
   TUs).

## Versioning rules

Releases follow [SemVer 2.0.0](https://semver.org/) with these
project-specific clarifications:

- **Major** (`x.0.0`) bumps when any `MITH_STABLE_API` surface changes
  in a way the table above flags as `major`, or when the
  ARCHITECTURE.md authoritative design changes (e.g. §16 milestone
  redefinition).
- **Minor** (`1.x.0`) bumps for added stable surface, promoted
  `MITH_EXPERIMENTAL_API` items, new CMake options, new built-in
  components or systems.
- **Patch** (`1.0.x`) bumps for bug fixes, internal-only changes,
  documentation updates, performance improvements that don't change
  observable behaviour.

Pre-release tags (`-alpha`, `-rc.1`) are reserved for v2.x+ when a
larger API churn is staged.

## What's in each tier at v1.0

The Doxygen output's "Module" tabs split public types by tier:

- **Stable** — core runtime (`World`, `EntityRegistry`,
  `SystemScheduler`, `System`, `SystemDescriptor`), identity (`UUID`,
  `HierarchicalID`, `IdentityKey`, `IdentityVerifier`,
  `Ed25519IdentityVerifier`), comms (`StateVector`, `Message`,
  `NeighbourTable`, `TransportLayer`, `BeaconTransport`,
  `MessageTransport`, `BeaconSystem`), all §4.4 built-in components,
  all v0.1–v0.3 systems, observability (`TraceSink`, `JsonTraceSink`,
  `NullTraceSink`).
- **Experimental** — `BinaryTraceSink` (small-buffer truncation
  semantics may tighten), `SerialTransport` (CMake-gated, needs
  hardware validation per §16 v0.3), `UDPMulticastTransport` (works
  but field validation pending), `PartitionMergeSystem` (heal-event
  threshold tuning may change once epoch-leader / version-vector
  reconciliation lands per the deferred §16 v0.4 entry).
- **Internal** — `mith::detail::`, `mith::udp_wire::` byte
  serialisation helpers, `mith::sim::SimBus::Impl`-style pImpls.

If you read a header and the tier marker is missing, treat the
surface as **stable** — that's the default we're promising on
shipped v1.0 declarations. New surfaces added without a marker
between v1.0 and v2.0 are bugs; please file an issue.
