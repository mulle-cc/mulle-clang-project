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
# min required runtime version: parent release tag is passed as MULLE_RELEASE_TAG
# Format: strip a trailing ".0" so "0.30.0" is shown as "0.30" (matches reference style)
RUNTIME_VER="${MULLE_RELEASE_TAG:-unknown}"
RUNTIME_VER="${RUNTIME_VER%.0}"
echo "Required mulle-objc-runtime: ${RUNTIME_VER}"
echo "  (from parent MULLE_RELEASE_TAG: ${MULLE_RELEASE_TAG:-unset})"

# load version
LOAD_VERSION=$(grep -oP 'COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION\s+\K\d+' \
   "${PWD}/clang/include/clang/Basic/ObjCRuntime.h" 2>/dev/null || true)
echo "Load version: ${LOAD_VERSION}"

# assets present on release
ASSETS="$(gh release view "${VERSION}" --repo "${REPO}" --json assets -q '.assets[].name' 2>/dev/null || true)"
echo "Assets:"
echo "${ASSETS}"

# distro debs present
DISTROS="$(echo "${ASSETS}" | grep -oE 'mulle-clang-[^-]+-[a-z]+-amd64\.deb' | sed -E 's/.*-([a-z]+)-amd64\.deb/\1/' | tr '\n' ' ')"
echo "Distro debs: ${DISTROS}"

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

# --- 4. Compose body ---
BODY="This is a feature and bug fix release

* requires mulle-objc-runtime ${RUNTIME_VER} or better
* requires load version ${LOAD_VERSION}
* provides prebuilt .deb packages for ${DISTROS}(Debian era)
* forky builds have no compiler runtimes (e.g. ASAN)

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

# --- 5. Set body ---
echo "Setting release body..."
gh release edit "${VERSION}" --repo "${REPO}" --notes "${BODY}"

# --- 6. Verify ---
echo "=== Verifying release body ==="
SET_BODY="$(gh release view "${VERSION}" --repo "${REPO}" --json body -q '.body')"
echo "${SET_BODY}"
echo "${SET_BODY}" | grep -q 'requires mulle-objc-runtime' || { echo "ERROR: body missing runtime requirement" >&2; exit 1; }
echo "${SET_BODY}" | grep -q "load version ${LOAD_VERSION}" || { echo "ERROR: body missing load version" >&2; exit 1; }
echo "${SET_BODY}" | grep -q 'Mulle-specific patches' || { echo "ERROR: body missing mulle patches section" >&2; exit 1; }
echo "OK: release notes verified"