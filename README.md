# Input Buffer (Geometry Dash, Geode mod)

Click up to N physics frames early; the click fires on the first frame you can act
(landing, or touching an orb/ring).

## Build (easiest: GitHub Actions)
1. Create a GitHub repo, upload these files, push to `main`.
2. Actions tab -> "Build Mod" -> download the artifact, it contains `yourname.input-buffer.geode`.
3. Put the `.geode` file in `Geometry Dash/geode/mods/`.

## Build locally
Install the Geode CLI + SDK (docs.geode-sdk.org), then:
    geode build

## Settings
In game: Geode menu -> Mods -> Input Buffer -> Settings.
- Buffer window: 0-60 physics steps (240 TPS -> 4 steps = 1 frame @ 60 FPS)
- Click must still be held / tap mode
- Per-player toggles, master enable

Change `yourname` in mod.json (id + developer) before publishing.
