# MithAtomas

Swarm robotics orchestration runtime — C++17.

> **Decentralised by default. Composable by design. Hardware-agnostic by necessity.**

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full design.

## Status

**Pre-v0.1** — design-resolution phase. Open architectural questions in §15 must be settled before implementation begins. The current `include/` tree is namespace stubs; no source files yet.

## Build

```sh
cmake -B build
cmake --build build
```

CMake options (see §11):

| Option | Default | Purpose |
|---|---|---|
| `MITH_BUILD_EXAMPLES` | `ON`  | Build example programs |
| `MITH_BUILD_TESTS`    | `ON`  | Build test suite |
| `MITH_BUILD_SIM`      | `ON`  | Build simulation harness |
| `MITH_ENABLE_UDP`     | `ON`  | Build UDP transport |
| `MITH_ENABLE_SERIAL`  | `OFF` | Build serial transport |

## License

Apache 2.0 — see [LICENSE](LICENSE).
