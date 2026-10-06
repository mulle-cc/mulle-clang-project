#!/bin/bash

#
# Promote the mulle-clang-project Homebrew formula from the prerelease tap to
# the stable taps. See i-promote_homebrew-e7c1a9b4.md.
#
# Runs on the Linux release host (GNU sed / base64, authenticated gh).
#

set -e

[ -n "${MULLE_CLANG_PROJECT_TAG}" ] || { echo "MULLE_CLANG_PROJECT_TAG is not set" >&2; exit 1; }
[ -n "${MULLE_OBJC_RUNTIME_LOAD_VERSION}" ] || { echo "MULLE_OBJC_RUNTIME_LOAD_VERSION is not set" >&2; exit 1; }

SOURCE_REPO="mulle-objc/homebrew-prerelease"
TARGET_REPOS="mulle-objc/homebrew-software mulle-kybernetik/homebrew-software"
FORMULA="mulle-clang-project.rb"

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

echo "Fetching ${FORMULA} from ${SOURCE_REPO} (master)..."
gh api "repos/${SOURCE_REPO}/contents/${FORMULA}?ref=master" --jq '.content' \
   | base64 --decode > "${tmpdir}/${FORMULA}"

if ! grep -q "version \"${MULLE_CLANG_PROJECT_TAG}\"" "${tmpdir}/${FORMULA}"
then
   echo "error: ${SOURCE_REPO}/${FORMULA} is not at ${MULLE_CLANG_PROJECT_TAG}" >&2
   echo "       finish the prerelease tap (macos) steps first" >&2
   exit 1
fi

#
# the prerelease formula is hand-edited; make the load version authoritative
#
sed -i "s/runtime-load-version: [0-9][0-9]*/runtime-load-version: ${MULLE_OBJC_RUNTIME_LOAD_VERSION}/" \
   "${tmpdir}/${FORMULA}"

if ! grep -q "runtime-load-version: ${MULLE_OBJC_RUNTIME_LOAD_VERSION}" "${tmpdir}/${FORMULA}"
then
   echo "error: could not set runtime-load-version in ${FORMULA}" >&2
   exit 1
fi

content="$(base64 --wrap=0 "${tmpdir}/${FORMULA}")"

for repo in ${TARGET_REPOS}
do
   sha="$(gh api "repos/${repo}/contents/${FORMULA}?ref=master" --jq '.sha')"
   gh api -X PUT "repos/${repo}/contents/${FORMULA}" \
      -f message="mulle-clang-project ${MULLE_CLANG_PROJECT_TAG}" \
      -f content="${content}" \
      -f sha="${sha}" \
      -f branch=master > /dev/null
   echo "promoted ${FORMULA} ${MULLE_CLANG_PROJECT_TAG} to ${repo}"
done

echo "done"
