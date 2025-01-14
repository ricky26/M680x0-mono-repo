//===-- M68kTargetMachine.cpp - M68k target machine ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains implementation for M68k target machine.
///
//===----------------------------------------------------------------------===//

#include "M68kTargetMachine.h"
#include "M68k.h"

#include "M68kSubtarget.h"
#include "M68kTargetObjectFile.h"

#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/TargetRegistry.h"
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "m68k"

extern "C" void LLVMInitializeM68kTarget() {
  RegisterTargetMachine<M68kTargetMachine> X(TheM68kTarget);
}

namespace {

// FIXME This layout is true for M68000 original cpu, other variants will
// affect DL computation
std::string computeDataLayout(const Triple &TT, StringRef CPU,
                              const TargetOptions &Options) {
  std::string Ret = "";
  // M68k is Big Endian
  Ret += "E";

  // FIXME #28 how to wire it with the used object format?
  Ret += "-m:e";

  // M68k pointers are always 32 bit wide even for 16 bit cpus
  Ret += "-p:32:32";

  // M68k requires i8 to align on 2 byte boundry
  Ret += "-i8:8:8-i16:16:16-i32:32:32";

  // FIXME #29 no floats at the moment

  // The registers can hold 8, 16, 32 bits
  Ret += "-n8:16:32";

  // Aggregates are 32 bit aligned and stack is 16 bit aligned
  Ret += "-a:0:32-S16";

  return Ret;
}

Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                    Optional<Reloc::Model> RM) {
  // If not defined we default to static
  if (!RM.hasValue()) {
    return Reloc::Static;
  }

  return *RM;
}

CodeModel::Model getEffectiveCodeModel(Optional<CodeModel::Model> CM,
                                       bool JIT) {
  if (!CM) {
    return CodeModel::Small;
  } else if (CM == CodeModel::Large) {
    llvm_unreachable("Large code model is not supported");
  } else if (CM == CodeModel::Kernel) {
    // FIXME #31 Kernel afaik is small cm plus some weird binding
    llvm_unreachable("Kernel code model is not supported");
  }
  return CM.getValue();
}
} // end anonymous namespace

M68kTargetMachine::M68kTargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     Optional<Reloc::Model> RM,
                                     Optional<CodeModel::Model> CM,
                                     CodeGenOpt::Level OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT, CPU, Options), TT, CPU, FS,
                        Options, getEffectiveRelocModel(TT, RM),
                        ::getEffectiveCodeModel(CM, JIT), OL),
      TLOF(std::make_unique<M68kELFTargetObjectFile>()),
      Subtarget(TT, CPU, FS, *this) {
  initAsmInfo();
}

M68kTargetMachine::~M68kTargetMachine() {}

const M68kSubtarget *
M68kTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  auto CPU = CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  auto FS = FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  auto &I = SubtargetMap[CPU + FS];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = std::make_unique<M68kSubtarget>(TargetTriple, CPU, FS, *this);
  }
  return I.get();
}

//===----------------------------------------------------------------------===//
// Pass Pipeline Configuration
//===----------------------------------------------------------------------===//

namespace {
class M68kPassConfig : public TargetPassConfig {
public:
  M68kPassConfig(M68kTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  M68kTargetMachine &getM68kTargetMachine() const {
    return getTM<M68kTargetMachine>();
  }

  const M68kSubtarget &getM68kSubtarget() const {
    return *getM68kTargetMachine().getSubtargetImpl();
  }

  bool addInstSelector() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *M68kTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new M68kPassConfig(*this, PM);
}

bool M68kPassConfig::addInstSelector() {
  // Install an instruction selector.
  addPass(createM68kISelDag(getM68kTargetMachine()));
  addPass(createM68kGlobalBaseRegPass());
  return false;
}

void M68kPassConfig::addPreSched2() { addPass(createM68kExpandPseudoPass()); }

void M68kPassConfig::addPreEmitPass() {
  addPass(createM68kCollapseMOVEMPass());
  // addPass(createM68kConvertMOVToMOVMPass());
}
