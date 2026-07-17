// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=mulle -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,MULLE %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=macosx-10.15 -fblocks -emit-llvm -o - %s | FileCheck --check-prefixes=COMMON,APPLE %s

typedef int (^IntBlock)(int);
typedef void (^VoidBlock)(void);

typedef struct Triple {
  long long first;
  long long second;
  long long third;
} Triple;

@class Object;

static const struct mulle_clang_objccompilerinfo {
  unsigned int load_version;
  unsigned int runtime_version;
} __mulle_objc_objccompilerinfo = {19, (28 << 8)};

extern void consumeIntBlock(IntBlock);
extern void consumeVoidBlock(VoidBlock);
extern void *_Block_copy(const void *block);
extern void _Block_release(const void *block);

IntBlock globalIdentity = ^(int value) { return value; };

int captureScalars(int base, _Bool enabled, long long bias, int value) {
  IntBlock block = ^(int input) {
    return enabled ? base + input + (int)bias : -1;
  };
  return block(value);
}

int mutateByref(void) {
  __block int value = 40;
  VoidBlock block = ^{ value += 2; };
  block();
  return value;
}

void captureNested(int base) {
  IntBlock inner = ^(int value) { return base + value; };
  IntBlock outer = ^(int value) { return inner(value) + 1; };
  consumeIntBlock(outer);
}

void captureObjectAndBlock(Object *object, int base) {
  IntBlock inner = ^(int value) { return base + value; };
  IntBlock outer = ^(int value) { return object ? inner(value) : 0; };
  consumeIntBlock(outer);
}

void captureTwoObjects(Object *first, Object *second) {
  IntBlock block = ^(int value) {
    return first ? value : (second ? -value : 0);
  };
  consumeIntBlock(block);
}

void captureNilObject(void) {
  Object *object = (Object *)0;
  VoidBlock block = ^{ (void)object; };
  consumeVoidBlock(block);
}

void captureByrefObject(Object *object) {
  __block Object *byrefObject = object;
  VoidBlock block = ^{ (void)byrefObject; };
  consumeVoidBlock(block);
}

Triple addTriples(Triple left, Triple right) {
  Triple (^block)(Triple) = ^(Triple value) {
    Triple result = {value.first + left.first, value.second + left.second,
                     value.third + left.third};
    return result;
  };
  return block(right);
}

void copyAndRelease(VoidBlock block) {
  VoidBlock copy = (VoidBlock)_Block_copy((const void *)block);
  _Block_release((const void *)copy);
}

// Both runtimes use the public Blocks ABI for block objects, invocation,
// nested blocks, and byref storage. The final descriptor field is deliberately
// runtime-specific: Apple emits an encoded ownership layout while Mulle leaves
// it null because Mulle object ownership is handled by the copy/dispose helpers.
// COMMON: @_NSConcreteGlobalBlock = external global ptr
// COMMON: @__block_literal_global = internal constant
// COMMON-SAME: ptr @_NSConcreteGlobalBlock, i32 1342177280
// COMMON: @_NSConcreteStackBlock = external global ptr

// COMMON: @{{.*__block_descriptor_45_.*}} = linkonce_odr hidden unnamed_addr constant { i64, i64, ptr, ptr } { i64 0, i64 45, ptr @.str,
// APPLE-SAME: ptr @OBJC_CLASS_NAME_
// MULLE-SAME: ptr null
// COMMON: @{{.*__block_descriptor_40_8_32r_.*}} = linkonce_odr hidden unnamed_addr constant
// COMMON-SAME: ptr @__copy_helper_block_8_32r, ptr @__destroy_helper_block_8_32r
// APPLE-SAME: i64 16
// MULLE-SAME: ptr null
// COMMON: @{{.*__block_descriptor_40_8_32b_.*}} = linkonce_odr hidden unnamed_addr constant
// COMMON-SAME: ptr @__copy_helper_block_8_32b, ptr @__destroy_helper_block_8_32b
// APPLE-SAME: i64 256
// MULLE-SAME: ptr null
// COMMON: @{{.*__block_descriptor_48_8_32o40b_.*}} = linkonce_odr hidden unnamed_addr constant
// COMMON-SAME: ptr @__copy_helper_block_8_32o40b, ptr @__destroy_helper_block_8_32o40b
// APPLE-SAME: i64 512
// MULLE-SAME: ptr null
// COMMON: @{{.*__block_descriptor_48_8_32o40o_.*}} = linkonce_odr hidden unnamed_addr constant
// COMMON-SAME: ptr @__copy_helper_block_8_32o40o, ptr @__destroy_helper_block_8_32o40o
// APPLE-SAME: i64 512
// MULLE-SAME: ptr null
// COMMON: @{{.*__block_descriptor_40_8_32o_.*}} = linkonce_odr hidden unnamed_addr constant
// COMMON-SAME: ptr @__copy_helper_block_8_32o, ptr @__destroy_helper_block_8_32o
// APPLE-SAME: i64 256
// MULLE-SAME: ptr null
// COMMON: @{{.*__block_descriptor_56_.*Triple.*}} = linkonce_odr hidden unnamed_addr constant { i64, i64, ptr, ptr } { i64 0, i64 56,
// APPLE-SAME: ptr @{{.*}}, ptr @OBJC_CLASS_NAME_
// MULLE-SAME: ptr @{{.*}}, ptr null

