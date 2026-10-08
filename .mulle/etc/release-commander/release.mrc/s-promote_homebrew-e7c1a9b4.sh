#!/bin/bash
#
# Finalize the mulle-clang-project Homebrew formula for MULLE_CLANG_PROJECT_TAG and
# publish it to the stable taps. See i-promote_homebrew-e7c1a9b4.md.
#
# CONTEXT
#   Bottle building is now automated by .github/workflows/bottle.yml (the
#   "Build macOS bottles via GitHub Actions" task). That workflow uploads arm64
#   bottles to the GitHub release but does NOT touch any formula. The old manual
#   flow filled the `bottle do` block in the prerelease tap by hand; that tap is
#   now stale. This task therefore RECONSTRUCTS the finalized formula from the
#   release's ACTUAL assets:
#
#     version                       <- MULLE_CLANG_PROJECT_TAG
#     url / source sha256           <- the release source tarball (downloaded+hashed)
#     vendor runtime-load-version   <- MULLE_OBJC_RUNTIME_LOAD_VERSION
#     bottle do block               <- one line per *.bottle.tar.gz asset on the
#                                      release, hashed from the downloaded bottle
#
#   The base formula body (class, install, caveats, test) is taken from the current
#   stable tap (authoritative). Publishing is idempotent: if a target's remote
#   content already equals the generated formula, no commit is made.
#
set -euo pipefail

REPO="mulle-cc/mulle-clang-project"
TAG="${MULLE_CLANG_PROJECT_TAG:?MULLE_CLANG_PROJECT_TAG must be set}"
LOAD="${MULLE_OBJC_RUNTIME_LOAD_VERSION:?MULLE_OBJC_RUNTIME_LOAD_VERSION must be set}"
TEMPLATE_REPO="${TEMPLATE_REPO:-mulle-objc/homebrew-software}"
TEMPLATE_BRANCH="${TEMPLATE_BRANCH:-master}"
TARGET_REPOS="${TARGET_REPOS:-mulle-objc/homebrew-software mulle-kybernetik/homebrew-software}"
FORMULA="mulle-clang-project.rb"

work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

sha_of() {
   if command -v sha256sum >/dev/null 2>&1
   then sha256sum "$1" | awk '{print $1}'
   else shasum -a 256 -b "$1" | awk '{print $1}'
   fi
}

has_key() {
   local k
   for k in "${KEYS[@]}"; do [ "${k}" = "$1" ] && return 0; done
   return 1
}

echo "=== finalize + publish ${FORMULA} ${TAG} (load ${LOAD}) ==="

# --- 0. base formula from the current stable tap --------------------------------
echo "--- fetching base formula from ${TEMPLATE_REPO}@${TEMPLATE_BRANCH} ---"
gh api "repos/${TEMPLATE_REPO}/contents/${FORMULA}?ref=${TEMPLATE_BRANCH}" --jq '.content' \
   | base64 --decode > "${work}/${FORMULA}.base"
[ -s "${work}/${FORMULA}.base" ] || { echo "FAIL: empty base formula" >&2; exit 1; }
grep -q '^class MulleClangProject < Formula' "${work}/${FORMULA}.base" \
   || { echo "FAIL: base formula is not a MulleClangProject formula" >&2; exit 1; }

# --- 1. source tarball sha256 ---------------------------------------------------
src_url="https://github.com/${REPO}/archive/refs/tags/${TAG}.tar.gz"
echo "--- downloading source tarball ${src_url} ---"
curl -sSL -o "${work}/source.tar.gz" "${src_url}"
[ -s "${work}/source.tar.gz" ] || { echo "FAIL: empty source download" >&2; exit 1; }
gzip -t "${work}/source.tar.gz" 2>/dev/null || { echo "FAIL: source download is not gzip" >&2; exit 1; }
src_sha="$(sha_of "${work}/source.tar.gz")"
echo "source sha256: ${src_sha}"

# --- 2. bottle assets actually present on the release ---------------------------
echo "--- listing bottle assets on ${TAG} ---"
mapfile -t bottles < <(gh release view "${TAG}" --repo "${REPO}" --json assets \
   -q '.assets[].name' | grep -E "^${FORMULA%.rb}-${TAG}\.arm64_.*\.bottle\.tar\.gz$" | sort)
if [ "${#bottles[@]}" -eq 0 ]; then
   echo "FAIL: no arm64 bottles for ${TAG} on ${REPO}. Run the bottle CI task first." >&2
   exit 1
fi
printf '  %s\n' "${bottles[@]}"

KEYS=()
for b in "${bottles[@]}"; do
   k="${b#${FORMULA%.rb}-${TAG}.}"        # arm64_sonoma.bottle.tar.gz
   k="${k%.bottle.tar.gz}"                 # arm64_sonoma
   [ "${k#arm64_}" != "${k}" ] || { echo "FAIL: cannot parse codename from '${b}'" >&2; exit 1; }
   KEYS+=("${k}")
