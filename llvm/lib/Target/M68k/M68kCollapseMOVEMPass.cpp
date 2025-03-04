//===----- M68kCollapseMOVEMPass.cpp - Expand MOVEM pass --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a pass that collapses sequential MOVEM instructions into
/// a single one.
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
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

#define DEBUG_TYPE "M68k-collapse-movem"

namespace {

enum UpdateType { Ascending, Descending, Intermixed };

struct MOVEMState {
private:
  MachineBasicBlock::iterator Begin;
  MachineBasicBlock::iterator End;

  unsigned Base;

  int Start;
  int Stop;

  unsigned Mask;

  enum { None, Load, Store } Type;

public:
  MOVEMState()
      : Begin(nullptr), End(nullptr), Base(0), Start(INT_MIN), Stop(INT_MAX),
        Mask(0), Type(None) {}

  void setBegin(MachineBasicBlock::iterator &MI) {
    assert(Begin == nullptr);
    Begin = MI;
  }

  void setEnd(MachineBasicBlock::iterator &MI) {
    assert(End == nullptr);
    End = MI;
  }

  bool hasBase() { return Base != 0; }

  unsigned getBase() {
    assert(Base);
    return Base;
  }

  MachineBasicBlock::iterator begin() {
    assert(Begin != nullptr);
    return Begin;
  }

  MachineBasicBlock::iterator end() {
    assert(End != nullptr);
    return End;
  }

  unsigned getMask() { return Mask; }

  void setBase(int Value) {
    assert(!hasBase());
    Base = Value;
  }

  // You need to call this before Mask update
  UpdateType classifyUpdateByMask(unsigned Value) {
    assert(Value);

    if (Mask == 0) {
      return Ascending;
    } else if (Mask < Value) {
      return Ascending;
    } else if (Mask > Value) {
      return Descending;
    }

    return Intermixed;
  }

  bool update(int O, int M) {
    UpdateType Type = classifyUpdateByMask(M);
    if (Type == Intermixed)
      return false;
    if (Start == INT_MIN) {
      Start = Stop = O;
      updateMask(M);
      return true;
    } else if (Type == Descending && O == Start - 4) {
      Start -= 4;
      updateMask(M);
      return true;
    } else if (Type == Ascending && O == Stop + 4) {
      Stop += 4;
      updateMask(M);
      return true;
    }

    return false;
  }

  int getFinalOffset() {
    assert(
        Start != INT_MIN &&
        "MOVEM in control mode should increment the address in each iteration");
    return Start;
  }

  bool updateMask(unsigned Value) {
    assert(isUInt<16>(Value) && "Mask must fit 16 bit");
    assert(!(Value & Mask) &&
           "This is weird, there should be no intersections");
    Mask |= Value;
    return true;
  }

  void setLoad() { Type = Load; }
  void setStore() { Type = Store; }

  bool isLoad() { return Type == Load; }
  bool isStore() { return Type == Store; }
};

class M68kCollapseMOVEM : public MachineFunctionPass {
public:
  static char ID;

  const M68kSubtarget *STI;
  const M68kInstrInfo *TII;
  const M68kRegisterInfo *TRI;
  const M68kMachineFunctionInfo *MFI;
  const M68kFrameLowering *FL;

  M68kCollapseMOVEM() : MachineFunctionPass(ID) {}

  void Finish(MachineBasicBlock &MBB, MOVEMState &State) {
    auto MI = State.begin();
    auto End = State.end();
    DebugLoc DL = MI->getDebugLoc();

    // No need to delete then add a single instruction
    if (std::next(MI) == End) {
      State = MOVEMState();
      return;
    }

    // Delete all the MOVEM instruction till the end
    while (MI != End) {
      auto Next = std::next(MI);
      MBB.erase(MI);
      MI = Next;
    }

    // Add a unified one
    if (State.isLoad()) {
      BuildMI(MBB, End, DL, TII->get(M68k::MOVM32mp))
          .addImm(State.getMask())
          .addImm(State.getFinalOffset())
          .addReg(State.getBase());
    } else {
      BuildMI(MBB, End, DL, TII->get(M68k::MOVM32pm))
          .addImm(State.getFinalOffset())
          .addReg(State.getBase())
          .addImm(State.getMask());
    }

    State = MOVEMState();
  }

