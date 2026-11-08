# REQUIRES: mips
## --irx: PlayStation 2 IOP modules (IRX).

# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=mipsel -mcpu=mips1 a.s -o a.o
# RUN: ld.lld --irx a.o -o a.irx
# RUN: llvm-readelf -h -l -S -r -s -x .iopmod -x .text -x .rodata -x .data a.irx \
# RUN:   | FileCheck %s

## The ELF header and the two program headers, .iopmod right after them.
# CHECK:      Type: Processor Specific: (ff80)
# CHECK:      Entry point address: 0x0
# CHECK:      Number of program headers: 2

## .iopmod first, the loaded sections from address 0 in TEXT, DATA, BSS order.
## The relocations after the section header table (at 0x154), then the symbols.
# CHECK:      [Nr] Name        Type       Address  Off    Size   ES Flg Lk Inf Al
# CHECK-NEXT: [ 0]             NULL       00000000 000000 000000 00      0   0  0
# CHECK-NEXT: [ 1] .iopmod     SCE_IOPMOD 00000000 000074 00001b 00      0   0  4
# CHECK-NEXT: [ 2] .text       PROGBITS   00000000 000090 000030 00  AX  0   0 16
# CHECK-NEXT: [ 3] .rodata     PROGBITS   00000030 0000c0 000008 00   A  0   0 16
# CHECK-NEXT: [ 4] .data       PROGBITS   00000040 0000d0 000025 00  WA  0   0 16
# CHECK-NEXT: [ 5] .bss        NOBITS     00000070 000100 000010 00  WA  0   0 16
# CHECK-NEXT: [ 6] .symtab     SYMTAB     00000000 0003a4 0000f0 10      8   7  4
# CHECK-NEXT: [ 7] .shstrtab   STRTAB     00000000 0000f5 00005c 00      0   0  1
# CHECK-NEXT: [ 8] .strtab     STRTAB     00000000 000494 000055 00      0   0  1
# CHECK-NEXT: [ 9] .rel.text   REL        00000000 000334 000028 08   I  6   2  4
# CHECK-NEXT: [10] .rel.rodata REL        00000000 00035c 000010 08   I  6   3  4
# CHECK-NEXT: [11] .rel.data   REL        00000000 00036c 000038 08   I  6   4  4

# CHECK:      Type       Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
# CHECK-NEXT: SCE_IOPMOD 0x000074 0x00000000 0x00000000 0x0001b 0x00000 R   0x4
# CHECK-NEXT: LOAD       0x000090 0x00000000 0x00000000 0x00070 0x00080 RWE 0x10

## No symbols, addresses in the module. The b to 1: and the HI16/LO16 of _gp
## relative to the code (none here) need no entry; _gp moves with the module.
# CHECK:      Relocation section '.rel.text' at offset 0x334 contains 5 entries:
# CHECK-NEXT: Offset     Info    Type
# CHECK-NEXT: 00000000  00000005 R_MIPS_HI16
# CHECK-NEXT: 00000004  00000006 R_MIPS_LO16
# CHECK-NEXT: 00000008  00000004 R_MIPS_26
# CHECK-NEXT: 00000018  00000005 R_MIPS_HI16
# CHECK-NEXT: 0000001c  00000006 R_MIPS_LO16
# CHECK:      Relocation section '.rel.rodata' at offset 0x35c contains 2 entries:
# CHECK-NEXT: Offset     Info    Type
# CHECK-NEXT: 00000030  00000002 R_MIPS_32
# CHECK-NEXT: 00000034  00000002 R_MIPS_32
# CHECK:      Relocation section '.rel.data' at offset 0x36c contains 7 entries:
# CHECK-NEXT: Offset     Info    Type
# CHECK-NEXT: 00000040  00000002 R_MIPS_32
# CHECK-NEXT: 00000048  00000002 R_MIPS_32
# CHECK-NEXT: 0000004c  00000002 R_MIPS_32
# CHECK-NEXT: 00000050  00000002 R_MIPS_32
# CHECK-NEXT: 00000054  00000002 R_MIPS_32
# CHECK-NEXT: 00000058  00000002 R_MIPS_32
# CHECK-NEXT: 0000005c  00000002 R_MIPS_32

