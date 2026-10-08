#!/bin/bash
#
# Stage the mulle-clang-project formula for MULLE_CLANG_PROJECT_TAG onto the release
# branch, so the CI bottle workflow builds bottles for the RIGHT version.
#
# WHY THIS EXISTS
#   .github/workflows/bottle.yml builds from the in-repo staged formula
#   (.github/workflows/mulle-clang-project.rb) via a faked local tap. But
#   bottle-build.sh does NOT verify that the formula version matches the requested
#   tag -- a stale staged formula silently produces mis-versioned bottles and
#   uploads them to the release. This task is the guard: it rewrites the staged
#   formula to the release tag BEFORE the bottle CI runs.
#
# WHAT IT DOES
#   * base formula body (class/install/caveats/test) <- current stable tap (authoritative)
#   * version / url                                <- MULLE_CLANG_PROJECT_TAG
#   * source sha256                                <- the actual release tarball (hashed)
#   * vendor runtime-load-version                  <- MULLE_OBJC_RUNTIME_LOAD_VERSION
#   * bottle do block                              <- REMOVED (CI builds fresh bottles;
#                                                     a stale block would mislead brew)
#   * PUT to GITHUB_REPO@MULLE_CLANG_BRANCH via the Contents API (no clone) and then
#     re-read it to confirm the staged version really equals the tag.
#
# Idempotent: if the remote already equals the generated file, no commit is made
# (but the version assertion still runs).
#
set -euo pipefail

REPO="${GITHUB_REPO:-mulle-cc/mulle-clang-project}"
TAG="${MULLE_CLANG_PROJECT_TAG:?MULLE_CLANG_PROJECT_TAG must be set}"
BRANCH="${MULLE_CLANG_BRANCH:?MULLE_CLANG_BRANCH must be set}"
LOAD="${MULLE_OBJC_RUNTIME_LOAD_VERSION:?MULLE_OBJC_RUNTIME_LOAD_VERSION must be set}"
TEMPLATE_REPO="${TEMPLATE_REPO:-mulle-objc/homebrew-software}"
TEMPLATE_BRANCH="${TEMPLATE_BRANCH:-master}"
FORMULA="mulle-clang-project.rb"
FORMULA_IN_REPO="${FORMULA_IN_REPO:-.github/workflows/mulle-clang-project.rb}"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

sha_of() {
   if command -v sha256sum >/dev/null 2>&1
   then sha256sum "$1" | awk '{print $1}'
   else shasum -a 256 -b "$1" | awk '{print $1}'
   fi
}

echo "=== stage ${FORMULA} ${TAG} -> ${REPO}@${BRANCH} (load ${LOAD}) ==="

# --- 0. base formula from the current stable tap --------------------------------
echo "--- fetching base formula from ${TEMPLATE_REPO}@${TEMPLATE_BRANCH} ---"
gh api "repos/${TEMPLATE_REPO}/contents/${FORMULA}?ref=${TEMPLATE_BRANCH}" --jq '.content' \
   | base64 --decode > "${work}/${FORMULA}"
[ -s "${work}/${FORMULA}" ] || { echo "FAIL: empty base formula" >&2; exit 1; }
grep -q '^class MulleClangProject < Formula' "${work}/${FORMULA}" \
   || { echo "FAIL: base formula is not a MulleClangProject formula" >&2; exit 1; }

# --- 1. source tarball sha256 ---------------------------------------------------
src_url="https://github.com/${REPO}/archive/refs/tags/${TAG}.tar.gz"
echo "--- downloading source tarball ${src_url} ---"
curl -sSL -o "${work}/source.tar.gz" "${src_url}"
[ -s "${work}/source.tar.gz" ] || { echo "FAIL: empty source download" >&2; exit 1; }
gzip -t "${work}/source.tar.gz" 2>/dev/null || { echo "FAIL: source download is not gzip" >&2; exit 1; }
src_sha="$(sha_of "${work}/source.tar.gz")"
echo "source sha256: ${src_sha}"

# --- 2. rewrite formula for the tag ---------------------------------------------
ruby - "${work}/${FORMULA}" "${TAG}" "${src_url}" "${src_sha}" "${LOAD}" <<'RUBY'
formula, tag, url, src_sha, load = ARGV
s = File.read(formula)

