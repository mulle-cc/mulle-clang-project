#!/bin/bash

set -e

# Read version from the build script
VERSION="$(grep -o 'VERSION:-[0-9.]*' /home/src/srcL/mulle-clang-21.1.8/mulle-clang-project/clang/bin/cmake-ninja.linux | head -1 | sed 's/VERSION:-//')"

if [ -z "${VERSION}" ]; then
   echo "Could not determine version" >&2
   exit 1
fi

echo "Version to release: ${VERSION}"

# Check if release already exists
if gh release view "${VERSION}" --repo mulle-cc/mulle-clang-project > /dev/null 2>&1; then
   # Release exists - but check if .deb is already uploaded
   existing_deb="$(gh release view "${VERSION}" --repo mulle-cc/mulle-clang-project --json assets -q '.assets[].name' | grep '\.deb' | head -1)"
   if [ -n "${existing_deb}" ]; then
      echo "Release ${VERSION} already has a .deb package - nothing to do" >&2
      exit 1
   fi
   echo "Release ${VERSION} exists but has no .deb yet - proceeding"
else
   # Create tag and release
   gh release create "${VERSION}" \
      --repo mulle-cc/mulle-clang-project \
      --title "${VERSION}" \
      --notes "mulle-clang ${VERSION}"
   echo "Created release ${VERSION}"
fi
