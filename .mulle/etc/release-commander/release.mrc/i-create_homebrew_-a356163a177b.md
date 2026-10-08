# Stage the mulle-clang-project formula into the release branch for the CI bottles

Put `.github/workflows/mulle-clang-project.rb` on `MULLE_CLANG_BRANCH` at exactly
`MULLE_CLANG_PROJECT_TAG`, so the following bottle-CI task builds bottles for the
right version.

## Why this is required

The bottle CI (`.github/workflows/bottle.yml`) builds from the **in-repo staged
formula** via a faked local tap. `bottle-build.sh` does **not** check that the
formula's `version` matches the requested tag — a stale formula silently produces
mis-versioned bottles (e.g. it was still at `22.1.8.7` and would have built
`22.1.8.7` bottles onto the `22.1.8.8` release). This task is the guard against
that.

## What the script does

1. Fetches the base formula (class body, `install`, `caveats`, `test`) from the
   current **stable tap** `${TEMPLATE_REPO}` (default `mulle-objc/homebrew-software`).
2. Downloads the release source tarball and computes its sha256 (never guessed).
3. Rewrites `version`, `url`, the **source** sha256, and the `vendor` version +
   `runtime-load-version` (from `${MULLE_OBJC_RUNTIME_LOAD_VERSION}`), and
   **removes** the `bottle do` block (CI builds fresh bottles for this tag; a stale
   block would mislead `brew`).
4. `ruby -c` syntax-checks the result and asserts the `version` line is the tag.
5. PUTs it to `${REPO}/${FORMULA_IN_REPO}` on `${MULLE_CLANG_BRANCH}` via the
   Contents API (no clone), idempotently, then re-reads the branch and confirms the
   staged version equals the tag.
6. Fails if `.github/workflows/bottle-build.sh` is missing on the branch.

## Ordering

This task **must run before** *"Build macOS bottles via GitHub Actions"*. It is
placed immediately before it in the task list; do not reorder it later.

## Environment

- `MULLE_CLANG_PROJECT_TAG` — release tag (e.g. `22.1.8.8`)
- `MULLE_CLANG_BRANCH` — release branch (e.g. `mulle/22.1.8`)
- `MULLE_OBJC_RUNTIME_LOAD_VERSION` — load version (e.g. `21`)
- `GITHUB_REPO` (optional; default `mulle-cc/mulle-clang-project`)
- `TEMPLATE_REPO` / `TEMPLATE_BRANCH` (optional; default `mulle-objc/homebrew-software` / `master`)
- `FORMULA_IN_REPO` (optional; default `.github/workflows/mulle-clang-project.rb`)

## Note

The **finalized** formula (with the real `bottle do` shas) is produced later by the
*"Finalize and promote homebrew formula to stable taps"* task, from the release
assets. This task only stages the build-time formula.

## Next release: use the attached source asset

The GitHub tag archive (`archive/refs/tags/<TAG>.tar.gz`) is generated on demand
and is **not deterministic** (`export-subst` + `git describe` auto-abbrev varies
by backend), so the source `sha256` can change unexpectedly. From the next
compiler release onward, the *"Attach stable source tarball as release asset"*
task runs first and this task must set `url` to that asset
(`SOURCE_ASSET_URL`) and the source `sha256` to `SOURCE_ASSET_SHA256` instead of
downloading the tag archive.