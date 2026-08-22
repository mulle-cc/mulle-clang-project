#!/bin/bash

set -e

cd "${MULLE_CLANG_CPACK_DIR}"

VERSION="${MULLE_CLANG_PROJECT_TAG}" RC= ./create-deb "trixie"
VERSION="${MULLE_CLANG_PROJECT_TAG}" RC= ./create-deb "bookworm"

# forky: build in docker (no VM needed, no sanitizers due to linux/scc.h removal)
echo "=== Building forky deb in docker ==="
mkdir -p /tmp/forky-output
docker run --rm --privileged --memory=32g --cpus=32 \
  -v "${MULLE_CLANG_CPACK_DIR}:/cpack:ro" \
  -v /tmp/forky-output:/output \
  debian:testing bash -c '
set -e
ln -s /usr/bin/env /usr/local/bin/sudo
apt-get update -qq
apt-get install -y -qq git cmake ninja-build clang wget python3 lsb-release file linux-libc-dev
mkdir -p /build && cd /build
cp -r /cpack mulle-clang-cpack
VERSION="'"${MULLE_CLANG_PROJECT_TAG}"'" RC="" NINJAFLAGS="" CMAKEFLAGS="-DLLVM_ENABLE_RUNTIMES=" \
  ./mulle-clang-cpack/package-build clean download build verpack
cp *.deb /output/
'
cp /tmp/forky-output/mulle-clang-${MULLE_CLANG_PROJECT_TAG}-forky-amd64.deb .
echo "forky deb ready"

