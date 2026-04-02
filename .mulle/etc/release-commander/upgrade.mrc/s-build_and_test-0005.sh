#!/bin/bash
set -e

: "${MULLE_CLANG_DIR:?set MULLE_CLANG_DIR}"
: "${MULLE_OBJC_RUNTIME_DIR:?set MULLE_OBJC_RUNTIME_DIR}"

cd "${MULLE_CLANG_DIR}/mulle-clang-project"

# Use a fresh build dir — reusing old one causes check-builtins target conflicts
BUILD_DIR="${MULLE_CLANG_DIR}/build22"

# Build clang
cmake --build "${BUILD_DIR}" --target clang -- -j"$(nproc)"

# Build lit helper tools (required for C test suite)
cmake --build "${BUILD_DIR}" --target FileCheck count not llvm-config -- -j"$(nproc)"

# psutil required for per-test timeout support in lit
pip install psutil -q 2>/dev/null || true

# Clang built-in C tests (C only, no C++)
"${BUILD_DIR}/bin/llvm-lit" "${MULLE_CLANG_DIR}/mulle-clang-project/clang/test/C" -j"$(nproc)"

echo "Build and C lit tests OK."
echo "Run manually:"
echo "  cd ${MULLE_OBJC_RUNTIME_DIR}/test-compiler && MULLE_CLANG=${BUILD_DIR}/bin/clang mulle-test run -l"
echo "  mulle-test --dir-name test-compiler-runtime --project-dialect objc"
echo ""
echo "Running rewriter tests..."
: "${MULLE_REWRITER_TEST_DIR:?set MULLE_REWRITER_TEST_DIR}"
cd "${MULLE_REWRITER_TEST_DIR}" && \
  CLANG="${BUILD_DIR}/bin/clang" mulle-sde test run
