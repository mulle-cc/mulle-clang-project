#!/bin/bash

set -e

cd mulle-clang-cpack

VERSION="${MULLE_CLANG_PROJECT_TAG}" RC= ./create-deb "trixie"
VERSION="${MULLE_CLANG_PROJECT_TAG}" RC= ./create-deb "bookworm"

