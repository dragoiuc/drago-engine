# DragoEngine

DragoEngine is a lightweight, cross-platform 2D game engine written in C++ with support for Windows, Linux, and macOS.

The engine uses SDL2 for windowing, rendering, input, audio, and text rendering, Box2D for 2D physics simulation, Lua and LuaBridge for gameplay scripting, and RapidJSON for scene/configuration loading. Gameplay behavior is defined through Lua components, allowing users to create actors, control scenes, handle input, render images/text, play audio, manipulate physics, and define game logic without recompiling the C++ engine.

DragoEngine also includes custom editor and workflow features beyond the base engine requirements. A Dear ImGui-powered editor interface allows users to construct games and write scripts from within the engine environment. CMake support was added for cross-platform builds on Windows and Linux, improving portability outside IDE-specific workflows. The engine also includes a shadow-main autosave system that preserves progress while edits are pending, helping prevent work loss before changes are merged.

### Features

- Cross-platform 2D engine written in modern C++
- SDL2-based rendering, input, audio, text, and window management
- Lua scripting support for externalized gameplay logic
- Component-based actor system with lifecycle methods such as `OnStart`, `OnUpdate`, and `OnLateUpdate`
- Box2D-powered Rigidbody physics and collision support
- Scene loading, actor instantiation, destruction, and persistence APIs
- Data-driven configuration using JSON scene and resource files
- Runtime scripting APIs for input, audio, images, text, camera, scene management, and application control
- Data-oriented particle system for efficient visual effects
- Dear ImGui editor tooling for in-engine game construction and scripting
- CMake build support for Windows and Linux
- Shadow-main autosave workflow to preserve user progress during edits

# Installation

### Requirements

- A C++17-capable compiler
- CMake 3.10 or newer
- A checked out copy of this repository, including the `resources/` folder
- One of these supported environments:
  - Windows 10 or 11 with Visual Studio 2022 (or the Visual Studio 2022 Build Tools) and the Desktop development with C++ workload
  - Linux with `pkg-config`, `SDL2`, `SDL2_image`, `SDL2_mixer`, and `SDL2_ttf` development packages installed

This repository already includes the project source plus vendored `Lua`, `Box2D`, `ImGui`, and the Windows SDL import libraries/runtime DLLs, so you do not need to install those separately.

### Windows

1. Install Visual Studio 2022 and make sure the Desktop development with C++ workload is selected.
2. Open a terminal in the repository root.
3. Configure the project:

```bash
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
```

4. Build the Release version:

```bash
cmake --build build-windows --config Release
```

5. Run the executable:

```bash
build-windows/Release/game_engine_dragoiuc.exe
```

If you want a portable release folder instead of just a local build, run:

```bash
cmake --install build-windows --config Release --prefix dist/finalGame-windows
```

### Linux

On Ubuntu or Debian, install the required tools and SDL development packages with:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libsdl2-dev libsdl2-image-dev libsdl2-mixer-dev libsdl2-ttf-dev
```

Then, from the repository root:

1. Configure the project:

```bash
cmake -S . -B build-linux
```

2. Build it:

```bash
cmake --build build-linux -j
```

3. Launch the engine from the repository root:

```bash
./build-linux/game_engine_dragoiuc
```

The engine looks for game content in `./resources`, so keep the `resources/` directory next to the executable in packaged builds, or run the Linux binary from the repository root as shown above.

### Optional Packaging Scripts

- `./scripts/package_linux_release.sh` creates `dist/finalGame-linux` from an existing Linux build.
- `./scripts/package_windows_release.sh` can build and package the Windows release from WSL if Windows CMake and Visual Studio Build Tools are installed.


# Support

## If you find a security vulnerability, do NOT open an issue. Please message supportking on Discord instead.

If you have a bug, or an issue with this project, please contact me via discord at: supportking or open an issue. If opening, an issue for a bug report, please include your operating system, a description of the bug, a reproduction of the bug visually via video, and a list of steps taken to reproduce the bug.

# Roadmap

# If you have ideas for releases in the future, it is a good idea to list them in the README.
 - Add GoogleTest for both regression and integration testing
 - Add AlphaRemover, a tool that will let editors quickly iterate on AI generated pixel art
 - Add a drag-and-drop viewport via ImGui
 - Add CMake support for macOS
 - Improve ImGui UI
 - Add Badges to the GitHub Repo

# 

# Contributing -- Currently accepting contributions.

As the project is still in it's infancy, and doesn't have testing added yet, the contribution process is relatively informal. Please include a description of what you changed and why you made those specific changes. When testing is added, please include a regression test.

# Authors and acknowledgment

Reserved for those who have contributed to the project.
 
# This Project uses a zLib License
## Badges -- WIP

# Project status
I'm focusing my energy into AlphaRemover, a seperate repository I intend to become a submodule within this project. This project is open to all contribution within the scope of the current road map.

