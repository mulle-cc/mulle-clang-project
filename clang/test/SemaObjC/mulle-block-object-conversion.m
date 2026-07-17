// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=mulle \
// RUN:   -fblocks -fsyntax-only -verify=mulle %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=mulle \
// RUN:   -fblocks -Wno-error=mulle-block-object-conversion -fsyntax-only \
// RUN:   -verify=downgraded %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=mulle \
// RUN:   -fblocks -Wno-mulle-block-object-conversion -fsyntax-only \
// RUN:   -verify=none %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin \
// RUN:   -fobjc-runtime=macosx-10.15 -fblocks -fsyntax-only -verify=none %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu \
// RUN:   -fobjc-runtime=gnustep-2.0 -fblocks -fsyntax-only -verify=none %s

// none-no-diagnostics

typedef void (^VoidBlock)(void);
typedef void *NSObjectPointer __attribute__((NSObject));

@protocol NSObject
@end

@protocol NSCopying
@end

__attribute__((objc_root_class))
@interface NSObject
- (id)copy;
@end

extern void consumeObject(id object);
extern void *mulle_objc_object_call(void *object, unsigned int methodID,
                                    void *parameter);

void testImplicitConversions(VoidBlock block) {
  // mulle-error@+2 {{to Objective-C retainable type 'id'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'id'}}
  id object = block;
  // mulle-error@+2 {{to Objective-C retainable type 'id<NSObject,NSCopying>'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'id<NSObject,NSCopying>'}}
  id<NSObject, NSCopying> qualified = block;
  // mulle-error@+2 {{to Objective-C retainable type 'NSObject<NSCopying> *'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'NSObject<NSCopying> *'}}
  NSObject<NSCopying> *typed = block;
  // mulle-error@+2 {{to Objective-C retainable type 'NSObjectPointer'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'NSObjectPointer'}}
  NSObjectPointer attributed = block;
  // mulle-error@+2 {{to Objective-C retainable type 'id'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'id'}}
  consumeObject(block);
  // mulle-error@+2 {{blocks are not Mulle Objective-C objects}}
  // downgraded-warning@+1 {{blocks are not Mulle Objective-C objects}}
  [block copy];

  void *untyped = block;
  VoidBlock sameType = block;
  id explicitObject = (id)block;
  NSObjectPointer explicitAttributed = (NSObjectPointer)block;
  [(id)block copy];

  (void)object;
  (void)qualified;
  (void)typed;
  (void)attributed;
  (void)untyped;
  (void)sameType;
  (void)explicitObject;
  (void)explicitAttributed;
}

id testReturn(VoidBlock block) {
  // mulle-error@+2 {{to Objective-C retainable type 'id'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'id'}}
  return block;
}
