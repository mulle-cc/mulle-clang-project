Determine the version to release and ensure a GitHub release + tag exists for it.

## Steps

1. Read the version from the build script:
   ```
   ${PWD}/clang/bin/cmake-ninja.linux
   ```
   Look for the line like `INSTALL_PREFIX=".../mulle-clang-project/${VERSION:-X.Y.Z.W}${RC}"` and extract `X.Y.Z.W`.

2. Check if a release with that tag already exists on GitHub **and already has a .deb asset**:
   ```bash
   gh release view "${VERSION}" --repo mulle-cc/mulle-clang-project --json assets -q '.assets[].name'
   ```
   If a `.deb` is already present → **stop**, nothing to do.
   If the release exists but has no `.deb` → proceed (we still need to build and upload).
   If the release doesn't exist → create it, then proceed.

3. If it does not exist, create the tag and release:
   ```bash
   gh release create "${VERSION}" \
     --repo mulle-cc/mulle-clang-project \
     --title "${VERSION}" \
     --notes "mulle-clang ${VERSION}"
   ```

4. Update `MULLE_CLANG_PROJECT_TAG` in `environmentVariables` in `db.json` to the new version.
