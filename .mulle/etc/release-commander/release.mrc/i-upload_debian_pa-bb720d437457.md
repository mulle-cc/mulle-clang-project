# Build the mulle-clang macOS bottles via GitHub Actions

**Bottle building is automated. Do NOT build bottles on a Mac by hand — that is
obsolete.**

The `arm64` Homebrew bottles are built from the in-repo staged formula and uploaded
directly to the GitHub release by the manually-triggered, idempotent workflow
`.github/workflows/bottle.yml`, installed by the `mulle-clang-project-bottle-ci.mrc`
bundle.

## Run

Just run the script. It:

1. asserts the repo **default branch** equals `MULLE_CLANG_BRANCH` (a
   `workflow_dispatch` workflow is only dispatchable from the default branch),
2. exits immediately if all four bottles are already on the release,
3. otherwise **attaches** to an in-flight `bottle.yml` run, or **dispatches** one
   only if none is running (never creating duplicate runs), passing
   `-f tag=${MULLE_CLANG_PROJECT_TAG}`,
4. **polls for at most `MULLE_CI_POLL_SECONDS` (default 300 = 5 min)**, printing a
   run status plus a per-job snapshot every 30 s, then returns.

### Non-blocking poll model (important)

Each bottle is an ~1 h build behind a canary, so blocking on `gh run watch` would
hide all progress. The script deliberately does **not** block to completion: the
task stays `in-progress` and is re-run every ~5 min, each time printing one
progress update and **re-attaching to the same run** rather than dispatching a new
one.

### Exit codes

| exit | meaning |
|---|---|
| `0` | all four bottles are present → done, certifiable |
| `1` | the tracked run finished `failure`/`cancelled`, or it finished but bottles are still missing → real failure |
| `3` | still running at the poll deadline → task stays `in-progress`; re-run to continue polling |

Re-dispatching skips bottles that already exist; use `-f force=true` to rebuild.

The workflow's `canary-select` job detects the current `macos-latest` codename and
hard-fails if there is no matching `namespace-profile-macos-arm-<codename>` runner
— that is an intervention point, not a silent failure.
