Update version strings in the outer `mulle-clang-X.Y.Z` repo
and create the new outer directory.

**Always create a fresh outer directory** named `mulle-clang-<new-version>`.
Never rename the old one — keep it as reference. Use `cp -a`:

```bash
cd /path/to/srcL
cp -a mulle-clang-22.1.2 mulle-clang-22.1.8
cd mulle-clang-22.1.8
```

(Ask the user before copying since LLVM builds are large — ~20GB.)

## Steps

1. The `migrate-to-next-release` script already updated `clang/bin/migrate-to-next-release`
   and the outer copy. Verify:
   ```bash
   grep '^NEW_LLVM_TAG=' mulle-clang-project/../migrate-to-next-release
   ```

2. Run the version string update script:
   ```bash
   OLD_VERSION=22.1.2 NEW_VERSION=22.1.8 \
     bash mulle-clang-project/.mulle/etc/release-commander/upgrade.mrc/s-update_outer_repo-0004.sh
   ```
   This replaces version strings in `BUILD-AND-TEST.md`, `migrate-to-next-release`,
   and the `VERSION` default in all cmake scripts.

3. **Bump the VERSION default in cmake scripts.** The mulle-clang version is
   `<llvm-version>.<mulle-patch>` (e.g. `22.1.8.7`). The mulle patch component
   (4th number) stays the same unless mulle features were added:
   ```bash
   cd mulle-clang-project
   sed -i 's/VERSION="${VERSION:-<old-llvm>.<mulle-patch>}"/VERSION="${VERSION:-<new-llvm>.<mulle-patch>}"/' \
     clang/bin/cmake-ninja.linux \
     clang/bin/cmake-ninja.darwin \
     clang/bin/cmake-ninja.windows \
     clang/bin/cmake-make.linux \
     clang/bin/cmake-msbuild.windows \
     clang/bin/cmake-distribution-ninja.linux
   ```

3. Delete stale build directories (they reference old paths) and reconfigure:
   ```bash
   rm -rf build
   mkdir build && cd build
   cmake -G Ninja \
     -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
     -DLLVM_ENABLE_RUNTIMES="compiler-rt" \
     -DLLVM_TARGETS_TO_BUILD="X86;ARM;AArch64;WebAssembly" \
     -DCLANG_VENDOR=mulle \
     -DCMAKE_BUILD_TYPE=Release \
     ../mulle-clang-project/llvm
   ```
   A full reconfigure is needed whenever the outer directory path changes.

4. Commit the outer repo changes (but the outer dir typically has no git repo —
   it's just a working directory).