#!/bin/bash
# Write GitHub release notes for mulle-clang compiler 22.1.x
# Fill body of gh release with verified, authoritative facts. Never guess.

set -e

REPO="mulle-cc/mulle-clang-project"
VERSION="${MULLE_CLANG_PROJECT_TAG:?must be set}"

echo "=== Release notes for ${VERSION} ==="

# --- 1. Verify release exists ---
gh release view "${VERSION}" --repo "${REPO}" --json name -q '.name' > /dev/null || {
   echo "ERROR: release ${VERSION} does not exist on ${REPO}" >&2
   exit 1
}

# --- 2. Authoritative facts ---
# min required runtime version: authoritative source is
# MULLE_OBJC_RUNTIME_MIN_VERSION (the compiler's minimum compatible runtime),
# NOT MULLE_RELEASE_TAG (the parent objc release version).
# Format: strip a trailing ".0" so "0.30.0" is shown as "0.30" (reference style)
RUNTIME_VER="${MULLE_OBJC_RUNTIME_MIN_VERSION:-unknown}"
RUNTIME_VER="${RUNTIME_VER%.0}"
echo "Required mulle-objc-runtime: ${RUNTIME_VER}"
echo "  (from MULLE_OBJC_RUNTIME_MIN_VERSION: ${MULLE_OBJC_RUNTIME_MIN_VERSION:-unset})"

# load version
LOAD_VERSION=$(grep -oP 'COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION\s+\K\d+' \
   "${PWD}/clang/include/clang/Basic/ObjCRuntime.h" 2>/dev/null || true)
echo "Load version: ${LOAD_VERSION}"

# assets present on release
ASSETS="$(gh release view "${VERSION}" --repo "${REPO}" --json assets -q '.assets[].name' 2>/dev/null || true)"
echo "Assets:"
echo "${ASSETS}"

# Debian/Ubuntu .deb codenames actually present (amd64 + arm64)
DEB_CODENAMES="$(echo "${ASSETS}" | sed -nE \
   's/^mulle-clang-[0-9.]+-([A-Za-z0-9.]+)-(amd64|arm64)\.deb$/\1/p' | sort -u)"
echo "Debian/Ubuntu codenames: $(echo ${DEB_CODENAMES} | tr '\n' ' ')"

# distro debs present (summary bullet keeps the historical Debian-only look)
DISTROS="$(echo "${ASSETS}" | grep -oE 'mulle-clang-[^-]+-[a-z]+-amd64\.deb' | sed -E 's/.*-([a-z]+)-amd64\.deb/\1/' | tr '\n' ' ')"
echo "Distro debs (summary): ${DISTROS}"

# macOS bottle tags actually present, e.g. sonoma, arm64_sequoia
BOTTLE_TAGS="$(echo "${ASSETS}" | sed -nE \
   's/^mulle-clang-project-[0-9.]+\.([A-Za-z0-9_]+)\.bottle\.tar\.gz$/\1/p' | sort -u)"
echo "macOS bottle tags: $(echo ${BOTTLE_TAGS} | tr '\n' ' ')"

# windows tgz present?
HAS_WINDOWS=$(echo "${ASSETS}" | grep -c 'windows' || true)
echo "Windows tgz present: ${HAS_WINDOWS}"

# --- 3. Determine previous release for changelog link ---
PREV=$(gh release list --repo "${REPO}" --json tagName --jq '.[1].tagName' 2>/dev/null || true)
echo "Previous release: ${PREV}"

# Mulle's own patches — a curated, verifiable summary of the mulle-specific
# additions carried in this fork (see the squash commit on top of the vanilla
# llvmorg base). Keep this list consistent with what the source actually ships.
MULLE_PATCHES="\
Mulle Objective-C runtime backend, meta-ABI packing and tiny-struct forwarding
tagged-pointer/fast-call/thread-affine modes (-fobjc-tps/-fcs/-tao + __MULLE_OBJC_* macros)
mulle_confined_loop / mulle_confined_return attributes with Sema checking
extended ObjC property attributes (@forward, container, relationship, observable, adder/remover, serializable)
Objective-C grammar extensions (@mixin, class variables, @signature/@invocation/@dependency)
emitters: -emit-nh, --mulle-objc-emit-c rewriter, dependency emitter, portable-call
libclang additions (CXType_ObjCProtocol, property-attr flags, adder/remover introspection)
distribution script suite (cmake-ninja/make/msbuild, install-prerequisites, migrate-to-next-release)"

# --- 4. Build the Installation section (carried over from the 22.1.8.7 release page) ---
# distro codename -> human label (only rows whose .deb is actually present are shown)
declare -A DISTRO_LABEL=(
   [bookworm]="Debian 12 (bookworm)"
   [trixie]="Debian 13 (trixie)"
   [forky]="Debian 14 (forky)"
   [ubuntu22.04]="Ubuntu 22.04 (jammy)"
   [ubuntu24.04]="Ubuntu 24.04 (noble)"
   [ubuntu26.04]="Ubuntu 26.04"
)

