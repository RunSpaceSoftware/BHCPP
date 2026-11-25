# AGENTS.md

Conventions for AI agents working in this repo.

## Commit attribution

Every commit an agent makes ends with this trailer:

```
Co-authored-by: DeepSeek Harness <noreply@deepseek.com>
```

Only add it to commits the agent actually wrote. Never add it to a commit
authored by a human.

## Before committing

```
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --target check
```

Builds the game, editor and shape tests, then runs the shape suite, the
editor's pattern/scene/level validation, the panel-layout check, and the
legacy pattern-file check.

## Third-party code

`third_party/lua` is vendored Lua 5.4.7 (MIT). Do not edit it; the build
compiles every `.c` there except `lua.c` and `luac.c`, which have their own
`main()`. Pattern scripts live in `resources/scripts/` instead.
