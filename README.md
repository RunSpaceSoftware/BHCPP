# BHCPP

A small bullet-hell engine and playable demo, written in C99 on top of
[raylib](https://www.raylib.com/). Everything that makes the game interesting is
meant to be tuned without touching C: the attack patterns, the bullets
themselves, and the player's own numbers all live in plain files under
`resources/`, and a standalone editor edits them while you watch the result.

Bullet-hell in this sense is a top-down dodging game: enemies never appear, the
arena simply fills with overlapping patterns of projectiles and the player stays
alive by reading the gaps between them. The engine ships 33 of those patterns,
from fans and spirals to walls with one safe slot, converging rings, piercing
spears, geometric formations, lobbed gravity shells and scripted mortar fire.

## What is in the box

- **Patterns as data.** Each pattern is a block of text in
  `resources/patterns.txt` - origin, aim, count, spread, speed, delay,
  repeat, damage, radius, colour, sprite, spin, gravity and shape parameters.
  `F5` reloads them in a running game.
- **All spawn logic in Lua.** Lua 5.4 is embedded. The built-in aims live in
  `resources/scripts/aims.lua`, so `player`, `angle`, `radial`, `wall`, `rain`,
  `crossfire`, `encircle`, `cross`, `spear`, `formation` and `alternate` are
  editable scripts rather than compiled C. A pattern can also point at its own
  script with the `script` key and take over spawning completely.
- **Bullets are PNGs.** Drop any image into `resources/` and a pattern can use
  it with `sprite`. The hitbox stays the pattern's `radius`, so art never
  changes how hard a pattern is to dodge. Circles remain the fallback when no
  sprite is set or the file is missing.
- **Editable player.** HP, move speed, size and invulnerability time live in
  `resources/player.txt` and the editor can change them too.
- **A pattern editor.** `tools/pattern_editor.exe` lists every pattern, previews
  it live against a movable player marker, and exposes all of its fields as
  sliders and pickers, plus a sprite picker filled from the PNGs you have
  dropped in.
- **Checks.** A headless test binary covers the shape geometry, the Lua spawn
  path, the sprite plumbing and the stats parser; two more checks keep
  `patterns.txt` and the built-in table in sync and prove the editor panel fits
  its window.

## Requirements

You need **raylib installed** - it is not vendored, and nothing runs or builds
without it:

- **raylib 6.2**, built as a static library, with its headers and library
  reachable at `C:/raylib/raylib/src`.
- **gcc (MinGW-w64)**, by default at `C:/raylib/w64devkit/bin`. The Makefile
  uses `windres` and GNU ld features, so a MinGW toolchain on Windows is what
  this is built and tested with.

Both locations can be overridden on the command line:

```
mingw32-make PLATFORM=PLATFORM_DESKTOP RAYLIB_PATH=D:/raylib/raylib COMPILER_PATH=D:/mingw64/bin
```

Lua is the one dependency you do *not* install: 5.4.7 is vendored in
`third_party/lua` under the MIT licence, whose notice travels in `lua.h`.

## Build

```
mingw32-make PLATFORM=PLATFORM_DESKTOP            # game.exe
mingw32-make PLATFORM=PLATFORM_DESKTOP editor     # tools/pattern_editor.exe
mingw32-make PLATFORM=PLATFORM_DESKTOP shapetest  # tools/shape_test.exe
```

`PLATFORM=PLATFORM_DESKTOP` is required. The Makefile defaults to the web
target, which does not link against a desktop raylib.

## Play

Run `game.exe`.

- Menu: `Up`/`Down` to choose, `Left`/`Right` to change difficulty, `Enter` to
  start, `Esc` to quit.
- In game: arrow keys to move, `R` to restart after game over, `Esc` for the
  menu, `F5` to reload patterns, sprites, scripts and player stats without
  restarting. If `patterns.txt` has a typo, F5 says so on the menu and keeps
  the patterns you already had.
- Patterns that fire from a fixed point (rings, spirals, lattices) flash a
  warning circle for a second first, so you can get off the spot. Set `warn`
  and `warn_radius` per pattern in `patterns.txt` (`warn off` removes it).
- The window cannot be made smaller than 800x480; below that the holes in the
  bullet walls get too narrow to fit through.

## Edit

Run `tools/pattern_editor.exe` from a terminal (it is a console program, so
messages like "could not write ..." stay visible).

- Click a pattern in the list to preview it; move the mouse over the arena to
  move the player marker the patterns aim at.
- Sliders and pickers on the right edit the selected pattern; the block under
  the arena edits the player.
- `Space` pauses the preview, `R` restarts it, `Up`/`Down` change pattern,
  `Ctrl+S` writes `patterns.txt` and `player.txt`, `Ctrl+R` reloads both from
  disk.

## Checks

```
mingw32-make PLATFORM=PLATFORM_DESKTOP check
```

Builds everything and runs three checks: the shape/script/stats tests,
`patterns.txt` against the built-in table, and the editor panel-fit check.
`tools/shape_test.exe` prints its results, so run it from a terminal rather
than by double-clicking it.

## Files

- `resources/patterns.txt` - the patterns; `patterns_io.h` documents every key.
- `resources/player.txt` - hp, speed, size, invuln.
- `resources/*.png` - bullet sprites, picked per pattern with `sprite`.
- `resources/scripts/aims.lua` - spawn logic for the built-in aims.
- `resources/scripts/*.lua` - per-pattern scripts, picked with `script`.
  Scripts only get the base, coroutine, table, string, math and utf8
  libraries - no `io`, `os` or `require` - so a shared pattern pack cannot
  touch your files.
- `tools/make_sprites.py` - regenerates the sample sprites (needs Pillow).

## TODO

- [ ] **Audio support** - music and sound effects for the engine, loaded from
  `resources/` and driven by the same data files as the patterns.
- [ ] **Level system** - group patterns into levels and move on to the next one
  when a level is cleared.
- [ ] **Better menu and a pause button** - a proper menu, plus pausing mid-game
  instead of only being able to restart.
- [ ] **Window-scaled bullet counts** - a mode where the number of bullets
  follows the window size, so a bigger window means a denser pattern.
- [ ] **Customizable game over screen** - the death screen's text and look
  configurable from `resources/`.
- [ ] **Dialogue system** - text-based conversations, for story lines before or
  after a level.

## Licence

The game and its assets are licensed CC BY 4.0 - see `resources/LICENSE`.
Vendored Lua 5.4.7 is MIT; see the notice in `third_party/lua/lua.h`.