// Mixed-width scalar captures retain the same order, padding, flags, and
// indirect call convention.
// COMMON-LABEL: define i32 @captureScalars(
// COMMON: alloca <{ ptr, i32, i32, ptr, ptr, i64, i32, i8 }>, align 8
// COMMON: store ptr @_NSConcreteStackBlock
// COMMON: store i32 -1073741824
// COMMON: store ptr @__captureScalars_block_invoke
// COMMON: store i8 %{{.*}}, ptr %{{.*}}, align 4
// COMMON: store i32 %{{.*}}, ptr %{{.*}}, align 8
// COMMON: store i64 %{{.*}}, ptr %{{.*}}, align 8
// COMMON: call i32 %{{.*}}(ptr noundef %{{.*}}, i32 noundef %{{.*}})

// COMMON-LABEL: define internal i32 @__captureScalars_block_invoke(
// COMMON: trunc i8 %{{.*}} to i1
// COMMON: add nsw i32
// COMMON: trunc i64 %{{.*}} to i32
// COMMON: phi i32

// A scalar __block variable is moved by the standard Blocks runtime in both
// modes; flag 8 means BLOCK_FIELD_IS_BYREF.
// COMMON-LABEL: define i32 @mutateByref(
// COMMON: store i32 536870912
// COMMON: store i32 -1040187392
// COMMON: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)

// COMMON-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32r(
// COMMON: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 8)
// COMMON-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32r(
// COMMON: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)

// Nested block ownership remains a Blocks-runtime operation; flag 7 means a
// block pointer rather than an Objective-C object pointer.
// COMMON-LABEL: define void @captureNested(
// COMMON: store ptr @__captureNested_block_invoke
// COMMON: store ptr @__captureNested_block_invoke_2
// COMMON-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32b(
// COMMON: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 7)
// COMMON-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32b(
// COMMON: call void @_Block_object_dispose(ptr %{{.*}}, i32 7)

// In a mixed object+block capture, only object ownership changes runtime.
// COMMON-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32o40b(
// APPLE: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// MULLE: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 -1949542456, ptr %{{.*}})
// COMMON: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 7)
// MULLE-NOT: call ptr @mulle_objc_object_call
// COMMON-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32o40b(
// COMMON: call void @_Block_object_dispose(ptr %{{.*}}, i32 7)
// APPLE: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)
// MULLE: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 57337825, ptr %{{.*}})
// MULLE-NOT: call ptr @mulle_objc_object_call

// Multiple object captures must perform one ownership operation per field.
// COMMON-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32o40o(
// APPLE-COUNT-2: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// APPLE-NOT: call void @_Block_object_assign
// MULLE-COUNT-2: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 -1949542456, ptr %{{.*}})
// MULLE-NOT: call ptr @mulle_objc_object_call
// COMMON-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32o40o(
// APPLE-COUNT-2: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)
// APPLE-NOT: call void @_Block_object_dispose
// MULLE-COUNT-2: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 57337825, ptr %{{.*}})
// MULLE-NOT: call ptr @mulle_objc_object_call

// A statically nil object still uses the normal ownership helper. This checks
// that neither frontend introduces an incompatible special block layout.
// COMMON-LABEL: define void @captureNilObject(
// COMMON: store ptr null, ptr %{{.*}}, align 8
// COMMON-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32o(
// APPLE: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// APPLE-NOT: call void @_Block_object_assign
// MULLE: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 -1949542456, ptr %{{.*}})
// MULLE-NOT: call ptr @mulle_objc_object_call
// COMMON-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32o(
// APPLE: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)
// APPLE-NOT: call void @_Block_object_dispose
// MULLE: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 57337825, ptr %{{.*}})
// MULLE-NOT: call ptr @mulle_objc_object_call

// A __block object's container stays standard Blocks ABI (flag 8). Under MRC,
// its object payload is non-owning in both runtimes: BLOCK_BYREF_CALLER makes
// flag 131 copy the pointer without retaining it.
// COMMON-LABEL: define void @captureByrefObject(
// COMMON: store ptr @__Block_byref_object_copy_
// COMMON: store ptr @__Block_byref_object_dispose_
// COMMON: call void @_Block_object_dispose(ptr %{{.*}}, i32 8)
// COMMON-LABEL: define internal void @__Block_byref_object_copy_(
// MULLE-NOT: @mulle_objc_object_call
// COMMON: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 131)
// MULLE-NOT: @mulle_objc_object_call
// COMMON: ret void
// COMMON-LABEL: define internal void @__Block_byref_object_dispose_(
// MULLE-NOT: @mulle_objc_object_call
// COMMON: call void @_Block_object_dispose(ptr %{{.*}}, i32 131)
// MULLE-NOT: @mulle_objc_object_call
// COMMON: ret void

// A 24-byte aggregate exercises BLOCK_HAS_STRET and the x86_64 sret/byval ABI.
// COMMON-LABEL: define void @addTriples(ptr dead_on_unwind noalias writable sret(%struct.Triple) align 8
// COMMON-SAME: ptr noundef byval(%struct.Triple) align 8
// COMMON: store i32 -536870912
// COMMON: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %{{.*}}, ptr align 8 %{{.*}}, i64 24, i1 false)
// COMMON: call void %{{.*}}(ptr dead_on_unwind writable sret(%struct.Triple) align 8 %{{.*}}, ptr noundef %{{.*}}, ptr noundef byval(%struct.Triple) align 8 %{{.*}})
// COMMON-LABEL: define internal void @__addTriples_block_invoke(ptr dead_on_unwind noalias writable sret(%struct.Triple) align 8
// COMMON-SAME: ptr noundef byval(%struct.Triple) align 8

// Explicit heap promotion and release always go through the Blocks runtime.
// COMMON-LABEL: define void @copyAndRelease(
// COMMON: call ptr @_Block_copy(ptr noundef %{{.*}})
// COMMON: call void @_Block_release(ptr noundef %{{.*}})
