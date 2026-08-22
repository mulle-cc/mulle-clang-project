#!/bin/bash

set -e 

echo "mulle-cc/github-ci"
cd "${MULLE_REPOS_DIR}/mulle-cc/github-ci"

mulle-replace --regex 'version="[^"]*" # default'  \
                      'version="'"${MULLE_CLANG_PROJECT_TAG}"'" # default' \
                      install.sh

# Determine the current and next tag
CURRENT_TAG=$(git tag --sort=-creatordate | grep -E '^v[0-9]+$' | head -n1)
NUM=${CURRENT_TAG#v}        # remove the leading 'v'
NEXT_NUM=$((NUM + 1))       # increment
NEXT_TAG="v$NEXT_NUM"       # new tag

# if github-ci has uncommitted changes, commit and tag
if git status --porcelain | grep -q .
then
   mulle-replace --word "${CURRENT_TAG}" "${NEXT_TAG}" *.sh
   git add -u
   git commit -m "new compiler version ${MULLE_CLANG_PROJECT_TAG}" 
   git tag -f "${NEXT_TAG}"  

   PREVIOUS_TAG="${CURRENT_TAG}"
else
   echo "github-ci already committed for this version"
   # CURRENT_TAG is already the newest (v11), so PREVIOUS_TAG is one below
   PREVIOUS_TAG="v$((NUM - 1))"
   NEXT_TAG="${CURRENT_TAG}"
fi

cd "${MULLE_REPOS_DIR}"

FILES="mulle-objc/mulle-objc-developer/.github/workflows/mulle-sde-ci.yml
mulle-objc/mulle-objc-developer/src/mulle-objc/github-actions/project/all/.github/workflows/mulle-sde-ci.yml
MulleFoundation/mulle-foundation-developer/.github/workflows/mulle-sde-ci.yml
MulleFoundation/mulle-foundation-developer/src/mulle-foundation/github-actions/project/all/.github/workflows/mulle-sde-ci.yml
MulleFoundation/mulle-foundation-developer/src/mulle-foundation/github-actions/project/all/.github/workflows/mulle-sde-artifacts.yml
MulleFoundation/foundation-developer/.github/workflows/mulle-sde-ci.yml
MulleFoundation/foundation-developer/src/foundation/github-actions/project/all/.github/workflows/mulle-sde-ci.yml
MulleFoundation/foundation-developer/src/foundation/github-actions/project/all/.github/workflows/mulle-sde-artifacts.yml
MulleWeb/mulle-web-developer/.github/workflows/mulle-sde-ci.yml"

# Update yml files (idempotent - mulle-replace won't change if already correct)
mulle-replace "mulle-cc/github-ci@${PREVIOUS_TAG}" \
              "mulle-cc/github-ci@${NEXT_TAG}" \
              ${FILES}

#
# push github-ci (idempotent - push is no-op if already up to date)
#
cd "${MULLE_REPOS_DIR}/mulle-cc/github-ci"

git push origin 
git push origin "${NEXT_TAG}"

cd "${MULLE_REPOS_DIR}"

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
