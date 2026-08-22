Build the new compiler and run the test suite.

## Configure

Always use a **fresh** build directory in the **new** outer repo. The old
`build/` dir has stale paths and will cause `check-builtins` target conflicts.

```bash
cd /path/to/mulle-clang-<new-version>
rm -rf build
mkdir build && cd build
cmake -G Ninja \
  -DLLVM_BUILD_LLVM_DYLIB=ON \
  -DLLVM_BUILD_TESTS=OFF \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
  -DLLVM_ENABLE_RUNTIMES="compiler-rt" \
  -DLLVM_LINK_LLVM_DYLIB=ON \
  -DLLVM_TARGETS_TO_BUILD="X86;ARM;AArch64;WebAssembly" \
  -DCLANG_VENDOR=mulle \
  -DCMAKE_BUILD_TYPE=Release \
  ../mulle-clang-project/llvm
```

> **Note:** `compiler-rt` must be in `LLVM_ENABLE_RUNTIMES`, not
> `LLVM_ENABLE_PROJECTS`. This was a llvm 22 change.

## Build clang

```bash
cd build && ninja clang
```

Expect ~3,800 targets. On a modern machine this takes ~30-60 minutes.

## Build lit helper tools

```bash
cmake --build build --target FileCheck count not llvm-config -- -j$(nproc)
```

## Clang C lit tests

```bash
build/bin/llvm-lit mulle-clang-project/clang/test/C -j$(nproc)
# Expected: 141 passed (llvm 22)
```

Missing optional tools (`yaml2obj`, `llvm-profdata`, etc.) produce notes but
are not fatal for the C test suite.

## mulle-objc-runtime compiler tests (mulle-sde)

The test framework uses `mulle-sde`, which resolves `mulle-clang` from `PATH`.
The build/bin must have a `mulle-clang` symlink:

```bash
cd build/bin
ln -sf clang-22 mulle-clang

# Verify it's picked up
cd ${MULLE_OBJC_RUNTIME_DIR}/test-compiler
PATH="$(pwd)/../../build/bin:$PATH" mulle-sde exec which mulle-clang
# Expected: /path/to/build/bin/mulle-clang

# Run tests
PATH="/path/to/build/bin:$PATH" mulle-sde test
# Expected: ~199 passed (llvm 22)
```

> Setting `MULLE_CLANG` env var does **not** work — `mulle-sde` ignores it.
> Only `PATH` works.

## C output rewriter tests (test-c-output)

The `run-test` script uses the `MULLE_CLANG` env var (unlike mulle-sde).

**First**, ensure the runtime subproject is built:
```bash
cd test-c-output/runtime
PATH="/path/to/build/bin:$PATH" mulle-sde craft
```

**Then** run the tests:
```bash
cd test-c-output
MULLE_CLANG=/path/to/build/bin/clang bash run-test
# Expected: 156 passed (39 tests × 4 compilers: mulle-clang, gcc, clang, tcc)
# Use --skip-emit-deps to skip emit-deps subtests
```

The `run` script (single-file) uses `CLANG` env var instead:
```bash
CLANG=/path/to/build/bin/clang bash run t01_pure_c.m
```

## Same-major patch bumps

For same-major patch bumps (e.g. 22.1.2 → 22.1.8), the build typically
succeeds with **zero source changes needed**. The llvm API is stable within
a major release series.

## Historical: build errors from llvm 21 → 22

These API changes in llvm 22 affected our mulle code and required manual
fixes before build. Kept for reference for future major jumps.

1. **`getTagDeclType` → `getTypeDeclType`** (renamed)
   Files: `CGCall.cpp`, `CGObjC.cpp`, `CGObjCMulleRuntime.cpp`, `SemaDeclObjC.cpp`

2. **`getTypeDeclType(TypedefDecl*)` / `getTypeDeclType(TagDecl*)` deleted**
   File: `ASTContext.h`. Fix: cast to `(TypeDecl *)`. Beware: regex cast
   also mangles multi-arg `getTypeDeclType(ElaboratedTypeKeyword::None, ...)`
   calls — those were originally `getElaboratedType(...)`.

3. **`LangOptions::Optimize` / `OptimizeSize` removed**
   Files: `CGDebugInfo.cpp`, `CGObjCMulleRuntime.cpp`
   - `getLangOpts().Optimize` → `getCodeGenOpts().OptimizationLevel == 0`
   - `getLangOpts().OptimizeSize` → `getCodeGenOpts().OptimizeSize`

4. **`EmitLifetimeEnd(Value*&, Value*&)` / `EmitLifetimeStart(TypeSize, Value*)` sig changed**
   New: `EmitLifetimeStart(Value*)` returns `bool`, `EmitLifetimeEnd(Value*)`
   Files: `CGObjC.cpp`, `CGObjCMulleRuntime.cpp`, `CGObjCRuntime.h`

5. **`PredefinedTypeIDs` enum overflow** (`ASTBitCodes.h`)
   Our `ObjCProtocol` builtin type ID exceeded `NUM_PREDEF_TYPE_IDS = 514`.
   Fix: bump to 600.

6. **`TypedefNameDecl::setTypeForDecl` deleted** (`SemaDecl.cpp`)
   Fix: use `setModedTypeSourceInfo(New->getTypeSourceInfo(), type)` instead.