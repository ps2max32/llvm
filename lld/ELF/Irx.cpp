//===- Irx.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PlayStation 2 IOP modules (IRX). With --irx, lld links MIPS32 (R3000)
// objects into the IOP's relocatable executable, the format that Sony's
// iopfixup makes from a relocatable link ("IOP Object Format" in the IOP
// kernel overview of the SDK):
//
// - e_type ET_SCE_IOPRELEXEC, exactly two program headers: PT_SCE_IOPMOD for
//   the .iopmod section (module information), then one PT_LOAD at address 0
//   that holds the TEXT, DATA and BSS segments, in that order.
// - The loader copies the PT_LOAD to the block it allocates and applies every
//   SHT_REL section. Their entries have no symbol (r_sym is 0); r_offset is
//   the address in the module, and the field holds the value at address 0,
//   to which the loader adds the module's base. Only R_MIPS_16, R_MIPS_32,
//   R_MIPS_26 and R_MIPS_HI16 directly followed by its R_MIPS_LO16 may be
//   used. PC- and gp-relative references need no entry.
// - The loader reads the first 144 bytes to find the module: .iopmod follows
//   the program headers. The file is laid out as the format prescribes: the
//   headers, .iopmod, the segment data, other sections, the section header
//   table, the relocations, the symbol table.
//
//===----------------------------------------------------------------------===//

#include "Irx.h"
#include "Config.h"
#include "InputFiles.h"
#include "InputSection.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::elf;

// The layout of ps2sdk's linkfile for IOP modules, which iopfixup expects:
// each input section 16-byte aligned, the first object's .text first (it may
// hold the export table), small data between DATA and BSS.
static const char irxDefaultScript[] = R"(
ENTRY(_start)
SECTIONS {
  .text 0 : SUBALIGN(16) { *(.text .text.*) *(.stub) }
  .rodata : SUBALIGN(16) { *(.rodata .rodata.*) }
  .data : SUBALIGN(16) { *(.data .data.*) }
  .sdata : { *(.sdata .sdata.*) *(.lit8) *(.lit4) }
  .sbss : { *(.sbss .sbss.*) *(.scommon) }
  .bss : SUBALIGN(16) { *(.bss .bss.*) *(COMMON) }
  /DISCARD/ : {
    *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes)
    *(.mdebug.*) *(.note.*) *(.llvm_addrsig)
  }
}
)";

StringRef elf::getIrxDefaultScript() { return irxDefaultScript; }

namespace {
// .iopmod: Elf32_IopMod. moduleinfo is the address of the module's
// irx_id (_irx_id, or Module in old sources), or -1 without one; moduleversion
// is its version. The name is left empty, as ps2sdk's iopfixup does: the
// loader reads 144 bytes, and the structure ends at 143 with an empty name.
class IopModSection final : public SyntheticSection {
public:
  IopModSection(Ctx &ctx)
      : SyntheticSection(ctx, ".iopmod", SHT_SCE_IOPMOD, 0, 4) {}
  size_t getSize() const override { return 6 * 4 + 2 + 1; }
  void writeTo(uint8_t *buf) override {
    write32le(buf, moduleInfo);
    write32le(buf + 4, entry);
    write32le(buf + 8, gp);
    write32le(buf + 12, textSize);
    write32le(buf + 16, dataSize);
    write32le(buf + 20, bssSize);
    write16le(buf + 24, moduleVersion);
    buf[26] = 0;
  }

  uint32_t moduleInfo = -1, entry = 0, gp = 0;
  uint32_t textSize = 0, dataSize = 0, bssSize = 0;
  uint16_t moduleVersion = 0;
};

// The relocations of one loaded output section, for the loader.
class IrxRelSection final : public SyntheticSection {
public:
  struct Entry {
    const InputSection *sec;
    uint64_t offset;
    RelType type;
  };

  IrxRelSection(Ctx &ctx, OutputSection *target)
      : SyntheticSection(ctx, ctx.saver.save(Twine(".rel") + target->name),
                         SHT_REL, SHF_INFO_LINK, 4),
        target(target) {
    entsize = sizeof(Elf32_Rel);
  }
  size_t getSize() const override { return entries.size() * entsize; }
  void writeTo(uint8_t *buf) override {
    for (const Entry &e : entries) {
      write32le(buf, e.sec->getVA(e.offset));
      write32le(buf + 4, e.type);
      buf += entsize;
    }
  }

