#!/bin/bash

DOWNLOAD_DIR="mulle-clang-cpack"

for dist in trixie bookworm
do
   for arch in amd64
   do
      file="${DOWNLOAD_DIR}/mulle-clang-${MULLE_CLANG_PROJECT_TAG}-${dist}-${arch}.deb"
      if [ -e "${file}" ]
      then
         echo "Uploading $(basename -- "${file}") found"
         gh release upload --clobber "${MULLE_CLANG_PROJECT_TAG}" \
                           --repo mulle-cc/mulle-clang-project \
                           "${file}"
      else
         echo "No ${file} found"
      fi
   done
done
      