## gp is 0x7ff0 past the start of BSS (no small data); the segment bounds.
# CHECK:      00008060 0 NOTYPE LOCAL  HIDDEN  5 _gp
# CHECK:      00000000 0 NOTYPE GLOBAL DEFAULT 2 _ftext
# CHECK-NEXT: 00000030 0 NOTYPE GLOBAL DEFAULT 2 _etext
# CHECK-NEXT: 00000030 0 NOTYPE GLOBAL DEFAULT 3 _fdata
# CHECK-NEXT: 00000065 0 NOTYPE GLOBAL DEFAULT 4 _edata
# CHECK-NEXT: 00000070 0 NOTYPE GLOBAL DEFAULT 5 _fbss
# CHECK-NEXT: 00000080 0 NOTYPE GLOBAL DEFAULT 5 _end

## moduleinfo (_irx_id), entry, gp, text, data and bss size, version 1.2, an
## empty name.
# CHECK:      Hex dump of section '.iopmod':
# CHECK-NEXT: 0x00000000 40000000 00000000 60800000 30000000
# CHECK-NEXT: 0x00000010 40000000 10000000 020100

## The values at address 0: buf at 0x70, func at 0x28 (jal 0xa), _gp 0x8060.
# CHECK:      Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 0000023c 70004224 0a00000c 00000000
# CHECK-NEXT: 0x00000010 01000010 00000000 0100033c 60806324
# CHECK-NEXT: 0x00000020 0800e003 00000000 0800e003 00000000
# CHECK:      Hex dump of section '.rodata':
# CHECK-NEXT: 0x00000030 28000000 74000000
# CHECK:      Hex dump of section '.data':
# CHECK-NEXT: 0x00000040 60000000 02010000 00000000 30000000
# CHECK-NEXT: 0x00000050 30000000 65000000 70000000 80000000
# CHECK-NEXT: 0x00000060 74657374 00

## -s leaves out the symbol table; the relocations stay.
# RUN: ld.lld --irx -s a.o -o s.irx
# RUN: llvm-readelf -S -r s.irx | FileCheck --check-prefix=STRIP %s

# STRIP:      [ 6] .shstrtab   STRTAB 00000000 0000f5 00004c 00     0 0 1
# STRIP-NEXT: [ 7] .rel.text   REL    00000000 0002d4 000028 08 I   0 2 4
# STRIP-NEXT: [ 8] .rel.rodata REL    00000000 0002fc 000010 08 I   0 3 4
# STRIP-NEXT: [ 9] .rel.data   REL    00000000 00030c 000038 08 I   0 4 4
# STRIP:      '.rel.text' at offset 0x2d4 contains 5 entries

## The loader cannot apply GOT relocations, nor a R_MIPS_LO16 alone.
# RUN: llvm-mc -filetype=obj -triple=mipsel -mcpu=mips1 got.s -o got.o
# RUN: not ld.lld --irx got.o -o /dev/null 2>&1 | FileCheck --check-prefix=GOT %s
# GOT: error: got.o:(.text+0x0): --irx: relocation R_MIPS_CALL16 against foo cannot be applied by the IOP loader

# RUN: llvm-mc -filetype=obj -triple=mipsel -mcpu=mips1 lo16.s -o lo16.o
# RUN: not ld.lld --irx lo16.o -o /dev/null 2>&1 | FileCheck --check-prefix=LO16 %s
# LO16: error: lo16.o:(.text+0x8): --irx: R_MIPS_LO16 without a R_MIPS_HI16 before it

# RUN: not ld.lld --irx -r a.o -o /dev/null 2>&1 | FileCheck --check-prefix=REL %s
# REL: error: -r and --irx may not be used together

#--- a.s
  .set noreorder
  .text
  .globl _start
_start:
  lui   $2, %hi(buf)
  addiu $2, $2, %lo(buf)
  jal   func
  nop
  b     1f
  nop
1:
  lui   $3, %hi(_gp)
  addiu $3, $3, %lo(_gp)
  jr    $ra
  nop
func:
  jr    $ra
  nop

  .rodata
ptrs:
  .word func, buf + 4

  .data
  .globl _irx_id
_irx_id:
  .word name
  .short 0x0102
  .short 0
bounds:
  .word _ftext, _etext, _fdata, _edata, _fbss, _end
name:
  .asciz "test"

  .bss
buf:
  .space 16

#--- got.s
  .text
  .globl _start
_start:
  lw    $25, %call16(foo)($gp)
  jr    $ra
  nop
foo:
  jr    $ra
  nop

#--- lo16.s
  .set noreorder
  .text
  .globl _start
_start:
  lui   $2, %hi(buf)
  lw    $3, %lo(buf)($2)
  lw    $4, %lo(buf+4)($2)
  jr    $ra
  nop

  .bss
buf:
  .space 16
