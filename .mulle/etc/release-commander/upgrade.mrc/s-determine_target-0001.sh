#!/bin/bash
set -e

: "${MULLE_CLANG_DIR:?set MULLE_CLANG_DIR}"

cd "${MULLE_CLANG_DIR}/mulle-clang-project"

git fetch llvm --tags

# Current major from migrate script
CURRENT_TAG="$(grep '^NEW_LLVM_TAG=' clang/bin/migrate-to-next-release | cut -d'"' -f2)"
CURRENT_MAJOR="$(echo "${CURRENT_TAG}" | sed 's/llvmorg-//' | cut -d. -f1)"
TARGET_MAJOR=$(( CURRENT_MAJOR + 1 ))

echo "Current: ${CURRENT_TAG}  (major ${CURRENT_MAJOR})"
echo "Target major: ${TARGET_MAJOR}"

# Highest patch of target major
NEW_LLVM_TAG="$(git tag -l "llvmorg-${TARGET_MAJOR}.*" \
  | grep -E "^llvmorg-${TARGET_MAJOR}\.[0-9]+\.[0-9]+$" \
  | sort -t. -k2,2n -k3,3n | tail -1)"

if [ -z "${NEW_LLVM_TAG}" ]; then
   echo "No tags found for major ${TARGET_MAJOR} — already at latest major" >&2
   exit 0
fi

echo "NEW_LLVM_TAG=${NEW_LLVM_TAG}"
echo "NEW_MULLE_DEV_BRANCH=mulle/$(echo "${NEW_LLVM_TAG}" | sed 's/llvmorg-//')"