DEB_TABLE="| Distro | codename |
|---|---|"
for codename in ${DEB_CODENAMES}
do
   label="${DISTRO_LABEL[${codename}]:-${codename}}"
   DEB_TABLE="${DEB_TABLE}
| ${label} | \`${codename}\` |"
done

BOTTLE_LIST=""
for tag in ${BOTTLE_TAGS}
do
   [ -n "${BOTTLE_LIST}" ] && BOTTLE_LIST="${BOTTLE_LIST}, "
   BOTTLE_LIST="${BOTTLE_LIST}\`${tag}\`"
done
if [ -n "${BOTTLE_LIST}" ]
then
   MACOS_BOTTLES="Bottles are attached to this release for ${BOTTLE_LIST}."
else
   MACOS_BOTTLES="No bottles are attached to this release; \`brew install\` builds from source."
fi

INSTALL_TEMPLATE="$(cat <<'INSTALL_EOF'

## Installation

### Debian / Ubuntu (`.deb`)

@@DEB_TABLE@@

`amd64` and `arm64` are provided. Pick your codename and architecture, then:

```bash
version=@@VERSION@@
codename=bookworm                                  # see table above
arch=$(dpkg --print-architecture)                  # amd64 or arm64

curl -LO "https://github.com/mulle-cc/mulle-clang-project/releases/download/${version}/mulle-clang-${version}-${codename}-${arch}.deb"
sudo apt-get install -y "./mulle-clang-${version}-${codename}-${arch}.deb"
```

The package installs the toolchain under `/opt/mulle-clang-project/${version}/`
and puts `mulle-clang`, `mulle-nm` and `mulle-scan-build` on your `PATH`
(via `/usr/bin` symlinks).

> Note: the **forky** build ships **without compiler runtimes** (no ASAN/etc.).

### macOS (Homebrew)

```bash
brew install mulle-objc/software/mulle-clang-project
```

@@MACOS_BOTTLES@@

### Verify

```bash
mulle-clang --version
```

### From source

```bash
git clone --branch @@VERSION@@ --depth 1 https://github.com/mulle-cc/mulle-clang-project.git
cd mulle-clang-project
PREFIX="$HOME/opt/mulle-clang-project/@@VERSION@@" NAME=@@VERSION@@ clang/bin/cmake-ninja.linux
```

INSTALL_EOF
)"

INSTALL_SECTION="${INSTALL_TEMPLATE//@@VERSION@@/${VERSION}}"
INSTALL_SECTION="${INSTALL_SECTION//@@DEB_TABLE@@/${DEB_TABLE}}"
INSTALL_SECTION="${INSTALL_SECTION//@@MACOS_BOTTLES@@/${MACOS_BOTTLES}}"

# --- 5. Compose body ---
BODY="This is a feature and bug fix release

* requires mulle-objc-runtime ${RUNTIME_VER} or better
* requires load version ${LOAD_VERSION}
* provides prebuilt .deb packages for ${DISTROS}(Debian era)
* forky builds have no compiler runtimes (e.g. ASAN)
${INSTALL_SECTION}
Mulle-specific patches in this fork continue to provide:

${MULLE_PATCHES}"

if [ "${HAS_WINDOWS}" -ne 0 ]; then
   BODY="${BODY}

mulle-clang-project-windows-${VERSION}.tgz is a cross-compiler to build windows executables on linux.

cd / && tar xfz ~/Downloads/mulle-clang-project-windows-${VERSION}.tgz
cd /opt/mulle-clang-project-windows && ln -sf ${VERSION} latest"
fi

BODY="${BODY}

If you want pre-built binaries or a bottle for some platform, put up an \"issue\".

**Full Changelog**: https://github.com/${REPO}/compare/${PREV}...${VERSION}"

# --- 6. Set body (use a file: the body contains backticks and ${...}) ---
NOTES_FILE="$(mktemp)"
trap 'rm -f "${NOTES_FILE}"' EXIT

printf '%s\n' "${BODY}" > "${NOTES_FILE}"

echo "Setting release body..."
gh release edit "${VERSION}" --repo "${REPO}" --notes-file "${NOTES_FILE}"

# --- 7. Verify ---
echo "=== Verifying release body ==="
SET_BODY="$(gh release view "${VERSION}" --repo "${REPO}" --json body -q '.body')"
echo "${SET_BODY}"
echo "${SET_BODY}" | grep -q 'requires mulle-objc-runtime' || { echo "ERROR: body missing runtime requirement" >&2; exit 1; }
echo "${SET_BODY}" | grep -q "load version ${LOAD_VERSION}" || { echo "ERROR: body missing load version" >&2; exit 1; }
echo "${SET_BODY}" | grep -q '## Installation' || { echo "ERROR: body missing Installation section" >&2; exit 1; }
echo "${SET_BODY}" | grep -q 'Mulle-specific patches' || { echo "ERROR: body missing mulle patches section" >&2; exit 1; }
echo "OK: release notes verified"
