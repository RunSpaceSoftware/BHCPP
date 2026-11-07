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
mingw32-make PLATFORM=PLATFORM_DESKTOP check
```

Builds the game, editor and shape tests, then runs all three checks.
`PLATFORM=PLATFORM_DESKTOP` is required; the Makefile defaults to WEB and
that fails to link.
