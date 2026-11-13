# 01 — Getting Started

Everything you need to build, run and control the emulator.

**Contents**

1. [Prerequisites](#1-prerequisites)
2. [Building](#2-building)
3. [Running](#3-running)
4. [Command Line Options](#4-command-line-options)
5. [Controls](#5-controls)
6. [Save Data](#6-save-data)
7. [Troubleshooting](#7-troubleshooting)

---

## 1. Prerequisites

| Platform | Requirements |
|---|---|
| Linux | GCC 9+ or Clang 10+ (C++17), CMake 3.16+, SDL2 development libraries |
| macOS | Xcode Command Line Tools, CMake 3.16+ (Homebrew), SDL2 (Homebrew) |
| Windows | Visual Studio 2019+, CMake 3.16+, SDL2 via vcpkg |

Minimum compiler standard: **C++17**.

### 1.1 Installing dependencies

**Ubuntu / Debian:**

```bash
sudo apt update
sudo apt install build-essential cmake libsdl2-dev
```

**Fedora / Arch:**

```bash
sudo dnf install gcc-c++ cmake SDL2-devel      # Fedora
sudo pacman -S base-devel cmake sdl2           # Arch
```

**macOS:**

```bash
brew install cmake sdl2
```

**Windows (vcpkg):**

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install sdl2:x64-windows
C:\vcpkg\vcpkg integrate install
```

## 2. Building

### 2.1 Standard build

```bash
git clone https://github.com/InanXR/GameBoyEmulator.git
cd GameBoyEmulator
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable is `build/gameboy` (Linux/macOS) or
`build/Release/gameboy.exe` (Windows).

### 2.2 Windows with vcpkg

```powershell
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

### 2.3 CMake presets (CMake 3.21+)

Presets encode the standard configurations, including the
`release-accurate` variant that registers the ROM-downloading accuracy
suite with CTest:

```bash
cmake --preset release              # configure -> build/release
cmake --build --preset release      # build
ctest --preset release-accurate     # unit + accuracy tests
```

Other presets: `debug`, `release`, `release-accurate` (see
`CMakePresets.json`). The classic `-B build` flow from 2.1 works with
any CMake >= 3.16 and keeps binaries in `build/`.

### 2.4 Build types

| Type | Flag | Use |
|---|---|---|
| Release | `-DCMAKE_BUILD_TYPE=Release` | Default; optimized |
| Debug | `-DCMAKE_BUILD_TYPE=Debug` | Symbols + assertions |
| RelWithDebInfo | `-DCMAKE_BUILD_TYPE=RelWithDebInfo` | Optimized with symbols |

If no build type is given, the CMake configuration defaults to Release.

### 2.5 Build artifacts

| Target | Description |
|---|---|
| `gameboy` | The emulator executable |
| `gb_unit_tests` | Unit test binary (also runs via `ctest`) |

## 3. Running

```bash
./build/gameboy "roms/YourGame.gb"
```

Windowed mode opens a 160×144 window scaled 4× (configurable), with
audio and both keyboard and gamepad input. Frame pacing targets
59.7275 fps using vsync when available, with a timer fallback.

Place ROM files in the `roms/` directory (they are git-ignored; do not
commit copyrighted material).

### 3.1 Headless mode

For automation and testing, the emulator can run without a window:

```bash
./build/gameboy "roms/cpu_instrs.gb" --headless 2400
```

Anything the ROM writes to the serial port is echoed to stdout — this
is how test ROMs report results. See [04-Testing](04-TESTING.md).

## 4. Command Line Options

| Option | Effect |
|---|---|
| `--headless N` | Run N frames without a window or audio, then exit |
| `--screenshot FILE` | Headless only: write the final framebuffer as a binary PPM (P6) image |
| `--scale N` | Window integer scale factor, 1–8 (default 4) |
| `--help`, `-h` | Print usage |

Exit codes: `0` on normal completion, non-zero on a bad ROM path or
invalid arguments.

## 5. Controls

### 5.1 Game Boy buttons

| Keyboard | Gamepad | GameBoy Button |
|---|---|---|
| Arrow keys | D-Pad | D-Pad |
| Z | A | A |
| X | B | B |
| Enter | Start | Start |
| Shift | Back/Select | Select |

Keyboard and gamepad inputs are merged, so both work simultaneously.

### 5.2 Hotkeys

| Hotkey | Action |
|---|---|
| **F1** | Reset the console (keeps the battery save) |
| **F2** | Screenshot → `<rom>.ppm` next to the ROM |
| **F5** | Save state → `<rom>.state` |
| **F8** | Load state |
| **P** | Pause / resume |
| **Tab** | Turbo: hold to run ~5× speed |
| **F11** | Toggle borderless fullscreen |
| **+ / =** | Volume up |
| **-** | Volume down |
| **M** | Mute / unmute |
| **ESC** | Quit |

## 6. Save Data

| File | Written when | Contents |
|---|---|---|
| `<rom>.sav` | On exit (and periodically for battery carts) | Battery-backed cartridge RAM, plus RTC registers for MBC3 timer carts |
| `<rom>.state` | On F5 | Complete machine state (see [02-Architecture §5](02-ARCHITECTURE.md#5-save-state-format)) |

Both files live next to the ROM. They are git-ignored.

## 7. Troubleshooting

**"SDL initialization failed"** — install the SDL2 *development*
package, not just the runtime library. Verify with
`pkg-config --modversion sdl2` (Linux) or `sdl2-config --version`.

**"ROM file too small to be a valid GameBoy ROM"** — the file is not a
cartridge dump. A real ROM is at least 0x150 bytes (the header area) and
contains the Nintendo logo at 0x104.

**No audio** — check that no other application holds the audio device.
The emulator logs the opened device's sample rate at startup.

**Audio crackling on some games** — the mixer follows the documented
Game Boy output model including DC-bias pops when games switch DACs
on/off; some pops are accurate emulation, not a defect.

---
*Next: [02 — Architecture](02-ARCHITECTURE.md)*
