#pragma once

// MithAtomas API stability annotations — see docs/SEMVER.md.
//
// Three tiers, applied to public class/struct declarations and to
// free functions in the public namespace. The macros expand to
// nothing at compile time — they are documentation hints picked up
// by Doxygen (the docs site groups by tier) and a uniform anchor
// for future static-analysis tools.
//
//   MITH_STABLE_API
//     Public type or function whose signature is locked under the
//     v1.0 SemVer contract. Removal or signature change is a major
//     version bump. This is the default for everything exposed in
//     a v1.0 release; you don't need to opt in unless you want the
//     marker visible.
//
//   MITH_EXPERIMENTAL_API
//     Public surface that may change in any release — minor, patch,
//     or major. Use at your own risk; pin to a specific version if
//     you depend on it. Items shipped behind a CMake gate that's
//     OFF by default (e.g. MITH_ENABLE_SERIAL → SerialTransport)
//     are experimental until the gate flips on by default.
//
//   MITH_INTERNAL
//     Implementation detail. NOT part of the public API even if
//     accidentally visible. Anything under mith::detail:: implicitly
//     has this tier; this macro is for cases that live outside the
//     detail namespace by necessity (template helpers, friend types,
//     CRTP base classes).
//
// Future versions may add visibility attributes (e.g.
// __attribute__((visibility("default")))) to MITH_STABLE_API to
// shape the shared-library symbol table; for now they're purely
// documentation.

#define MITH_STABLE_API
#define MITH_EXPERIMENTAL_API
#define MITH_INTERNAL
