Run `migrate-to-next-release` for one version step (major or same-major patch).

## Preparation

Patch `clang/bin/migrate-to-next-release` with the new OLD/NEW values,
commit, then run the migrate script.

**Before running:** make sure all working tree changes in `mulle-clang-project`
are committed — the script does a `git checkout` internally and will abort if
there are uncommitted changes.

## Steps

1. Make sure you are on the current dev branch:
   ```bash
   cd mulle-clang-project
   git checkout <current-dev-branch>
   ```

2. Patch `clang/bin/migrate-to-next-release` vars:
   ```bash
   MIGRATE=clang/bin/migrate-to-next-release
   sed -i \
     -e "s|^OLD_LLVM_TAG=.*|OLD_LLVM_TAG=\"<old-llvm-tag>\"|" \
     -e "s|^OLD_MULLE_DEV_BRANCH=.*|OLD_MULLE_DEV_BRANCH=\"<old-dev-branch>\"|" \
     -e "s|^NEW_LLVM_TAG=.*|NEW_LLVM_TAG=\"${NEW_LLVM_TAG}\"|" \
     -e "s|^NEW_MULLE_DEV_BRANCH=.*|NEW_MULLE_DEV_BRANCH=\"${NEW_MULLE_DEV_BRANCH}\"|" \
     "${MIGRATE}"
   cp "${MIGRATE}" ../migrate-to-next-release  # outer copy
   git add -A && git commit -m "Prepare migration: <old> → <new>"
   ```

3. Run the migration:
   ```bash
   bash clang/bin/migrate-to-next-release
   ```
   This squashes the old dev branch into one commit, cherry-picks it onto
   the new llvm tag, and creates the new dev branch.

4. If the cherry-pick fails with conflicts, the script stops after step 9.
   Resolve conflicts manually (see step 3), then continue:
   ```bash
   bash /tmp/migrate-to-next-release continue
   ```
   Use `-f` flag to skip the marker diff check if file paths changed
   (false positive — markers moved to a new path):
   ```bash
   bash /tmp/migrate-to-next-release -f continue
   ```

5. On success the script verifies `@mulle-` markers and deletes the tmp
   branch automatically.

## Pitfalls

- **Script bug: don't use s-run_migrate-0002.sh directly** — it patches vars
  then launches migrate without committing first. The migrate script does
  `git checkout` internally and will abort on uncommitted changes. Instead,
  manually patch + commit + run as shown above.

- **Uncommitted changes abort the script** — commit everything in
  `mulle-clang-project` before running.

- **Always verify the vars before running:**
  ```bash
  grep '^OLD_LLVM_TAG\|^OLD_MULLE\|^NEW_LLVM_TAG\|^NEW_MULLE' clang/bin/migrate-to-next-release
  ```

## Same-major patch bumps

The process works for same-major patch bumps (e.g. 22.1.2 → 22.1.8) just like
major jumps. Patch bumps typically have **zero cherry-pick conflicts** since
there are no structural changes between minor releases.

## Historical: structural changes from previous major upgrades

### llvm 21 → 22

- `clang/include/clang/Driver/Options.td` moved to `clang/include/clang/Options/Options.td`
  → use `-f` flag on continue to suppress false-positive marker diff
- `clang/include/clang/AST/Type.h` split into `TypeBase.h` + thin `Type.h`
  → mulle changes to Type.h must be applied to `TypeBase.h` instead