#!/bin/bash
# Shared bottle build+upload logic, run identically by the canary and fanout jobs.
# Staged in-repo by the MRC (alongside the formula) and invoked from bottle.yml.
#
# Required env (from the workflow):
#   TAG        release tag (e.g. 22.1.8.7)
#   CODENAME   macOS codename for this runner (sonoma|sequoia|tahoe|golden-gate)
#   FORCE      "true" to rebuild even if the bottle already exists
#   GH_TOKEN   token for gh
#   GITHUB_REPO, LOCAL_TAP, FORMULA_NAME, FORMULA_IN_REPO  (workflow env)
set -euo pipefail

: "${TAG:?}"; : "${CODENAME:?}"; : "${GITHUB_REPO:?}"
: "${LOCAL_TAP:?}"; : "${FORMULA_NAME:?}"; : "${FORMULA_IN_REPO:?}"
FORCE="${FORCE:-false}"

expected="${FORMULA_NAME}-${TAG}.arm64_${CODENAME}.bottle.tar.gz"
echo "== ${CODENAME}: target bottle ${expected} =="

# --- PREFLIGHT: release exists + upload path actually works (cheap, pre-build) --
echo "::group::Preflight (release + upload probe)"
gh release view "${TAG}" --repo "${GITHUB_REPO}" >/dev/null 2>&1 || {
  echo "::error::release '${TAG}' not found"; exit 1; }
probe="__probe-${CODENAME}-${GITHUB_RUN_ID:-0}-${GITHUB_RUN_ATTEMPT:-0}.txt"
printf 'probe %s\n' "${TAG}" > "${probe}"
gh release upload "${TAG}" "${probe}" --repo "${GITHUB_REPO}" --clobber || {
  echo "::error::probe upload failed; real upload would too. Aborting before build."; rm -f "${probe}"; exit 1; }
gh release delete-asset "${TAG}" "${probe}" --repo "${GITHUB_REPO}" --yes || \
  echo "::warning::could not delete probe ${probe}"
rm -f "${probe}"
echo "upload path OK"
echo "::endgroup::"

# --- SKIP-IF-EXISTS: don't rebuild a bottle already on the release --------------
if [ "${FORCE}" != "true" ]; then
  if gh release view "${TAG}" --repo "${GITHUB_REPO}" --json assets \
       --jq '.assets[].name' | grep -qxF "${expected}"; then
    echo "bottle '${expected}' already exists → SKIPPING build (not an error)"
    exit 0
  fi
fi
echo "bottle '${expected}' missing (or force=true) → building"

# --- FAKE LOCAL TAP from the staged formula (no public tap involved) ------------
echo "::group::Create fake local tap"
formula_path="${GITHUB_WORKSPACE}/${FORMULA_IN_REPO}"
[ -f "${formula_path}" ] || { echo "::error::staged formula not found at ${formula_path}"; exit 1; }
command -v brew >/dev/null 2>&1 || { echo "::error::brew not found"; exit 1; }
brew tap-new "${LOCAL_TAP}" --no-git
tap_dir="$( brew --repository )/Library/Taps/${LOCAL_TAP%/*}/homebrew-${LOCAL_TAP#*/}"
mkdir -p "${tap_dir}/Formula"
cp -v "${formula_path}" "${tap_dir}/Formula/${FORMULA_NAME}.rb"
echo "staged version: $( brew info --json=v2 "${LOCAL_TAP}/${FORMULA_NAME}" | jq -r '.formulae[0].versions.stable' )"
echo "::endgroup::"

# --- BUILD (expensive) ----------------------------------------------------------
echo "::group::Build --build-bottle"
brew install --build-bottle "${LOCAL_TAP}/${FORMULA_NAME}"
echo "::endgroup::"

# --- BOTTLE + authoritative naming + tag guard ----------------------------------
workdir="$( mktemp -d )"; cd "${workdir}"
brew bottle --json --no-rebuild "${LOCAL_TAP}/${FORMULA_NAME}"
json_file="$( ls -1 ./*.bottle.json | head -n1 )"
[ -n "${json_file}" ] || { echo "::error::no bottle json produced"; exit 1; }

canonical="$( jq -r '.. | objects | select(has("tags")) | .tags | to_entries[0].value.filename' "${json_file}" )"
localname="$( jq -r '.. | objects | select(has("tags")) | .tags | to_entries[0].value.local_filename' "${json_file}" )"
sha="$(       jq -r '.. | objects | select(has("tags")) | .tags | to_entries[0].value.sha256' "${json_file}" )"
tagkey="$(    jq -r '.. | objects | select(has("tags")) | .tags | to_entries[0].key' "${json_file}" )"
[ -n "${canonical}" ] && [ "${canonical}" != "null" ] || { echo "::error::could not read canonical filename"; exit 1; }

# TAG GUARD: brew-detected arch tag must match this runner's OS. golden-gate is
# beta → warn (its final tag may differ); everything else → hard fail (mislabeled runner).
expected_tag="arm64_${CODENAME}"
if [ "${tagkey}" != "${expected_tag}" ]; then
  if [ "${CODENAME}" = "golden-gate" ]; then
    echo "::warning::brew tag '${tagkey}' != expected '${expected_tag}' (golden-gate beta; continuing)"
  else
    echo "::error::brew tag '${tagkey}' != expected '${expected_tag}' — runner OS mismatch. Aborting."; exit 1
  fi
fi

if [ "${localname}" != "${canonical}" ] && [ -f "${localname}" ]; then
  mv -v "${localname}" "${canonical}"
fi
echo "resolved bottle: ${canonical} (tag=${tagkey}, sha256=${sha})"

# --- UPLOAD ---------------------------------------------------------------------
gh release upload "${TAG}" "${canonical}" --repo "${GITHUB_REPO}" --clobber
echo "uploaded ${canonical} to ${GITHUB_REPO}@${TAG}"

# --- job summary (sha line for the formula bottle do block) ---------------------
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
  {
    echo "### Bottle — ${CODENAME}"
    echo '```ruby'
    echo "  sha256 cellar: :any, ${tagkey}: \"${sha}\""
    echo '```'
  } >> "${GITHUB_STEP_SUMMARY}"
fi
