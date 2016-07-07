// RUN: rm -f CompGen.h.pch
// RUN: rm -rf %T
// RUN: mkdir -p %T/Rel/Path
// RUN: clang -x c++-header -fexceptions -fcxx-exceptions -std=c++14 -pthread %S/Inputs/CompGen.h -o CompGen.h.pch
// RUN: clang -x c++-header -fexceptions -fcxx-exceptions -std=c++14 -pthread %S/Inputs/CompGen.h -o %T/Rel/Path/Relative.pch
// RUN: cat %s | %cling -I%p -Xclang -include-pch -Xclang CompGen.h.pch  2>&1 | FileCheck --check-prefixes CHECK,WARN %s
// RUN: cat %s | %cling -I%p -I%T/Rel/Path -include-pch Relative.pch 2>&1 | FileCheck --check-prefixes CHECK,WARN %s
// RUN: cat %s | %cling -Wno-invalid-pch -I%p -Xclang -include-pch -Xclang CompGen.h.pch  2>&1 | FileCheck %s

// WARN: warning: please rebuild precompiled header '{{.*}}' with with cling [-Winvalid-pch]

//.storeState "a"
.x TriggerCompGen.h
.x TriggerCompGen.h
 // CHECK: I was executed
 // CHECK: I was executed
 //.compareState "a"
