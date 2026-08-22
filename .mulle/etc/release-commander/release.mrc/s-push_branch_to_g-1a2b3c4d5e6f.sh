#!/bin/bash

set -e

BRANCH="${MULLE_CLANG_BRANCH}"
if [ -z "${BRANCH}" ]; then
   echo "MULLE_CLANG_BRANCH is not set" >&2
   exit 1
fi

REPO="${PWD}"

cd "${REPO}"

CURRENT="$(git branch --show-current)"
if [ "${CURRENT}" != "${BRANCH}" ]; then
   echo "ERROR: current branch is '${CURRENT}', expected '${BRANCH}'" >&2
   exit 1
fi

echo "Pushing ${BRANCH} to mulle remote..."
git push mulle "${BRANCH}:${BRANCH}"

echo "Pushing ${BRANCH} to GitHub (origin)..."
git push origin "${BRANCH}:${BRANCH}"

echo "Done."