  bool ProcessMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                 MOVEMState &State, unsigned Mask, int Offset, unsigned Reg,
                 bool isStore = false) {
    if (State.hasBase()) {
      // If current Type, Reg, Offset and Mask is in proper order  then
      // merge in the state
      MOVEMState Temp = State;
      if (State.isStore() == isStore && State.getBase() == Reg &&
          State.update(Offset, Mask)) {
        return true;
        // Otherwise we Finish processing of the current MOVEM sequance and
        // start a new one
      } else {
        State = Temp;
        State.setEnd(MI);
        Finish(MBB, State);
        return ProcessMI(MBB, MI, State, Mask, Offset, Reg, isStore);
      }
      // If this is the first instruction is sequance then initialize the State
    } else if (Reg == TRI->getStackRegister() ||
               Reg == TRI->getBaseRegister() ||
               Reg == TRI->getFrameRegister(*MBB.getParent())) {
      State.setBegin(MI);
      State.setBase(Reg);
      State.update(Offset, Mask);
      isStore ? State.setStore() : State.setLoad();
      return true;
    }
    return false;
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    STI = &static_cast<const M68kSubtarget &>(MF.getSubtarget());
    TII = STI->getInstrInfo();
    TRI = STI->getRegisterInfo();
    MFI = MF.getInfo<M68kMachineFunctionInfo>();
    FL = STI->getFrameLowering();

    bool Modified = false;

    MOVEMState State;

    unsigned Mask = 0;
    unsigned Reg = 0;
    int Offset = 0;

    for (auto &MBB : MF) {
      auto MI = MBB.begin(), E = MBB.end();
      while (MI != E) {
        // Processing might change current instruction, save next first
        auto NMI = std::next(MI);
        switch (MI->getOpcode()) {
        default:
          if (State.hasBase()) {
            State.setEnd(MI);
            Finish(MBB, State);
            Modified = true;
          }
          break;
        case M68k::MOVM32jm:
          Mask = MI->getOperand(1).getImm();
          Reg = MI->getOperand(0).getReg();
          Offset = 0;
          Modified |= ProcessMI(MBB, MI, State, Mask, Offset, Reg, true);
          break;
        case M68k::MOVM32pm:
          Mask = MI->getOperand(2).getImm();
          Reg = MI->getOperand(1).getReg();
          Offset = MI->getOperand(0).getImm();
          Modified |= ProcessMI(MBB, MI, State, Mask, Offset, Reg, true);
          break;
        case M68k::MOVM32mj:
          Mask = MI->getOperand(0).getImm();
          Reg = MI->getOperand(1).getReg();
          Offset = 0;
          Modified |= ProcessMI(MBB, MI, State, Mask, Offset, Reg, false);
          break;
        case M68k::MOVM32mp:
          Mask = MI->getOperand(0).getImm();
          Reg = MI->getOperand(2).getReg();
          Offset = MI->getOperand(1).getImm();
          Modified |= ProcessMI(MBB, MI, State, Mask, Offset, Reg, false);
          break;
        }
        MI = NMI;
      }

      if (State.hasBase()) {
        State.setEnd(MI);
        Finish(MBB, State);
      }
    }

    return Modified;
  }

  StringRef getPassName() const override { return "M68k MOVEM collapser pass"; }
};

char M68kCollapseMOVEM::ID = 0;
} // anonymous namespace.

/// Returns an instance of the pseudo instruction expansion pass.
FunctionPass *llvm::createM68kCollapseMOVEMPass() {
  return new M68kCollapseMOVEM();
}
