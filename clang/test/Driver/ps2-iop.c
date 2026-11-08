// PlayStation 2 IOP modules: mipsel-none-irx.

// RUN: %clang -### --target=mipsel-none-irx -c %s 2>&1 | FileCheck --check-prefix=CC1 %s
// CC1: "-cc1" "-triple" "mipsel-none-irx"
// CC1-SAME: "-mrelocation-model" "static"
// CC1-SAME: "-nostdsysteminc" "-D_IOP"
// CC1-SAME: "-target-cpu" "r3000a"
// CC1-SAME: "-target-feature" "+noabicalls"
// CC1-SAME: "-target-abi" "o32" "-msoft-float" "-mfloat-abi" "soft"

// No gp-relative data: a module's exports run with their caller's gp.
// RUN: %clang -### --target=mipsel-none-irx -mno-abicalls -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=NOGP %s
// NOGP-NOT: "-mgpopt"
// RUN: %clang -### --target=mipsel-none-irx -mgpopt -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=GP %s
// GP: "-mllvm" "-mgpopt"

// No unwind tables.
// RUN: %clang -### --target=mipsel-none-irx -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=UNWIND %s
// UNWIND-NOT: "-funwind-tables

// The link: ld.lld --irx, no startup files or libraries.
// RUN: %clang -### --target=mipsel-none-irx --ld-path=ld.lld %s -o foo.irx 2>&1 \
// RUN:   | FileCheck --check-prefix=LINK %s
// LINK: ld.lld{{(.exe)?}}" "-m" "elf32ltsmip" "--irx" "{{[^"]*}}.o" "-o" "foo.irx"

// RUN: %clang -### --target=mipsel-none-irx --ld-path=ld.lld %s -r -o foo.o 2>&1 \
// RUN:   | FileCheck --check-prefix=PARTIAL %s
// PARTIAL: ld.lld{{(.exe)?}}" "-m" "elf32ltsmip" "{{[^"]*}}.o" "-r" "-o" "foo.o"

// Headers: clang's own, then the sysroot's.
// RUN: %clang -### --target=mipsel-none-irx --sysroot=%S/Inputs/basic_baremetal_tree -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=INC %s
// INC: "-internal-isystem" "{{[^"]*}}{{/|\\\\}}include"
// INC-SAME: "-internal-externc-isystem" "{{[^"]*}}basic_baremetal_tree{{/|\\\\}}include"

int main(void) { return 0; }