  OutputSection *target;
  SmallVector<Entry, 0> entries;
};
} // namespace

static IopModSection &iopMod(Ctx &ctx) {
  return static_cast<IopModSection &>(*ctx.in.iopMod);
}

// The loaded output sections in address order, and the starts of the segments.
namespace {
struct IrxSegments {
  SmallVector<OutputSection *, 0> secs;
  OutputSection *firstText = nullptr, *lastText = nullptr;
  OutputSection *firstData = nullptr, *lastData = nullptr;
  OutputSection *firstBss = nullptr, *lastBss = nullptr;
  OutputSection *firstGp = nullptr; // .sdata/.sbss
};
} // namespace

static bool isText(const OutputSection *os) {
  return os->flags & SHF_EXECINSTR;
}
static bool isBss(const OutputSection *os) { return os->type == SHT_NOBITS; }

static IrxSegments getSegments(Ctx &ctx) {
  IrxSegments s;
  for (OutputSection *os : ctx.outputSections) {
    if (!(os->flags & SHF_ALLOC))
      continue;
    s.secs.push_back(os);
    OutputSection *&first = isText(os)  ? s.firstText
                            : isBss(os) ? s.firstBss
                                        : s.firstData;
    OutputSection *&last = isText(os)  ? s.lastText
                           : isBss(os) ? s.lastBss
                                       : s.lastData;
    if (!first)
      first = os;
    last = os;
    if (!s.firstGp && (os->flags & SHF_MIPS_GPREL))
      s.firstGp = os;
  }
  return s;
}

// _ftext, _etext, _fdata, _edata, _fbss, _end and their aliases, as iopfixup
// defines them, if referenced. With the linker script the generic ones are not
// made; their sections are set in setIrxReservedSymbolSections().
void elf::addIrxReservedSymbols(Ctx &ctx) {
  auto add = [&](StringRef name, int64_t pos) -> Defined * {
    Symbol *s = ctx.symtab->find(name);
    if (!s || s->isDefined() || s->isCommon())
      return nullptr;
    ctx.synthesizedSymbols.push_back(s);
    s->resolve(ctx, Defined{ctx, ctx.internalFile, StringRef(), STB_GLOBAL,
                            STV_DEFAULT, STT_NOTYPE, (uint64_t)pos,
                            /*size=*/0, ctx.out.elfHeader.get()});
    s->isUsedInRegularObj = true;
    return cast<Defined>(s);
  };
  ctx.sym.irxFtext = add("_ftext", 0);
  ctx.sym.etext1 = add("etext", -1);
  ctx.sym.etext2 = add("_etext", -1);
  ctx.sym.irxFdata = add("_fdata", 0);
  ctx.sym.edata1 = add("edata", -1);
  ctx.sym.edata2 = add("_edata", -1);
  ctx.sym.irxFbss = add("_fbss", 0);
  ctx.sym.end1 = add("end", -1);
  ctx.sym.end2 = add("_end", -1);
}

// Classifies a relocation of a loaded section: does the loader have to apply
// it again at the module's base?
enum class IrxRel { Skip, Emit, Error };

static IrxRel classify(Ctx &ctx, const Relocation &r) {
  // Absolute symbols and undefined weak ones (0) do not move with the module.
  // _gp is still absolute here; it becomes relative to a section of the
  // module in finalizeIrx().
  auto *d = dyn_cast<Defined>(r.sym);
  bool moves = (d && d->section) || r.sym == ctx.sym.mipsGp;
  switch (r.type) {
  case R_MIPS_NONE:
  case R_MIPS_JALR:
    return IrxRel::Skip;
  case R_MIPS_HI16:
  case R_MIPS_LO16:
    // The address of gp (__gnu_local_gp) moves with the module, its offset
    // from the code (_gp_disp) does not.
    if (r.expr == RE_MIPS_GOT_GP)
      return IrxRel::Emit;
    if (r.expr == RE_MIPS_GOT_GP_PC)
      return IrxRel::Skip;
    [[fallthrough]];
  case R_MIPS_16:
  case R_MIPS_32:
  case R_MIPS_26:
    if (r.expr != R_ABS && r.expr != R_PLT)
      return IrxRel::Error;
    return moves ? IrxRel::Emit : IrxRel::Skip;
  default:
    // Offsets from the code or from gp stay the same at any base.
    if (r.expr == R_PC || r.expr == R_PLT_PC || r.expr == RE_MIPS_GOTREL)
      return IrxRel::Skip;
    return IrxRel::Error;
  }
}

