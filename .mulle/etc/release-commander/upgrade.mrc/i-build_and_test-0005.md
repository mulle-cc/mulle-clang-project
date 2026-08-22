Build the new compiler and run the test suite.

## Configure

Use the provided cmake script — do NOT run cmake directly.
The existing `build/` dir was configured for the old version — always use a
fresh build dir (reusing causes `check-builtins` target conflicts):

```bash
cd /home/src/srcL/mulle-clang-22.1.2
mkdir build22
cd build22
../mulle-clang-project/clang/bin/cmake-ninja.linux
```

**llvm 22 cmake change:** `compiler-rt` must move from `LLVM_ENABLE_PROJECTS`
to `LLVM_ENABLE_RUNTIMES` — leaving it in `PROJECTS` causes a `check-builtins`
duplicate target error. Update `cmake-ninja.linux`:

```
-DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
-DLLVM_ENABLE_RUNTIMES="compiler-rt;libcxxabi;libcxx;libunwind" \
```

## Build clang

```bash
cd build22 && ninja clang
```

## Known llvm 22 build errors to fix before build succeeds

These are API changes in llvm 22 that affect our mulle code:

1. **`getTagDeclType` → `getTypeDeclType`** (renamed)
   Files: `CGCall.cpp`, `CGObjC.cpp`, `CGObjCMulleRuntime.cpp`, `SemaDeclObjC.cpp`
   Fix: `sed -i 's/getTagDeclType/getTypeDeclType/g' <files>`

2. **`getTypeDeclType(TypedefDecl*)` and `getTypeDeclType(TagDecl*)` deleted**
   File: `ASTContext.h`, `CGCall.cpp`, `CGObjC.cpp`, `CGObjCMulleRuntime.cpp`, `SemaDeclObjC.cpp`
   Fix: cast argument to `(TypeDecl *)` — but beware the regex cast will also
   mangle multi-arg calls like `getTypeDeclType(ElaboratedTypeKeyword::None, ...)`.
   Those were originally `getElaboratedType(...)` calls — restore them manually.

3. **`LangOptions::Optimize` / `OptimizeSize` removed**
   Files: `CGDebugInfo.cpp`, `CGObjCMulleRuntime.cpp`
   Fix:
   - `getLangOpts().Optimize` → `getCodeGenOpts().OptimizationLevel == 0` (note: inverted)
   - `getLangOpts().OptimizeSize` → `getCodeGenOpts().OptimizeSize`

4. **`EmitLifetimeEnd(Value*&, Value*&)` / `EmitLifetimeStart(TypeSize, Value*)` signature changed**
   New signature: `EmitLifetimeStart(Value*)` returns `bool`, `EmitLifetimeEnd(Value*)`
   Files: `CGObjC.cpp`, `CGObjCMulleRuntime.cpp`, `CGObjCRuntime.h`
   Fix: change `CGObjCRuntimeLifetimeMarker::SizeV` from `llvm::Value*` to `bool`,
   update all call sites, replace `nullptr` assignments with `false`.

5. **`PredefinedTypeIDs` enum overflow** (`ASTBitCodes.h`)
   Our `ObjCProtocol` builtin type ID exceeds `NUM_PREDEF_TYPE_IDS = 514`.
   Fix: bump to 600.

6. **`TypedefNameDecl::setTypeForDecl` deleted** (`SemaDecl.cpp`)
   Fix: use `setModedTypeSourceInfo(New->getTypeSourceInfo(), type)` instead
   (same pattern as the SEL typedef just below it).

## Running clang C lit tests

`llvm-lit` needs several helper binaries built first. Build them all at once:

```bash
cmake --build build22 --target FileCheck count not llvm-config -- -j$(nproc)
pip install psutil   # required for per-test timeout support
```

Then run:
```bash
build22/bin/llvm-lit mulle-clang-project/clang/test/C -j$(nproc)
# Expected: 141 passed (llvm 22)
```

Missing optional tools (`yaml2obj`, `llvm-profdata`, etc.) produce notes but
are not fatal for the C test suite.

## mulle-objc-runtime compiler tests

**Note:** `mulle-test run` uses the compiler from the mulle-sde environment,
not the `MULLE_CLANG` env var. To test with the new compiler, either install
it to `/usr/local/bin/mulle-clang` or temporarily set the sde compiler env.

```bash
cd ${MULLE_OBJC_RUNTIME_DIR}/test-compiler
MULLE_CLANG=/path/to/build22/bin/clang mulle-test run -l
# Expected: 4/5 pass
# metaabi/unwanted-promotion fails with both old and new compiler:
#   fatal error: 'mulle-objc-runtime/mulle-objc-runtime.h' file not found
# Bug in that test's CMakeLists.txt — missing target_include_directories for
# functions.c. Not related to the upgrade.
```

## mulle-objc-runtime compiler-runtime tests

```bash
cd ${MULLE_OBJC_RUNTIME_DIR}
mulle-test --dir-name test-compiler-runtime --project-dialect objc
```

## C output rewriter tests

```bash
cd /home/src/srcL/mulle-clang-21.1.8/test-c-output
CLANG=/path/to/build22/bin/clang bash run
# Or via mulle-sde recraft (exit 0 = all pass)
cd runtime && CLANG=/path/to/build22/bin/clang mulle-sde recraft
```
