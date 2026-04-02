#!/bin/bash

# hinges on the fact that developer directories are sorted last
# in REPOS.release 
# get old values first starting from mulle-objc which is the first to 
# use mulle-clang
[ -z "${MULLE_CLANG_PROJECT_TAG}" ] && exit 1

mulle-project-all -r REPOS.release \
                  --from mulle-objc/mulle-objc-developer \
                  mulle-project-mulle-clang-version list

mulle-project-all -r REPOS.release \
                  --from mulle-objc/mulle-objc-developer \
                  mulle-project-mulle-clang-version \
                     set ${MULLE_CLANG_PROJECT_TAG}

for repo in mulle-objc/mulle-objc-developer \
            MulleFoundation/mulle-foundation-developer \
            MulleFoundation/foundation-developer \
            MulleWeb/mulle-web-developer
do
   if git -C "${repo}" status --porcelain | grep -q .
   then
      git -C "${repo}" add -u
      git -C "${repo}" commit -m "update mulle-clang to ${MULLE_CLANG_PROJECT_TAG}"
      git -C "${repo}" push
   fi
done
