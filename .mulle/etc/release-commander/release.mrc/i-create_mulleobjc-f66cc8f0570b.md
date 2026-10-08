# Build the mulle-clang `.deb` packages via GitHub Actions

**Package building is automated. Do NOT build debs locally in a VM or docker — that
is obsolete.**

The `.deb` files for **bookworm / trixie / forky (Debian)** and
**ubuntu22.04 / ubuntu24.04 / ubuntu26.04**, each for **amd64 and arm64**, are built
from the release source tarball and uploaded directly to the GitHub release by two
manually-triggered, idempotent workflows installed by the
`mulle-clang-project-deb-ci.mrc` bundle:

| Workflow | Distros |
|---|---|
| `.github/workflows/debian-debs.yml` | bookworm / trixie / forky |
| `.github/workflows/debs.yml` | ubuntu22.04 / ubuntu24.04 / ubuntu26.04 |

## Run

Just run the script. It:

1. asserts the repo **default branch** equals `MULLE_CLANG_BRANCH` (a
   `workflow_dispatch` workflow is only dispatchable from the default branch — a
   hard rule enforced by the CI bundle preflight),
2. exits immediately if all 12 expected `.deb` assets are already on the release,
3. otherwise, **per workflow**: if that workflow's debs are already all present it
   is skipped entirely; else it **attaches** to an in-flight run, or **dispatches**
   a fresh one only when none is running (never creating duplicate runs), passing
   `-f tag=${MULLE_CLANG_PROJECT_TAG}`,
4. **polls for at most `MULLE_CI_POLL_SECONDS` (default 300 = 5 min)**, printing a
   status snapshot every 30 s, then returns.

### Non-blocking poll model (important)

CI here is long — canary then a 5-way fanout, and the runner pool can serialize it
to well over an hour. A blocking `gh run watch` gives the commander no visibility
until the call returns, so the script deliberately does **not** block to
completion. That is why the task stays `in-progress` and is re-run every ~5 min;
each re-run prints one progress update and **re-attaches to the same runs** rather
than dispatching new ones.

### Exit codes

| exit | meaning |
|---|---|
| `0` | all 12 expected `.deb` assets are present → done, certifiable |
| `1` | a tracked run finished `failure`/`cancelled`, or the run finished but assets are still missing → real failure |
| `3` | still running at the poll deadline → task stays `in-progress`; re-run to continue polling |

Re-dispatching is cheap and safe: each workflow first checks whether its `.deb`
already exists on the release and skips the build if so. Use `-f force=true` to
force a rebuild.

> Historical note: the forky build ships **without compiler runtimes**
> (`CMAKEFLAGS="-DLLVM_ENABLE_RUNTIMES="`). This is handled inside the CI build
> script; it is noted in the release notes.
