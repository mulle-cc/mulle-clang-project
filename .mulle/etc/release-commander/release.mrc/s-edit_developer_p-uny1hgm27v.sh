#!/bin/bash

# hinges on the fact that developer directories are sorted last
# in REPOS.release 
# get old values first starting from mulle-objc which is the first to 
# use mulle-clang
[ -z "${MULLE_CLANG_PROJECT_TAG}" ] && exit 1

mulle-project-all -r REPOS.release --from mulle-objc/mulle-objc-developer mulle-project-mulle-clang-version list

mulle-project-all -r REPOS.release --from mulle-objc/mulle-objc-developer mulle-project-mulle-clang-version set ${MULLE_CLANG_PROJECT_TAG}
