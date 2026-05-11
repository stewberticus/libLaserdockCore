# 06 — Build

CMake configuration, targets, options, and platform notes.

---

## Prerequisites (macOS arm64)

```bash
brew install cmake ninja pkg-config libusb qt
```

Qt version tested: **6.7** (latest recommended). Library also supports Qt 5.15.

---

## Building libLaserdockCore (all tiers)

```bash
cd libLaserdockCore
cmake -S . -B build-macos-arm64 -G Ninja \
      -DQTDIR=/opt/homebrew/opt/qt \
      -DLD_CORE_BUILD_EXAMPLE=OFF \
      -DLD_CORE_BUILD_TESTS=OFF \
      -DLD_CORE_BUILD_NETWORK_DEMO=ON   # build the Tier 1 demo

cmake --build build-macos-arm64 --target ld_network_demo
```

The build directory is independent of the source tree. Qt is located via `QTDIR` (or `$QTDIR` env var).

---

## Root CMake Options

| Option | Default | Purpose |
|--------|---------|---------|
| `LD_CORE_BUILD_EXAMPLE` | ON | Full Qt Quick GUI example (`ldCore_Example`) |
| `LD_CORE_BUILD_TESTS` | OFF | Unit tests |
| `LD_CORE_BUILD_NETWORK_DEMO` | OFF | Headless Tier 1 connectivity demo (`ld_network_demo`) |
| `LD_CORE_ENABLE_GAMES` | OFF | Game visualizer modules |
| `LD_CORE_ENABLE_BOX2D` | OFF | Physics simulation |
| `LD_CORE_ENABLE_LUAGAME` | OFF | Lua scripting |
| `LD_CORE_ENABLE_RAZER` | OFF | Razer peripheral integration |
| `LD_CORE_ENABLE_MIDI` | OFF | MIDI input |
| `LD_CORE_ENABLE_OPENCV` | OFF | OpenCV image processing |
| `LD_CORE_USE_OPENGL` | OFF | OpenGL rendering path (Qt Quick compat) |
| `LD_CORE_ENABLE_QT_QUICK` | OFF | Qt Quick/QML (auto-on when example is built) |
| `LD_CORE_ENABLE_QUAZIP` | OFF | ZIP resource extraction (Android) |
| `LASERDOCKLIB_USB_SUPPORT` | ON | Compile USB device support into laserdocklib |

---

## CMake Targets

| Target | Library | Description |
|--------|---------|-------------|
| `laserdocklib` | SHARED (STATIC on iOS) | Low-level Tier 1 network + USB transport |
| `ldCore` | SHARED | Full Tier 2 + Tier 3 pipeline |
| `ld_network_demo` | Executable | Headless Tier 1 integration demo |
| `ldCore_Example` | Executable | Full Qt Quick GUI example (requires `LD_CORE_BUILD_EXAMPLE=ON`) |

---

## Linking Against Tier 1 Only (no ldCore)

If you only need `LaserdockNetworkDevice`:

```cmake
find_package(Qt6 COMPONENTS Core Network REQUIRED)

target_link_libraries(my_target PRIVATE
    laserdocklib
    Qt6::Core
    Qt6::Network
)
```

Set the include path from the `laserdocklib` target (it exports its include dirs as a CMake interface):

```cmake
# laserdocklib already exports:
# target_include_directories(laserdocklib PUBLIC include/)
# So after linking, you can #include <laserdocklib/LaserDockNetworkDevice.h>
```

---

## Linking Against Full ldCore

```cmake
find_package(Qt6 COMPONENTS Core Gui Network Multimedia Qml REQUIRED)

target_link_libraries(my_target PRIVATE ldCore)
# ldCore re-exports laserdocklib, so you get Tier 1 headers too
```

---

## macOS-Specific Notes

- **Architecture:** The build defaults to the host processor (`arm64` on Apple Silicon). Do not force `CMAKE_OSX_ARCHITECTURES=x86_64`; Homebrew Qt is arm64.
- **Deployment target:** Set automatically based on Qt version (Qt 6.6+ → macOS 11.0).
- **libusb:** If `LASERDOCKLIB_USB_SUPPORT=ON`, the build prefers the system libusb from Homebrew (`/opt/homebrew`) over the bundled Intel-only dylib in `3rdparty/`. The bundled dylib is x86_64 only.
- **RPATH:** The `ld_network_demo` target sets `BUILD_RPATH` to `$<TARGET_FILE_DIR:laserdocklib>` so the dylib is found at runtime from the build directory without `install_name_tool`.

---

## Building Just laserdocklib (Tier 1 standalone)

`3rdparty/laserdocklib/CMakeLists.txt` has its own `project()` call and can be built independently:

```bash
cd libLaserdockCore/3rdparty/laserdocklib
cmake -S . -B build -G Ninja \
      -DQTDIR=/opt/homebrew/opt/qt \
      -DLASERDOCKLIB_USB_SUPPORT=OFF   # network only, no libusb needed

cmake --build build --target laserdocklib
```

---

## C++ Standard

- `laserdocklib`: C++17 (set by parent; compatible down to C++11 for its own code)
- `ldCore`: C++17 required
- `ld_network_demo`: C++17

---

## Qt Version Requirements

| Qt | Min | Tested |
|----|-----|--------|
| Qt 6 | 6.6 | 6.7 |
| Qt 5 | 5.15 | 5.15 |

Qt 6 is the primary development target on macOS. Qt 5 support is maintained but receives less testing.

---

## Running the Demo

```bash
cd libLaserdockCore/build-macos-arm64

# Discovery mode (5 s timeout)
./demo/ld_network_demo

# Direct connect
./demo/ld_network_demo --ip 192.168.x.x

# Help
./demo/ld_network_demo --help
```

The demo binary is at `build-macos-arm64/demo/ld_network_demo`. It is not installed anywhere by default.

---

## Windows (Visual Studio)

```bat
cmake -G "Visual Studio 17 2022" -A x64 ^
      -DQTDIR="C:\Qt\6.7.0\msvc2019_64" ^
      -DLD_CORE_BUILD_EXAMPLE=OFF ..
cmake --build . --target ld_network_demo --config Release
```

Note: the Windows build has not been exercised with `LD_CORE_BUILD_NETWORK_DEMO`. Tier 1 (`LaserdockNetworkDevice`) uses `QUdpSocket` which is cross-platform.
