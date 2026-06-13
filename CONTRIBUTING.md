# Contributing to MithAtomas

Thanks for taking the time. This project is in a high-iteration phase — the architecture is settled (see [ARCHITECTURE.md](ARCHITECTURE.md) §16 Pre-v0.1, 9/9 resolved) but implementation is still evolving. Contributions are welcome at every layer.

## Before you start

1. Read [ARCHITECTURE.md](ARCHITECTURE.md) — at minimum §0 (philosophy), §15 (constraints), and §16 (roadmap). The roadmap tells you what's in flight and what's deferred to which version.
2. For a non-trivial change, **open an issue first** describing what you want to do and why. We'll align on shape before you write code. Saves you rework, saves us the conversation about why the PR can't merge as-is.
3. For a security-relevant change (anything touching §3.3 identity, §13.5 EW posture, transport authentication): say so in the issue. Those land with extra review.

## Set up

```sh
git clone https://github.com/Vighnesh-Patidar/mith-atomas.git
cd mith-atomas
cmake -B build -S .
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Requirements:
- C++17 compiler (GCC ≥ 9 or Clang ≥ 10).
- CMake ≥ 3.21.
- pthreads (linked automatically via `find_package(Threads)`).
- Python 3 + `matplotlib` if you want to run the flocking visualizer.

Optional: enable signed-mode auth scaffolding (no actual crypto yet — that's v0.2):

```sh
cmake -B build -S . -DMITH_ENABLE_AUTH=ON
```

## Coding conventions

Mostly mirrors what's already in `src/` and `include/mith/`. Specifically:

- **Naming**: `snake_case` for files, functions, variables. `PascalCase` for types. `UPPER_CASE` for constants and macros.
- **Headers**: all public API in `include/mith/`. Implementation details in `src/`. No implementation in headers except templates and tiny inline helpers.
- **`noexcept`**: liberal — anything that doesn't allocate or call user code should be marked. Per §15 the runtime does not throw; user-overridable hooks (e.g., `System::tick`) are documented as no-throw contracts even when the keyword isn't on the base.
- **No RTTI** in the component system. Type IDs are compile-time FNV-1a hashes of `__PRETTY_FUNCTION__` (Clang / GCC only).
- **No exceptions in the runtime core** — fail via `std::optional`, status enums, or `std::abort` via the structured `*_assert_fail` helpers for hard contract violations.
- **No dynamic allocation in hot path systems after init** — `std::array`, `BoundedQueue<T, N, Policy>`. Observability paths (`dump_state`, `JsonTraceSink`) may allocate.
- **STL heap allowed at init**; `std::vector` is fine for setup. `std::vector` is *not* fine inside a tight tick loop unless preallocated.

## Tests

Every PR should include tests. The suite uses [doctest](https://github.com/doctest/doctest) (vendored under `tests/third_party/`). Add new cases to the appropriate `tests/unit/test_*.cpp` file or create a new one.

```sh
./build/tests/mith_unit_tests                                 # full suite
./build/tests/mith_unit_tests -tc='your test case name*'      # filter
./build/tests/mith_unit_tests --count=100                     # stress
./build/tests/mith_unit_tests --list-test-cases               # list all
```

Helpers in `tests/unit/test_helpers.h` — `mith_test::JsonCapturingSink` for verifying trace events, `mith_test::contains` for JSON substring checks.

## Commit style

Conventional Commits — see existing log for examples.

```
feat(scope): short summary

Longer body explaining what changed and why. Keep technical
context here, not in the PR description.

- bulleted detail
- bulleted detail
```

Common scopes: `core`, `identity`, `comms`, `sim`, `systems`, `examples`, `docs`, `build`.

Don't include AI attribution lines (`Co-Authored-By: Claude ...`) — commits stand under their author's name. The project has a memory of this preference; AI assistants helping with the work should honor it.

## PR process

1. Branch off `main` (the project has `dev`/`main` discipline in §17 but operates on `main` for now).
2. Push, open a PR using the template (auto-filled from `.github/PULL_REQUEST_TEMPLATE.md`).
3. CI (when configured) should pass — build + tests + no warnings under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
4. Review focuses on: correctness, alignment with the §16 roadmap tier you're targeting, test coverage, and whether the change introduces dependencies the architecture forbids.

## What we look for

- **Bug fixes**: include a reproducing test that fails before the fix.
- **New features**: roadmap-aligned. If it's not on the §16 roadmap, that's a conversation, not a blocker.
- **Refactors**: must reduce code or improve clarity. Refactors that move code around without changing behaviour need a strong motivation.
- **Doc improvements**: always welcome.

## What we don't accept (without prior discussion)

- New external dependencies. The core has zero; sim has one (`nlohmann/json` gated by `MITH_BUILD_SIM`); tests have one (doctest, vendored). Anything else needs §11 justification.
- Public-API changes to types that v0.1 has frozen (identity, registry, scheduler) without §16 v0.5 / v1.0 trigger.
- Use of RTTI, exceptions in core, or runtime allocation in hot paths.
- Cryptographic implementations without a peer-reviewed source (Ed25519, ChaCha20 land in v0.2 from vendored, audited code).

## Reporting bugs

Use the `Bug report` issue template. Include:
- Compiler + version
- CMake configuration (`-DMITH_*` flags)
- Minimal reproducer (test case in the suite or short main.cpp)
- Expected vs actual

## Reporting security issues

For security-sensitive findings (identity, transport, authentication): **do not file a public issue.** Email the maintainer directly. We'll triage privately and coordinate disclosure.

## Code of Conduct

Participation is governed by the [Code of Conduct](CODE_OF_CONDUCT.md). Be patient with people learning the architecture; the doc is long but it's accurate. Be exacting about correctness — this runtime will eventually run on real robots.
