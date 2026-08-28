#!/bin/bash
# Shared deb build+upload logic, run identically by the canary and fanout jobs.
# Staged in-repo by the MRC (alongside the workflow) and invoked from debs.yml.
#
# Ubuntu-only. Builds the mulle-clang compiler from the release source tarball via
# mulle-cc/mulle-clang-cpack's package-build, producing a .deb whose DIST comes
# from THIS runner's /etc/os-release VERSION_CODENAME (jammy / noble / <26>).
#
# Required env (from the workflow):
#   TAG        release tag (e.g. 22.1.8.7)
#   ARCH       debian arch for this runner (amd64 | arm64)
#   FORCE      "true" to rebuild even if the deb already exists
#   GH_TOKEN   token for gh
#   GITHUB_REPO, FORMULA_NAME, CPACK_REPO  (workflow env)
set -euo pipefail

: "${TAG:?}"; : "${ARCH:?}"; : "${GITHUB_REPO:?}"
: "${FORMULA_NAME:?}"; : "${CPACK_REPO:?}"
FORCE="${FORCE:-false}"

# --- Distribution identifiers from THIS runner (never guessed) ------------------
# Two distinct values, both read from /etc/os-release:
#
#   CODENAME = VERSION_CODENAME (jammy/noble/...). This is what cpack's package-build
#              (its own get_dist) embeds in the file it produces. We need it ONLY to
#              locate cpack's output artifact.
#   DIST     = the identifier WE publish under: "ubuntu<VERSION_ID>" (e.g. ubuntu24.04).
#              This is the release-facing name. Chosen (vs the bare codename) so the
#              Ubuntu debs are unambiguous and clearly distinct from the Debian
#              codename debs (bookworm/trixie/forky) that share this release.
#
# Ubuntu-only workflow, so ID must be "ubuntu"; assert it rather than guess.
read_osrel()
{
  # $1 = field name; echoes its value from /etc/os-release
  if [ -f /etc/os-release ]; then ( . /etc/os-release && eval "printf '%s' \"\${$1}\"" ); fi
}
os_id="$( read_osrel ID )"
CODENAME="$( read_osrel VERSION_CODENAME )"
[ -z "${CODENAME}" ] && CODENAME="$( lsb_release -sc 2>/dev/null | tail -1 )"
version_id="$( read_osrel VERSION_ID )"
[ -z "${version_id}" ] && version_id="$( lsb_release -sr 2>/dev/null | tail -1 )"

if [ "${os_id}" != "ubuntu" ]; then
  echo "::error::this workflow is Ubuntu-only but /etc/os-release ID='${os_id}'. Aborting."; exit 1
fi
[ -n "${CODENAME}" ]   || { echo "::error::could not determine VERSION_CODENAME on this runner"; exit 1; }
[ -n "${version_id}" ] || { echo "::error::could not determine VERSION_ID on this runner"; exit 1; }

DIST="ubuntu${version_id}"   # e.g. ubuntu24.04

# cpack produces:            mulle-clang-${VERSION}-${CODENAME}-${ARCH}.deb  (codename)
# we upload it renamed as:   mulle-clang-${VERSION}-${DIST}-${ARCH}.deb      (ubuntu<ver>)
produced_by_cpack="mulle-clang-${TAG}-${CODENAME}-${ARCH}.deb"
expected="mulle-clang-${TAG}-${DIST}-${ARCH}.deb"
echo "== ${DIST} (${CODENAME})/${ARCH}: target deb ${expected} =="

# --- PREFLIGHT: release exists + upload path actually works (cheap, pre-build) --
echo "::group::Preflight (release + upload probe)"
gh release view "${TAG}" --repo "${GITHUB_REPO}" >/dev/null 2>&1 || {
  echo "::error::release '${TAG}' not found"; exit 1; }
probe="__probe-${DIST}-${ARCH}-${GITHUB_RUN_ID:-0}-${GITHUB_RUN_ATTEMPT:-0}.txt"
printf 'probe %s\n' "${TAG}" > "${probe}"
gh release upload "${TAG}" "${probe}" --repo "${GITHUB_REPO}" --clobber || {
  echo "::error::probe upload failed; real upload would too. Aborting before build."; rm -f "${probe}"; exit 1; }
