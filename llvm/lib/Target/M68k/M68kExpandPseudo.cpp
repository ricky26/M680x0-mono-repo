//===--M68kExpandPseudo.cpp - Expand pseudo instructions ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a pass that expands pseudo instructions into target
/// instructions to allow proper scheduling, if-conversion, other late
/// optimizations, or simply the encoding of the instructions.
///
//===----------------------------------------------------------------------===//

#include "M68k.h"
#include "M68kFrameLowering.h"
#include "M68kInstrInfo.h"
#include "M68kMachineFunction.h"
#include "M68kSubtarget.h"

#include "llvm/Analysis/EHPersonalities.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h" // For IDs of passes that are preserved.
#include "llvm/IR/GlobalValue.h"

using namespace llvm;

#define DEBUG_TYPE "M68k-expand-pseudos"

namespace {
class M68kExpandPseudo : public MachineFunctionPass {
public:
  static char ID;
  M68kExpandPseudo() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreservedID(MachineLoopInfoID);
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  const M68kSubtarget *STI;
  const M68kInstrInfo *TII;
  const M68kRegisterInfo *TRI;
  const M68kMachineFunctionInfo *MFI;
  const M68kFrameLowering *FL;

  bool runOnMachineFunction(MachineFunction &Fn) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override {
    return "M68k pseudo instruction expansion pass";
  }

private:
  bool ExpandMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI);
  bool ExpandMBB(MachineBasicBlock &MBB);
};
char M68kExpandPseudo::ID = 0;
} // End anonymous namespace.

