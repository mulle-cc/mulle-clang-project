#!/bin/bash
# Semi-automated: edits migrate script vars then launches it.
# Requires NEW_LLVM_TAG and NEW_MULLE_DEV_BRANCH as env vars
# (copy them from the output of s-determine_target-0001.sh).
set -e

: "${NEW_LLVM_TAG:?set NEW_LLVM_TAG}"
: "${NEW_MULLE_DEV_BRANCH:?set NEW_MULLE_DEV_BRANCH}"

: "${MULLE_CLANG_DIR:?set MULLE_CLANG_DIR}"

cd "${MULLE_CLANG_DIR}/mulle-clang-project"

MIGRATE=clang/bin/migrate-to-next-release

# Read current values to use as OLD_*
OLD_LLVM_TAG="$(grep '^NEW_LLVM_TAG=' "${MIGRATE}" | cut -d'"' -f2)"
OLD_MULLE_DEV_BRANCH="$(grep '^NEW_MULLE_DEV_BRANCH=' "${MIGRATE}" | cut -d'"' -f2)"

echo "OLD: ${OLD_LLVM_TAG}  ${OLD_MULLE_DEV_BRANCH}"
echo "NEW: ${NEW_LLVM_TAG}  ${NEW_MULLE_DEV_BRANCH}"

# Patch the script in-place
sed -i \
  -e "s|^OLD_LLVM_TAG=.*|OLD_LLVM_TAG=\"${OLD_LLVM_TAG}\"|" \
  -e "s|^OLD_MULLE_DEV_BRANCH=.*|OLD_MULLE_DEV_BRANCH=\"${OLD_MULLE_DEV_BRANCH}\"|" \
  -e "s|^NEW_LLVM_TAG=.*|NEW_LLVM_TAG=\"${NEW_LLVM_TAG}\"|" \
  -e "s|^NEW_MULLE_DEV_BRANCH=.*|NEW_MULLE_DEV_BRANCH=\"${NEW_MULLE_DEV_BRANCH}\"|" \
  "${MIGRATE}"

# Patch outer repo copy too
OUTER=../migrate-to-next-release
if [ -f "${OUTER}" ]; then
   cp "${MIGRATE}" "${OUTER}"
fi

bash "${MIGRATE}"