// Creates .iopmod and the .rel sections. Runs after the relocations are
// scanned and the output sections sorted, before the section indices are
// assigned.
void elf::addIrxSections(Ctx &ctx) {
  auto addOutputSection = [&](SyntheticSection *sec, bool first) {
    OutputDesc *osd = ctx.script->createOutputSection(sec->name, "<internal>");
    auto *isd = make<InputSectionDescription>("");
    isd->sections.push_back(sec);
    osd->osec.commands.push_back(isd);
    osd->osec.commitSection(sec);
    auto &cmds = ctx.script->sectionCommands;
    cmds.insert(first ? cmds.begin() : cmds.end(), osd);
  };

  ctx.in.iopMod = std::make_unique<IopModSection>(ctx);
  // Section 1, as iopfixup has it. Non-SHF_ALLOC sections take no address.
  addOutputSection(ctx.in.iopMod.get(), /*first=*/true);

  if (ctx.in.ehFrame && ctx.in.ehFrame->isNeeded())
    Err(ctx) << "--irx: .eh_frame is not supported (compile with "
                "-fno-unwind-tables -fno-asynchronous-unwind-tables)";

  SmallVector<OutputSection *, 0> loaded;
  for (SectionCommand *cmd : ctx.script->sectionCommands)
    if (auto *osd = dyn_cast<OutputDesc>(cmd))
      if ((osd->osec.flags & SHF_ALLOC) && osd->osec.type != SHT_NOBITS)
        loaded.push_back(&osd->osec);

  SmallVector<InputSection *, 0> storage;
  for (OutputSection *os : loaded) {
    auto rel = std::make_unique<IrxRelSection>(ctx, os);
    for (InputSection *isec : getInputSections(*os, storage)) {
      for (const Relocation &r : isec->relocs()) {
        switch (classify(ctx, r)) {
        case IrxRel::Skip:
          break;
        case IrxRel::Emit:
          rel->entries.push_back({isec, r.offset, r.type});
          break;
        case IrxRel::Error:
          Err(ctx) << isec->getLocation(r.offset) << ": --irx: relocation "
                   << r.type << " against " << r.sym
                   << " cannot be applied by the IOP loader (IOP modules "
                      "are built with -mno-abicalls -fno-pic)";
          break;
        }
      }
    }

    // The IOP's loader takes each R_MIPS_HI16 with the R_MIPS_LO16 after it,
    // and no R_MIPS_LO16 alone.
    auto &es = rel->entries;
    for (size_t i = 0; i < es.size(); ++i) {
      if (es[i].type == R_MIPS_HI16) {
        if (i + 1 == es.size() || es[i + 1].type != R_MIPS_LO16)
          Err(ctx) << es[i].sec->getLocation(es[i].offset)
                   << ": --irx: R_MIPS_HI16 is not followed by its "
                      "R_MIPS_LO16";
        else
          ++i;
      } else if (es[i].type == R_MIPS_LO16) {
        Err(ctx) << es[i].sec->getLocation(es[i].offset)
                 << ": --irx: R_MIPS_LO16 without a R_MIPS_HI16 before it";
      }
    }

    if (es.empty())
      continue;
    addOutputSection(rel.get(), /*first=*/false);
    ctx.in.irxRels.push_back(std::move(rel));
  }
}

