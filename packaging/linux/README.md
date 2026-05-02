# finalGame Linux Build

This folder is the portable Linux build prepared for itch.io.

## Launching

1. If you downloaded this build manually, make sure the launcher and binary are executable:

```bash
chmod +x launch.sh game_engine_dragoiuc
```

2. Start the game:

```bash
./launch.sh
```

3. The engine launcher will show `finalGame`. Select it and click `Play`.

## Folder layout

- `launch.sh` starts the game from this folder so the engine can find `./resources`.
- `resources/finalGame/` contains the game data for this release.
- `.itch.toml` tells the itch app to launch `launch.sh`.

## Linux runtime libraries

This build depends on system SDL runtime libraries. If it fails to launch on a machine, install the libraries that provide:

- `libSDL2-2.0.so.0`
- `libSDL2_image-2.0.so.0`
- `libSDL2_mixer-2.0.so.0`
- `libSDL2_ttf-2.0.so.0`
