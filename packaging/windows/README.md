# finalGame Windows Build

This folder is the portable Windows build prepared for itch.io.

## Launching

1. Double-click `launch.bat`, or launch the build through itch.io.
2. The engine launcher will show `finalGame`. Select it and click `Play`.

## Folder layout

- `launch.bat` starts the game from this folder so the engine can find `.\resources`.
- `game_engine_dragoiuc.exe` is the engine executable.
- `SDL2.dll`, `SDL2_image.dll`, `SDL2_mixer.dll`, and `SDL2_ttf.dll` are the SDL runtime DLLs needed by this game.
- `resources/finalGame/` contains the game data for this release.
- `.itch.toml` tells the itch app to launch `launch.bat`.

## Notes

- For the current `finalGame` assets, the core SDL DLLs above are sufficient because the game only uses `.png`, `.wav`, and `.ttf` assets.
- If you later add asset formats that rely on optional codecs, you may also need to bundle additional SDL helper DLLs.
