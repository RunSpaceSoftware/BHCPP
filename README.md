# BHCPP
An engine for making bullethell games on raylib

## Build

```
mingw32-make PLATFORM=PLATFORM_DESKTOP            # game.exe
mingw32-make PLATFORM=PLATFORM_DESKTOP editor     # tools/pattern_editor.exe
mingw32-make PLATFORM=PLATFORM_DESKTOP shapetest  # tools/shape_test.exe
```

`PLATFORM=PLATFORM_DESKTOP` is required. Without it the Makefile builds for the
web target, which does not link against the desktop raylib. Expects gcc and
raylib under `C:/raylib`.

## Checks

```
mingw32-make PLATFORM=PLATFORM_DESKTOP check
```

Builds the game and both tools, then runs the shape tests, the
`patterns.txt` vs built-in table sync check, and the editor panel-fit check.

## Tools

- `game.exe` - the game. `F5` reloads `resources/patterns.txt` and bullet sprites.
- `tools/pattern_editor.exe` - edits patterns visually and saves
  `resources/patterns.txt`. It is a console program: run it from a terminal so
  messages like "could not write ..." stay on screen.
- `tools/shape_test.exe` - headless, prints its results. Run it from a terminal
  (or use `make check`); double-clicking closes the window before you can read
  anything.

## Pattern data

- `resources/patterns.txt` - one block per pattern; `patterns_io.h` documents
  every key.
- `resources/player.txt` - hp, speed, size and invulnerability time. The
  pattern editor edits these too.
- `resources/*.png` - bullet sprites, picked per pattern with `sprite`.
- `resources/scripts/aims.lua` - the spawn logic for every built-in aim
  (`player`, `angle`, `radial`, `wall`, `rain`, `crossfire`, `encircle`,
  `cross`, `spear`, `formation`, `alternate`).
- `resources/scripts/*.lua` - per-pattern logic, picked with `script`; a
  pattern with a script ignores its `aim`.

Lua 5.4.7 is vendored in `third_party/lua` (MIT) so there is nothing to install.
