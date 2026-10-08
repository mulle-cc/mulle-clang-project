# Update mulle-clang-mingw version and commit

Update the version references in `mulle-clang-mingw` to the new compiler version.

## Files to update

- `build-llvm.sh` — `LLVM_VERSION` default (the mulle-clang-project git ref to
  check out, e.g. `22.1.8.8`; it is NOT an upstream `llvmorg-*` ref)
- `README.md` — all `LLVM_VERSION=x.x.x.x` and install path references

## Steps

```bash
cd ${MULLE_REPOS_DIR}/mulle-cc/mulle-clang-mingw
# update LLVM_VERSION default in build-llvm.sh to the mulle-clang tag (e.g. 22.1.8.8)
# update README.md version references
git add build-llvm.sh README.md
git commit -m "new compiler version ${MULLE_CLANG_PROJECT_TAG}"
git push
```
