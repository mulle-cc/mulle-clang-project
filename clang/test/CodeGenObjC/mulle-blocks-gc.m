// RUN: %clang_cc1 -triple i386-apple-darwin10 -fobjc-runtime=mulle \
// RUN:   -fobjc-gc -fblocks -emit-llvm -o - %s | \
// RUN:   FileCheck --implicit-check-not=mulle_objc_object_call %s
// RUN: %clang_cc1 -triple i386-apple-darwin10 -fobjc-runtime=mulle \
// RUN:   -fobjc-gc-only -fblocks -emit-llvm -o - %s | \
// RUN:   FileCheck --implicit-check-not=mulle_objc_object_call %s
// RUN: %clang_cc1 -triple i386-apple-darwin10 \
// RUN:   -fobjc-runtime=macosx-fragile-10.5 -fobjc-gc -fblocks \
// RUN:   -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple i386-apple-darwin10 \
// RUN:   -fobjc-runtime=macosx-fragile-10.5 -fobjc-gc-only -fblocks \
// RUN:   -emit-llvm -o - %s | FileCheck %s

typedef void (^VoidBlock)(void);

@class Object;

static const struct mulle_clang_objccompilerinfo {
  unsigned int load_version;
  unsigned int runtime_version;
} __mulle_objc_objccompilerinfo = {19, (28 << 8)};

extern void consumeBlock(VoidBlock block);

void captureStrongObject(Object *object) {
  consumeBlock(^{ (void)object; });
}

void captureWeakByrefObject(Object *object) {
  __block __weak Object *weakObject = object;
  consumeBlock(^{ (void)weakObject; });
}

// GC and GC-only modes leave all object and weak ownership operations to
// BlocksRuntime, exactly as the Apple control runtime does. In particular,
// Mulle's MRC-only retain/release override must never run here.
// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_{{[0-9_]+}}o(
// CHECK: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_{{[0-9_]+}}o(
// CHECK: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)

// The byref helper copies and disposes its weak object payload using
// BLOCK_FIELD_IS_OBJECT | BLOCK_FIELD_IS_WEAK | BLOCK_BYREF_CALLER (147).
// CHECK-LABEL: define internal void @__Block_byref_object_copy_(
// CHECK: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 147)
// CHECK-LABEL: define internal void @__Block_byref_object_dispose_(
// CHECK: call void @_Block_object_dispose(ptr %{{.*}}, i32 147)

// The block owns the weak byref container using flag 24.
// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_{{[0-9_]+}}rw(
// CHECK: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 24)
// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_{{[0-9_]+}}rw(
// CHECK: call void @_Block_object_dispose(ptr %{{.*}}, i32 24)