gh release delete-asset "${TAG}" "${probe}" --repo "${GITHUB_REPO}" --yes || \
  echo "::warning::could not delete probe ${probe}"
rm -f "${probe}"
echo "upload path OK"
echo "::endgroup::"

# --- SKIP-IF-EXISTS: don't rebuild a deb already on the release -----------------
if [ "${FORCE}" != "true" ]; then
  if gh release view "${TAG}" --repo "${GITHUB_REPO}" --json assets \
       --jq '.assets[].name' | grep -qxF "${expected}"; then
    echo "deb '${expected}' already exists → SKIPPING build (not an error)"
    exit 0
  fi
fi
echo "deb '${expected}' missing (or force=true) → building"

# --- TOOLCHAIN: install build prerequisites (Ubuntu) ----------------------------
echo "::group::Install build prerequisites"
export DEBIAN_FRONTEND=noninteractive
sudo apt-get update -qq
sudo apt-get install -y -qq git cmake ninja-build clang wget python3 lsb-release file linux-libc-dev
echo "::endgroup::"

# --- FETCH cpack (the build harness) at the release line ------------------------
echo "::group::Fetch mulle-clang-cpack"
workroot="$( mktemp -d )"
cd "${workroot}"
git clone --depth 1 "${CPACK_REPO}" mulle-clang-cpack
# align cpack to the release line (mulle/<major.minor.patch>), like create-deb's prepare()
branch="mulle/${TAG%.*}"
( cd mulle-clang-cpack && git fetch --depth 1 origin "${branch}" && git checkout "${branch}" ) || \
  echo "::warning::cpack branch '${branch}' not found; using default branch"
echo "::endgroup::"

# --- BUILD (expensive): clean download build verpack ----------------------------
# package-build downloads the source tarball for ${VERSION}${RC} from the
# mulle-clang-project GitHub archive, builds with cmake/ninja, and packages a .deb.
echo "::group::package-build clean download build verpack"
VERSION="${TAG}" RC="" ARCH="${ARCH}" MAKETOOL="ninja" \
  ./mulle-clang-cpack/package-build clean download build verpack
echo "::endgroup::"

# --- LOCATE cpack's output (codename-named) + RENAME to the ubuntu<ver> name ----
# cpack's verpack names the file with the CODENAME (e.g. ...-noble-amd64.deb). Find
# that, then rename to our release-facing DIST name (...-ubuntu24.04-amd64.deb).
if [ ! -f "${produced_by_cpack}" ]; then
  echo "::error::cpack artifact not found: ${produced_by_cpack}"
  echo "debs present:"; ls -1 ./*.deb 2>/dev/null || true
  exit 1
fi
produced="./${expected}"
mv -v "${produced_by_cpack}" "${produced}"

# --- verify the produced artifact -----------------------------------------------
# sanity: dpkg metadata arch should match ARCH
if command -v dpkg-deb >/dev/null 2>&1; then
  deb_arch="$( dpkg-deb -f "${produced}" Architecture 2>/dev/null || echo '?' )"
  echo "dpkg Architecture: ${deb_arch} (runner ARCH=${ARCH})"
  if [ "${deb_arch}" != "${ARCH}" ] && [ "${deb_arch}" != "?" ]; then
    echo "::error::deb Architecture '${deb_arch}' != runner ARCH '${ARCH}' — mislabeled runner. Aborting."; exit 1
  fi
fi
echo "produced ${produced} ($( du -h "${produced}" | cut -f1 ))"

# --- UPLOAD ---------------------------------------------------------------------
gh release upload "${TAG}" "${produced}" --repo "${GITHUB_REPO}" --clobber
echo "uploaded $( basename -- "${produced}" ) to ${GITHUB_REPO}@${TAG}"

# --- job summary ----------------------------------------------------------------
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
  {
    echo "### deb — ${DIST}/${ARCH}"
    echo "- artifact: \`$( basename -- "${produced}" )\`"
    echo "- size: $( du -h "${produced}" | cut -f1 )"
  } >> "${GITHUB_STEP_SUMMARY}"
fi
