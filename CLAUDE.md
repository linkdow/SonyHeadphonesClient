# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

**Standard build (native):**
```bash
mkdir build && cd build
cmake ..
cmake --build . --target SonyHeadphonesClient
```

**Emscripten (Web) build:**
```bash
mkdir build && cd build
emcmake cmake ..
cmake --build . --target SonyHeadphonesClient
# Serve with: emrun client/index.html
```

**Codegen only** (after modifying protocol headers):
```bash
cmake --build <build-dir> --target codegen
```

**Linux prerequisites:**
```bash
sudo apt install libbluetooth-dev libdbus-1-dev   # Debian/Ubuntu
sudo dnf install bluez-libs-devel dbus-devel       # Fedora
sudo pacman -S bluez dbus                          # Arch
```

**Useful CMake options:**
- `-DMDR_DEBUG=ON` — Enable verbose debug logging
- `-DMDR_BUILD_WITH_ASAN=ON` — Enable address sanitizer
- `-DMDR_ENABLE_CODEGEN=OFF` — Skip LLVM-based codegen (requires LLVM when ON)
- `-DCMAKE_BUILD_TYPE=RelWithDebInfo` — CI build type

**LLVM setup for codegen (Linux):** `llvm-17-dev libclang-17-dev`

There are no automated tests; quality is enforced via clang-tidy and CI platform builds.

## Code Style

- **clang-format**: LLVM base, 120-char lines, 4-space indent. Run before committing.
- **Naming**: Classes/methods/constants → PascalCase; members → camelCase; macros → UPPER_CASE; constants → `kPrefixed`.
- **C++ standard**: C++20 (coroutines, concepts, ranges used throughout).

## Architecture Overview

### Module Boundaries

```
libmdr/      — Sony MDR protocol library (no UI dependency)
client/      — ImGui + SDL3 reference UI (depends on libmdr)
tooling/     — LLVM-based AST codegen tools (build-time only)
contrib/     — FetchContent deps: fmt, SDL3, ImGui
```

### libmdr Protocol Stack

The library is layered:

1. **MDRConnection** (C interface) — Low-level Bluetooth framing; non-blocking send/receive buffers. Exposed as a C API for cross-language compatibility.

2. **Protocol headers** (`libmdr/include/MDR/Protocol/`) — Strongly-typed POD structs representing every payload type. Two protocol tables:
   - `ProtocolV2T1.hpp` — Table 1: ~100+ base commands
   - `ProtocolV2T2.hpp` — Table 2: ~50 extended commands

3. **Generated code** (`libmdr/include/MDR/Protocol/Generated/`) — **Do not edit by hand.** Produced by `tooling/` from the protocol headers:
   - `*Serialization.cpp` — Encode/decode each payload
   - `*Validation.cpp` — Range/enum field validation
   - `*Traits.hpp` — Type metadata
   - `*Enum.hpp` — Enum-to-string conversion

4. **MDRHeadphones** — High-level device object. Owns a property model and drives the async task loop via C++20 coroutines (`MDRTask`).

### Property Model

Every device setting is represented as:
```cpp
MDRProperty<T> {
    T desired;   // pending write
    T current;   // last known device value
};
```
`MDRHeadphones::IsDirty()` detects `desired != current`. The main loop calls:
- `InitV2()` — negotiate capabilities on connect
- `SyncV2()` — read current values from device
- `CommitV2()` — push all dirty properties

### Codegen Annotations

When adding or changing protocol struct fields, use these comment annotations so the codegen tools emit correct validation:
```cpp
// CODEGEN EnumRange VALUE_A VALUE_B VALUE_C
SomeEnum field;

// CODEGEN Range 0 100
uint8_t level;

MDR_DEFINE_EXTERN_SERIALIZATION(MyPayload);    // dynamic (vector/string)
MDR_DEFINE_TRIVIAL_SERIALIZATION(MyPayload);   // POD: direct memcpy
```
After editing a protocol header, re-run `cmake --build <build-dir> --target codegen`.

### Platform Abstraction

Each platform implements the abstract interface in `Platform.hpp`:

| Platform | Directory | Notes |
|----------|-----------|-------|
| Linux | `Platform/Linux/` | DBus + BlueZ |
| Windows | `Platform/Windows/` | WinRT |
| macOS | `Platform/macOS/` | CoreBluetooth |
| Web | `Platform/Emscripten/` | Web Serial API |

### Client (ImGui)

The client is immediate-mode: every frame queries `MDRHeadphones` properties and renders them directly. `PollEvents()` is called each frame to drain the async task queue without blocking.

Entry points are platform-specific (`WinMain` on Windows, `main` elsewhere, `emscripten_set_main_loop` on Web).

## CI

- **cmake.yml** — Native builds: Ubuntu 24.04 (GCC 14), macOS (universal), Windows x64 + ARM64. Build type: `RelWithDebInfo`.
- **emscripten.yml** — Web build + deploys to GitHub Pages at https://mos9527.com/SonyHeadphonesClient/ on push to `rewrite`.
