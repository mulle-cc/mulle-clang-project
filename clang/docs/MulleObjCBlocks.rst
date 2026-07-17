==========================
Mulle Objective-C Blocks
==========================

Mulle Objective-C supports Clang's Blocks syntax when ``-fblocks`` is passed.
The implementation has no operating-system-specific code-generation gate.
Generated closures use the standard Blocks ABI, while ownership of captured
Mulle Objective-C objects is performed through Mulle ``retain`` and ``release``
message dispatch.

Blocks deliberately remain opt-in for Mulle Objective-C, including on Darwin.
This keeps ``__BLOCKS__`` and Blocks-dependent source code disabled unless the
application also arranges for a compatible Blocks runtime. This fork now
restores Clang's upstream Blocks defaults for C, C++, and Objective-C using an
Apple runtime; earlier fork releases suppressed those defaults. The opt-in
decision uses the resolved Objective-C runtime, so Apple platform triples and
``-fobjc-nonfragile-abi`` can select an Apple runtime and its default, while an
explicit Mulle runtime remains opt-in.

Supported operations
====================

The current implementation supports:

* block pointer declarations, global and stack block literals, and invocation;
* scalar captures and escaping blocks;
* ``__block`` scalar captures;
* blocks captured by other blocks;
* direct and ``__block`` Mulle Objective-C object captures under manual
  reference counting, including MRC's non-owning ``__block`` semantics; and
* Objective-C++ captures and exception cleanup in block copy helpers.

Copy an escaping block with ``_Block_copy`` and balance it with
``_Block_release``. For example:

.. code-block:: objc

   extern void *_Block_copy(const void *);
   extern void _Block_release(const void *);

   typedef int (^int_block_t)(int);

   static int_block_t make_adder(int base) {
     int_block_t block = ^(int value) { return base + value; };
     return (int_block_t)_Block_copy(block);
   }

   int main(void) {
     int_block_t add = make_adder(40);
     int result = add(2);
     _Block_release(add);
     return result != 42;
   }

Runtime and ownership model
===========================

The compiler delegates block allocation, block captures, and movement of
``__block`` containers to the platform Blocks runtime. Copy/dispose helpers for
direct Mulle Objective-C object captures instead emit Mulle message sends for
``retain`` and ``release``. This avoids sending Apple Objective-C ownership
operations to a Mulle object while preserving standard handling for nested
blocks and byref storage.

As in Apple Objective-C under manual reference counting, an object stored in a
``__block`` variable is non-owning. Its byref helper keeps the standard
``BLOCK_FIELD_IS_OBJECT | BLOCK_BYREF_CALLER`` operation, which copies the
pointer without retaining or releasing it. The object must therefore outlive
every invocation of an escaping block that reads that variable.

The Mulle ownership override applies only under manual reference counting and
only to a direct object field. Hybrid GC, GC-only, weak ``__block`` payloads,
nested blocks, and byref containers retain the normal
``_Block_object_assign``/``_Block_object_dispose`` path and flags. This narrow
boundary also keeps Objective-C++ exception cleanup paired with the runtime
that performed the corresponding copy operation.

Blocks are not Objective-C objects
==================================

The current Blocks runtime objects do not participate in Mulle's Objective-C
object model. Consequently, the compiler diagnoses an implicit conversion from
a block pointer to ``id``, ``Class``, an Objective-C interface or qualified
object pointer, or an ``__attribute__((NSObject))`` pointer. The diagnostic is
``-Wmulle-block-object-conversion`` and defaults to an error. It also covers a
block used directly as a message receiver, such as ``[block copy]``.

An explicit cast is the deliberate opt-out. In Objective-C++, C-style,
functional, and ``static_cast`` forms accepted by Clang have the same opt-out
meaning. A cast or warning suppression does not turn a block into a Mulle
object; code that then sends Mulle messages to it or stores it in an
Objective-C collection remains outside the supported object model. Use
``-Wno-error=mulle-block-object-conversion`` only to audit such conversions,
and ``-Wno-mulle-block-object-conversion`` only at a boundary that supplies its
own compatible representation.

Conversions laundered through ``void *``, variadic arguments, or separately
compiled code cannot be identified by this source-level diagnostic. Only a
Mulle-aware Blocks runtime can remove the object-model limitation itself.

Platform support
================

The implementation contains no operating-system-specific ownership path. The
regression matrix emits and compares LLVM IR for:

* Darwin, Linux, FreeBSD, and Windows targets;
* Mach-O, ELF, and COFF object formats;
* AArch64, ARM, x86-64, x86, and big-endian PowerPC64; and
* LP64 and ILP32 data models, plus dynamic and static COFF Blocks runtime
  linkage.

Representative AArch64 Mach-O, ELF, and COFF objects are also inspected with
``llvm-readobj`` to verify their undefined Blocks and Mulle runtime symbols.
The same compiler path therefore applies to any Clang target with a compatible
Mulle Objective-C runtime and Blocks runtime; adding a new operating system
does not require a Blocks code-generation port.

On Darwin, the system supplies the required Blocks ABI symbols. On ELF systems,
link compiler-rt's ``libBlocksRuntime`` or another compatible implementation,
typically with ``-lBlocksRuntime``. On Windows, link the corresponding import
library or pass ``-static-libclosure`` when linking a static
``BlocksRuntime.lib``. A compatible runtime must provide at least
``_NSConcreteGlobalBlock``, ``_NSConcreteStackBlock``, ``_Block_copy``,
``_Block_release``, ``_Block_object_assign``, and
``_Block_object_dispose``. Targets whose toolchain does not guarantee those
symbols continue to receive Clang's ``-fblocks-runtime-optional`` behavior.

Current limitations
===================

* Copy escaping blocks with ``_Block_copy`` and release them with
  ``_Block_release``. Native Mulle messages and collections require a future
  Mulle-aware Blocks runtime; the compiler diagnostic above fences this
  unsupported conversion by default.
* This work adds and validates manual-reference-counting object captures. It
  does not add ARC or weak-capture semantics for the Mulle runtime.
* The Mulle runtime currently supplies no extended GC, RC, or byref layout
  string. The compiler emits a typed null layout field in the standard block
  descriptor.
* End-to-end Mulle object-lifetime validation has been performed on Apple
  silicon macOS. Non-Darwin compiler output is covered by cross-target IR and
  object tests; native execution there additionally depends on the selected
  Mulle and Blocks runtime builds.
