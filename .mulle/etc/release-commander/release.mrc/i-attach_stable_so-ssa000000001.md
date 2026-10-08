> **⚠️ MANDATORY: Before executing this task, reread `AGENTS.md` (aka `AI-EXECUTION.md`) in this `.mrc` bundle directory. It contains the authoritative execution protocol. Do NOT rely on memory after context compaction.**

# Attach a stable source tarball as a release asset

**Run this BEFORE any deb/bottle CI and BEFORE the formula staging/promote
tasks.** It exists because the GitHub *tag archive* URL
(`…/archive/refs/tags/<TAG>.tar.gz`) is generated on demand and is **not
deterministic**: `clang/bindings/python/.git_archival.txt` is `export-subst`, so
`git describe`'s auto-abbreviated hash length depends on which GitHub backend
serves the request. Observed on `22.1.8.8`: two different sha256 values
(`4b441b5b…` and `6fa3e37b…`) for the same commit, differing by a single
character in `describe-name` — which broke a bottle build and makes the
Homebrew source `sha256` unreliable.

A release **asset** we upload ourselves is immutable, so its sha256 is stable.

## What the script does

1. Builds `mulle-clang-project-<TAG>.tar.gz` from the local git checkout at
   `refs/tags/<TAG>` (`git archive --prefix=mulle-clang-project-<TAG>/`).
2. Uploads it to release `<TAG>` on `mulle-cc/mulle-clang-project`
   (`gh release upload --clobber`, idempotent).
3. Prints `SOURCE_ASSET_URL` and `SOURCE_ASSET_SHA256` for the formula.

## Follow-up wiring (part of this task, next release)

After the asset exists, the formula tasks must use it instead of the dynamic
tag archive:

- `i-create_homebrew_-a356163a177b.md` (*Stage … formula*) — set the formula
  `url` to `SOURCE_ASSET_URL` and the source `sha256` to `SOURCE_ASSET_SHA256`
  rather than `archive/refs/tags/…`.
- `i-promote_homebrew-e7c1a9b4.md` (*Finalize and promote …*) — same: download
  and hash `SOURCE_ASSET_URL`, not the tag archive.
- Deb/bottle CI that downloads sources by tag should likewise be pointed at the
  attached asset (or a codeload URL with a pinned commit) so every build uses
  the same bytes.

## Expected outcome

- `mulle-clang-project-<TAG>.tar.gz` present on the release.
- A deterministic `SOURCE_ASSET_SHA256` that does not change between downloads.

## Note for 22.1.8.8

Skipped for the `22.1.8.8` release (bottles/debs were already built from the
dynamic archive). MUST run on the next compiler release, before the bottle CI.
