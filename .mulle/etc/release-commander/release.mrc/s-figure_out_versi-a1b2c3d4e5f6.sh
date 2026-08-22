#!/bin/bash

set -e

VERSION="${MULLE_CLANG_PROJECT_TAG}"

if [ -z "${VERSION}" ]; then
   echo "MULLE_CLANG_PROJECT_TAG is not set" >&2
   exit 1
fi

echo "Version to release: ${VERSION}"

EXPECTED_COMMIT="$(cd "${PWD}" && git rev-parse "${MULLE_CLANG_BRANCH}")"
echo "Expected commit: ${EXPECTED_COMMIT} (${MULLE_CLANG_BRANCH})"

# Check if release/tag already exists
if gh release view "${VERSION}" --repo mulle-cc/mulle-clang-project > /dev/null 2>&1; then
   ACTUAL_COMMIT="$(gh api repos/mulle-cc/mulle-clang-project/git/ref/tags/${VERSION} -q '.object.sha' 2>/dev/null || true)"
   # Dereference annotated tag if needed
   if [ -z "${ACTUAL_COMMIT}" ]; then
      ACTUAL_COMMIT="$(gh api repos/mulle-cc/mulle-clang-project/git/refs/tags/${VERSION} -q '.object.sha' 2>/dev/null || true)"
   fi
   echo "Existing tag points to: ${ACTUAL_COMMIT}"

   if [ "${ACTUAL_COMMIT}" != "${EXPECTED_COMMIT}" ]; then
      echo "Tag ${VERSION} points to wrong commit - deleting and recreating..." >&2
      gh release delete "${VERSION}" --repo mulle-cc/mulle-clang-project --yes
      gh api repos/mulle-cc/mulle-clang-project/git/refs/tags/${VERSION} -X DELETE
   else
      existing_deb="$(gh release view "${VERSION}" --repo mulle-cc/mulle-clang-project --json assets -q '.assets[].name' | grep '\.deb' | head -1)"
      if [ -n "${existing_deb}" ]; then
         echo "Release ${VERSION} already has a .deb package - nothing to do" >&2
         exit 1
      fi
      echo "Release ${VERSION} exists at correct commit but has no .deb yet - proceeding"
      exit 0
   fi
fi

gh release create "${VERSION}" \
   --repo mulle-cc/mulle-clang-project \
   --title "${VERSION}" \
   --notes "mulle-clang ${VERSION}" \
   --target "${EXPECTED_COMMIT}"
echo "Created release ${VERSION} at ${EXPECTED_COMMIT}"
