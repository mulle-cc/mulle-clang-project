# Finalize and promote the Homebrew formula to the stable taps

Reconstruct the finalized `mulle-clang-project` formula for
`MULLE_CLANG_PROJECT_TAG` from the release's **actual assets** and publish it to
both stable taps, so `brew install mulle-objc/software/mulle-clang-project`
installs the new compiler.

## Why this changed

The old workflow assumed the macOS bottle steps had already hand-updated the
`mulle-objc/homebrew-prerelease` tap with a `bottle do` block, and this task just
copied it across. Bottle building is now automated by
`.github/workflows/bottle.yml` (task *"Build macOS bottles via GitHub Actions"*),
which uploads arm64 bottles to the GitHub release but **does not touch any
formula** — and the prerelease tap is now stale. So the formula no longer exists
in final form anywhere; this task builds it from scratch.

## What the script does

1. Fetches the base formula (class body, `install`, `caveats`, `test`) from the
   current **stable tap** `${TEMPLATE_REPO}` (default `mulle-objc/homebrew-software`).
2. Downloads the release source tarball and computes its sha256 (never guessed).
3. Lists the `*.bottle.tar.gz` assets actually on the release, downloads each and
   computes its sha256, and builds the `bottle do` block with `arm64_<codename>`
   keys and a `root_url` pointing at the release.
4. Rewrites `version`, `url`, the **source** sha256, the `vendor` version and
   `runtime-load-version` (from `${MULLE_OBJC_RUNTIME_LOAD_VERSION}`), and replaces
   the entire `bottle do ... end` block.
5. `ruby -c` syntax-checks the result.
6. Publishes the file to `${TARGET_REPOS}` (default `mulle-objc/homebrew-software`
   and `mulle-kybernetik/homebrew-software`) via the GitHub Contents API — no
   clone; idempotent (if the remote already equals the generated file, no commit).

## Guards the script enforces

- Base formula must parse as a `MulleClangProject` formula.
- Source download must be non-empty and gzip.
- At least one arm64 bottle must exist on the release, and the stable Apple-silicon
  codenames **`arm64_sonoma`, `arm64_sequoia`, `arm64_tahoe`** must all be present.
  `arm64_golden_gate` (beta) is warned about but not required.
- Push permission on each target tap.

**Do NOT run this before the bottle CI task has produced the bottles** — it fails
loudly if the `arm64_*` assets are missing. Never list a bottle that is not on the
release.

## Environment

- `MULLE_CLANG_PROJECT_TAG` — release tag (e.g. `22.1.8.8`)
- `MULLE_OBJC_RUNTIME_LOAD_VERSION` — load version (e.g. `21`)
- `TEMPLATE_REPO` / `TEMPLATE_BRANCH` (optional; default `mulle-objc/homebrew-software` / `master`)
- `TARGET_REPOS` (optional; default both stable taps)

## Note on the parent release

`MULLE_RELEASE_TAG` (the objc release, `0.31`) is **not** part of the formula — the
formula carries the compiler tag and the `runtime-load-version`, not the objc
release version. The compiler's minimum compatible runtime is `0.30`
(`MULLE_OBJC_RUNTIME_MIN_VERSION`), which is used by the release-notes and README
tasks, not here.

## Next release: use the attached source asset

The GitHub tag archive (`archive/refs/tags/<TAG>.tar.gz`) is generated on demand
and is **not deterministic** (`export-subst` + `git describe` auto-abbrev varies
by backend), so the source `sha256` can change unexpectedly. From the next
compiler release onward, download and hash `SOURCE_ASSET_URL` (produced by the
*"Attach stable source tarball as release asset"* task) for the source `sha256`
instead of the tag archive.