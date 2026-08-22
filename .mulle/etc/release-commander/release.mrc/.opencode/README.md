# MRC Multi-Agent Usage Guide

## Quick Start

```bash
cd /home/src/srcO/mulle-objc-0.29.0.mrc
opencode run --agent commander "recertify all done tasks please"
```

Then type one of the commands below.

## Commands

| Command | What it does |
|---------|-------------|
| `run` | Execute the next todo task (executor → certifier → update db.json) |
| `run task N` | Execute task number N specifically |
| `status` | Show current task list status (todo/in-progress/done counts) |
| `certify task N` | Re-certify task N (read its log and run certifier) |
| `skip task N` | Mark task N as done without certification (requires you to explain why) |
| `what's next` | Report the next todo task without executing |

## Architecture

```
User
  │
  ▼
commander (primary agent)
  │  ├── reads AI-EXECUTION.md fresh each time
  │  ├── reads task instruction (i-*.md)
  │  ├── dispatches executor with full context
  │  └── dispatches certifier with task + log
  │
  ├──▶ executor (subagent)
  │      ├── has full bash/edit permissions
  │      ├── runs the script (s-*.sh)
  │      ├── writes log file (l-*.log)
  │      └── reports back: log path + exit code
  │
  └──▶ certifier (subagent)
         ├── read-only permissions (no edit, limited bash)
         ├── reviews log adversarially
         ├── checks for errors/failures
         └── outputs CERTIFIED+hash or REJECTED+reasons
```

## How certification works

1. Executor runs task, produces log
2. Commander dispatches certifier with task instructions + log
3. Certifier independently reviews for errors
4. If CERTIFIED: commander updates db.json with `certHash` and `status: "done"`
5. If REJECTED: commander sets `status: "in-progress"`, reports failure, STOPS

## Key rules

- **Commander never executes scripts itself** — only dispatches
- **Certifier never saw the execution** — only reviews the log
- **AI-EXECUTION.md is re-read before every task** — compaction can't lose it
- **No task is "done" without a certHash** — hard requirement
- **On rejection, processing stops** — no skipping failed tasks

## Environment variables

All variables from `db.json.environmentVariables` are exported before script execution.
Key ones:
- `PWD` — working directory (typically `/home/src/srcO`)
- `MULLE_RELEASE_TAG` — the release version (e.g., `0.29.0`)
- `MULLE_CLANG_PROJECT_TAG` — compiler version

## Running in non-interactive (batch) mode

```bash
cd /home/src/srcO/mulle-objc-0.29.0.mrc
opencode run --auto --agent commander "run"
```

This executes one task and exits. Chain multiple:
```bash
opencode run --auto --agent commander "run" && \
opencode run --auto --agent commander "run" && \
opencode run --auto --agent commander "run"
```

Each invocation is a fresh context — no compaction risk.

## Fallback: single-agent mode with mulle-ai-certify

If not using the multi-agent setup, certify manually:
```bash
mulle-ai-certify <task-file.md> <log-file.log>
```

Returns exit 0 + JSON with hash if certified, exit 2 if rejected.

## Troubleshooting

- **"Unexpected AI output"**: The certifier's response didn't match CERTIFIED/REJECTED format. Re-run.
- **Executor timeout**: Some tasks (like `act` CI) take 20-30 min. Increase timeout.
- **Permission denied**: Check that `opencode.json` has `"bash": "allow"`.
- **Agent not found**: Run `opencode agent list` to verify agents are detected.
