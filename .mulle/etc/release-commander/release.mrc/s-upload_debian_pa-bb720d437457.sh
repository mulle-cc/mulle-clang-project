#!/bin/bash
#
# Build + upload the mulle-clang macOS Homebrew bottles via GitHub Actions.
#
# The old manual macOS builds are OBSOLETE. Bottle building is automated by the
# manually-triggered, idempotent workflow installed by the
# mulle-clang-project-bottle-ci.mrc bundle:
#
#   .github/workflows/bottle.yml   macOS arm64 bottles (sonoma/sequoia/tahoe/golden-gate)
#
# It builds from the in-repo staged formula via a faked local tap and uploads the
# bottles directly to the GitHub release `MULLE_CLANG_PROJECT_TAG`. Re-dispatching
# only fills MISSING bottles.
#
# workflow_dispatch workflows are only dispatchable when their file exists on the
# repo's DEFAULT branch, so the default branch MUST be the release branch.
#
# NON-BLOCKING POLL MODEL
# -----------------------
# Each bottle is an ~1 h build behind a canary. A blocking `gh run watch` gives the
# commander NO visibility until the call returns. Instead this script polls for at
# most MULLE_CI_POLL_SECONDS (default 300 = 5 min), printing a status snapshot every
# 30 s, and then RETURNS so the commander can show progress and re-enter.
#
# Exit codes:
#   0  all four bottles are present on the release -> done, certifiable
#   1  a tracked run finished with failure/cancelled, or the run finished but
#      bottles are still missing -> real failure
#   3  still running at the poll deadline -> task stays "in-progress"; re-run to
#      continue polling (each re-run = one <=5 min progress update)
#
set -euo pipefail

REPO="mulle-cc/mulle-clang-project"
TAG="${MULLE_CLANG_PROJECT_TAG:?MULLE_CLANG_PROJECT_TAG must be set}"
BRANCH="${MULLE_CLANG_BRANCH:?MULLE_CLANG_BRANCH must be set}"
POLL_SECONDS="${MULLE_CI_POLL_SECONDS:-300}"

stamp() { date -u +%H:%M:%SZ; }

# The three deterministic codenames; golden-gate is beta and its brew tag may
# differ, so it is covered by the >=4 count check rather than an exact filename.
STABLE_CODENAMES=( sonoma sequoia tahoe )
MIN_BOTTLES=4

missing_bottles() {
   local assets m c e count
   assets="$(gh release view "${TAG}" --repo "${REPO}" --json assets -q '.assets[].name' 2>/dev/null || true)"
   m=""
   for c in "${STABLE_CODENAMES[@]}"; do
      e="mulle-clang-project-${TAG}.arm64_${c}.bottle.tar.gz"
      printf '%s\n' "${assets}" | grep -qxF "${e}" || m="${m} ${e}"
   done
   count="$(printf '%s\n' "${assets}" | grep -cE "mulle-clang-project-${TAG}\.arm64_.*\.bottle\.tar\.gz" || true)"
   if [ "${count}" -lt "${MIN_BOTTLES}" ]; then
      m="${m} <only ${count}/${MIN_BOTTLES} bottles present>"
   fi
   printf '%s' "${m# }"
}

echo "=== bottle CI: ${TAG} on ${BRANCH} (poll ${POLL_SECONDS}s) ==="

default_branch="$(gh api "repos/${REPO}" --jq '.defaultBranchRef.name // .default_branch')"
echo "repo default branch: ${default_branch}"
if [ "${default_branch}" != "${BRANCH}" ]; then
   echo "FAIL: default branch '${default_branch}' != release branch '${BRANCH}'." >&2
   echo "      workflow_dispatch is ONLY available from the DEFAULT branch." >&2
   echo "      Fix (confirm with the user first):" >&2
   echo "        gh repo edit ${REPO} --default-branch ${BRANCH}" >&2
   exit 1
fi

if [ -z "$(missing_bottles)" ]; then
   echo "[$(stamp)] OK: all expected bottle assets already present on ${TAG}"
   exit 0
fi

inflight_id() {
   gh run list --repo "${REPO}" --workflow "$1" --limit 20 \
      --json databaseId,status \
      -q '[.[] | select(.status != "completed")][0].databaseId // empty' 2>/dev/null || true
}

RUN_ID="$(inflight_id bottle.yml)"
if [ -z "${RUN_ID}" ]; then
   echo "[$(stamp)] --- dispatching bottle.yml ---"
   gh workflow run bottle.yml --repo "${REPO}" --ref "${BRANCH}" -f tag="${TAG}"
   sleep 10
   RUN_ID="$(gh run list --repo "${REPO}" --workflow bottle.yml --limit 1 \
              --json databaseId -q '.[0].databaseId')"
else
   echo "[$(stamp)] --- attaching to in-flight bottle.yml run ${RUN_ID} ---"
fi

deadline=$(( $(date +%s) + POLL_SECONDS ))
while [ "$(date +%s)" -lt "${deadline}" ]; do
   all_done=1
   failed=0
   state="$(gh run view "${RUN_ID}" --repo "${REPO}" --json status,conclusion \
              -q '.status + " " + (.conclusion // "-")' 2>/dev/null || echo 'unknown')"
   echo "[$(stamp)] bottle.yml #${RUN_ID}: ${state}"
   case "${state}" in
      completed\ success*) : ;;
      completed\ *)        failed=1 ;;
      *)                   all_done=0 ;;
   esac

   # show per-job progress so the snapshot is informative even mid-run
   gh run view "${RUN_ID}" --repo "${REPO}" --json jobs \
      -q '.jobs[] | "    " + .name + ": " + .status + " " + (.conclusion // "")' 2>/dev/null || true

   if [ "${all_done}" = 1 ]; then
      m="$(missing_bottles)"
      if [ -z "${m}" ]; then
         echo "[$(stamp)] OK: all expected bottle assets present on ${TAG}"
         exit 0
      fi
      echo "FAIL: bottle.yml finished but bottle assets are missing:${m}" >&2
      exit 1
   fi

   remaining=$(( deadline - $(date +%s) ))
   [ "${remaining}" -le 0 ] && break
   if [ "${remaining}" -lt 30 ]; then sleep "${remaining}"; else sleep 30; fi
done

m="$(missing_bottles)"
if [ -z "${m}" ]; then
   echo "[$(stamp)] OK: all expected bottle assets present on ${TAG}"
   exit 0
fi
if [ "${failed}" = 1 ]; then
   echo "FAIL: bottle.yml run failed; bottle assets still missing:${m}" >&2
   exit 1
fi
echo "[$(stamp)] STILL IN PROGRESS after ${POLL_SECONDS}s; bottle assets missing:${m}"
echo "Re-run this script to continue polling (in-flight runs are attached, not re-dispatched)."
exit 3