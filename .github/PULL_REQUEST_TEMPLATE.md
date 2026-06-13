<!-- Thanks for the PR. Keep this template; fill in the sections that apply. -->

## Summary

<!-- One or two sentences: what does this PR do, and why? -->

## Roadmap alignment

<!-- Which §16 tier does this target? (Pre-v0.1 / v0.1 / v0.2 / v0.3 / v0.4 / v0.5 / v1.0 / post-v1.0) -->
<!-- If the change isn't on the roadmap, link the issue where we agreed to do it. -->

- Tier:
- Related issue: #

## Changes

<!-- Bulleted list of what changed. Group by area if it spans modules. -->

-

## Test plan

<!-- How did you test this? doctest cases added? Manual repro? -->

- [ ] New / updated unit tests in `tests/unit/`
- [ ] `ctest --output-on-failure` is green locally
- [ ] No new compiler warnings under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`
- [ ] If it affects sim: `./build/examples/flocking_demo/flocking_demo` still runs

## Checklist

- [ ] My code follows the conventions in [CONTRIBUTING.md](../CONTRIBUTING.md)
- [ ] I've updated `ARCHITECTURE.md` if I changed the spec
- [ ] I've updated `README.md` if I changed the public API or build pipeline
- [ ] My commits use the Conventional Commits style
- [ ] No new external dependencies (or I've justified the addition per §11)
- [ ] No RTTI / runtime exceptions in core / heap allocation in hot path

## Notes for reviewers

<!-- Anything specific to look at, design trade-offs, follow-up work parked for later. -->
