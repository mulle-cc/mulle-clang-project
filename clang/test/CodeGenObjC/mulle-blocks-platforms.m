// RUN: %clang_cc1 -triple arm64-apple-macosx14.0 -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,NONCOFF,MULLE %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14.0 -fobjc-runtime=macosx-14.0 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,NONCOFF,CONTROL %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,NONCOFF,MULLE %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=gnustep-2.0 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,NONCOFF,CONTROL %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,NONCOFF,MULLE %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fobjc-runtime=gnustep-2.0 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,NONCOFF,CONTROL %s
// RUN: %clang_cc1 -triple powerpc64-unknown-linux-gnu -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,NONCOFF,MULLE %s
// RUN: %clang_cc1 -triple powerpc64-unknown-linux-gnu -fobjc-runtime=gnustep-2.0 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,NONCOFF,CONTROL %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,ILP32,NONCOFF,MULLE %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fobjc-runtime=gnustep-2.0 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,ILP32,NONCOFF,CONTROL %s
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,ILP32,NONCOFF,MULLE %s
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabihf -fobjc-runtime=gnustep-2.0 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,ILP32,NONCOFF,CONTROL %s
// RUN: %clang_cc1 -triple x86_64-unknown-freebsd13.0 -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,NONCOFF,MULLE %s
// RUN: %clang_cc1 -triple x86_64-unknown-freebsd13.0 -fobjc-runtime=gnustep-2.0 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,NONCOFF,CONTROL %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,COFF-DYNAMIC,MULLE %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fobjc-runtime=gnustep-2.0 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,COFF-DYNAMIC,CONTROL %s
// RUN: %clang_cc1 -triple i686-pc-windows-msvc -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,ILP32,COFF-DYNAMIC,MULLE %s
// RUN: %clang_cc1 -triple i686-pc-windows-msvc -fobjc-runtime=gnustep-2.0 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,ILP32,COFF-DYNAMIC,CONTROL %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,COFF-DYNAMIC,MULLE %s
// RUN: %clang_cc1 -triple x86_64-w64-windows-gnu -fobjc-runtime=gnustep-2.0 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,COFF-DYNAMIC,CONTROL %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc -fobjc-runtime=mulle -fblocks -static-libclosure -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,LP64,COFF-STATIC,MULLE %s

typedef int (^IntBlock)(int);

typedef struct Triple {
  long long first;
  long long second;
  long long third;
} Triple;

typedef void *NSObjectPointer __attribute__((NSObject));

@class Object;

static const struct mulle_clang_objccompilerinfo {
  unsigned int load_version;
  unsigned int runtime_version;
} __mulle_objc_objccompilerinfo = {19, (28 << 8)};

extern void consume(IntBlock block);

IntBlock globalIdentity = ^(int value) { return value; };

void captureMixed(Object *object, int base) {
  __block int counter = base;
  IntBlock inner = ^(int value) { return value + counter++; };
  IntBlock outer = ^(int value) { return object ? inner(value) : value; };
  consume(outer);
}

void captureByrefObject(Object *object) {
  __block Object *byrefObject = object;
  consume(^(int value) { return byrefObject ? value : 0; });
}

void captureNSObjectPointer(NSObjectPointer object) {
  consume(^(int value) { return object ? value : 0; });
}

Triple invokeAggregate(Triple value) {
  Triple (^identity)(Triple) = ^(Triple input) { return input; };
  return identity(value);
}

// The same source follows the standard Blocks ABI with the Mulle runtime and
// each platform's control Objective-C runtime.
// COMMON-DAG: @_NSConcreteGlobalBlock = external {{(dllimport |dso_local )?}}global ptr
// COMMON-DAG: @_NSConcreteStackBlock = external {{(dllimport |dso_local )?}}global ptr
// LP64-DAG: @globalIdentity ={{.*}} global ptr @__block_literal_global, align 8
// ILP32-DAG: @globalIdentity ={{.*}} global ptr @__block_literal_global, align 4

// NONCOFF-DAG: @__block_literal_global = internal constant {{.*}} ptr @_NSConcreteGlobalBlock

// COFF-DYNAMIC-DAG: @__block_literal_global = internal global {{.*}} ptr null
// COFF-DYNAMIC-DAG: @.block_isa_init_ptr = internal constant ptr @.block_isa_init, section ".CRT$XCLa"
// COFF-DYNAMIC: define internal void @.block_isa_init()
// COFF-DYNAMIC: store ptr @_NSConcreteGlobalBlock, ptr @__block_literal_global
// COFF-DYNAMIC: declare dllimport void @_Block_object_assign
// COFF-DYNAMIC: declare dllimport void @_Block_object_dispose

// COFF-STATIC: declare dso_local void @_Block_object_assign
// COFF-STATIC: declare dso_local void @_Block_object_dispose

// A direct object capture is the only ownership operation customized by the
// Mulle runtime. The control runtimes use the standard flag-3 operation.
// MULLE-LABEL: define linkonce_odr hidden void @{{.*copy_helper_block.*o.*b.*}}(
// MULLE: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 -1949542456, ptr %{{.*}})
// MULLE: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 7)
// MULLE-LABEL: define linkonce_odr hidden void @{{.*destroy_helper_block.*o.*b.*}}(
// MULLE: call void @_Block_object_dispose(ptr %{{.*}}, i32 7)
// MULLE: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 57337825, ptr %{{.*}})

// CONTROL-LABEL: define linkonce_odr hidden void @{{.*copy_helper_block.*o.*b.*}}(
// CONTROL: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// CONTROL: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 7)
// CONTROL-LABEL: define linkonce_odr hidden void @{{.*destroy_helper_block.*o.*b.*}}(
// CONTROL: call void @_Block_object_dispose(ptr %{{.*}}, i32 7)
// CONTROL: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)

// In MRC, __block object payloads are non-owning in both implementations.
// COMMON-LABEL: define internal void @__Block_byref_object_copy_(
// COMMON: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 131)
// COMMON-LABEL: define internal void @__Block_byref_object_dispose_(
// COMMON: call void @_Block_object_dispose(ptr %{{.*}}, i32 131)

// __attribute__((NSObject)) pointers take the same direct-object path as id.
// MULLE-LABEL: define linkonce_odr hidden void @{{.*copy_helper_block.*o.*}}(
// MULLE: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 -1949542456, ptr %{{.*}})
// CONTROL-LABEL: define linkonce_odr hidden void @{{.*copy_helper_block.*o.*}}(
// CONTROL: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)

// Aggregate invocation remains an indirect Blocks call on every target.
// COMMON-LABEL: define {{.*}} @invokeAggregate(
// COMMON: call {{.*}} %{{.*}}(ptr {{.*}}%{{.*}}, ptr {{.*}}%{{.*}})