// PT_SCE_IOPMOD for .iopmod, then the single PT_LOAD.
SmallVector<std::unique_ptr<PhdrEntry>, 0> elf::createIrxPhdrs(Ctx &ctx) {
  SmallVector<std::unique_ptr<PhdrEntry>, 0> ret;
  ret.push_back(std::make_unique<PhdrEntry>(ctx, PT_SCE_IOPMOD, PF_R));
  ret.back()->add(ctx.in.iopMod->getParent());
  ret.push_back(std::make_unique<PhdrEntry>(ctx, PT_LOAD, PF_R | PF_W | PF_X));
  for (OutputSection *os : ctx.outputSections)
    if (os->flags & SHF_ALLOC)
      ret.back()->add(os);
  return ret;
}

// Points the reserved symbols at their segments. gp is set in finalizeIrx().
void elf::setIrxReservedSymbolSections(Ctx &ctx) {
  IrxSegments s = getSegments(ctx);
  if (s.secs.empty())
    return;
  auto set = [](Defined *d, OutputSection *os, uint64_t value) {
    if (d) {
      d->section = os;
      d->value = value;
    }
  };
  // An empty segment starts where the next one does: at the end of the one
  // before it.
  OutputSection *last = s.secs.back();
  auto startOf =
      [&](OutputSection *first,
          OutputSection *prevLast) -> std::pair<OutputSection *, uint64_t> {
    if (first)
      return {first, 0};
    if (prevLast)
      return {prevLast, (uint64_t)-1};
    return {s.secs.front(), 0};
  };
  auto [ftext, ftextV] = startOf(s.firstText, nullptr);
  auto [etext, etextV] = s.lastText ? std::pair{s.lastText, (uint64_t)-1}
                                    : std::pair{ftext, ftextV};
  auto [fdata, fdataV] = startOf(s.firstData, s.lastText);
  OutputSection *prevData = s.lastData ? s.lastData : s.lastText;
  auto [edata, edataV] =
      prevData ? std::pair{prevData, (uint64_t)-1} : std::pair{fdata, fdataV};
  auto [fbss, fbssV] = startOf(s.firstBss, prevData);

  set(ctx.sym.irxFtext, ftext, ftextV);
  set(ctx.sym.etext1, etext, etextV);
  set(ctx.sym.etext2, etext, etextV);
  set(ctx.sym.irxFdata, fdata, fdataV);
  set(ctx.sym.edata1, edata, edataV);
  set(ctx.sym.edata2, edata, edataV);
  set(ctx.sym.irxFbss, fbss, fbssV);
  set(ctx.sym.end1, last, -1);
  set(ctx.sym.end2, last, -1);
}

static uint64_t endOf(const OutputSection *os) { return os->addr + os->size; }

