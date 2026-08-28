#!/bin/bash
# Debian .deb build+upload logic (Option 1: Docker-in-runner), run identically by
# the debian canary and fanout jobs. Staged in-repo by the MRC alongside the
# workflow and invoked from debian-debs.yml.
#
# WHY A CONTAINER (vs the native-Ubuntu deb-build.sh):
#   A .deb's real compatibility is set by the glibc / libstdc++ / dependency
#   versions it links against, NOT by a filename. Building a "bookworm" deb on the
#   Ubuntu runner's own userland would link against Ubuntu's glibc and compute
#   Debian-wrong Depends: — it might install yet fail at runtime on real Debian.
#   So we build each Debian deb INSIDE its matching debian:<codename> container,
#   which supplies that release's actual glibc + package versions. cpack's
#   package-build (its get_dist reads the CONTAINER's /etc/os-release
#   VERSION_CODENAME) then naturally names the artifact by codename — which for
#   Debian IS the canonical, unambiguous identifier, so (unlike the Ubuntu path)
#   there is NO rename step.
#
# ARCH: the container runs on a matching-arch host runner (amd64 deb on an amd64
#   runner, arm64 deb on an arm64 runner) so the build is native — no qemu.
#
# Required env (from the workflow):
#   TAG        release tag (e.g. 22.1.8.7)
#   ARCH       debian arch for this runner/container (amd64 | arm64)
#   DEB_DIST   Debian codename to build for (bookworm | trixie | forky)
#   FORCE      "true" to rebuild even if the deb already exists
#   GH_TOKEN   token for gh
#   GITHUB_REPO, FORMULA_NAME, CPACK_REPO  (workflow env)
set -euo pipefail

: "${TAG:?}"; : "${ARCH:?}"; : "${DEB_DIST:?}"; : "${GITHUB_REPO:?}"
: "${FORMULA_NAME:?}"; : "${CPACK_REPO:?}"
FORCE="${FORCE:-false}"

# For Debian, the codename IS the release-facing identifier (bookworm/trixie/forky).
# cpack's package-build produces exactly this name inside the container; we upload
# it as-is (no rename, in contrast to the Ubuntu path's ubuntu<ver> scheme).
expected="mulle-clang-${TAG}-${DEB_DIST}-${ARCH}.deb"
echo "== ${DEB_DIST}/${ARCH}: target deb ${expected} =="

# --- PREFLIGHT: release exists + upload path actually works (cheap, pre-build) --
echo "::group::Preflight (release + upload probe)"
gh release view "${TAG}" --repo "${GITHUB_REPO}" >/dev/null 2>&1 || {
  echo "::error::release '${TAG}' not found"; exit 1; }
probe="__probe-${DEB_DIST}-${ARCH}-${GITHUB_RUN_ID:-0}-${GITHUB_RUN_ATTEMPT:-0}.txt"
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
echo "deb '${expected}' missing (or force=true) → building in debian:${DEB_DIST} (${ARCH})"

# --- BUILD INSIDE debian:<codename> ---------------------------------------------
# We assume docker is present on the runner. The build runs as root in the
# container (Debian base images have no sudo and package-build uses sudo for the
# /opt symlink), so we install sudo among the prereqs. The produced .deb is copied
# to a bind-mounted output dir that we then upload from the host.
outdir="$( mktemp -d )"
platform="linux/${ARCH}"   # docker arch names match debian arch names (amd64/arm64)

# In-container build script. Runs cpack's package-build, whose get_dist reads THIS
# container's /etc/os-release -> ${DEB_DIST}, so it emits ...-${DEB_DIST}-${ARCH}.deb.
incontainer="$( cat <<INNER
set -eux
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq \
  git cmake ninja-build clang wget python3 file sudo ca-certificates lsb-release linux-libc-dev
workroot="\$( mktemp -d )"
cd "\${workroot}"
git clone --depth 1 "${CPACK_REPO}" mulle-clang-cpack
branch="mulle/${TAG%.*}"
( cd mulle-clang-cpack && git fetch --depth 1 origin "\${branch}" && git checkout "\${branch}" ) || \
  echo "WARNING: cpack branch '\${branch}' not found; using default branch"
VERSION="${TAG}" RC="" ARCH="${ARCH}" MAKETOOL="ninja" \
  ./mulle-clang-cpack/package-build clean download build verpack
# package-build writes the .deb into \${workroot}; hand it back via the mount
cp -v "${expected}" /out/ 2>/dev/null || {
  echo "ERROR: expected artifact ${expected} not found in \${workroot}" >&2
  ls -1 ./*.deb 2>/dev/null || true
  exit 1
}
INNER
)"

echo "::group::docker build in debian:${DEB_DIST} (${platform})"
docker run --rm \
  --platform "${platform}" \
  -v "${outdir}:/out" \
  -e DEBIAN_FRONTEND=noninteractive \
  "debian:${DEB_DIST}" \
  bash -c "${incontainer}"
echo "::endgroup::"

produced="${outdir}/${expected}"
if [ ! -f "${produced}" ]; then
  echo "::error::container did not produce ${expected}"
  ls -la "${outdir}" || true
  exit 1
fi

# --- verify the produced artifact (host has dpkg-deb) ---------------------------
if command -v dpkg-deb >/dev/null 2>&1; then
  deb_arch="$( dpkg-deb -f "${produced}" Architecture 2>/dev/null || echo '?' )"
  echo "dpkg Architecture: ${deb_arch} (runner/container ARCH=${ARCH})"
  if [ "${deb_arch}" != "${ARCH}" ] && [ "${deb_arch}" != "?" ]; then
    echo "::error::deb Architecture '${deb_arch}' != ARCH '${ARCH}' — arch/platform mismatch. Aborting."; exit 1
  fi
fi
echo "produced ${produced} ($( du -h "${produced}" | cut -f1 ))"

# --- UPLOAD ---------------------------------------------------------------------
gh release upload "${TAG}" "${produced}" --repo "${GITHUB_REPO}" --clobber
echo "uploaded ${expected} to ${GITHUB_REPO}@${TAG}"

# --- job summary ----------------------------------------------------------------
if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
  {
    echo "### deb — ${DEB_DIST}/${ARCH} (containerized)"
    echo "- artifact: \`${expected}\`"
    echo "- size: $( du -h "${produced}" | cut -f1 )"
  } >> "${GITHUB_STEP_SUMMARY}"
fi

rm -rf "${outdir}"
