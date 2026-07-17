// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=mulle \
// RUN:   -fblocks -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefixes=COMMON,MULLE \
// RUN:     --implicit-check-not=_Block_object_assign \
// RUN:     --implicit-check-not=_Block_object_dispose %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin \
// RUN:   -fobjc-runtime=macosx-10.15 -fblocks -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefixes=COMMON,CONTROL \
// RUN:     --implicit-check-not=mulle_objc_object_call %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fobjc-runtime=mulle \
// RUN:   -fblocks -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefixes=COMMON,MULLE \
// RUN:     --implicit-check-not=_Block_object_assign \
// RUN:     --implicit-check-not=_Block_object_dispose %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu \
// RUN:   -fobjc-runtime=gnustep-2.0 -fblocks -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefixes=COMMON,CONTROL \
// RUN:     --implicit-check-not=mulle_objc_object_call %s

typedef void (^VoidBlock)(void);
typedef void *NSObjectPointer __attribute__((NSObject));

@protocol Marker
@end

extern void consumeBlock(VoidBlock block);
extern void *mulle_objc_object_call(void *object, unsigned int methodID,
                                    void *parameter);

__attribute__((objc_root_class))
@interface Object
- (void)consumeBlock:(VoidBlock)block;
- (void)captureSelf;
@end

static const struct mulle_clang_objccompilerinfo {
  unsigned int load_version;
  unsigned int runtime_version;
} __mulle_objc_objccompilerinfo = {19, (28 << 8)};

void captureObjectKinds(id dynamicObject, Class classObject,
                        id<Marker> qualifiedObject, Object *typedObject,
                        NSObjectPointer nsObject) {
  consumeBlock(^{
    (void)dynamicObject;
    (void)classObject;
    (void)qualifiedObject;
    (void)typedObject;
    (void)nsObject;
  });
}

@implementation Object

- (void)consumeBlock:(VoidBlock)block {
  consumeBlock(block);
}

- (void)captureSelf {
  [self consumeBlock:^{ (void)self; }];
}

@end

// Objective-C object pointers, Class, protocol-qualified id, concrete object
// pointers, and __attribute__((NSObject)) pointers share the direct object
// capture path. Each field receives exactly one ownership operation.
// COMMON-LABEL: define {{(dso_local )?}}void @captureObjectKinds(

// COMMON-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32o40o48o56o64o(
// CONTROL-COUNT-5: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// CONTROL-NOT: call void @_Block_object_assign
// MULLE-COUNT-5: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 -1949542456, ptr %{{.*}})
// MULLE-NOT: call ptr @mulle_objc_object_call

// COMMON-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32o40o48o56o64o(
// CONTROL-COUNT-5: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)
// CONTROL-NOT: call void @_Block_object_dispose
// MULLE-COUNT-5: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 57337825, ptr %{{.*}})
// MULLE-NOT: call ptr @mulle_objc_object_call

// Implicit self capture follows the same ownership rule as an explicit object
// pointer and still uses the normal runtime-specific message send for the
// enclosing Objective-C method.
// COMMON-LABEL: define internal void @{{.*}}captureSelf{{.*}}block_invoke
// COMMON: load ptr, ptr %{{.*}}
// COMMON: ret void

// COMMON-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32o(
// CONTROL: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// CONTROL-NOT: call void @_Block_object_assign
// MULLE: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 -1949542456, ptr %{{.*}})
// MULLE-NOT: call ptr @mulle_objc_object_call

// COMMON-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32o(
// CONTROL: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)
// CONTROL-NOT: call void @_Block_object_dispose
// MULLE: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 57337825, ptr %{{.*}})
// MULLE-NOT: call ptr @mulle_objc_object_call
