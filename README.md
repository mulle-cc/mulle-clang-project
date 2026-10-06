[![mulle-cc](https://avatars.githubusercontent.com/u/70896078?s=400&u=35dc427f9bb75c52c1d973cc3f895d2d18a461ae&v=4)](https://github.com/mulle-cc)

# mulle-clang

This is an Objective-C compiler based on clang 22.1.8, written for the
[mulle-objc](//www.mulle-kybernetik.com/weblog/2015/mulle_objc_a_new_objective_c_.html)
runtime. It corresponds to mulle-objc-runtime v0.30 or better (load version 21).

> See [README.txt](README.txt) for more information about clang

The compiler can be used to:

1. compile Objective-C code for **mulle-objc**
2. compile C code

It is not recommended to use it for C++, since that is not tested.
It is not recommended to use it for other Objective-C runtimes, since there
have been some changes, that affect other runtimes.


## Operation

The compiler compiles Objective-C source for the **mulle-objc** runtime by
default. When compiling for **mulle-objc** the compiler will use the
[meta-ABI](//www.mulle-kybernetik.com/weblog/2015/mulle_objc_meta_call_convention.html)
for all Objective-C method calls. C function calls use the platform convention.
The resultant `.o` files are linkable like any other compiled C code.


## Language features

mulle-objc keeps Objective-C's core and makes deliberate additions and
subtractions. The [De Re mulle-objc](//www.mulle-kybernetik.com/de-re-mulle-objc)
developer guide covers them in full; this is the index.

### Additions

| Feature | What it adds | Guide |
|---------|--------------|-------|
| `@mixin` | a protocol that ships default implementations (formerly "protocolclass") | [Mixins](//www.mulle-kybernetik.com/de-re-mulle-objc/mixin.html) |
| `@dependency` | declares that a category loads after another, and silences the redeclaration warning | [Organizing a Project](//www.mulle-kybernetik.com/de-re-mulle-objc/project.html) |
| `@method_implementation` | aliases one selector to another method, or to a C function | [@method_implementation](//www.mulle-kybernetik.com/de-re-mulle-objc/method-implementation.html) |
| `@signature` / `@invocation` | builds and replays a method invocation as an object (the block replacement) | [Invocations](//www.mulle-kybernetik.com/de-re-mulle-objc/invocations.html) |
| `@defs` | exposes a class's ivar layout as a C struct | [Instance Variables](//www.mulle-kybernetik.com/de-re-mulle-objc/ivars.html) |
| `NS_ENUM_TABLE` | gives enum values names you can print and parse | [C Interop and Types](//www.mulle-kybernetik.com/de-re-mulle-objc/c-interop.html) |
| Thread-Affine Objects (TAO) | the runtime flags cross-thread use of an object (`-fobjc-tao`) | [Thread-Affine Objects](//www.mulle-kybernetik.com/de-re-mulle-objc/tao.html) |
| Always Autorelease Mode | an ARC-like subset for `.aam` files (see [AAM](#aam)) | |

### Removals

| Feature | Instead | Guide |
|---------|---------|-------|
| ARC | manual reference counting | [Memory Management](//www.mulle-kybernetik.com/de-re-mulle-objc/memory.html) |
| Blocks (`^`) | `NSInvocation` via `@invocation` | [Porting](//www.mulle-kybernetik.com/de-re-mulle-objc/porting.html) |
| Dot syntax (`obj.prop`) | `[obj prop]` / `[obj setProp:x]` | [Porting](//www.mulle-kybernetik.com/de-re-mulle-objc/porting.html) |
| `@synchronized` | `mulle_thread_mutex_t`, or `NSLock` | [Locks](//www.mulle-kybernetik.com/de-re-mulle-objc/locks.html) |
| `weak` / `strong` | `retain`/`copy`, and `assign` for back-references | [Properties](//www.mulle-kybernetik.com/de-re-mulle-objc/property.html) |
| Atomic properties | properties aren't atomic; lock around access | |
| Lightweight generics (`NSArray<NSString *>`) | `id` and protocols, check at runtime | [Porting](//www.mulle-kybernetik.com/de-re-mulle-objc/porting.html) |
| Class extensions (`@interface Foo ()`) | declare in the main `@interface`, or a named category | |
| Subscripting (`array[0]`) | `[array objectAtIndex:0]` | [Porting](//www.mulle-kybernetik.com/de-re-mulle-objc/porting.html) |
| `@import` / modules | `#import` | |
| Objective-C++ | bridge across a C boundary | [Porting](//www.mulle-kybernetik.com/de-re-mulle-objc/porting.html) |
| `nullable` / `nonnull` | Objective-C is nullable by design | |

The full comparison lives in [Differences to Objective-C 2.0](//www.mulle-kybernetik.com/de-re-mulle-objc/differences.html).

### Property attributes

The familiar `assign`, `retain`, `copy`, `readonly` and `nonatomic` work as
expected, and mulle-objc adds several attributes of its own.
[De Re @property](//www.mulle-kybernetik.com/de-re-property) is the full
reference.

| Attribute | What it adds | Guide |
|-----------|--------------|-------|
| `class` | the property lives on the class object, not the instances | [class](//www.mulle-kybernetik.com/de-re-property/class.html) |
| `container` | a collection property; adds `addTo<Prop>:` / `removeFrom<Prop>:` | [container](//www.mulle-kybernetik.com/de-re-property/container.html) |
| `dynamic` | no instance variable or accessors are generated | [dynamic](//www.mulle-kybernetik.com/de-re-property/dynamic.html) |
| `forward` | accessors aren't synthesized; they're handed off to another receiver | [forward](//www.mulle-kybernetik.com/de-re-property/forward.html) |
| `observable` | calls `willChange` before the value changes | [observable](//www.mulle-kybernetik.com/de-re-property/observable.html) |
| `serializable` / `nonserializable` | marks the property for (or out of) archiving | [serializable](//www.mulle-kybernetik.com/de-re-property/serializable.html) |

`atomic`, `weak` and `strong` are the property attributes mulle-objc leaves out
(see the removals table above).


## ObjC to C Rewriter

The compiler can rewrite Objective-C source to portable C via `--mulle-objc-emit-c`.
This is used to produce C output that can be compiled by any C compiler targeting
platforms without a native clang port.

```bash
mulle-clang -fobjc-tao --mulle-objc-emit-c foo.m -o foo.c
```

Relevant flags for rewriter output:

| Flag                          | Description
|-------------------------------|--------------------------------------
| `--mulle-objc-emit-c`         | Rewrite ObjC source to C
| `--mulle-objc-portable-call`  | Emit `mulle_metaabi_object_call` macros instead of direct calls (cross-arch portable)
| `--mulle-objc-no-asm-names`   | Omit `__asm__` symbol renaming (for C compilers that don't support it)
| `-fobjc-encode-no-offsets`    | Strip byte offsets from type encodings in rewriter output


## AAM

The compiler has a special mode called AAM. This changes the Objective-C
language in the following ways:

1. There is a transformation done on selector names

    | Name          | Transformed Name
    |---------------|---------------------
    | `alloc`       | `instantiate`
    | `new`         | `instantiatedObject`
    | `copy`        | `immutableInstance`
    | `mutableCopy` | `mutableInstance`
2. You can not access instance variables directly, but must use properties (or methods)
3. You can not do explicit memory management (like `-dealloc`, `-autorelease`,
`-release`, `-retain`, `-retainCount` etc.)

The transformed methods will return objects that are autoreleased. Hence the
name of the mode. The net effect is, that you have a mode that is ARC-like, yet
understandable and much simpler.

Your ARC code may not run in AAM, but AAM code should run in ARC with no
problems. If you can't do something in AAM, put it in a category in regular
Objective-C style.

The compiler handles `.aam` files, which enables AAM ("Always Autoreleased
Mode").

## Additional Compiler options and defined macros

| Name                             | Compiler                 | Default | Description
|----------------------------------|--------------------------|---------|--------------------
| `__MULLE_OBJC__`                 | -                        | -       | Compiling for mulle-objc
| `__MULLE_OBJC_UNIVERSEID__`      | -fobjc-universename=name | -       | id of the universe, or 0 for default | universe
| `__MULLE_OBJC_UNIVERSENAME__`    | -fobjc-universename=name | -       | name of the universe, or NULL for default | universe
| `__OBJC_CLASS__`                 | -                        | -       | name of the class thats currently being compiled
| `__OBJC_CATEGORY__`              | -                        | -       | name of the category that's currently being compiled
| `__MULLE_OBJC_CLASSID__`         | -                        | -       | classid of the class thats currently being compiled
| `__MULLE_OBJC_CATEGORYID__`      | -                        | -       | uniqueid of the category thats currently being compiled

The following compiler flags control mulle-objc code generation:

| Flag                              | Description
|-----------------------------------|--------------------------------------
| `-fobjc-encode-no-offsets`        | Strip byte offsets from ObjC method type encodings (for cross-arch portability)
| `--mulle-objc-no-asm-names`       | Disable `__asm__` symbol renaming (for C compilers that don't support it)
| `--mulle-objc-portable-call`      | Emit `mulle_metaabi_object_call` macro calls in rewriter output for portable cross-arch C
| `--mulle-objc-emit-deps[=<dir>]`  | Emit a `.deps.inc` sidecar file for each compiled source (see below)
| `-emit-nh`                        | Emit a nuobjc `.nh` interface file describing ObjC classes, protocols, and C functions (see below)
| `-Wmulle-method-implementation`   | Warn when a category implements a method already declared in another category (see `@dependency`)


### `--mulle-objc-emit-deps` — dependency sidecar files

When this flag is set, the compiler writes a `<source>.deps.inc` file alongside
each object file (or into the directory given by `=<dir>`). The file contains
one initializer line per `@implementation` / `@implementation Foo(Cat)` found in
the translation unit:

```c
// src/Foo.m
      { @selector( Foo), MULLE_OBJC_NO_CATEGORYID },      // 752dd620;Foo;;
      { @selector( Foo), @selector( Extra) },      // 752dd620;Foo;1a593efb;Extra
```

These fragments can then be `#include`d into `objc-deps.inc` (the dependency
list for the runtime).


### `-emit-nh` — nuobjc interface file generation

A standalone action (like `-c` or `-S`) that parses an ObjC header and emits a
`.nh` interface file for the `mulle-nuobjc` transpiler:

```bash
mulle-clang -emit-nh -o MulleObjC.nh -x objective-c -fobjc-tao \
    -isystem dependency/Debug/include \
    MulleObjC.imports.h
```

The output describes classes (with superclass, protocols, methods), protocols,
and vetted C functions (value-only types, no pointers). Methods are written in
nuobjc dot-call syntax. See `SPEC-EMIT-NH.md` for full details.


The following table represents option pairs, that logically exclude each other.
Either one is always defined.

| Name                    | Compiler        | Default | Description
|-------------------------|-----------------|---------|--------------------
| `__MULLE_OBJC_AAM__`    | .aam file       | -       | AAM is enabled
| `__MULLE_OBJC_NO_AAM__` | .m file         | -       | AAM is not enabled
|  &nbsp;                 | &nbsp;          | &nbsp;  |
| `__MULLE_OBJC_TPS__`    | -fobjc-tps      | YES     | TPS (tagged pointer support) is enabled
| `__MULLE_OBJC_NO_TPS__` | -fno-objc-tps   | NO      | TPS is not enabled
| &nbsp;                  | &nbsp;          | &nbsp;  |
| `__MULLE_OBJC_FCS__`    | -fobjc-fcs      | YES     | FCS fast method/class support is enabled
| `__MULLE_OBJC_NO_FCS__` | -fno-objc-fcs   | NO      | FCS is not enabled
| &nbsp;                  | &nbsp;          | &nbsp;  |
| `__MULLE_OBJC_TAO__`    | -fobjc-tao      | -       | TAO (thread affine objects) is enabled
| `__MULLE_OBJC_NO_TAO__` | -fno-objc-tao   | -       | TAO is not enabled

The default is `-fobjc-tao` for `-O0` builds and `-fno-objc-tao` for optimized
builds.


## Macros used in Code Generation


The compiler output can be tweaked with the following preprocessor macros.
All macros must be defined with a simple integer, no expressions. All of them
are optional, unless indicated otherwise. The runtime, the Objective-C
Foundation on top of the runtime and the user application, will define them.


| Name                                  | Description
|---------------------------------------|--------------------------------------
| `MULLE_OBJC_RUNTIME_VERSION_MAJOR`    | Major of version of the runtime
| `MULLE_OBJC_RUNTIME_VERSION_MINOR`    | Minor of version of the runtime
| `MULLE_OBJC_RUNTIME_VERSION_PATCH`    | Patch of version of the runtime
| `MULLE_OBJC_FOUNDATION_VERSION_MAJOR` | Major of version of the Foundation
| `MULLE_OBJC_FOUNDATION_VERSION_MINOR` | Minor of version of the Foundation
| `MULLE_OBJC_FOUNDATION_VERSION_PATCH` | Patch of version of the Foundation
| `MULLE_OBJC_USER_VERSION_MAJOR`       | User supplied major of version
| `MULLE_OBJC_USER_VERSION_MINOR`       | User supplied minor of version
| `MULLE_OBJC_USER_VERSION_PATCH`       | User supplied patch of version. All these version information values will be stored in the emitted object file.
| `MULLE_OBJC_FASTCLASSHASH_0`          | First unique ID of a fast class
| ...                                   | ...
| `MULLE_OBJC_FASTCLASSHASH_63`         | Last unique ID of a fast class


## Functions used in Code Generation

These are the runtime functions used for method calling, retain/release
management, class lookup and exception handling. They are defined in the
runtime.

### All

| Function                        | Memo
|---------------------------------|---------
| `mulle_objc_exception_tryenter` | `@throw`
| `mulle_objc_exception_tryexit`  | `@finally`
| `mulle_objc_exception_extract`  | `@catch`
| `mulle_objc_exception_match`    | `@catch`



### Inlining method calls

With `-fobjc-inline-method-calls=[1-5]` you can control the level of inlining.
If you don't specify it, the optimization level is mapped to a inlining
setting. (See table below)

`cmake` sets `-O3` as the optimization level, for Release builds, though you
may want to prefer `-O2` or `-Oz` for Objective-C code.


| Inlining         | -fobjc-inline-method-calls | Optimization Level (if -fobjc-inline-method-calls is unset)
|------------------|----------------------------|------------------------
| No inlining      | 1                          | -O0
| Minimal inlining | 2                          | -O1/-Os
| Partial inlining | 3                          | -O2/-Oz
| Default inlining | 4                          | -O3/-O4
| Full inlining    | 5                          | -O5+


Also note, that if you combine -O0 with -fobjc-inline-method-calls, there
won't be any inlining, since -O0 prohibits it.

### No inlining

| Function                                            | Memo
|-----------------------------------------------------|-------------
| `mulle_objc_object_call`                            | `[self foo:bar]`
| `mulle_objc_object_call_super`                      | `[super foo:bar]`
| &nbsp;                                              |
| `mulle_objc_object_lookup_infraclass_nofail`        | `[Foo ...` for methods
| `mulle_objc_object_lookup_infraclass_nofast_nofail` | `__MULLE_OBJC_NO_FCS__`
| `mulle_objc_global_lookup_infraclass_nofail`        | `[Foo ...` for functions
| `mulle_objc_global_lookup_infraclass_nofast_nofail` | `__MULLE_OBJC_NO_FCS__`


### Minimal inlining

Like "No inlining" but one function is replaced with a minimally inlining
version:

| Function                                            | Memo
|-----------------------------------------------------|-------------
| `mulle_objc_object_call_inline_minimal`             | `[self foo:bar]`


### Partial inlining

Like "No inlining" but four functions are replaced with four inlining
functions, and two new inlining functions are used:

| Function                                                   | Memo
|------------------------------------------------------------|-------------
| `mulle_objc_object_call_inline_partial`                    | `[self foo:bar]`
| `mulle_objc_object_call_super_inline_partial`              | `[super foo:bar]`
| &nbsp;                                                     |
| `mulle_objc_object_lookup_infraclass_inline_nofail`        | `[Foo ...` for methods
| `mulle_objc_global_lookup_infraclass_inline_nofail`        |  `__MULLE_OBJC_NO_FCS__`
| `mulle_objc_object_lookup_infraclass_inline_nofail_nofast` | `[Foo ...` for functions
| `mulle_objc_global_lookup_infraclass_inline_nofail_nofast` | `__MULLE_OBJC_NO_FCS__`
| `mulle_objc_object_retain_inline`                          | `[foo retain]`
| `mulle_objc_object_release_inline`                         | `[foo release]`


The partial inlining code, tries to eliminate some constant setup cost by
moving it to the compilation. This can be good for multiple calls involving
the same object.


### Default inlining

Like "Partial inlining", but two functions are replaced with more inlined
functions:

| Function                                            | Memo
|-----------------------------------------------------|-------------
| `mulle_objc_object_call_inline`                     | `[self foo:bar]`
| `mulle_objc_object_call_super_inline`               | `[super foo:bar]`

The default inline code checks the cache for an immediate hit, then slower
shared coded is used for further cache lookup and cache misses.

### Full inlining

Like "Default inlining", but two functions are replaced with fully inlined
functions:

| Function                                            | Memo
|-----------------------------------------------------|-------------
| `mulle_objc_object_call_inline_full`                | `[self foo:bar]`
| `mulle_objc_object_call_super_inline_full`          | `[super foo:bar]`

The full inline code checks the cache for a hit, then slower
shared coded is used for cache misses.


## Build

You can use the original [llvm project instructions](#Getting-the-Source-Code-and-Building-LLVM)
to build the project. But do checkout [cmake-ninja](clang/bin/cmake-ninja) for
a small build script, that sets important build values.

If you are building inside a fresh system, try [install-prerequisites](clang/bin/install-prerequisites)
that can install all prerequisites for a macos or debian based system.


Afterwards head on over to [mulle-objc](//github.com/mulle-objc) to get the
runtime libraries.


## Author

[Nat!](//www.mulle-kybernetik.com/weblog) for
[Codeon GmbH](//www.codeon.de)

Original README.md follows
----

# The LLVM Compiler Infrastructure

[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/llvm/llvm-project/badge)](https://securityscorecards.dev/viewer/?uri=github.com/llvm/llvm-project)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8273/badge)](https://www.bestpractices.dev/projects/8273)
[![libc++](https://github.com/llvm/llvm-project/actions/workflows/libcxx-build-and-test.yaml/badge.svg?branch=main&event=schedule)](https://github.com/llvm/llvm-project/actions/workflows/libcxx-build-and-test.yaml?query=event%3Aschedule)

Welcome to the LLVM project!

This repository contains the source code for LLVM, a toolkit for the
construction of highly optimized compilers, optimizers, and run-time
environments.

The LLVM project has multiple components. The core of the project is
itself called "LLVM". This contains all of the tools, libraries, and header
files needed to process intermediate representations and convert them into
object files. Tools include an assembler, disassembler, bitcode analyzer, and
bitcode optimizer.

C-like languages use the [Clang](https://clang.llvm.org/) frontend. This
component compiles C, C++, Objective-C, and Objective-C++ code into LLVM bitcode
-- and from there into object files, using LLVM.

Other components include:
the [libc++ C++ standard library](https://libcxx.llvm.org),
the [LLD linker](https://lld.llvm.org), and more.

## Getting the Source Code and Building LLVM

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm)
page for information on building and running LLVM.

For information on how to contribute to the LLVM project, please take a look at
the [Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide.

## Getting in touch

Join the [LLVM Discourse forums](https://discourse.llvm.org/), [Discord
chat](https://discord.gg/xS7Z362),
[LLVM Office Hours](https://llvm.org/docs/GettingInvolved.html#office-hours) or
[Regular sync-ups](https://llvm.org/docs/GettingInvolved.html#online-sync-ups).

The LLVM project has adopted a [code of conduct](https://llvm.org/docs/CodeOfConduct.html) for
participants to all modes of communication within the project.
