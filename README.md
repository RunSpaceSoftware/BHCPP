# BHCPP

A small bullet-hell **game** on top of a reusable C99 **engine**, built with
[raylib](https://www.raylib.com/). The game owns the run, menus and scoring;
the engine owns the window, time, input, resources, scene graph, level format,
immediate-mode UI and the editor. The editor can open the game's scene, edit it
visually, and play the game inside the viewport without leaving the tool.

The demo ships 33 patterns: fans, spirals, walls with one safe slot,
converging rings, piercing spears, geometric formations, lobbed gravity shells
and scripted mortar fire. Patterns, bullets, sounds, levels and the player's
numbers are plain files under `resources/`.

## Engine / game split

```
build/bhcpp.dll      engine + game module + raylib + Lua
build/game.exe       thin host: window loop + GameUpdate/GameDraw
build/editor.exe     engine editor: scene, hierarchy, inspector, dock tabs
build/shape_test.exe headless checks
```

- `libs/engine.h`, `src/engine/engine.c` - window/frame lifecycle, delta and
  scaled time, paths, logging, engine audio device.
- `libs/input.h`, `src/engine/input.c` - action map and move vector.
- `libs/console.h`, `src/engine/console.c` - 256-line console with 32 commands.
- `libs/assets.h`, `src/engine/assets.c` - texture/sound/music caches, reload.
- `libs/scene.h`, `src/engine/scene.c` - 256-entity scene graph, parented
  transforms, sprites, colliders, `.scene` text format.
- `libs/level.h`, `src/engine/level.c` - named pattern waves in `levels.txt`.
- `libs/ui.h`, `src/engine/ui.c` - dark dockable-workbench widgets and icons.
- `libs/game.h`, `src/game/game.c` - the game client. Windowless: it draws into
  the current render target, so the standalone host paints the screen and the
  editor paints a `RenderTexture2D` for play-in-editor.

## What is in the box

- **Visual editor.** Menu bar, toolbar, Hierarchy/Assets, scene viewport with
  grid, gizmos, snapping and picking, Inspector, and Console/Patterns/Levels
  dock tabs. Draggable splitters, undo/redo, `Ctrl+S`, `F5` reload.
- **Play in editor.** The Game tab runs the real game at 1200x600 in the
  viewport; pause, step one frame and stop without recompiling.
- **Scenes.** `resources/scenes/main.scene` stores the arena, player spawn,
  parented entities, sprites and colliders. Missing files are created as a
  default scene in the engine's user directory.
- **Levels.** `resources/levels.txt` groups pattern names into waves. The game
  advances through them; the editor edits the same file.
- **Patterns as data.** Every field of a pattern is editable in the Patterns
  tab, with a live preview. `F5` reloads the running game.
- **All spawn logic in Lua.** Lua 5.4.7 is embedded. The built-in aims live in
  `resources/scripts/aims.lua`; a pattern can point at its own script with
  `script` and take over spawning completely.
- **Assets as data.** Drop a PNG, WAV or OGG in `resources/`, then assign it in
  the editor or a pattern. A missing file falls back to the circle/no-sound
  path.
- **Editable player and pause menu.** `resources/player.txt` holds HP, speed,
  size and invulnerability. `resources/pause.txt` holds the pause rows.
- **Checks.** Shape geometry, Lua spawn path, sprite plumbing, stats parser,
  pause menu, pattern-table sync and editor layout are all headless.

## Requirements

- **CMake 3.20+**.
- **raylib 6.2**, static library and headers at `C:/raylib/raylib/src`.
- **gcc (MinGW-w64)**, by default `C:/raylib/w64devkit/bin`.

Both raylib paths are overridable:

```
cmake -S . -B build -G "MinGW Makefiles" -DRAYLIB_ROOT=D:/raylib/raylib
```

Lua 5.4.7 is vendored in `third_party/lua` under the MIT licence. Do not edit
it; pattern scripts live in `resources/scripts/`.

## Build

```
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build                       # bhcpp.dll, game.exe and the tools
cmake --build build --target check        # build + run every check
```

Use the MinGW generator so CMake picks gcc rather than Visual Studio.
Everything lands in `build/`. The engine DLL also gets the GLFW joystick
shims on Windows; without them a broken DirectInput install can make raylib
report `PLATFORM: Failed to initialize graphics device` before any window
appears.

The asset copy includes `patterns.txt`, `levels.txt`, `player.txt`,
`pause.txt`, `scenes/*.scene`, `scripts/*.lua`, PNGs, WAV/OGG and the asset
LICENSE. `resources/game.rc` and `icon.ico` are build inputs, not assets.

## Play

Run `build/game.exe`.

- Menu: `Up`/`Down` choose, `Left`/`Right` change difficulty, `Enter` starts,
  `Esc` quits.
- In game: arrow keys move, `Esc` pauses. `R` restarts after game over, `F5`
  reloads patterns, sprites, sounds, scripts, player stats, levels and the
  pause menu without restarting. A typo in `patterns.txt` is reported on the
  menu and the previous table is kept.
- Fixed-point patterns flash a warning circle first. Set `warn` and
  `warn_radius` per pattern; `warn off` removes it.
- The window cannot be made smaller than 800x480.

## Edit

Run `build/editor.exe` from the repo root.

- **Scene** tab in the viewport: click to select, `W`/`E`/`R`/`T` or the
  toolbar switch Select/Move/Rotate/Scale. Drag the gizmo axes; right or middle
  drag pans, wheel zooms. `G` toggles the grid, `S` snapping.
- **Hierarchy** shows parented entities. Add, duplicate (`Ctrl+D`) and delete
  (`Del`); the Inspector edits name, tag, parent, transform, sprite, tint and
  collider.
- **Assets** assigns a PNG to the selected sprite, opens a scene, or reloads
  scripts.
- **Game** tab (or the toolbar Play button) runs the real game in the
  viewport. Pause, Step and Stop are next to it.
- **Console** accepts engine commands such as `help`, `scene`, `assets` and
  `clear`; Up/Down walks command history.
- **Patterns** edits every field with a live 1200x600 preview. Add, delete and
  save back to `patterns.txt`; Up/Down changes the selection.
- **Levels** edits the level set, adds/removes patterns by name and saves
  `levels.txt`.
- `Ctrl+S` saves the scene; `Ctrl+Z`/`Ctrl+Y` undo/redo; `F5` reloads patterns,
  levels, assets and the scene. Unsaved tabs show `*`.
- Layout is saved per run; `View > Reset Layout` restores the default docks.

## Checks

```
cmake --build build --target check
```

Builds the game, editor and tools, then runs:

- `build/shape_test.exe` - geometry, Lua, sprites, stats, pause menu, scene
  graph and level round trips.
- `build/editor.exe --check` - `patterns.txt`, `scenes/main.scene` and
  `levels.txt` all load.
- `build/editor.exe --layout` - the dock layout fits 1100x700 and up.
- `build/pattern_editor.exe --check resources/patterns.txt` - the legacy
  pattern editor still validates the file.

The stats and script tests write throwaway files under `tools/` and
`build/resources/scripts` and remove them again. Run the checks from the repo
root.

## Files

- `CMakeLists.txt` - the build: engine DLL, game host, editor and tools.
- `src/engine/*.c` - engine modules; `src/game/game.c` - game module;
  `src/main.c` - standalone host.
- `libs/*.h` - public engine, game and tool APIs.
- `resources/scenes/main.scene` - the demo scene.
- `resources/levels.txt` - pattern waves.
- `resources/patterns.txt` - the pattern table.
- `resources/player.txt`, `resources/pause.txt` - player and pause data.
- `resources/scripts/aims.lua` - built-in aim logic.
- `resources/scripts/*.lua` - per-pattern scripts.
- `resources/*.png`, `*.wav`, `*.ogg` - assets.
- `tools/editor.c` - engine editor; `tools/pattern_editor.c` - pattern tool;
  `tools/shape_test.c` - headless tests.

## TODO

- [x] **Level system** - named waves in `levels.txt`, with editor support.
- [x] **Editor** - Unity/Unreal-style docks, scene view, inspector and
  play-in-editor.
- [ ] **Music** - the engine asset cache can play a track, but the game does
  not start one yet.
- [ ] **Dialogue system** - text before or after a level.

## Licence

The game and its assets are CC BY 4.0 - see `resources/LICENSE`. Vendored Lua
5.4.7 is MIT; see `third_party/lua/lua.h`.
