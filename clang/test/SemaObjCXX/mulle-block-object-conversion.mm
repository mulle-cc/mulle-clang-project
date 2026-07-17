// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=mulle \
// RUN:   -fblocks -std=c++17 -fsyntax-only -verify=mulle %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=mulle \
// RUN:   -fblocks -std=c++17 -Wno-error=mulle-block-object-conversion \
// RUN:   -fsyntax-only -verify=downgraded %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fobjc-runtime=mulle \
// RUN:   -fblocks -std=c++17 -Wno-mulle-block-object-conversion \
// RUN:   -fsyntax-only -verify=none %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin \
// RUN:   -fobjc-runtime=macosx-10.15 -fblocks -std=c++17 -fsyntax-only \
// RUN:   -verify=none %s

// none-no-diagnostics

typedef void (^VoidBlock)(void);
typedef void *NSObjectPointer __attribute__((NSObject));

__attribute__((objc_root_class))
@interface Object
- (id)copy;
@end

extern void consumeObject(id object);
extern void *mulle_objc_object_call(void *object, unsigned int methodID,
                                    void *parameter);

template <typename T>
T boxBlock(VoidBlock block) {
  // mulle-error@+2 {{to Objective-C retainable type 'id'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'id'}}
  return block;
}

void testImplicitConversions(VoidBlock block) {
  // mulle-error@+2 {{to Objective-C retainable type 'id'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'id'}}
  id object = block;
  // mulle-error@+2 {{to Objective-C retainable type 'Class'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'Class'}}
  Class classObject = block;
  // mulle-error@+2 {{to Objective-C retainable type 'NSObjectPointer'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'NSObjectPointer'}}
  NSObjectPointer attributed = block;
  // mulle-error@+2 {{to Objective-C retainable type 'id'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'id'}}
  consumeObject(block);
  // mulle-error@+2 {{blocks are not Mulle Objective-C objects}}
  // downgraded-warning@+1 {{blocks are not Mulle Objective-C objects}}
  [block copy];
  // mulle-note@+2 {{in instantiation of function template specialization}}
  // downgraded-note@+1 {{in instantiation of function template specialization}}
  id templateObject = boxBlock<id>(block);

  void *untyped = block;
  VoidBlock sameType = block;
  id cStyle = (id)block;
  id functional = id(block);
  id staticCast = static_cast<id>(block);
  [(id)block copy];

  (void)object;
  (void)classObject;
  (void)attributed;
  (void)templateObject;
  (void)untyped;
  (void)sameType;
  (void)cStyle;
  (void)functional;
  (void)staticCast;
}

id testReturn(VoidBlock block) {
  // mulle-error@+2 {{to Objective-C retainable type 'id'}}
  // downgraded-warning@+1 {{to Objective-C retainable type 'id'}}
  return block;
}
