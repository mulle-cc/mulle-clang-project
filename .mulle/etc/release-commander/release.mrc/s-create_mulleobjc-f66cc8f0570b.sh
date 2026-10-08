#!/bin/bash
#
# Build + upload the mulle-clang Debian and Ubuntu .deb packages via GitHub Actions.
#
#   .github/workflows/debian-debs.yml   bookworm / trixie / forky   x amd64/arm64
#   .github/workflows/debs.yml          ubuntu22.04/24.04/26.04     x amd64/arm64
#
# NON-BLOCKING POLL MODEL
# -----------------------
# CI here is long (canary then a 5-way fanout; the runner pool can serialize it).
# This script polls for at most MULLE_CI_POLL_SECONDS (default 300 = 5 min),
# printing a status snapshot every 30 s, then RETURNS so the commander can show
# progress and re-enter.
#
# Per-workflow gating: a workflow whose debs are ALL already on the release is
# skipped entirely (never attached to, never re-dispatched). A workflow is only
# dispatched when it still has missing debs AND no in-flight run.
#
# Exit codes:
#   0  every expected .deb is present on the release -> done, certifiable
#   1  a tracked run finished with failure/cancelled, or all tracked runs finished
#      but expected assets are still missing -> real failure
#   3  still running at the poll deadline -> task stays "in-progress"; re-run to
#      continue polling (each re-run = one <=5 min progress update)
#
set -euo pipefail

REPO="mulle-cc/mulle-clang-project"
TAG="${MULLE_CLANG_PROJECT_TAG:?MULLE_CLANG_PROJECT_TAG must be set}"
BRANCH="${MULLE_CLANG_BRANCH:?MULLE_CLANG_BRANCH must be set}"
POLL_SECONDS="${MULLE_CI_POLL_SECONDS:-300}"

stamp() { date -u +%H:%M:%SZ; }

debian_expected() {
   local c a
   for c in bookworm trixie forky; do
      for a in amd64 arm64; do
         printf '%s\n' "mulle-clang-${TAG}-${c}-${a}.deb"
      done
   done
}

ubuntu_expected() {
   local v a
   for v in ubuntu22.04 ubuntu24.04 ubuntu26.04; do
      for a in amd64 arm64; do
         printf '%s\n' "mulle-clang-${TAG}-${v}-${a}.deb"
      done
   done
}

expected_for() {
   case "$1" in
      debian-debs.yml) debian_expected ;;
      debs.yml)        ubuntu_expected ;;
   esac
}

assets() {
   gh release view "${TAG}" --repo "${REPO}" --json assets -q '.assets[].name' 2>/dev/null || true
}

missing_for() {
   local a e m=""
   a="$(assets)"
   while read -r e; do
      [ -n "${e}" ] || continue
      printf '%s\n' "${a}" | grep -qxF "${e}" || m="${m} ${e}"
   done < <(expected_for "$1")
   printf '%s' "${m# }"
}

missing_debs() {
   local d u
   d="$(missing_for debian-debs.yml)"
   u="$(missing_for debs.yml)"
   if [ -z "${d}" ] && [ -z "${u}" ]; then printf ''; else printf '%s %s' "${d}" "${u}"; fi
}

inflight_id() {
   gh run list --repo "${REPO}" --workflow "$1" --limit 20 \
      --json databaseId,status \
      -q '[.[] | select(.status != "completed")][0].databaseId // empty' 2>/dev/null || true
}

echo "=== deb CI: ${TAG} on ${BRANCH} (poll ${POLL_SECONDS}s) ==="

default_branch="$(gh api "repos/${REPO}" --jq '.defaultBranchRef.name // .default_branch')"
echo "repo default branch: ${default_branch}"
if [ "${default_branch}" != "${BRANCH}" ]; then
   echo "FAIL: default branch '${default_branch}' != release branch '${BRANCH}'." >&2
   echo "      workflow_dispatch is ONLY available from the DEFAULT branch." >&2
   exit 1
fi

if [ -z "$(missing_debs)" ]; then
   echo "[$(stamp)] OK: all expected .deb assets already present on ${TAG}"
   exit 0
fi

# --- attach to / dispatch only the workflows that still have missing debs -------
TRACK=""
for wf in debian-debs.yml debs.yml; do
   if [ -z "$(missing_for "${wf}")" ]; then
      echo "[$(stamp)] ${wf}: all debs already present; skipping"
      continue
   fi
   id="$(inflight_id "${wf}")"
   if [ -z "${id}" ]; then
      echo "[$(stamp)] --- dispatching ${wf} ---"
      gh workflow run "${wf}" --repo "${REPO}" --ref "${BRANCH}" -f tag="${TAG}"
      sleep 10
      id="$(gh run list --repo "${REPO}" --workflow "${wf}" --limit 1 \
              --json databaseId -q '.[0].databaseId')"
   else
      echo "[$(stamp)] --- attaching to in-flight ${wf} run ${id} ---"
   fi
   TRACK="${TRACK}${wf} ${id}"$'\n'
done

if [ -z "${TRACK}" ]; then
   echo "[$(stamp)] OK: all expected .deb assets present on ${TAG}"
   exit 0
fi

deadline=$(( $(date +%s) + POLL_SECONDS ))
while [ "$(date +%s)" -lt "${deadline}" ]; do
   all_done=1
   failed=0
   while read -r wf id; do
      if [ -n "${wf}" ]; then
         state="$(gh run view "${id}" --repo "${REPO}" --json status,conclusion \
                    -q '.status + " " + (.conclusion // "-")' 2>/dev/null || echo 'unknown')"
         echo "[$(stamp)] ${wf} #${id}: ${state}"
         case "${state}" in
            completed\ success*) : ;;
            completed\ *)        failed=1 ;;
            *)                   all_done=0 ;;
         esac
      fi
   done <<< "${TRACK}"

   if [ "${all_done}" = 1 ]; then
      m="$(missing_debs)"
      if [ -z "${m}" ]; then
         echo "[$(stamp)] OK: all expected .deb assets present on ${TAG}"
         exit 0
      fi
      echo "FAIL: all tracked runs finished but .deb assets are missing:${m}" >&2
      exit 1
   fi

   remaining=$(( deadline - $(date +%s) ))
   [ "${remaining}" -le 0 ] && break
   if [ "${remaining}" -lt 30 ]; then sleep "${remaining}"; else sleep 30; fi
done

m="$(missing_debs)"
if [ -z "${m}" ]; then
   echo "[$(stamp)] OK: all expected .deb assets present on ${TAG}"
   exit 0
fi
if [ "${failed}" = 1 ]; then
   echo "FAIL: a tracked workflow run failed; .deb assets still missing:${m}" >&2
   exit 1
fi
echo "[$(stamp)] STILL IN PROGRESS after ${POLL_SECONDS}s; .deb assets missing:${m}"
echo "Re-run this script to continue polling (in-flight runs are attached, not re-dispatched)."
exit 3