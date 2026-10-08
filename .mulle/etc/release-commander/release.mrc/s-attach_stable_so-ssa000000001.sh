#!/bin/bash
#
# Attach a stable source tarball to the release, so the Homebrew source sha256
# is not at the mercy of GitHub's non-deterministic on-demand tag archive.
# See i-attach_stable_so-ssa000000001.md.
#
# MUST run before the deb/bottle CI and the formula staging/promote tasks.
#
set -euo pipefail

REPO="mulle-cc/mulle-clang-project"
TAG="${MULLE_CLANG_PROJECT_TAG:?MULLE_CLANG_PROJECT_TAG must be set}"
SRC="${PWD}"
ASSET="mulle-clang-project-${TAG}.tar.gz"
PREFIX="mulle-clang-project-${TAG}/"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

echo "=== build stable source asset ${ASSET} for ${TAG} ==="

git -C "${SRC}" rev-parse --verify -q "refs/tags/${TAG}" >/dev/null \
   || { echo "FAIL: refs/tags/${TAG} not found in ${SRC}" >&2; exit 1; }

# git archive applies export-subst with THIS local checkout's git; that is fine
# because the resulting bytes are pinned by uploading them as an asset.
git -C "${SRC}" archive \
   --format=tar.gz \
   --prefix="${PREFIX}" \
   "refs/tags/${TAG}" > "${work}/${ASSET}"

[ -s "${work}/${ASSET}" ] || { echo "FAIL: empty archive" >&2; exit 1; }
SHA="$( sha256sum "${work}/${ASSET}" | awk '{print $1}' )"
echo "source asset sha256: ${SHA}"

echo "--- uploading ${ASSET} (idempotent, --clobber) ---"
gh release upload "${TAG}" "${work}/${ASSET}" --repo "${REPO}" --clobber

URL="https://github.com/${REPO}/releases/download/${TAG}/${ASSET}"
echo "SOURCE_ASSET_URL=${URL}"
echo "SOURCE_ASSET_SHA256=${SHA}"

echo "OK: stable source asset attached to ${TAG}"
