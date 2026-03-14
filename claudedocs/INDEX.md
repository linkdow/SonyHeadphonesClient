# SonyHeadphonesClient — Project Knowledge Base

> Generated: 2026-03-11 | Branch: `rewrite`

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Repository Layout](#2-repository-layout)
3. [Architecture](#3-architecture)
4. [libmdr — Protocol Library](#4-libmdr--protocol-library)
5. [Client — Reference GUI](#5-client--reference-gui)
6. [Tooling — Code Generation](#6-tooling--code-generation)
7. [Build System](#7-build-system)
8. [Platform Support](#8-platform-support)
9. [Device Support](#9-device-support)
10. [Developer Guide: Payload Structs](#10-developer-guide-payload-structs)
11. [Key Patterns & Idioms](#11-key-patterns--idioms)
12. [CI/CD](#12-cicd)

---

## 1. Project Overview

**SonyHeadphonesClient** is a cross-platform desktop (and web) client for managing Sony wireless headphones via Bluetooth. It is a ground-up rewrite of [Plutoberth/SonyHeadphonesClient](https://github.com/Plutoberth/SonyHeadphonesClient) with a focus on:

- Standardized MDR protocol v1/v2 support for newer devices
- First-party support across Windows, Linux, macOS, and Web (Emscripten/Wasm)
- A clean, statically-linked build with no runtime dependencies
- LLVM-based code generation for protocol serialization/validation

**Live web version:** https://mos9527.com/SonyHeadphonesClient/

**Nightly builds:** https://nightly.link/mos9527/SonyHeadphonesClient/workflows/cmake/rewrite

### Feature Set (controlled via MDR protocol)
- Noise Cancellation (NC) and Ambient Sound modes
- Audio codec selection (SBC, AAC, LDAC, aptX, aptX-HD, LC3)
- Battery status monitoring
- Playback control and track metadata (AVRCP)
- Parametric equalizer
- Touch sensor enable/disable
- Voice guidance settings
- Auto power-off timer
- Multipoint Bluetooth connection management
- Map headphone gestures to shell commands
- Speak-to-chat integration

---

## 2. Repository Layout

```
SonyHeadphonesClient/
│
├── libmdr/                     # Core MDR protocol library (C++20)
│   ├── include/mdr/            # Public C++ headers (protocol, headphones state machine)
│   ├── include/mdr/Generated/  # Auto-generated serialization/validation code
│   ├── include/mdr-c/          # C language bindings (vtable-based)
│   └── src/
│       ├── *.cpp               # Protocol + headphones implementation
│       └── Platform/           # Bluetooth connectivity per OS
│           ├── Windows/
│           ├── Linux/
│           ├── MacOS/
│           └── Emscripten/
│
├── client/                     # Reference GUI (ImGui + SDL3)
│   ├── Client.cpp              # Full UI logic
│   ├── SDLMain.cpp             # Entry point
│   ├── Fonts/                  # PlexSansIcon + NeoXiHei-Code embedded fonts
│   └── Platform/               # Platform-specific windowing
│
├── tooling/                    # LLVM AST-based code generators
│   ├── Codegen.hpp             # Clang AST walker interface
│   ├── Codegen.cmake           # CMake target integration
│   ├── RunCodegen.cmake        # Build-step script
│   ├── SerializationCodegen.cpp
│   ├── ValidationCodegen.cpp
│   ├── EnumCodegen.cpp
│   ├── TraitsCodegen.cpp
│   └── BinaryEmbed.cpp         # Binary → C array embedding tool
│
├── contrib/
│   └── CMakeLists.txt          # FetchContent: fmt, SDL3, imgui
│
├── docs/
│   ├── README.md
│   └── device-support/         # Per-device feature matrices
│
├── .github/workflows/
│   ├── cmake.yml               # Native (Win/Lin/Mac) CI
│   └── emscripten.yml          # Web/Wasm CI
│
├── CMakeLists.txt              # Root build config
├── DetectPlatform.cmake
├── DetectGitInfo.cmake
├── AGENTS.md                   # AI agent payload-struct implementation guide
└── README.md
```

---

## 3. Architecture

### High-Level Layers

```
┌─────────────────────────────────┐
│         Client (GUI)            │  ImGui + SDL3 reference app
└──────────────┬──────────────────┘
               │ MDRHeadphones C++ API
┌──────────────▼──────────────────┐
│      libmdr (C++20 library)     │  Protocol state machine, async tasks
│  ┌──────────────────────────┐   │
│  │  ProtocolV2T1 / T2       │   │  MDR v2 packet definitions (codegen'd)
│  └──────────────────────────┘   │
│  ┌──────────────────────────┐   │
│  │  Command pack/unpack     │   │  Framing, escaping, checksum
│  └──────────────────────────┘   │
└──────────────┬──────────────────┘
               │ MDRConnection C vtable
┌──────────────▼──────────────────┐
│  Platform Bluetooth Layer       │  Win32 / BlueZ / IOBluetooth / WebSerial
└─────────────────────────────────┘
```

### Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| C++20 coroutines for async | Sequential-looking code without callbacks; timeout management |
| `MDRProperty<T>` dirty tracking | Desired vs. current state; efficient change detection |
| LLVM AST codegen for serialization | Single source of truth in headers; no macro template metaprogramming |
| `#pragma pack(1)` + big-endian wrappers | Byte-level protocol fidelity |
| C vtable for platform abstraction | Platform-agnostic core; easy to add new platforms |
| Static linking of all deps | Zero runtime dependencies; portable binaries |

---

## 4. libmdr — Protocol Library

### Public Headers (`include/mdr/`)

| File | Lines | Purpose |
|------|-------|---------|
| `Protocol.hpp` | ~432 | Base types: `MDRDataType`, big-endian integers (`Int16BE`, `Int32BE`), framing constants |
| `ProtocolV2.hpp` | ~211 | V2 protocol base: table support flags, version negotiation |
| `ProtocolV2T1.hpp` | ~2,652 | Table 1 payload structs (device info, codecs, NC, battery, playback, EQ, etc.) — mostly codegen'd |
| `ProtocolV2T2.hpp` | ~1,163 | Table 2 payload structs (speak-to-chat, gestures, sound pressure, extended codecs) |
| `Headphones.hpp` | ~472 | `MDRHeadphones` class — state machine, coroutine tasks, property management |
| `Command.hpp` | — | Packet packing/unpacking, escape sequences, checksum |

### C Bindings (`include/mdr-c/`)

| File | Purpose |
|------|---------|
| `Connection.h` | `MDRConnection` vtable: `connect`, `disconnect`, `send`, `recv`, `poll`, `getDevicesList` |
| `Headphones.h` | High-level C API over `MDRHeadphones` |
| `Base.h` | Shared primitive types |

### Source Files (`src/`)

| File | Purpose |
|------|---------|
| `Headphones.cpp` | Core headphones state machine |
| `HeadphonesV2.cpp` | MDR v2 protocol init + connection lifecycle |
| `HeadphonesV2T1.cpp` | Table 1 command dispatch |
| `HeadphonesV2T2.cpp` | Table 2 command dispatch |
| `Command.cpp` | Low-level packet framing |

### Platform Implementations (`src/Platform/`)

| Platform | Technology | Notes |
|----------|-----------|-------|
| Windows | Win32 Bluetooth API | RFCOMM via `BluetoothFindFirstRadio`, Winsock2 |
| Linux | BlueZ + D-Bus | Uses `DBusHelper.cpp`; MPRIS for AVRCP metadata |
| macOS | IOBluetoothFramework | Obj-C++ bridge |
| Emscripten | WebSerial API | Android Chrome 138+ supports BT serial |

### MDR Packet Structure

```
[START '<'] [DATA_TYPE] [SEQ_NUM] [LEN_HI] [LEN_LO] [PAYLOAD...] [CHECKSUM] [END '>']
```

- Escape byte `0x3D` precedes any special byte in the payload
- Max packet size: 2048 bytes
- Checksum: XOR of all payload bytes

### Protocol Flow

```
Client          Headphones
  │── ConnectGetProtocolInfo ──▶│
  │◀── ConnectRetProtocolInfo ──│
  │── ConnectGetSupportFunction ▶│
  │◀── ConnectRetSupportFunction │
  │── [Feature-specific GETs] ──▶│
  │◀── [Feature-specific RETs] ──│
  │    (bidirectional NOTIFY)    │
```

---

## 5. Client — Reference GUI

**Stack:** Dear ImGui (1.92.4) + SDL3 (3.2.26) + fmt (12.1.0)

**Entry Point:** `SDLMain.cpp` → creates SDL window/renderer → calls `Client.cpp` render loop

**Key Source:** `Client.cpp` — all UI panels (device list, NC controls, codec info, EQ, battery, playback, settings)

**Fonts:**
- `PlexSansIcon` — IBM Plex Sans + FontAwesome icons (merged via FontForge)
- `NeoXiHei-Code` — CJK support for Web client

**Platform windowing** (`client/Platform/`): thin wrappers for native window creation and Bluetooth device enumeration per OS.

---

## 6. Tooling — Code Generation

All codegen is a **proper CMake dependency** — modifying a header and running `cmake --build <dir> --target codegen` rebuilds only stale tools and regenerates affected files. A normal `cmake --build` also triggers codegen before compiling `mdr`.

### Tools

| Executable | Input | Output |
|-----------|-------|--------|
| `SerializationCodegen` | `ProtocolV2T1.hpp`, `T2.hpp` | `Generated/ProtocolV2T1Serialization.cpp` |
| `ValidationCodegen` | CODEGEN comments in headers | `Generated/*Validation.cpp` |
| `EnumCodegen` | Enum definitions | `Generated/*EnumToString.cpp` |
| `TraitsCodegen` | Struct definitions | `Generated/*Traits.hpp` |
| `BinaryEmbed` | Font/binary files | `Generated/*BinaryEmbed.cpp` |

### LLVM Setup

| Platform | Action |
|----------|--------|
| Windows | Download LLVM release; set `CMAKE_PREFIX_PATH=C:\Program Files\LLVM` |
| Linux | `sudo apt install llvm-17-dev libclang-17-dev` |
| macOS | `brew install llvm` |

### CODEGEN Comment Syntax

Placed above struct fields in headers to drive code generation:

```cpp
// CODEGEN EnumRange UNSETTLED SBC AAC LDAC APT_X APT_X_HD LC3 OTHER
AudioCodec codec;

// CODEGEN Range 0 30
UInt8 level;

// CODEGEN Field subField EnumRange VAL_A VAL_B
NestedStruct nested;
```

**Verbs:**
- `EnumRange [Values...]` — field must be one of the listed enum values
- `Range [Min] [Max]` — numeric field, inclusive bounds
- `Field [FieldName] [Verb] [Args]` — apply validation to a nested struct field

---

## 7. Build System

**Requires:** CMake 3.31+, C++20 compiler (GCC 14+, Clang 21+, MSVC 19+)

### Quick Start

```bash
# Linux (install deps first: libbluetooth-dev libdbus-1-dev)
mkdir build && cd build
cmake ..
cmake --build . --target SonyHeadphonesClient

# Web
mkdir build && cd build
emcmake cmake ..
cmake --build . --target SonyHeadphonesClient
emrun client/index.html
```

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_PREFIX_PATH` | — | Point to LLVM install for codegen tools |
| `MDR_BUILD_WITH_ASAN` | `OFF` | Enable AddressSanitizer |
| `MDR_DEBUG` | `OFF` | Enable verbose debug logging |

### FetchContent Dependencies

| Library | Version | License |
|---------|---------|---------|
| `fmt` | 12.1.0 | MIT |
| `SDL3` | 3.2.26 | zlib |
| `imgui` | 1.92.4 | MIT |

All dependencies are **statically linked** — no runtime DLLs/SOs required.

### Build Targets

| Target | Description |
|--------|-------------|
| `SonyHeadphonesClient` | Main executable (native or Web) |
| `mdr` | Core protocol library |
| `ImGuiSDL3` | Bundled GUI library |
| `codegen` | Rebuild codegen tools + regenerate stale files |

---

## 8. Platform Support

| Platform | Status | Maintainers | Architecture |
|----------|--------|-------------|-------------|
| Windows | Full | @mos9527, @Amrsatrio | x86, x64, ARM64 |
| Linux | Full | @mos9527 | x86, x64, ARM, ARM64 |
| macOS | Full | @mos9527 | Intel, Apple Silicon |
| Web (Emscripten) | Full | @mos9527 | Wasm |

### Linux Quirk: Player Metadata (MPRIS)
Track title/artist may not appear even with correct AVRCP support. Fix:
```bash
# Install: sudo apt install bluez-tools  (or bluez-utils on Arch)
mpris-proxy &   # or run as a systemd service
```
See: [Arch MPRIS wiki](https://wiki.archlinux.org/title/MPRIS), [BlueZ #868](https://github.com/bluez/bluez/issues/868)

### Web Platform Notes
- Requires [Web Serial](https://caniuse.com/wf-serial) browser support
- Works on all modern Desktop Chrome browsers
- Android Chrome 138+ supports Bluetooth serial

---

## 9. Device Support

Feature matrices are in `docs/device-support/`. Legend: ✅ Supported, ❌ Unsupported, ? Untested, ~ Officially supported / pending impl.

| Device | Overall Status |
|--------|---------------|
| WH-1000XM6 | Mostly supported (XM6 Listening Mode added in recent commits) |
| WH-1000XM5 | Mostly supported |
| WF-1000XM5 | Supported |
| WH-CH720N | Supported |
| WF-LS900N | Supported |
| WF-C510 | Supported |
| WH-1000XM4/XM3 | Planned (legacy v1 protocol — in roadmap) |

**Common supported features:** Battery, NC/AMB, Codec info, EQ, Touch sensors, Power off, Track controls, Gestures → shell commands
**Common gaps:** Voice Guidance Volume (device-dependent), Multipoint (device-dependent)

---

## 10. Developer Guide: Payload Structs

Full details in `AGENTS.md`. Summary:

### Trivially Serializable (POD)

```cpp
#pragma pack(push, 1)
struct ConnectRetProtocolInfo {
    Command command{Command::CONNECT_RET_PROTOCOL_INFO};
    ConnectInquiredType inquiredType{ConnectInquiredType::FIXED_VALUE};
    Int32BE protocolVersion;
    MessageMdrV2EnableDisable supportTable1Value;
    MessageMdrV2EnableDisable supportTable2Value;

    MDR_DEFINE_TRIVIAL_SERIALIZATION(ConnectRetProtocolInfo);
};
#pragma pack(pop)
```
- Use `MDR_DEFINE_TRIVIAL_SERIALIZATION` when: no vectors/strings, in-memory == on-wire layout.

### Non-Trivially Serializable (dynamic fields)

```cpp
struct ConnectRetSupportFunction {
    Command command{Command::CONNECT_RET_SUPPORT_FUNCTION};
    ConnectInquiredType inquiredType{ConnectInquiredType::FIXED_VALUE};
    MDRPodArray<MessageMdrV2SupportFunction> supportFunctions;

    MDR_DEFINE_EXTERN_SERIALIZATION(ConnectRetSupportFunction);
};
// Implementation auto-generated in Generated/ProtocolV2T1Serialization.cpp
```
- Use `MDR_DEFINE_EXTERN_SERIALIZATION` when: contains `MDRPodArray`, `MDRArray`, or `MDRPrefixedString`.
- Use `MDR_CODEGEN_IGNORE_SERIALIZATION` to opt out of codegen and implement manually.

### Serialization Helpers

| Helper | Use case |
|--------|----------|
| `MDRPod::Read/Write` | Basic types and PODs |
| `MDRPodArray::Read/Write` | Dynamic arrays of PODs |
| `MDRPrefixedString::Read/Write` | Length-prefixed strings |

### Validation Rules
Every payload struct **must** implement data validation via CODEGEN comments (see §6).

### Macros Summary

| Macro | Header / Impl? | When |
|-------|---------------|------|
| `MDR_DEFINE_TRIVIAL_SERIALIZATION(T)` | Header | POD, no dynamic fields |
| `MDR_DEFINE_EXTERN_SERIALIZATION(T)` | Header | Non-POD; codegen writes impl |
| `MDR_CODEGEN_IGNORE_SERIALIZATION` | Header | Opt-out codegen; manual impl |
| `MDR_DEFINE_EXTERN_READ_WRITE(T)` | Header | Custom field sub-type R/W |
| `MDR_CODEGEN_IGNORE_VALIDATION` | Header | Opt-out validation codegen |

---

## 11. Key Patterns & Idioms

### C++20 Coroutine-Based Async

```cpp
MDRTask MDRHeadphones::RequestInitV2() {
    SendCommandACK(t1::ConnectGetProtocolInfo);
    co_await Await(AWAIT_PROTOCOL_INFO);
    // process response...
    SendCommandACK(t1::ConnectGetSupportFunction);
    co_await Await(AWAIT_SUPPORT_FUNCTION);
    // ...
}
```

### MDRProperty — Desired vs. Current State

```cpp
MDRProperty<AudioCodec> codec;
codec.desired = AudioCodec::LDAC;   // user intent
// later, after device ACK:
codec.commit();                     // desired → current
// or:
bool changed = codec.dirty();       // desired != current
```

### Big-Endian Wrappers

```cpp
Int16BE val;
val = 1234;          // stores big-endian in memory
uint16_t n = val;    // reads as native int
```

### Platform VTable (C API)

```c
MDRConnection conn = {
    .connect = my_bt_connect,
    .disconnect = my_bt_disconnect,
    .recv = my_bt_recv,
    .send = my_bt_send,
    .poll = my_bt_poll,
};
MDRHeadphones* hp = MDRHeadphonesCreate(&conn, userdata);
```

---

## 12. CI/CD

**GitHub Actions workflows** (`.github/workflows/`):

| Workflow | File | Platforms | Trigger |
|---------|------|-----------|---------|
| Native builds | `cmake.yml` | Windows (LLVM 17), Linux (GCC 14), macOS (Clang 21) | push / PR |
| Web build | `emscripten.yml` | Wasm + Node.js | push / PR |

- Nightly artifacts published via [nightly.link](https://nightly.link/mos9527/SonyHeadphonesClient/workflows/cmake/rewrite)
- Deployment to GitHub Pages for the Web version is **automated** — do not deploy manually.
- After each push: use `gh run watch` to monitor the deployment.

---

---

## 13. WH-1000XM4 / V1 Protocol Support

Roadmap item in progress. Full analysis and implementation plan:
→ [`claudedocs/wh1000xm4-v1-support.md`](wh1000xm4-v1-support.md)

Key finding: the Linux transport layer **already uses RFCOMM** and needs zero changes. The only work is adding the V1 protocol UUID, command headers, and handlers.

---

*This index is maintained by Claude Code. For source of truth, always refer to the actual source files and `AGENTS.md`.*
