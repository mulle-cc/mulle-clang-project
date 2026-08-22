---
name: mrc-workflow
description: MRC release task workflow — commander dispatches executor then certifier.
---

# MRC Workflow

## Commands

| Command | What it does |
|---------|-------------|
| `run` | Execute the next todo task (executor → certifier → update db.json) |
| `status` | Show current task states from db.json |
| `certify <task_index>` | Re-certify a specific done task |
| `skip <task_index>` | Mark a task as skipped (manual decision only) |
| `what's next` | Show the next todo task without executing |

## Task selection

Find the next task to execute:
```bash
cat db.json | jq '.files | to_entries[] | select(.value.status == "todo") | .key' | head -1
```

## Log file naming convention

```
l-{titleSlug}-{uuidShort}-{timestamp}.log
```

Where:
- `titleSlug` = task title, lowercased, spaces→hyphens, max 20 chars
- `uuidShort` = first 12 chars of the task's uuid field
- `timestamp` = `date +%Y%m%d-%H%M%S`

## Certification hash format in db.json

```json
{
  "status": "done",
  "certHash": "a1b2c3d4e5f67890",
  "certTimestamp": "2026-07-22T20:00:00+02:00"
}
```

## Error handling

- If executor fails (non-zero exit): still write log, report to commander
- If certifier rejects: commander sets status to "in-progress", stops
- If log file is missing: automatic rejection
- If log is empty: automatic rejection
