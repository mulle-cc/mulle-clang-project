Update version strings in the outer `mulle-clang-X.Y.Z` repo.

The outer repo directory is named after the version (e.g. `mulle-clang-21.1.8`).
After a major upgrade the repo should be cloned/renamed to `mulle-clang-22.x.x`.

## Steps

1. The `migrate-to-next-release` script already updated `clang/bin/migrate-to-next-release`
   and the outer copy. Verify:
   ```bash
   grep '^NEW_LLVM_TAG=' ../migrate-to-next-release
   ```

2. Update `BUILD-AND-TEST.md` and any other docs that hardcode the path
   `/home/src/srcL/mulle-clang-21.1.8/` — replace with the new version path.

3. If the outer repo directory was renamed, update the cmake build dir:
   ```bash
   cmake -B build -S mulle-clang-project/llvm \
     -DLLVM_ENABLE_PROJECTS="clang" \
     -DCMAKE_BUILD_TYPE=Debug
   ```
   (A full reconfigure is needed after a major version jump.)

4. Commit the outer repo changes.
