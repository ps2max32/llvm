# REQUIRES: mips
## --irx with ILB files: the import tables of what a PS2 IOP module calls, and
## its export table (--irx-export).

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=mipsel -mcpu=mips1 a.s -o a.o
# RUN: ld.lld --irx a.o lib.ilb other.ilb dup.ilb --irx-export=exports.ilb \
# RUN:   -o a.irx 2>&1 | FileCheck --check-prefix=WARN %s
# RUN: llvm-readelf -r -s -x .text a.irx | FileCheck %s

## The first ILB that lists a name provides it.
# WARN: warning: foo is imported from other (other.ilb), and is also in dup (dup.ilb)

## The export table's addresses are relocated.
# CHECK:      Relocation section '.rel.text' at offset {{.*}} contains 10 entries:
# CHECK-NEXT: Offset     Info    Type
# CHECK-NEXT: 00000014  00000002 R_MIPS_32
# CHECK-NEXT: 00000018  00000002 R_MIPS_32
# CHECK-NEXT: 0000001c  00000002 R_MIPS_32
# CHECK-NEXT: 00000020  00000002 R_MIPS_32
# CHECK-NEXT: 00000024  00000002 R_MIPS_32

## One stub per used entry, in an import table per library: not
## ReleaseLibraryEntries nor getchar, printf once.
# CHECK:      00000040     0 NOTYPE  GLOBAL DEFAULT     2 _start
# CHECK-NEXT: 00000000    52 OBJECT  GLOBAL DEFAULT     2 _exp_test
# CHECK-NEXT: 00000084     8 FUNC    GLOBAL DEFAULT     2 RegisterLibraryEntries
# CHECK-NEXT: 000000b4     8 FUNC    GLOBAL DEFAULT     2 printf
# CHECK-NEXT: 000000e4     8 FUNC    GLOBAL DEFAULT     2 foo
# CHECK-NEXT: 00000064     0 NOTYPE  GLOBAL DEFAULT     2 mine

## The export table first: magic, next, version 1.2, name, the entries (1 to 3
## point to a function that only returns, at 0x2c), 0. Then the code, then the
## import tables: magic, next, version, name, jr $ra + addiu $0, $0, <index>
## per entry, two zero words; the padding between them is trap instructions.
# CHECK:      Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 0000c041 00000000 02010000 74657374
# CHECK-NEXT: 0x00000010 00000000 40000000 2c000000 2c000000
# CHECK-NEXT: 0x00000020 2c000000 64000000 00000000 0800e003
# CHECK-NEXT: 0x00000030 00000000 01001704 01001704 01001704
# CHECK-NEXT: 0x00000040 0000043c 2100000c 00008424 2d00000c
# CHECK-NEXT: 0x00000050 00000000 3900000c 00000000 0800e003
# CHECK-NEXT: 0x00000060 00000000 0800e003 00000000 01001704
# CHECK-NEXT: 0x00000070 0000e041 00000000 01010000 6c6f6164
# CHECK-NEXT: 0x00000080 636f7265 0800e003 06000024 00000000
# CHECK-NEXT: 0x00000090 00000000 01001704 01001704 01001704
# CHECK-NEXT: 0x000000a0 0000e041 00000000 02010000 73746469
# CHECK-NEXT: 0x000000b0 6f000000 0800e003 04000024 00000000
# CHECK-NEXT: 0x000000c0 00000000 01001704 01001704 01001704
# CHECK-NEXT: 0x000000d0 0000e041 00000000 01020000 6f746865
# CHECK-NEXT: 0x000000e0 72000000 0800e003 0a000024 00000000
# CHECK-NEXT: 0x000000f0 00000000

## With LTO, what only the export table refers to is kept.
# RUN: llvm-as lto.ll -o lto.o
# RUN: ld.lld --irx lto.o --irx-export=exports.ilb -o lto.irx
# RUN: llvm-readelf -s -x .text lto.irx | FileCheck --check-prefix=LTO %s
# LTO:      GLOBAL DEFAULT 2 _start
# LTO:      GLOBAL DEFAULT 2 mine
# LTO:      Hex dump of section '.text':
# LTO-NEXT: 0x00000000 0000c041 00000000 02010000 74657374

## Errors.
# RUN: not ld.lld --irx a.o lib.ilb other.ilb --irx-export=bad-export.ilb \
# RUN:   -o /dev/null 2>&1 | FileCheck --check-prefix=UNDEF %s
# UNDEF: error: bad-export.ilb: exported nothere is not defined
# RUN: not ld.lld --irx a.o bad.ilb -o /dev/null 2>&1 | FileCheck --check-prefix=BAD %s
# BAD: error: bad.ilb:3: unknown line: X 123
# RUN: not ld.lld a.o --irx-export=exports.ilb -o /dev/null 2>&1 \
# RUN:   | FileCheck --check-prefix=NOIRX %s
# NOIRX: error: --irx-export may not be used without --irx

#--- a.s
  .set noreorder
  .text
  .globl _start
_start:
  lui   $4, %hi(_exp_test)
  jal   RegisterLibraryEntries
  addiu $4, $4, %lo(_exp_test)
  jal   printf
  nop
  jal   foo
  nop
  jr    $ra
  nop
  .globl mine
mine:
  jr    $ra
  nop

#--- lib.ilb
#IOP-ILB#
# two libraries in one file
L loadcore
V 0101
F 0000
E 006 RegisterLibraryEntries
E 007 ReleaseLibraryEntries
#IOP-ILB#
L stdio
V 0102
F 0000
E 004 printf
E 005 getchar
E 004 printf

#--- other.ilb
#IOP-ILB#
L other
V 0201
F 0000
E 010 foo

#--- dup.ilb
#IOP-ILB#
L dup
V 0101
F 0000
E 004 foo

#--- exports.ilb
#IOP-ILB#
L test
V 0102
F 0000
E 000 _start
E 004 mine

#--- lto.ll
target datalayout = "e-m:m-p:32:32-i8:8:32-i16:16:32-i64:64-n32-S64"
target triple = "mipsel-none-irx"
define void @_start() { ret void }
define void @mine() { ret void }

#--- bad-export.ilb
#IOP-ILB#
L test
V 0101
F 0000
E 004 nothere

#--- bad.ilb
#IOP-ILB#
L bad
X 123