/// If \p MBBI is a pseudo instruction, this method expands
/// it to the corresponding (sequence of) actual instruction(s).
/// \returns true if \p MBBI has been expanded.
bool M68kExpandPseudo::ExpandMI(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI) {
  MachineInstr &MI = *MBBI;
  MachineInstrBuilder MIB(*MI.getParent()->getParent(), MI);
  unsigned Opcode = MI.getOpcode();
  DebugLoc DL = MBBI->getDebugLoc();
  switch (Opcode) {
  default:
    return false;
    /// TODO #37 would be nice to infer all these parameters

  case M68k::MOVXd16d8:
    return TII->ExpandMOVX_RR(MIB, MVT::i16, MVT::i8);
  case M68k::MOVXd32d8:
    return TII->ExpandMOVX_RR(MIB, MVT::i32, MVT::i8);
  case M68k::MOVXd32d16:
    return TII->ExpandMOVX_RR(MIB, MVT::i32, MVT::i16);

  case M68k::MOVSXd16d8:
    return TII->ExpandMOVSZX_RR(MIB, true, MVT::i16, MVT::i8);
  case M68k::MOVSXd32d8:
    return TII->ExpandMOVSZX_RR(MIB, true, MVT::i32, MVT::i8);
  case M68k::MOVSXd32d16:
    return TII->ExpandMOVSZX_RR(MIB, true, MVT::i32, MVT::i16);

  case M68k::MOVZXd16d8:
    return TII->ExpandMOVSZX_RR(MIB, false, MVT::i16, MVT::i8);
  case M68k::MOVZXd32d8:
    return TII->ExpandMOVSZX_RR(MIB, false, MVT::i32, MVT::i8);
  case M68k::MOVZXd32d16:
    return TII->ExpandMOVSZX_RR(MIB, false, MVT::i32, MVT::i16);

  case M68k::MOVSXd16j8:
    return TII->ExpandMOVSZX_RM(MIB, true, TII->get(M68k::MOV8dj), MVT::i16,
                                MVT::i8);
  case M68k::MOVSXd32j8:
    return TII->ExpandMOVSZX_RM(MIB, true, TII->get(M68k::MOV8dj), MVT::i32,
                                MVT::i8);
  case M68k::MOVSXd32j16:
    return TII->ExpandMOVSZX_RM(MIB, true, TII->get(M68k::MOV16rj), MVT::i32,
                                MVT::i16);

  case M68k::MOVZXd16j8:
    return TII->ExpandMOVSZX_RM(MIB, false, TII->get(M68k::MOV8dj), MVT::i16,
                                MVT::i8);
  case M68k::MOVZXd32j8:
    return TII->ExpandMOVSZX_RM(MIB, false, TII->get(M68k::MOV8dj), MVT::i32,
                                MVT::i8);
  case M68k::MOVZXd32j16:
    return TII->ExpandMOVSZX_RM(MIB, false, TII->get(M68k::MOV16rj), MVT::i32,
                                MVT::i16);

  case M68k::MOVSXd16p8:
    return TII->ExpandMOVSZX_RM(MIB, true, TII->get(M68k::MOV8dp), MVT::i16,
                                MVT::i8);
  case M68k::MOVSXd32p8:
    return TII->ExpandMOVSZX_RM(MIB, true, TII->get(M68k::MOV8dp), MVT::i32,
                                MVT::i8);
  case M68k::MOVSXd32p16:
    return TII->ExpandMOVSZX_RM(MIB, true, TII->get(M68k::MOV16rp), MVT::i32,
                                MVT::i16);

  case M68k::MOVZXd16p8:
    return TII->ExpandMOVSZX_RM(MIB, false, TII->get(M68k::MOV8dp), MVT::i16,
                                MVT::i8);
  case M68k::MOVZXd32p8:
    return TII->ExpandMOVSZX_RM(MIB, false, TII->get(M68k::MOV8dp), MVT::i32,
                                MVT::i8);
  case M68k::MOVZXd32p16:
    return TII->ExpandMOVSZX_RM(MIB, false, TII->get(M68k::MOV16rp), MVT::i32,
                                MVT::i16);

  case M68k::MOVSXd16f8:
    return TII->ExpandMOVSZX_RM(MIB, true, TII->get(M68k::MOV8df), MVT::i16,
                                MVT::i8);
  case M68k::MOVSXd32f8:
    return TII->ExpandMOVSZX_RM(MIB, true, TII->get(M68k::MOV8df), MVT::i32,
                                MVT::i8);
  case M68k::MOVSXd32f16:
    return TII->ExpandMOVSZX_RM(MIB, true, TII->get(M68k::MOV16rf), MVT::i32,
                                MVT::i16);

  case M68k::MOVZXd16f8:
    return TII->ExpandMOVSZX_RM(MIB, false, TII->get(M68k::MOV8df), MVT::i16,
                                MVT::i8);
  case M68k::MOVZXd32f8:
    return TII->ExpandMOVSZX_RM(MIB, false, TII->get(M68k::MOV8df), MVT::i32,
                                MVT::i8);
  case M68k::MOVZXd32f16:
    return TII->ExpandMOVSZX_RM(MIB, false, TII->get(M68k::MOV16rf), MVT::i32,
                                MVT::i16);

  case M68k::MOV8cd:
    return TII->ExpandCCR(MIB, /* isToCCR */ true);
  case M68k::MOV8dc:
    return TII->ExpandCCR(MIB, /* isToCCR */ false);

  case M68k::MOVM8jm_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32jm), /* isRM */ false);
  case M68k::MOVM16jm_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32jm), /* isRM */ false);
  case M68k::MOVM32jm_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32jm), /* isRM */ false);

  case M68k::MOVM8pm_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32pm), /* isRM */ false);
  case M68k::MOVM16pm_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32pm), /* isRM */ false);
  case M68k::MOVM32pm_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32pm), /* isRM */ false);

  case M68k::MOVM8mj_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32mj), /* isRM */ true);
  case M68k::MOVM16mj_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32mj), /* isRM */ true);
  case M68k::MOVM32mj_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32mj), /* isRM */ true);

  case M68k::MOVM8mp_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32mp), /* isRM */ true);
  case M68k::MOVM16mp_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32mp), /* isRM */ true);
  case M68k::MOVM32mp_P:
    return TII->ExpandMOVEM(MIB, TII->get(M68k::MOVM32mp), /* isRM */ true);

  case M68k::TCRETURNq:
  case M68k::TCRETURNj: {
    MachineOperand &JumpTarget = MI.getOperand(0);
    MachineOperand &StackAdjust = MI.getOperand(1);
    assert(StackAdjust.isImm() && "Expecting immediate value.");

    // Adjust stack pointer.
    int StackAdj = StackAdjust.getImm();
    int MaxTCDelta = MFI->getTCReturnAddrDelta();
    int Offset = 0;
    assert(MaxTCDelta <= 0 && "MaxTCDelta should never be positive");

    // Incoporate the retaddr area.
    Offset = StackAdj - MaxTCDelta;
    assert(Offset >= 0 && "Offset should never be negative");

    if (Offset) {
      // Check for possible merge with preceding ADD instruction.
      Offset += FL->mergeSPUpdates(MBB, MBBI, true);
      FL->emitSPUpdate(MBB, MBBI, Offset, /*InEpilogue=*/true);
    }

    // Jump to label or value in register.
    if (Opcode == M68k::TCRETURNq) {
      MachineInstrBuilder MIB =
          BuildMI(MBB, MBBI, DL, TII->get(M68k::TAILJMPq));
      if (JumpTarget.isGlobal()) {
        MIB.addGlobalAddress(JumpTarget.getGlobal(), JumpTarget.getOffset(),
                             JumpTarget.getTargetFlags());
      } else {
        assert(JumpTarget.isSymbol());
        MIB.addExternalSymbol(JumpTarget.getSymbolName(),
                              JumpTarget.getTargetFlags());
      }
    } else {
      BuildMI(MBB, MBBI, DL, TII->get(M68k::TAILJMPj))
          .addReg(JumpTarget.getReg(), RegState::Kill);
    }

    MachineInstr &NewMI = *std::prev(MBBI);
    NewMI.copyImplicitOps(*MBBI->getParent()->getParent(), *MBBI);

    // Delete the pseudo instruction TCRETURN.
    MBB.erase(MBBI);

    return true;
  }
  case M68k::RET: {
    // Adjust stack to erase error code
    int64_t StackAdj = MBBI->getOperand(0).getImm();
    MachineInstrBuilder MIB;

    if (StackAdj == 0) {
      MIB = BuildMI(MBB, MBBI, DL, TII->get(M68k::RTS));
    } else if (isUInt<16>(StackAdj)) {

      if (STI->atLeastM68020()) {
        llvm_unreachable("RTD is not implemented");
        // MIB = BuildMI(MBB, MBBI, DL, TII->get(M68k::RTD)).addImm(StackAdj);
      } else {
        // Copy PC from stack to a free address(A0 or A1) register
        // TODO #38 check if it is really free
        BuildMI(MBB, MBBI, DL, TII->get(M68k::MOV32aj), M68k::A1)
            .addReg(M68k::SP);

        // Adjust SP
        FL->emitSPUpdate(MBB, MBBI, StackAdj, /*InEpilogue=*/true);

        // Put the return address on stack
        BuildMI(MBB, MBBI, DL, TII->get(M68k::MOV32ja))
            .addReg(M68k::SP)
            .addReg(M68k::A1);

        // RTS
        BuildMI(MBB, MBBI, DL, TII->get(M68k::RTS));
      }
    } else {
      // TODO: RTD can only handle immediates as big as 2**16-1.
      // If we need to pop off bytes before the return address, we
      // must do it manually.
      llvm_unreachable("Stack adjustment size not supported");
    }

    // FIXME: Can rest of the operands be ignored, if there is any?
    MBB.erase(MBBI);
    return true;
  }
  }
  llvm_unreachable("Previous switch has a fallthrough?");
}

/// Expand all pseudo instructions contained in \p MBB.
/// \returns true if any expansion occurred for \p MBB.
bool M68kExpandPseudo::ExpandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  // MBBI may be invalidated by the expansion.
  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= ExpandMI(MBB, MBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool M68kExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  STI = &static_cast<const M68kSubtarget &>(MF.getSubtarget());
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  MFI = MF.getInfo<M68kMachineFunctionInfo>();
  FL = STI->getFrameLowering();

  bool Modified = false;
  for (MachineBasicBlock &MBB : MF)
    Modified |= ExpandMBB(MBB);
  return Modified;
}

/// Returns an instance of the pseudo instruction expansion pass.
FunctionPass *llvm::createM68kExpandPseudoPass() {
  return new M68kExpandPseudo();
}