// After the addresses are assigned: checks the layout, sets gp and fills in
// .iopmod and the links of the .rel sections.
void elf::finalizeIrx(Ctx &ctx) {
  IrxSegments s = getSegments(ctx);
  IopModSection &mod = iopMod(ctx);
  if (s.secs.empty()) {
    Err(ctx) << "--irx: the module has no loaded sections";
    return;
  }

  // TEXT, DATA and BSS follow each other from address 0.
  if (s.secs.front()->addr != 0)
    Err(ctx) << "--irx: the module must start at address 0, "
             << s.secs.front()->name << " is at 0x"
             << utohexstr(s.secs.front()->addr);
  int prevKind = 0;
  OutputSection *prev = nullptr;
  for (OutputSection *os : s.secs) {
    int kind = isText(os) ? 0 : isBss(os) ? 2 : 1;
    if (kind < prevKind)
      Err(ctx) << "--irx: section " << os->name << " follows " << prev->name
               << ": TEXT, DATA and BSS sections must come in that order";
    if (prev && os->addr < endOf(prev))
      Err(ctx) << "--irx: section " << os->name << " overlaps " << prev->name;
    prevKind = kind;
    prev = os;
  }

  uint64_t end = endOf(s.secs.back());
  uint64_t dataStart = s.firstData  ? s.firstData->addr
                       : s.firstBss ? s.firstBss->addr
                                    : end;
  uint64_t bssStart = s.firstBss ? s.firstBss->addr : end;
  mod.textSize = dataStart;
  mod.dataSize = bssStart - dataStart;
  mod.bssSize = end - bssStart;

  // gp is 0x7ff0 past the small data, or past where it would be: the start of
  // BSS. It moves with the module, so _gp stays relative to a section.
  uint64_t gpBase = s.firstGp ? s.firstGp->addr : bssStart;
  mod.gp = gpBase + 0x7ff0;
  if (Defined *gp = ctx.sym.mipsGp) {
    OutputSection *gpSec = s.secs.front();
    for (OutputSection *os : s.secs)
      if (os->addr <= gpBase)
        gpSec = os;
    gp->section = gpSec;
    gp->value = mod.gp - gpSec->addr;
  }

  Symbol *entry = ctx.symtab->find(ctx.arg.entry);
  if (!entry || !entry->isDefined())
    Err(ctx) << "--irx: entry symbol " << ctx.arg.entry << " is not defined";
  else
    mod.entry = entry->getVA(ctx);

  for (StringRef name : {"_irx_id", "Module"}) {
    auto *d = dyn_cast_or_null<Defined>(ctx.symtab->find(name));
    if (!d)
      continue;
    auto *isec = dyn_cast_or_null<InputSectionBase>(d->section);
    if (!isec || isec->type == SHT_NOBITS ||
        d->value + 6 > isec->content().size()) {
      Err(ctx) << "--irx: " << name << " is not an initialized irx_id";
      break;
    }
    // struct irx_id { const char *name; u16 version; }
    mod.moduleInfo = d->getVA(ctx);
    mod.moduleVersion = read16le(isec->content().data() + d->value + 4);
    break;
  }

  uint32_t symtab =
      ctx.in.symTab ? ctx.in.symTab->getParent()->sectionIndex : 0;
  for (auto &sec : ctx.in.irxRels) {
    auto &rel = static_cast<IrxRelSection &>(*sec);
    rel.getParent()->link = symtab;
    rel.getParent()->info = rel.target->sectionIndex;
  }
}

// The file layout of the IOP object format.
uint64_t elf::assignIrxFileOffsets(Ctx &ctx, uint64_t shdrSize,
                                   uint64_t &shOff) {
  ctx.out.programHeaders->offset = ctx.out.elfHeader->size;
  uint64_t off = ctx.out.elfHeader->size + ctx.out.programHeaders->size;
  auto place = [&](OutputSection *os) {
    os->offset = alignToPowerOf2(off, os->addralign);
    off = os->offset + os->size;
  };

  // The module information, where the loader's first read finds it.
  OutputSection *iopModSec = ctx.in.iopMod->getParent();
  place(iopModSec);

  // The PT_LOAD: file offsets follow the addresses.
  OutputSection *first = nullptr;
  for (OutputSection *os : ctx.outputSections) {
    if (!(os->flags & SHF_ALLOC))
      continue;
    if (!first) {
      first = os;
      os->offset = alignTo(off, ctx.arg.maxPageSize, os->addr);
    } else {
      os->offset = first->offset + os->addr - first->addr;
    }
    if (os->type != SHT_NOBITS)
      off = os->offset + os->size;
  }

  auto isLate = [&](OutputSection *os) {
    return isStaticRelSecType(os->type) ||
           (ctx.in.symTab && os == ctx.in.symTab->getParent()) ||
           (ctx.in.strTab && os == ctx.in.strTab->getParent()) ||
           (ctx.in.symTabShndx && os == ctx.in.symTabShndx->getParent());
  };
  for (OutputSection *os : ctx.outputSections)
    if (!(os->flags & SHF_ALLOC) && os != iopModSec && !isLate(os))
      place(os);

  shOff = alignToPowerOf2(off, 4);
  off = shOff + shdrSize;

  for (OutputSection *os : ctx.outputSections)
    if (isStaticRelSecType(os->type))
      place(os);
  for (OutputSection *os : ctx.outputSections)
    if (!(os->flags & SHF_ALLOC) && isLate(os) && !isStaticRelSecType(os->type))
      place(os);
  return off;
}

// The module information header takes no memory.
void elf::finalizeIrxPhdrs(Ctx &ctx) {
  for (auto &p : ctx.phdrs)
    if (p->p_type == PT_SCE_IOPMOD)
      p->p_vaddr = p->p_paddr = p->p_memsz = 0;
}
