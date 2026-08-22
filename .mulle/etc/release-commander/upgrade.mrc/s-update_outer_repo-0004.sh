#!/bin/bash
# Update version strings in outer repo docs and cmake scripts.
# Requires OLD_VERSION and NEW_VERSION as env vars.
set -e

: "${OLD_VERSION:?set OLD_VERSION e.g. 22.1.2}"
: "${NEW_VERSION:?set NEW_VERSION e.g. 22.1.8}"

: "${MULLE_CLANG_DIR:?set MULLE_CLANG_DIR}"

cd "${MULLE_CLANG_DIR}"

# 1. Outer repo docs
for f in "BUILD-AND-TEST.md" "migrate-to-next-release"; do
   [ -f "$f" ] && sed -i "s/${OLD_VERSION}/${NEW_VERSION}/g" "$f"
done

# 2. Cmake scripts: VERSION="${VERSION:-<llvm-version>.<mulle-patch>}"
# The mulle patch (4th component) stays the same, only llvm version changes.
for f in \
   mulle-clang-project/clang/bin/cmake-ninja.linux \
   mulle-clang-project/clang/bin/cmake-ninja.darwin \
   mulle-clang-project/clang/bin/cmake-ninja.windows \
   mulle-clang-project/clang/bin/cmake-make.linux \
   mulle-clang-project/clang/bin/cmake-msbuild.windows \
   mulle-clang-project/clang/bin/cmake-distribution-ninja.linux
do
   [ -f "$f" ] && sed -i "s/${OLD_VERSION}/${NEW_VERSION}/g" "$f"
done

echo "Updated version strings: ${OLD_VERSION} → ${NEW_VERSION}"
echo "Remember to cp -a the outer repo to 'mulle-clang-${NEW_VERSION}'"
echo "(not rename — keep the old one as reference) and reconfigure cmake."