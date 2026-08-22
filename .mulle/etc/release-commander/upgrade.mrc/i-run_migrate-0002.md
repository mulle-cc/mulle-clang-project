Run `migrate-to-next-release` for one major version step.

## Preparation

The script `s-run_migrate-0002.sh` patches `clang/bin/migrate-to-next-release` and
runs it. It reads the current NEW_* vars as the OLD_* values, so the script must
be run from the correct branch with the correct vars already committed.

**Before running:** make sure all working tree changes in `mulle-clang-project`
are committed — the script does a `git checkout` internally and will abort if
there are uncommitted changes.

## Steps

1. Make sure you are in `mulle-clang-project` and on the current dev branch:
   ```bash
   cd mulle-clang-project
   git checkout mulle/21.1.8
   ```

2. Run the script with NEW_LLVM_TAG and NEW_MULLE_DEV_BRANCH from step 1:
   ```bash
   NEW_LLVM_TAG=llvmorg-22.1.2 NEW_MULLE_DEV_BRANCH=mulle/22.1.2 \
     bash .mulle/etc/release-commander/upgrade.mrc/s-run_migrate-0002.sh
   ```
   The script patches the version vars in `clang/bin/migrate-to-next-release`
   (and the outer repo copy), commits, then runs the migrate script.

3. If the cherry-pick fails with conflicts, the script exits after step 9.
   Resolve conflicts manually (see step 3), then run:
   ```bash
   bash /tmp/migrate-to-next-release continue
   ```
   Use `-f` flag to skip the marker diff check if paths changed (e.g. Options.td
   moved from `Driver/` to `Options/` in llvm 22 — markers are present, just
   in a new path, which is a false positive):
   ```bash
   bash /tmp/migrate-to-next-release -f continue
   ```

4. On success the script verifies all `@mulle-` markers are intact and
   deletes the tmp branch automatically.

## Pitfalls

- **Uncommitted changes abort the script** — it does `git checkout` internally.
  Commit everything in `mulle-clang-project` before running.
- **Script reads OLD vars from the already-patched file** — if a previous run
  already patched `migrate-to-next-release`, the script will read wrong OLD values.
  Always verify before running:
  ```bash
  grep '^OLD_LLVM_TAG\|^OLD_MULLE\|^NEW_LLVM_TAG\|^NEW_MULLE' clang/bin/migrate-to-next-release
  ```

- `clang/include/clang/Driver/Options.td` moved to `clang/include/clang/Options/Options.td`
  → use `-f` flag on continue to suppress false-positive marker diff
- `clang/include/clang/AST/Type.h` split into `TypeBase.h` + thin `Type.h`
  → mulle changes to Type.h must be applied to `TypeBase.h` instead

## Known llvm 22 structural changes

- `clang/include/clang/Driver/Options.td` moved to `clang/include/clang/Options/Options.td`
  → use `-f` flag on continue to suppress false-positive marker diff
- `clang/include/clang/AST/Type.h` split into `TypeBase.h` + thin `Type.h`
  → mulle changes to Type.h must be applied to `TypeBase.h` instead
