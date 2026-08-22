# AI Agent Execution Guide for MRC Bundles

## Purpose

You are executing tasks defined in a mulle-release-commander (MRC) bundle. A bundle is a directory ending in `.mrc` containing a `db.json` task list, markdown instruction files, bash scripts, and log files.

## Bundle Structure

```
project-name.mrc/
├── db.json                          # Task database (ordered list)
├── i-{titleslug}-{uuid12}.md       # Markdown instructions per task
├── s-{titleslug}-{uuid12}.sh       # Bash script per task
└── l-{titleslug}-{uuid12}-{timestamp}.log  # Execution logs
```

## db.json Format

```json
{
  "files": [
    {
      "title": "Human-readable task name",
      "filename": "i-task_name_here_-abc123def456.md",
      "scriptFilename": "s-task_name_here_-abc123def456.sh",
      "uuid": "abc123de-f456-4000-0000-000000000000",
      "principalTool": "tool-name",
      "status": "todo|in-progress|done",
      "skillLevel": "Low|Medium|High"
    }
  ],
  "environmentVariables": {
    "KEY": "value",
    "PWD": "/working/directory"
  }
}
```

## Execution Procedure

### Step 1: Load the bundle

Read `db.json` from the `.mrc` directory. Parse the `files` array and `environmentVariables` object.

### Step 2: Iterate tasks in order

Process `files` array sequentially from index 0 to N. The array order IS the execution order. Do NOT reorder.

### Step 3: Skip completed tasks

If `status` is `"done"`, skip the task entirely. Only execute tasks with `status` `"todo"` or `"in-progress"`.

### Step 4: Read the markdown instructions

For each task, read the file at `{bundlePath}/{filename}` (the `.md` file). This contains human-written instructions explaining what the task should accomplish, context, prerequisites, and expected outcomes. Understand the goal before executing.

### Step 5: Read and execute the script

Read the file at `{bundlePath}/{scriptFilename}` (the `.sh` file). This is a bash script that performs the task.

Execute the script with:
- All keys from `environmentVariables` exported as environment variables
- If `environmentVariables.PWD` exists, use it as the working directory (`cwd`)
- Use `/bin/bash` as the interpreter
- **Always enable `set -x` tracing** by prepending `set -x` to the invocation so every command is visible in the log

The concrete invocation must capture all output:
```bash
LOG_FILE="{bundlePath}/l-{titleSlug}-{uuidShort}-{timestamp}.log"
(export VAR1=val1 VAR2=val2 ...; cd {PWD}; set -x; bash -x {scriptPath}) 2>&1 | tee "${LOG_FILE}"
```

Record the exit code from the script, not from `tee`.

If the script is empty or missing, perform the task described in the markdown instructions manually using appropriate tools and commands. Even in this case you MUST still create a log file recording exactly what commands you ran and their output — see Step 6.

### Step 6: Record a log file

After execution (success or failure), write a log file into the bundle directory.

Log filename format:
```
l-{titleSlug}-{uuidShort}-{timestamp}.log
```

Where:
- `titleSlug`: task title, lowercased, non-alphanumeric (except spaces) removed, spaces replaced with `_`, truncated to 16 chars
- `uuidShort`: the task's `uuid` field with all dashes removed, then truncated to 12 chars
- `timestamp`: ISO 8601 format with colons and dots replaced by dashes, truncated to seconds: `2026-03-16T12-34-59`

Example: task with title `"Edit github-ci project"` and uuid `"bbf5b1d6-2ddd-43bc-..."` produces:
```
l-edit_githubci_pr-bbf5b1d62ddd-2026-03-16T12-34-59.log
```

The log file content should contain the raw stdout and stderr output of the script execution, including the `set -x` trace lines so every command and its arguments are visible.

### Step 7: Update task status

After execution:
- Script exited 0 → set `status` to `"done"`
- Script exited non-zero → set `status` to `"in-progress"`, stop processing further tasks
- Manual execution succeeded → set `status` to `"done"`
- Manual execution failed → set `status` to `"in-progress"`, stop processing further tasks

Write the updated `db.json` back to the bundle.

### Step 8: Continue or stop

On success, proceed to the next task. On failure, stop. Do not skip failed tasks.

## Slug and UUID Computation Reference

```python
# titleSlug
title_slug = re.sub(r'[^a-z0-9\s]', '', title.lower()).replace(' ', '_')[:16]

# uuidShort
uuid_short = uuid.replace('-', '')[:12]

# timestamp
timestamp = datetime.now().isoformat().replace(':', '-').replace('.', '-')[:19]
```

## Important Rules

1. Tasks MUST be executed in array order
2. NEVER skip a failed task to continue with later tasks
3. ALWAYS write a log file, even for failed or empty executions — **no exceptions**
4. ALWAYS export `environmentVariables` before running scripts
5. ALWAYS use `PWD` from `environmentVariables` as working directory if present
6. Read the markdown BEFORE executing — it may contain critical context the script alone does not convey
7. Do NOT modify task `title`, `filename`, `scriptFilename`, or `uuid` fields — only modify `status`
8. **NEVER perform release work outside of MRC scripts.** Do not run ad-hoc shell commands, Python snippets, or inline edits to accomplish what an MRC step is supposed to do. If the script is inadequate, fix the script first, then run it.
9. **ALL shell commands executed as part of a task — including manual fallback steps — MUST be recorded in the log file.** If you ran commands manually (because the script was empty), write every command and its output to the log. "No log" is never acceptable.
10. When a script fails, do NOT attempt to work around the failure by running commands manually. Stop, report the failure, and let the user decide how to proceed.
11. **NEVER guess or assume values.** Every environment variable, version number, file path, or configuration value must be determined by:
    - Running an authoritative command (e.g. `mulle-clang --version`, not reading from stale metadata)
    - Reading the authoritative file (e.g. `db.json`, not an old release's values)
    - Asking the user explicitly when no authoritative source exists
    If you catch yourself thinking "I think it's probably X" — stop and verify instead.
12. **NEVER modify code, configuration, or data outside the explicit scope of the current MRC task.** Do not remove "unnecessary" tokens, fix "cosmetic" issues, or refactor without being asked. If a script is deficient, fix the script per rule 8. If db.json needs a change, the task instruction will say so.

## Certification Requirement (Multi-Agent Mode)

When running in multi-agent mode (commander/executor/certifier architecture):

1. The executor runs the task and produces a log.
2. The certifier independently reviews the log against the task instructions.
3. A task may ONLY transition to `"done"` if the certifier outputs `CERTIFIED` with a hash.
4. The certification hash is stored in db.json as `"certHash"` on the task entry.
5. A task without a `certHash` cannot be considered done, regardless of what `status` says.

### db.json certified task format

```json
{
  "title": "Task Name",
  "filename": "i-task_name-abc123.md",
  "scriptFilename": "s-task_name-abc123.sh",
  "uuid": "abc123...",
  "status": "done",
  "certHash": "a1b2c3d4e5f67890",
  "certTimestamp": "2026-07-22T20:00:00+02:00",
  "logFilename": "l-task_name-abc123-2026-07-22T20-00-00.log"
}
```

### Fallback (single-agent mode)

When not using multi-agent mode, use `mulle-ai-certify <task-file> <log-file>` as an
external certification step. It calls an independent AI reviewer and outputs the hash
on stdout if certified. Same hash must be stored in db.json.
