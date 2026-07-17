// RUN: %clang -### -target x86_64-apple-darwin -x objective-c -fobjc-runtime=mulle -fblocks -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target x86_64-apple-darwin -x objective-c -fobjc-runtime=macosx-10.15 -fblocks -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x objective-c -fobjc-runtime=mulle -fblocks -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target x86_64-pc-windows-msvc -x objective-c -fobjc-runtime=mulle -fblocks -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target arm64-apple-macosx14.0 -x objective-c -fblocks -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x objective-c++ -fblocks -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target x86_64-apple-darwin -x objective-c -fobjc-runtime=mulle -fblocks -fno-blocks -c %s 2>&1 | FileCheck --check-prefix=DISABLED --implicit-check-not=-fblocks %s
// RUN: %clang -### -target x86_64-apple-darwin -x objective-c -fobjc-runtime=macosx-10.15 -fblocks -fno-blocks -c %s 2>&1 | FileCheck --check-prefix=DISABLED --implicit-check-not=-fblocks %s
// RUN: %clang -### -target x86_64-apple-darwin -x objective-c -fobjc-runtime=mulle -fno-blocks -fblocks -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target x86_64-apple-darwin -x objective-c -fobjc-runtime=macosx-10.15 -fno-blocks -fblocks -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target x86_64-apple-darwin -x objective-c -fobjc-runtime=mulle -c %s 2>&1 | FileCheck --check-prefix=DISABLED --implicit-check-not=-fblocks %s
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x objective-c -fobjc-runtime=mulle -c %s 2>&1 | FileCheck --check-prefix=DISABLED --implicit-check-not=-fblocks %s
// RUN: %clang -### -target x86_64-pc-windows-msvc -x objective-c -fobjc-runtime=mulle -c %s 2>&1 | FileCheck --check-prefix=DISABLED --implicit-check-not=-fblocks %s
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x objective-c -fgnu-runtime -fobjc-nonfragile-abi -c %s 2>&1 | FileCheck --check-prefix=DISABLED --implicit-check-not=-fblocks %s
// RUN: %clang -### -target arm64-apple-macosx14.0 -x objective-c -c %s 2>&1 | FileCheck --check-prefix=DISABLED --implicit-check-not=-fblocks %s
// RUN: %clang -### -target arm64-apple-ios17.0 -x objective-c -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target x86_64-apple-darwin -x objective-c -fobjc-nonfragile-abi -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x objective-c++ -c %s 2>&1 | FileCheck --check-prefix=DISABLED --implicit-check-not=-fblocks %s
// RUN: %clang -### -target x86_64-apple-darwin -x objective-c -fobjc-runtime=macosx-10.15 -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target x86_64-apple-darwin -x c -c %s 2>&1 | FileCheck --check-prefix=ENABLED %s
// RUN: %clang -### -target wasm32-unknown-wasi -x objective-c -fobjc-runtime=mulle -fblocks -c %s 2>&1 | FileCheck --check-prefix=OPTIONAL %s
// RUN: %clang -### -target wasm32-unknown-wasi -x objective-c -fgnu-runtime -fobjc-runtime=mulle -fblocks -c %s 2>&1 | FileCheck --check-prefix=OPTIONAL %s
// RUN: %clang -### -target x86_64-unknown-linux-gnu -x objective-c -fobjc-runtime=gnustep-2.0 -fblocks -c %s 2>&1 | FileCheck --check-prefix=GNU-RUNTIME --implicit-check-not=-fblocks-runtime-optional %s
// RUN: %clang -target x86_64-unknown-linux-gnu -x objective-c -fobjc-runtime=mulle -fblocks -dM -E %s | FileCheck --check-prefix=BLOCKS-MACRO %s
// RUN: %clang -target x86_64-unknown-linux-gnu -x objective-c -fobjc-runtime=mulle -dM -E %s | FileCheck --check-prefix=NO-BLOCKS-MACRO --implicit-check-not=__BLOCKS__ %s

// ENABLED: "-cc1"
// ENABLED-SAME: "-fblocks"
// DISABLED: "-cc1"
// OPTIONAL: "-cc1"
// OPTIONAL-SAME: "-fblocks"
// OPTIONAL-SAME: "-fblocks-runtime-optional"
// GNU-RUNTIME: "-cc1"
// GNU-RUNTIME-SAME: "-fblocks"
// BLOCKS-MACRO: #define __BLOCKS__ 1
// NO-BLOCKS-MACRO: #define __OBJC__ 1
