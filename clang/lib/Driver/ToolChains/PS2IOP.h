//===--- PS2IOP.h - PS2 IOP module (IRX) ToolChain --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_PS2IOP_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_PS2IOP_H

#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"

namespace clang {
namespace driver {
namespace toolchains {

/// PlayStation 2 IOP modules (mipsel-none-irx): freestanding R3000A code,
/// soft float, without abicalls or gp-relative data, linked by ld.lld --irx
/// into an IRX, the IOP's relocatable executable.
class LLVM_LIBRARY_VISIBILITY PS2IOP : public ToolChain {
public:
  PS2IOP(const Driver &D, const llvm::Triple &Triple,
         const llvm::opt::ArgList &Args);

  bool HasNativeLLVMSupport() const override { return true; }
  bool isPICDefault() const override { return false; }
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override {
    return false;
  }
  bool isPICDefaultForced() const override { return false; }
  bool SupportsProfiling() const override { return false; }

  UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &Args) const override {
    return UnwindTableLevel::None;
  }

  const char *getDefaultLinker() const override { return "ld.lld"; }
  RuntimeLibType GetDefaultRuntimeLibType() const override {
    return ToolChain::RLT_CompilerRT;
  }

  void
  AddClangSystemIncludeArgs(const llvm::opt::ArgList &DriverArgs,
                            llvm::opt::ArgStringList &CC1Args) const override;
  void addClangTargetOptions(const llvm::opt::ArgList &DriverArgs,
                             llvm::opt::ArgStringList &CC1Args, BoundArch,
                             Action::OffloadKind) const override;

protected:
  Tool *buildLinker() const override;
};

} // end namespace toolchains

namespace tools {
namespace ps2iop {

class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("ps2iop::Linker", "ld.lld", TC) {}
  bool isLinkJob() const override { return true; }
  bool hasIntegratedCPP() const override { return false; }
  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

} // end namespace ps2iop
} // end namespace tools

} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_PS2IOP_H
