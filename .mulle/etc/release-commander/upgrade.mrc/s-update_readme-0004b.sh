#!/bin/bash
# Determine the minimum compatible runtime version and show recent mulle commits.
# README update itself is manual.
set -e

: "${MULLE_CLANG_DIR:?set MULLE_CLANG_DIR}"
: "${MULLE_OBJC_RUNTIME_DIR:?set MULLE_OBJC_RUNTIME_DIR}"

cd "${MULLE_CLANG_DIR}/mulle-clang-project"

# 1. Find COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION in the compiler
COMPAT_LOAD="$(grep -r '#define COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION' clang/lib/CodeGen/ \
  | grep -o '[0-9]*$')"
echo "Compiler requires runtime load version: ${COMPAT_LOAD}"

# 2. Find the first *tagged* release in the runtime where LOAD_VERSION >= that value
cd "${MULLE_OBJC_RUNTIME_DIR}"
RUNTIME_VERSION=""
for tag in $(git tag -l | sort -V); do
   val=$(git show "${tag}:src/mulle-objc-load.h" 2>/dev/null \
     | grep "MULLE_OBJC_RUNTIME_LOAD_VERSION" | grep -o '[0-9]*$' | head -1)
   if [ "${val}" -ge "${COMPAT_LOAD}" ] 2>/dev/null; then
      RUNTIME_VERSION="$(git show "${tag}":CMakeLists.txt \
        | grep 'project.*VERSION' | grep -o '[0-9]*\.[0-9]*\.[0-9]*' | head -1)"
      echo "First tagged runtime with load version >= ${COMPAT_LOAD}: ${tag} (${RUNTIME_VERSION})"
      break
   fi
done

if [ -z "${RUNTIME_VERSION}" ]; then
   echo "No tagged runtime release has load version ${COMPAT_LOAD} yet — use next planned version"
else
   echo "Update README.md: 'mulle-objc-runtime v${RUNTIME_VERSION%.*} or better'"
fi

echo ""
echo "Mulle-specific commits to document:"
cd "${MULLE_CLANG_DIR}/mulle-clang-project"
OLD_BRANCH="$(grep '^OLD_MULLE_DEV_BRANCH=' clang/bin/migrate-to-next-release | cut -d'"' -f2)"
git log --oneline "squashed_${OLD_BRANCH}..${OLD_BRANCH}" -- clang/ 2>/dev/null || \
  git log --oneline "${OLD_BRANCH}" -- clang/ | head -20

echo ""
echo "Edit README.md manually, then:"
echo "  git add README.md && git commit -m 'Update README for new version'"
