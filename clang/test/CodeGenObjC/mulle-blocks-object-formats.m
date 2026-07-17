// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple arm64-apple-macosx14.0 -fobjc-runtime=mulle \
// RUN:   -fblocks -emit-obj -o %t.macho %S/mulle-blocks-platforms.m
// RUN: llvm-readobj --file-headers --symbols %t.macho | \
// RUN:   FileCheck --check-prefix=MACHO %s

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fobjc-runtime=mulle \
// RUN:   -fblocks -emit-obj -o %t.elf %S/mulle-blocks-platforms.m
// RUN: llvm-readobj --file-headers --symbols %t.elf | \
// RUN:   FileCheck --check-prefix=ELF %s

// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc -fobjc-runtime=mulle \
// RUN:   -fblocks -emit-obj -o %t.dynamic.obj %S/mulle-blocks-platforms.m
// RUN: llvm-readobj --file-headers --symbols %t.dynamic.obj | \
// RUN:   FileCheck --check-prefix=COFF-DYNAMIC %s

// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc -fobjc-runtime=mulle \
// RUN:   -fblocks -static-libclosure -emit-obj -o %t.static.obj \
// RUN:   %S/mulle-blocks-platforms.m
// RUN: llvm-readobj --file-headers --symbols %t.static.obj | \
// RUN:   FileCheck --check-prefix=COFF-STATIC %s

// MACHO: Format: Mach-O arm64
// MACHO: Arch: aarch64
// MACHO-DAG: Name: __NSConcreteGlobalBlock
// MACHO-DAG: Name: __NSConcreteStackBlock
// MACHO-DAG: Name: __Block_object_assign
// MACHO-DAG: Name: __Block_object_dispose
// MACHO-DAG: Name: _mulle_objc_object_call

// ELF: Format: elf64-littleaarch64
// ELF: Arch: aarch64
// ELF-DAG: Name: _NSConcreteGlobalBlock
// ELF-DAG: Name: _NSConcreteStackBlock
// ELF-DAG: Name: _Block_object_assign
// ELF-DAG: Name: _Block_object_dispose
// ELF-DAG: Name: mulle_objc_object_call

// A dynamically linked Windows BlocksRuntime is represented with COFF import
// symbols. The Mulle runtime call follows the existing static-friendly Mulle
// ABI and remains an ordinary undefined symbol.
// COFF-DYNAMIC: Format: COFF-ARM64
// COFF-DYNAMIC: Arch: aarch64
// COFF-DYNAMIC-DAG: Name: __imp__NSConcreteGlobalBlock
// COFF-DYNAMIC-DAG: Name: __imp__NSConcreteStackBlock
// COFF-DYNAMIC-DAG: Name: __imp__Block_object_assign
// COFF-DYNAMIC-DAG: Name: __imp__Block_object_dispose
// COFF-DYNAMIC-DAG: Name: mulle_objc_object_call

// -static-libclosure suppresses dllimport exactly as required by a static
// BlocksRuntime.lib.
// COFF-STATIC: Format: COFF-ARM64
// COFF-STATIC: Arch: aarch64
// COFF-STATIC-DAG: Name: _NSConcreteGlobalBlock
// COFF-STATIC-DAG: Name: _NSConcreteStackBlock
// COFF-STATIC-DAG: Name: _Block_object_assign
// COFF-STATIC-DAG: Name: _Block_object_dispose
// COFF-STATIC-DAG: Name: mulle_objc_object_call
// COFF-STATIC-NOT: Name: __imp_
