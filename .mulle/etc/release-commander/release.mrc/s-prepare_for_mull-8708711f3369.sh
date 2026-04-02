#!/bin/bash

set -e

cd mulle-clang-cpack 

# 

mulle-replace -lx --regex 'VERSION="\${VERSION:-[0-9.][0-9.]*}"' \
                          "VERSION=\"\\\${VERSION:-${MULLE_CLANG_PROJECT_TAG}}\"" \
		          create-deb \
                          generate-package 

mulle-replace -lx --regex 'VERSION="[0-9.][0-9.]*"' \
                          "VERSION=\"${MULLE_CLANG_PROJECT_TAG}\"" \
		          README.md

mulle-replace -lx --regex 'VERSION=[0-9.][0-9.]*' \
                          "VERSION=${MULLE_CLANG_PROJECT_TAG}" \
		          README.md

mulle-replace -lx --regex 'mulle-cc\/mulle-clang-project\/refs\/heads\/mulle\/[0-9.][0-9.]*\/clang' \
                          "mulle-cc\/mulle-clang-project\/refs\/heads\/mulle\/${MULLE_CLANG_PROJECT_TAG%.*}\/clang" \
		          README.md


# modernize example i, dont know even...
if ! grep trixie README.md 2> /dev/null
then
   mulle-replace -lx bookworm trixie   README.md
   mulle-replace -lx bullseye bookworm README.md
fi

git add -u
git commit -m "update for ${MULLE_CLANG_PROJECT_TAG%\.*}"
git checkout -b "mulle/${MULLE_CLANG_PROJECT_TAG%\.*}"
git push github "mulle/${MULLE_CLANG_PROJECT_TAG%\.*}"
git checkout master
git push github master


