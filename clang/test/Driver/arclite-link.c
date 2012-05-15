// RUN: touch %t.o
// RUN: %clang -### -target x86_64-apple-darwin10 -fobjc-link-runtime -lfoo -mmacosx-version-min=10.7 %t.o 2>&1 | FileCheck -check-prefix=CHECK-ARCLITE-OSX %s
// RUN: %clang -### -target x86_64-apple-darwin10 -fobjc-link-runtime -mmacosx-version-min=10.8 %t.o 2>&1 | FileCheck -check-prefix=CHECK-NOARCLITE %s
// RUN: %clang -### -target i386-apple-darwin10 -fobjc-link-runtime -mmacosx-version-min=10.7 %t.o 2>&1 | FileCheck -check-prefix=CHECK-NOARCLITE %s
// RUN: %clang -### -target x86_64-apple-darwin10 -fobjc-link-runtime -nostdlib %t.o 2>&1 | FileCheck -check-prefix=CHECK-NOSTDLIB %s

// CHECK-ARCLITE-OSX: -lfoo
// CHECK-ARCLITE-OSX: libarclite_macosx.a
// CHECK-ARCLITE-OSX: -framework
// CHECK-ARCLITE-OSX: Foundation
// CHECK-ARCLITE-OSX: -lobjc
// CHECK-NOARCLITE-NOT: libarclite
// CHECK-NOSTDLIB-NOT: -lobjc