s = s.sub(/^(\s*version\s+")[^"]*(")/m)     { "#{$1}#{tag}#{$2}" }
s = s.sub(/^(\s*url\s+")[^"]*(")/m)         { "#{$1}#{url}#{$2}" }
s = s.sub(/^(\s*sha256\s+")[0-9a-f]*(")/m)  { "#{$1}#{src_sha}#{$2}" }

# vendor string: "mulle-clang X.Y.Z.W (runtime-load-version: N)"
s = s.gsub(/(mulle-clang\s+)[0-9][0-9.]*(\s+\(runtime-load-version)/) { "#{$1}#{tag}#{$2}" }
s = s.gsub(/(runtime-load-version:\s*)[0-9][0-9]*/) { "#{$1}#{load}" }

# Drop the bottle do ... end block: CI builds fresh bottles for this tag.
# Match only a real block at the start of a line -- bare "bottle do" text also
# appears in prose comments of the stable-tap formula.
abort "FATAL: no 'bottle do ... end' block found to remove" unless s =~ /^[ \t]*bottle do\b/m
s = s.sub(/^[ \t]*bottle do\b.*?^[ \t]*end[ \t]*\n?/m, "")
abort "FATAL: 'bottle do ... end' block still present after removal" if s =~ /^[ \t]*bottle do\b/m

File.write(formula, s)
puts "rewrote #{formula}"
RUBY

ruby -c "${work}/${FORMULA}"
grep -q "version \"${TAG}\"" "${work}/${FORMULA}" \
   || { echo "FAIL: rewritten formula is not at version ${TAG}" >&2; exit 1; }

# --- 3. stage onto the release branch (idempotent) ------------------------------
path="repos/${REPO}/contents/${FORMULA_IN_REPO}"
remote_raw="$(gh api "${path}?ref=${BRANCH}" 2>/dev/null || true)"
remote_sha="$(printf '%s' "${remote_raw}" \
                | jq -r 'if type=="object" and has("sha") then .sha else empty end' 2>/dev/null || true)"
local_blob="$(git hash-object "${work}/${FORMULA}")"

if [ -n "${remote_sha}" ] && [ "${remote_sha}" = "${local_blob}" ]; then
   echo "up to date: ${FORMULA_IN_REPO} on ${REPO}@${BRANCH} already equals the staged formula"
else
   content_b64="$(base64 --wrap=0 < "${work}/${FORMULA}")"
   msg="ci: stage ${TAG} formula for bottle build"
   if [ -n "${remote_sha}" ]; then
      gh api -X PUT "${path}" -f message="${msg}" -f content="${content_b64}" \
         -f branch="${BRANCH}" -f sha="${remote_sha}" --jq '.commit.sha'
   else
      gh api -X PUT "${path}" -f message="${msg}" -f content="${content_b64}" \
         -f branch="${BRANCH}" --jq '.commit.sha'
   fi
   echo "STAGED ${FORMULA_IN_REPO} ${TAG} on ${REPO}@${BRANCH}"
fi

# --- 4. verify what is actually on the branch now -------------------------------
echo "--- verifying staged formula on ${BRANCH} ---"
staged_ver="$(gh api "${path}?ref=${BRANCH}" --jq '.content' | base64 --decode \
   | grep -E '^[[:space:]]*version[[:space:]]+"' | head -1 \
   | sed -E 's/^[[:space:]]*version[[:space:]]+"([^"]+)".*/\1/')"
echo "staged version on branch: ${staged_ver}"
[ "${staged_ver}" = "${TAG}" ] \
   || { echo "FAIL: staged formula version '${staged_ver}' != '${TAG}'" >&2; exit 1; }

gh api "repos/${REPO}/contents/.github/workflows/bottle-build.sh?ref=${BRANCH}" \
   --jq '.name' >/dev/null \
   || { echo "FAIL: bottle-build.sh missing on ${BRANCH}" >&2; exit 1; }

echo "OK: staged ${FORMULA} ${TAG} on ${REPO}@${BRANCH}"