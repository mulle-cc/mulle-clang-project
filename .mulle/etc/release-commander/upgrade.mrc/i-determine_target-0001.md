Determine the target llvm version to upgrade to.

## Steps

1. Fetch llvm remote tags (do NOT pull):
   ```bash
   cd mulle-clang-project
   git fetch llvm --tags
   ```

2. Find the current version from `migrate-to-next-release`:
   ```bash
   grep '^NEW_LLVM_TAG=' clang/bin/migrate-to-next-release
   # e.g. llvmorg-22.1.2 → current major = 22
   ```

3. List available major versions from llvm tags:
   ```bash
   git tag -l 'llvmorg-*' | grep -E '^llvmorg-[0-9]+\.[0-9]+\.[0-9]+$' \
     | sed 's/llvmorg-//' | cut -d. -f1 | sort -un
   ```

4. The upgrade target is `current_major + 1`. If the latest available major
   is more than one step ahead, we still only go one major at a time and
   iterate (see step 6).

5. Pick the highest **final** (non-RC) release of `target_major`:
   ```bash
   TARGET_MAJOR=$(($CURRENT_MAJOR + 1))
   git tag -l "llvmorg-${TARGET_MAJOR}.*" \
     | grep -E "^llvmorg-${TARGET_MAJOR}\.[0-9]+\.[0-9]+$" \
     | sort -t. -k2,2n -k3,3n | tail -1
   # e.g. llvmorg-22.1.8
   ```

6. Record `NEW_LLVM_TAG` and `NEW_MULLE_DEV_BRANCH` for use in step 2.

## Same-major patch bumps

This script always looks for **current+1 major**. If you want a same-major
patch bump (e.g. 22.1.2 → 22.1.8), find it manually:

```bash
CURRENT_MAJOR=22
git tag -l "llvmorg-${CURRENT_MAJOR}.*" \
  | grep -E "^llvmorg-${CURRENT_MAJOR}\.[0-9]+\.[0-9]+$" \
  | sort -t. -k2,2n -k3,3n | tail -1
```

Then proceed directly to step 2 with the new tag — no iteration needed.