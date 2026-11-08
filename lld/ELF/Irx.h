//===- Irx.h ----------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PlayStation 2 IOP modules (IRX), written with --irx. See Irx.cpp.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_IRX_H
#define LLD_ELF_IRX_H

#include "lld/Common/LLVM.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <memory>

namespace llvm {
class MemoryBufferRef;
}

namespace lld::elf {
struct Ctx;
struct IrxLibrary;
struct PhdrEntry;

// ILB files: Sony's description of IOP libraries ("#IOP-ILB#").
bool isIrxLibrary(llvm::MemoryBufferRef mb);
void readIrxLibraries(Ctx &ctx, llvm::MemoryBufferRef mb,
                      SmallVector<IrxLibrary, 0> &out);
// Reads --irx-export and keeps its entries from LTO; before LTO.
void readIrxExport(Ctx &ctx);
// Defines the imported symbols and _exp_<name>; after symbol resolution.
void addIrxTables(Ctx &ctx);

// The layout used when no linker script is given.
llvm::StringRef getIrxDefaultScript();

// Writer steps, in the order they run.
void addIrxReservedSymbols(Ctx &ctx);
void addIrxSections(Ctx &ctx);
SmallVector<std::unique_ptr<PhdrEntry>, 0> createIrxPhdrs(Ctx &ctx);
void setIrxReservedSymbolSections(Ctx &ctx);
void finalizeIrx(Ctx &ctx);
// Returns the file size; sets shOff, the offset of the section header table.
uint64_t assignIrxFileOffsets(Ctx &ctx, uint64_t shdrSize, uint64_t &shOff);
void finalizeIrxPhdrs(Ctx &ctx);
// After the sections are written: the branches of --irx-relax.
void writeIrxBranches(Ctx &ctx);
} // namespace lld::elf

#endif
