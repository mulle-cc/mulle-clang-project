// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=mulle \
// RUN:   -fblocks -Wno-mulle-block-object-conversion -emit-llvm -o - %s | \
// RUN:   FileCheck --implicit-check-not=_Block_object_assign \
// RUN:     --implicit-check-not=_Block_object_dispose %s

typedef void (^VoidBlock)(void);

static const struct mulle_clang_objccompilerinfo {
  unsigned int load_version;
  unsigned int runtime_version;
} __mulle_objc_objccompilerinfo = {19, (28 << 8)};

extern void consumeBlock(VoidBlock block);

void captureDeliberatelyBoxedBlock(VoidBlock block) {
  id boxed = block;
  consumeBlock(^{ (void)boxed; });
}

// Suppressing the diagnostic is an explicit compatibility escape hatch; it
// does not turn a Blocks-runtime object into a native Mulle object. Pin the
// resulting ownership operation so this limitation remains visible.
// CHECK-LABEL: define linkonce_odr hidden void @__copy_helper_block_8_32o(
// CHECK: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 -1949542456, ptr %{{.*}})
// CHECK-NOT: call ptr @mulle_objc_object_call
// CHECK-LABEL: define linkonce_odr hidden void @__destroy_helper_block_8_32o(
// CHECK: call ptr @mulle_objc_object_call(ptr %{{.*}}, i32 57337825, ptr %{{.*}})
// CHECK-NOT: call ptr @mulle_objc_object_call
