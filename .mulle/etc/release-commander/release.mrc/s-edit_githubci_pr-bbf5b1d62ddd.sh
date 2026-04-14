#!/bin/bash

set -e 

echo "mulle-cc/github-ci"
cd mulle-cc/github-ci 

mulle-replace --regex 'version="[^"]*" # default'  \
                      'version="'"${MULLE_CLANG_PROJECT_TAG}"'" # default' \
                      install.sh
# if no change then 
if ! git status --porcelain | grep -q .
then
   echo "Still same compiler version, nothing to do"
   exit 0
fi

CURRENT_TAG=$(git tag --sort=-creatordate | grep -E '^v[0-9]+$' | head -n1)
NUM=${CURRENT_TAG#v}        # remove the leading 'v'
NEXT_NUM=$((NUM + 1))       # increment
NEXT_TAG="v$NEXT_NUM"       # new tag
      
mulle-replace --word "${CURRENT_TAG}" "${NEXT_TAG}" *.sh
git add -u
git commit -m "new compiler version ${MULLE_CLANG_PROJECT_TAG}" 
git tag -f "${NEXT_TAG}"  

cd ../..

FILES="mulle-objc/mulle-objc-developer/.github/workflows/mulle-sde-ci.yml
mulle-objc/mulle-objc-developer/src/mulle-objc/github-actions/project/all/.github/workflows/mulle-sde-ci.yml
MulleFoundation/mulle-foundation-developer/.github/workflows/mulle-sde-ci.yml
MulleFoundation/mulle-foundation-developer/src/mulle-foundation/github-actions/project/all/.github/workflows/mulle-sde-ci.yml
MulleFoundation/mulle-foundation-developer/src/mulle-foundation/github-actions/project/all/.github/workflows/mulle-sde-artifacts.yml
MulleFoundation/foundation-developer/.github/workflows/mulle-sde-ci.yml
MulleFoundation/foundation-developer/src/foundation/github-actions/project/all/.github/workflows/mulle-sde-ci.yml
MulleFoundation/foundation-developer/src/foundation/github-actions/project/all/.github/workflows/mulle-sde-artifacts.yml
MulleWeb/mulle-web-developer/.github/workflows/mulle-sde-ci.yml"

mulle-replace "mulle-cc/github-ci@${CURRENT_TAG}" \
              "mulle-cc/github-ci@${NEXT_TAG}" \
              ${FILES}

#
# do this last
#
cd mulle-cc/github-ci 

git push origin 
git push origin --tags

cd ../..

# commit and push the developer repos that had yml files updated
for repo in mulle-objc/mulle-objc-developer \
            MulleFoundation/mulle-foundation-developer \
            MulleFoundation/foundation-developer \
            MulleWeb/mulle-web-developer
do
   if git -C "${repo}" status --porcelain | grep -q '.'
   then
      echo "Committing ${repo}"
      git -C "${repo}" add -u
      git -C "${repo}" commit -m "new compiler version ${MULLE_CLANG_PROJECT_TAG}"
      git -C "${repo}" push --set-upstream origin develop
   else
      echo "No yml changes in ${repo}, skipping"
   fi
done