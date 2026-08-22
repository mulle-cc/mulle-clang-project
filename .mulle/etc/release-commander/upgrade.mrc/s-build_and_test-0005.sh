#!/bin/bash
set -e

: "${MULLE_CLANG_DIR:?set MULLE_CLANG_DIR}"

cd "${MULLE_CLANG_DIR}/mulle-clang-project"

# Use a fresh build dir — reusing old one causes check-builtins target conflicts
BUILD_DIR="${MULLE_CLANG_DIR}/build"

# Build clang
cmake --build "${BUILD_DIR}" --target clang -- -j"$(nproc)"

# Build lit helper tools (required for C test suite)
cmake --build "${BUILD_DIR}" --target FileCheck count not llvm-config -- -j"$(nproc)"

# Clang built-in C tests (C only, no C++)
"${BUILD_DIR}/bin/llvm-lit" "${MULLE_CLANG_DIR}/mulle-clang-project/clang/test/C" -j"$(nproc)"

echo "Build and C lit tests OK."

# Ensure mulle-clang symlink exists for mulle-sde tests
if [ ! -e "${BUILD_DIR}/bin/mulle-clang" ]; then
   ln -sf clang-22 "${BUILD_DIR}/bin/mulle-clang"
fi

# mulle-objc-runtime compiler tests (run via mulle-sde, not mulle-test run -l)
if [ -n "${MULLE_OBJC_RUNTIME_DIR}" ]; then
   echo ""
   echo "Running mulle-objc-runtime test-compiler (mulle-sde: PATH-based)..."
   cd "${MULLE_OBJC_RUNTIME_DIR}/test-compiler" && \
     PATH="${BUILD_DIR}/bin:${PATH}" mulle-sde test
else
   echo "Set MULLE_OBJC_RUNTIME_DIR to run runtime compiler tests."
fi

# C output rewriter tests
if [ -d "${MULLE_CLANG_DIR}/test-c-output" ]; then
   echo ""
   echo "Running test-c-output rewriter tests..."
   cd "${MULLE_CLANG_DIR}/test-c-output/runtime" && \
     PATH="${BUILD_DIR}/bin:${PATH}" mulle-sde craft 2>/dev/null || true
   cd "${MULLE_CLANG_DIR}/test-c-output" && \
     MULLE_CLANG="${BUILD_DIR}/bin/clang" bash run-test --skip-emit-deps
fi

echo ""
echo "All tests passed."