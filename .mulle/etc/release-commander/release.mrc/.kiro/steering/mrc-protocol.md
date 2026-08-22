# MRC Release Protocol

This bundle is a mulle-release-commander (MRC) task bundle for a software release.

## Bundle structure

- `db.json` — task database (array of files with status, title, filenames)
- `i-*.md` — task instruction files (what to do)
- `s-*.sh` — task scripts (how to do it)
- `l-*.log` — execution logs (evidence of completion)
- `AI-EXECUTION.md` — execution protocol rules (authoritative)
- `opencode.json` / `.opencode/` — opencode agent configs (legacy)

## Task lifecycle

```
todo → in-progress → done (with certHash)
```

## Agent architecture

- **commander** — reads db.json, dispatches executor+certifier, updates status
- **executor** — runs the script, produces a log file
- **certifier** — reviews the log adversarially, issues certification hash or rejection

## Key rules

1. Tasks execute in order. Never skip ahead.
2. A task is "done" ONLY when certifier issues a hash.
3. On rejection, stop. Do not auto-retry.
4. The AI-EXECUTION.md file in the bundle root is the authoritative protocol.
   Read it fresh before every execution.

## Environment variables

db.json contains an `environmentVariables` object. These MUST be exported
before running any task script.

## Git safety

Scripts that perform git operations require the git guard token to be set.
This token is stored in db.json's environmentVariables — never hardcode it
in agent prompts or steering files.
