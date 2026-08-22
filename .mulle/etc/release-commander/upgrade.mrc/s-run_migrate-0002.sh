#!/bin/bash
# Semi-automated: edits migrate script vars, commits, then launches it.
# Requires NEW_LLVM_TAG and NEW_MULLE_DEV_BRANCH as env vars
# (copy them from the output of s-determine_target-0001.sh).
#
# NOTE: Patches clang/bin/migrate-to-next-release, commits, then runs it.
# The migrate script does git checkout internally and aborts on uncommitted
# changes — that's why we commit first.
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

# Verify working tree is already clean (only the db.json should be dirty)
if ! git diff --quiet || ! git diff --cached --quiet; then
   echo "ERROR: uncommitted changes — commit everything before running" >&2
   exit 1
fi

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

# Must commit before running: migrate-to-next-release does git checkout internally
git add -A
git commit -m "Prepare migration: ${OLD_LLVM_TAG} → ${NEW_LLVM_TAG}"

bash "${MIGRATE}"