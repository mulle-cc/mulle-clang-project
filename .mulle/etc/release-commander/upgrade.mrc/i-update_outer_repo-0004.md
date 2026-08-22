Update version strings in the outer `mulle-clang-X.Y.Z` repo.

The outer repo directory is named after the version (e.g. `mulle-clang-22.1.2`).
**Always create a new outer directory** named after the new version
(e.g. `mulle-clang-22.1.8`), even for patch bumps within the same major.

**Copy, don't rename** — the old outer directory should remain intact
as a reference. Use `cp -a` to preserve everything, then work in the copy.
Ask the user before copying if the disk space is large.

```bash
cp -a mulle-clang-22.1.2 mulle-clang-22.1.8 && cd mulle-clang-22.1.8
```

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
