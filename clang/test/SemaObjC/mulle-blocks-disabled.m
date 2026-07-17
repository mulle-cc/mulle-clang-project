// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu \
// RUN:   -fobjc-runtime=mulle -fsyntax-only %s 2>&1 | FileCheck %s

void blocksRemainOptIn(void) {
  (void)^{};
}

// CHECK: error: blocks support disabled - compile with -fblocks or pick a deployment target that supports them
