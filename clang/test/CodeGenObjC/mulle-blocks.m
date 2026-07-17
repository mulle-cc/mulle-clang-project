// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=mulle -fblocks -fobjc-gc -emit-llvm -o - %s | FileCheck --check-prefix=GC %s

typedef int (^IntBlock)(int);
typedef void (^VoidBlock)(void);

@class Object;

static const struct mulle_clang_objccompilerinfo {
  unsigned int load_version;
  unsigned int runtime_version;
} __mulle_objc_objccompilerinfo = {19, (28 << 8)};

extern void consumeIntBlock(IntBlock);
extern void consumeVoidBlock(VoidBlock);

IntBlock globalIncrement = ^(int value) { return value + 1; };

int invokeAdder(int base, int value) {
  IntBlock add = ^(int input) { return base + input; };
  return add(value);
}

void captureObject(Object *object) {
  IntBlock block = ^(int value) { return object ? value : 0; };
  consumeIntBlock(block);
}

void captureByrefObject(Object *object) {
  __block Object *byrefObject = object;
  VoidBlock block = ^{ (void)byrefObject; };
  consumeVoidBlock(block);
}

// CHECK: @_NSConcreteGlobalBlock = external global ptr
// CHECK: @[[GLOBAL_BLOCK:__block_literal_global.*]] = internal constant
// CHECK-SAME: ptr @_NSConcreteGlobalBlock
// CHECK: @_NSConcreteStackBlock = external global ptr
// CHECK: @[[SCALAR_DESCRIPTOR:"__block_descriptor_36_.*"]] = linkonce_odr hidden unnamed_addr constant
// CHECK-SAME: ptr null

// CHECK-LABEL: define i32 @invokeAdder(
// CHECK: store ptr @_NSConcreteStackBlock
// CHECK: store ptr @[[INVOKE:__invokeAdder_block_invoke]]
// CHECK: store ptr @[[SCALAR_DESCRIPTOR]]
// CHECK: call i32 %{{.*}}(ptr noundef %{{.*}}, i32 noundef %{{.*}})

// CHECK-LABEL: define internal i32 @__invokeAdder_block_invoke(
// CHECK: getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, i32 }>, ptr %{{.*}}, i32 0, i32 5
// CHECK: add nsw i32

// A copied Mulle Objective-C object must use Mulle message dispatch for its
// ownership operations. Apple's _Block_object_assign would call objc_retain,
// which cannot retain a Mulle object.
// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32o(
// CHECK: %blockcopy.src = load ptr
// CHECK: call ptr @mulle_objc_object_call(ptr %blockcopy.src, i32 -1949542456, ptr %blockcopy.src)
// CHECK: store ptr %blockcopy.src, ptr %{{.*}}
// CHECK-NOT: @_Block_object_assign
// CHECK: ret void

// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32o(
// CHECK: [[OBJECT_FIELD:%.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, ptr }>, ptr %{{.*}}, i32 0, i32 5
// CHECK: [[COPIED_OBJECT:%.*]] = load ptr, ptr [[OBJECT_FIELD]]
// CHECK: call ptr @mulle_objc_object_call(ptr [[COPIED_OBJECT]], i32 57337825, ptr [[COPIED_OBJECT]])
// CHECK-NOT: @_Block_object_dispose
// CHECK: ret void

// A __block object remains non-owning under MRC, matching Apple BlocksRuntime.
// BLOCK_BYREF_CALLER makes flag 131 copy the pointer without objc_retain.
// CHECK-LABEL: define internal void @__Block_byref_object_copy_(
// CHECK: %src-object = getelementptr
// CHECK: [[BYREF_OBJECT:%.*]] = load ptr, ptr %src-object
// CHECK: call void @_Block_object_assign(ptr %dest-object, ptr [[BYREF_OBJECT]], i32 131)
// CHECK-NOT: @mulle_objc_object_call
// CHECK: ret void

// CHECK-LABEL: define internal void @__Block_byref_object_dispose_(
// CHECK: %object = getelementptr
// CHECK: [[COPIED_BYREF_OBJECT:%.*]] = load ptr, ptr %object
// CHECK: call void @_Block_object_dispose(ptr [[COPIED_BYREF_OBJECT]], i32 131)
// CHECK-NOT: @mulle_objc_object_call
// CHECK: ret void

// Moving the byref container itself remains the Blocks runtime's job.
// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32r(
// CHECK: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 8)

// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32r(
// CHECK: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)

// Objective-C GC delegates object ownership back to BlocksRuntime. Mulle's
// retain/release override is intentionally restricted to manual reference
// counting.
// GC-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32o(
// GC-NOT: @mulle_objc_object_call
// GC: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// GC-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32o(
// GC-NOT: @mulle_objc_object_call
// GC: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)
