// RUN: %clang_cc1 -triple arm64-apple-macosx14.0 -fobjc-runtime=mulle \
// RUN:   -fblocks -fcxx-exceptions -fexceptions -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefixes=MULLE,MULLE-ITANIUM %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14.0 \
// RUN:   -fobjc-runtime=macosx-14.0 -fblocks -fcxx-exceptions -fexceptions \
// RUN:   -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefixes=CONTROL,CONTROL-ITANIUM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=mulle \
// RUN:   -fblocks -fcxx-exceptions -fexceptions -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefixes=MULLE,MULLE-ITANIUM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu \
// RUN:   -fobjc-runtime=gnustep-2.0 -fblocks -fcxx-exceptions -fexceptions \
// RUN:   -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefixes=CONTROL,CONTROL-ITANIUM %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fobjc-runtime=mulle \
// RUN:   -fblocks -fcxx-exceptions -fexceptions -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefixes=MULLE,MULLE-WINDOWS %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc \
// RUN:   -fobjc-runtime=gnustep-2.0 -fblocks -fcxx-exceptions -fexceptions \
// RUN:   -emit-llvm -o - %s | \
// RUN:   FileCheck --check-prefixes=CONTROL,CONTROL-WINDOWS %s

struct Throwing {
  char value;

  Throwing();
  Throwing(const Throwing &);
  ~Throwing();
};

@class Object;

static const struct mulle_clang_objccompilerinfo {
  unsigned int load_version;
  unsigned int runtime_version;
} __mulle_objc_objccompilerinfo = {19, (28 << 8)};

extern void consume(void (^block)(void));

void captureObjectBeforeThrowing(Object *object, const Throwing &value) {
  Throwing local(value);
  consume(^{
    (void)object;
    (void)local;
  });
}

// The object has greater alignment than Throwing, so it is copied first. If
// Throwing's copy constructor throws, the helper must undo that ownership
// operation with the same Objective-C runtime that performed it.
// MULLE-LABEL: define linkonce_odr hidden void @{{.*copy_helper_block.*}}(
// MULLE: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 -1949542456, ptr %{{.*}})
// MULLE: invoke {{.*}} @{{.*Throwing.*}}
// MULLE-ITANIUM: landingpad
// MULLE-ITANIUM: cleanup
// MULLE-ITANIUM: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 57337825, ptr %{{.*}})
// MULLE-WINDOWS: cleanuppad
// MULLE-WINDOWS: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 57337825, ptr %{{.*}}){{.*}}[ "funclet"

// CONTROL-LABEL: define linkonce_odr hidden void @{{.*copy_helper_block.*}}(
// CONTROL: call void @_Block_object_assign(ptr %{{.*}}, ptr %{{.*}}, i32 3)
// CONTROL: invoke {{.*}} @{{.*Throwing.*}}
// CONTROL-ITANIUM: landingpad
// CONTROL-ITANIUM: cleanup
// CONTROL-ITANIUM: call void @_Block_object_dispose(ptr %{{.*}}, i32 3)
// CONTROL-WINDOWS: cleanuppad
// CONTROL-WINDOWS: call void @_Block_object_dispose(ptr %{{.*}}, i32 3){{.*}}[ "funclet"
