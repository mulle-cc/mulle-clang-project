#!/bin/bash
# Update version strings in outer repo docs.
# Requires OLD_VERSION and NEW_VERSION as env vars.
set -e

: "${OLD_VERSION:?set OLD_VERSION e.g. 21.1.8}"
: "${NEW_VERSION:?set NEW_VERSION e.g. 22.1.0}"

: "${MULLE_CLANG_DIR:?set MULLE_CLANG_DIR}"

for f in "${MULLE_CLANG_DIR}/BUILD-AND-TEST.md" "${MULLE_CLANG_DIR}/migrate-to-next-release"; do
   [ -f "$f" ] && sed -i "s/${OLD_VERSION}/${NEW_VERSION}/g" "$f"
done

echo "Updated version strings: ${OLD_VERSION} → ${NEW_VERSION}"
echo "Remember to rename the outer repo directory and reconfigure cmake if needed."
