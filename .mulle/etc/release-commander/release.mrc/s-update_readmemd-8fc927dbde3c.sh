#!/bin/bash
set -e

LOAD_VERSION=$(grep -oP 'COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION\s+\K\d+' clang/include/clang/Basic/ObjCRuntime.h)
echo "Load version: ${LOAD_VERSION}"

if ! grep -q "v0\.[0-9]* or better (load version ${LOAD_VERSION})" README.md; then
   echo "README.md needs updating" >&2
   exit 1
fi

echo "OK"