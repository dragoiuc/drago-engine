# Building With CMake

## Windows (bundled SDL libraries)

From the repo root:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix dist/finalGame-windows
```

This CMake setup:

- builds vendored `Lua` from `libraries/lua/src`
- builds vendored `Box2D` from `libraries/box2d/src`
- links against the SDL `.lib` files already stored in this repo
- copies `resources/` and the runtime DLLs from `dlls/` next to the executable after build
- installs a clean Windows release folder containing `game_engine_dragoiuc.exe`, the SDL runtime DLLs, `resources/finalGame`, and the MSVC runtime when CMake can detect it

### Windows from WSL

If you have Visual Studio Build Tools and CMake installed on Windows, you can
drive the Windows build directly from WSL with:

```bash
./scripts/package_windows_release.sh
```

That script:

- invokes Windows `cmake.exe` with the Visual Studio generator
- builds the `Release` configuration
- installs a portable folder at `dist/finalGame-windows`
- adds `launch.bat` and `.itch.toml` so itch.io launches the game from the
  correct working directory

For the current `finalGame` assets, the packaged Windows folder only needs:

- `game_engine_dragoiuc.exe`
- `SDL2.dll`
- `SDL2_image.dll`
- `SDL2_mixer.dll`
- `SDL2_ttf.dll`
- `resources/finalGame/`

## Linux

Install development packages for:

- `sdl2`
- `SDL2_image`
- `SDL2_mixer`
- `SDL2_ttf`

Then run:

```bash
cmake -S . -B build
cmake --build build -j
```

Linux uses system SDL packages and still builds vendored `Lua` and `Box2D`.

### Linux itch.io release folder

To refresh `dist/finalGame-linux` from the current Linux build and the current
`resources/finalGame`, run:

```bash
./scripts/package_linux_release.sh
```

That script copies:

- the current `build-linux/game_engine_dragoiuc`
- the current `resources/finalGame`
- the itch.io metadata files (`launch.sh`, `.itch.toml`, `README.md`)

It also removes stale files from `dist/finalGame-linux/resources/finalGame` so
the release folder cannot drift out of sync with your live game resources.
