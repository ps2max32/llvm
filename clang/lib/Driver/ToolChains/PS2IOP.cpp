//===--- PS2IOP.cpp - PS2 IOP module (IRX) ToolChain ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PS2IOP.h"
#include "clang/Driver/CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/InputInfo.h"
#include "clang/Options/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

PS2IOP::PS2IOP(const Driver &D, const llvm::Triple &Triple, const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  getProgramPaths().push_back(D.Dir);
  if (!D.SysRoot.empty()) {
    SmallString<128> Lib(D.SysRoot);
    llvm::sys::path::append(Lib, "lib");
    getFilePaths().push_back(std::string(Lib));
  }
}

void PS2IOP::addClangTargetOptions(const ArgList &DriverArgs,
                                   ArgStringList &CC1Args, BoundArch,
                                   Action::OffloadKind) const {
  // Only the include paths below: none of the host's.
  CC1Args.push_back("-nostdsysteminc");
  // The IOP side of the PS2 SDK headers selects itself with _IOP.
  CC1Args.push_back("-D_IOP");
}

void PS2IOP::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                       ArgStringList &CC1Args) const {
  const Driver &D = getDriver();
  if (DriverArgs.hasArg(options::OPT_nostdinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> ResourceDir(D.ResourceDir);
    llvm::sys::path::append(ResourceDir, "include");
    addSystemInclude(DriverArgs, CC1Args, ResourceDir);
  }

  if (DriverArgs.hasArg(options::OPT_nostdlibinc) || D.SysRoot.empty())
    return;
  SmallString<128> Include(D.SysRoot);
  llvm::sys::path::append(Include, "include");
  addExternCSystemInclude(DriverArgs, CC1Args, Include);
}

Tool *PS2IOP::buildLinker() const { return new tools::ps2iop::Linker(*this); }

void tools::ps2iop::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                         const InputInfo &Output,
                                         const InputInfoList &Inputs,
                                         const ArgList &Args,
                                         const char *LinkingOutput) const {
  const ToolChain &TC = getToolChain();
  ArgStringList CmdArgs;

  CmdArgs.push_back("-m");
  CmdArgs.push_back("elf32ltsmip");
  // A partial link (-r) stays a relocatable object; everything else is an
  // IRX. IRX modules have no startup files or C library: they import what
  // they use from other modules.
  if (!Args.hasArg(options::OPT_r))
    CmdArgs.push_back("--irx");

  Args.addAllArgs(CmdArgs, {options::OPT_L});
  TC.AddFilePathLibArgs(Args, CmdArgs);
  // -r itself comes with the linker inputs.
  Args.addAllArgs(CmdArgs, {options::OPT_u, options::OPT_T_Group,
                            options::OPT_s, options::OPT_t});

  if (auto LTO = TC.getLTOMode(Args); LTO != LTOK_None)
    addLTOOptions(TC, Args, CmdArgs, Output, Inputs, LTO == LTOK_Thin);

  AddLinkerInputs(TC, Inputs, Args, CmdArgs, JA);

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  C.addCommand(std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileCurCP(),
      Args.MakeArgString(TC.GetLinkerPath()), CmdArgs, Inputs, Output));
}