done

# Apple-silicon users on these must get a bottle (not a source build).
for req in arm64_sonoma arm64_sequoia arm64_tahoe; do
   has_key "${req}" || { echo "FAIL: no '${req}' bottle for ${TAG} on the release" >&2; exit 1; }
done
has_key arm64_golden_gate \
   || echo "WARN: no arm64_golden_gate bottle (beta) on ${TAG}; publishing without it"

echo "--- downloading + hashing ${#bottles[@]} bottle(s) ---"
for b in "${bottles[@]}"; do
   gh release download "${TAG}" --repo "${REPO}" --pattern "${b}" --dir "${work}"
done

{
   echo "  bottle do"
   echo "    root_url \"https://github.com/${REPO}/releases/download/${TAG}/\""
   for b in "${bottles[@]}"; do
      k="${b#${FORMULA%.rb}-${TAG}.}"; k="${k%.bottle.tar.gz}"
      sha="$(sha_of "${work}/${b}")"
      printf '    sha256 cellar: :any, %s: "%s"\n' "${k}" "${sha}"
   done
   echo "  end"
} > "${work}/bottle-block.txt"
echo "--- bottle block ---"
cat "${work}/bottle-block.txt"

# --- 3. rewrite the formula -----------------------------------------------------
cp "${work}/${FORMULA}.base" "${work}/${FORMULA}"
ruby - "${work}/${FORMULA}" "${work}/bottle-block.txt" "${TAG}" "${src_url}" "${src_sha}" "${LOAD}" <<'RUBY'
formula, block_file, tag, url, src_sha, load = ARGV
s = File.read(formula)

s = s.sub(/^(\s*version\s+")[^"]*(")/m)     { "#{$1}#{tag}#{$2}" }
s = s.sub(/^(\s*url\s+")[^"]*(")/m)         { "#{$1}#{url}#{$2}" }
# only the source sha256 (immediately after url); bottle shas are replaced as a block
s = s.sub(/^(\s*sha256\s+")[0-9a-f]*(")/m)  { "#{$1}#{src_sha}#{$2}" }

# vendor string: "mulle-clang X.Y.Z.W (runtime-load-version: N)"
s = s.gsub(/(mulle-clang\s+)[0-9][0-9.]*(\s+\(runtime-load-version)/) { "#{$1}#{tag}#{$2}" }
s = s.gsub(/(runtime-load-version:\s*)[0-9][0-9]*/) { "#{$1}#{load}" }

# replace the whole bottle do ... end block with the generated one
block = File.read(block_file).rstrip
re = /^[ \t]*bottle do\b.*?^[ \t]*end[ \t]*$/m
abort "FATAL: no 'bottle do ... end' block in base formula" unless s =~ re
s = s.sub(re, block)

File.write(formula, s)
puts "rewrote #{formula}"
RUBY

ruby -c "${work}/${FORMULA}"

# --- 4. publish to each stable tap (idempotent) ---------------------------------
for repo in ${TARGET_REPOS}; do
   path="repos/${repo}/contents/${FORMULA}"
   remote_raw="$(gh api "${path}?ref=${TEMPLATE_BRANCH}" 2>/dev/null || true)"
   remote_sha="$(printf '%s' "${remote_raw}" \
                   | jq -r 'if type=="object" and has("sha") then .sha else empty end' 2>/dev/null || true)"
   local_blob="$(git hash-object "${work}/${FORMULA}")"

   if [ -n "${remote_sha}" ] && [ "${remote_sha}" = "${local_blob}" ]; then
      echo "up to date: ${repo}/${FORMULA} already equals the generated formula"
      continue
   fi

   canpush="$(gh api "repos/${repo}" --jq '.permissions.push' 2>/dev/null || echo false)"
   if [ "${canpush}" != "true" ]; then
      echo "FAIL: no push permission on ${repo}" >&2
      exit 1
   fi

   content_b64="$(base64 --wrap=0 < "${work}/${FORMULA}")"
   msg="${FORMULA%.rb} ${TAG}"
   if [ -n "${remote_sha}" ]; then
      gh api -X PUT "${path}" -f message="${msg}" -f content="${content_b64}" \
         -f branch="${TEMPLATE_BRANCH}" -f sha="${remote_sha}" >/dev/null
   else
      gh api -X PUT "${path}" -f message="${msg}" -f content="${content_b64}" \
         -f branch="${TEMPLATE_BRANCH}" >/dev/null
   fi
   echo "PUBLISHED ${FORMULA} ${TAG} to ${repo}@${TEMPLATE_BRANCH}"
done

echo "OK: finalized + published ${FORMULA} ${TAG}"