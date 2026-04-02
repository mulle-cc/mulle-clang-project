#!/bin/bash
# Check if we've reached the latest available major version.
set -e

: "${MULLE_CLANG_DIR:?set MULLE_CLANG_DIR}"

cd "${MULLE_CLANG_DIR}/mulle-clang-project"

CURRENT_TAG="$(grep '^NEW_LLVM_TAG=' clang/bin/migrate-to-next-release | cut -d'"' -f2)"
CURRENT_MAJOR="$(echo "${CURRENT_TAG}" | sed 's/llvmorg-//' | cut -d. -f1)"
TARGET_MAJOR=$(( CURRENT_MAJOR + 1 ))

NEXT="$(git tag -l "llvmorg-${TARGET_MAJOR}.*" \
  | grep -E "^llvmorg-${TARGET_MAJOR}\.[0-9]+\.[0-9]+$" \
  | sort -t. -k2,2n -k3,3n | tail -1)"

if [ -z "${NEXT}" ]; then
   echo "At latest major (${CURRENT_MAJOR}) — upgrade complete."
else
   echo "Next major available: ${NEXT}"
   echo "Re-run steps 1–5 with NEW_LLVM_TAG=${NEXT}"
fi
