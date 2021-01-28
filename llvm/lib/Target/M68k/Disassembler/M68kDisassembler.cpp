//===- M68kDisassembler.cpp - Disassembler for M68k -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the M68k Disassembler.
//
//===----------------------------------------------------------------------===//

#include "M68k.h"
#include "M68kRegisterInfo.h"
#include "M68kSubtarget.h"
#include "MCTargetDesc/M68kMCTargetDesc.h"
#include "MCTargetDesc/M68kMCCodeEmitter.h"
#include "TargetInfo/M68kTargetInfo.h"

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/TargetRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "m68k-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {
struct M68kInstructionLookup {
  unsigned OpCode;
  unsigned Length;
  uint64_t Mask;
  uint64_t Value;

  M68kInstructionLookup(unsigned OpCode)
      : OpCode(OpCode), Length(0), Mask(0), Value(0) {}

  void addBits(uint64_t Bits, unsigned Count) {
    uint64_t BitMask = (1 << Count) - 1;
    Mask |= BitMask << Length;
    Value |= (Bits & BitMask) << Length;
    Length += Count;
  }

  void addWildcard(unsigned Count) {
    Length += Count;
  }

  // Check whether this instruction could possibly match the given bytes.
  bool matches(uint64_t Test, unsigned NumBits) const {
    if (Length > NumBits) {
      return false;
    }
    return (Test & Mask) == Value;
  }
};

/// A disassembler class for M68k.
class M68kDisassembler : public MCDisassembler {
  MCInstrInfo* MCII;
  std::vector<M68kInstructionLookup> Lookups;

public:
  M68kDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx, MCInstrInfo* MCII)
      : MCDisassembler(STI, Ctx), MCII(MCII) {
    buildBeadTable();
  }
  virtual ~M68kDisassembler() {}

  void buildBeadTable();
  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
  void decodeReg(MCInst &Instr, unsigned int Bead,
                 uint64_t Value, unsigned &NumRead, unsigned &Scratch) const;
  void decodeImm(MCInst &Instr, unsigned int Bead,
                 uint64_t Value, unsigned &NumRead, unsigned &Scratch) const;
  unsigned int getRegOperandIndex(MCInst &Instr, unsigned int Bead) const;
  unsigned int getImmOperandIndex(MCInst &Instr, unsigned int Bead) const;
};

static unsigned RegisterDecode[] = {
    M68k::A0, M68k::A1, M68k::A2, M68k::A3,
    M68k::A4, M68k::A5, M68k::A6, M68k::SP,
    M68k::D0, M68k::D1, M68k::D2, M68k::D3,
    M68k::D4, M68k::D5, M68k::D6, M68k::D7,
};

} // namespace

// This is a bit of a hack: we can't generate this table at table-gen time
// because some of the definitions are in our platform.
void M68kDisassembler::buildBeadTable() {
  const unsigned NumInstr = M68k::getNumMCInstrBeads();
  Lookups.reserve(NumInstr);

  for (unsigned I = 0; I < NumInstr; ++I) {
    M68kInstructionLookup Lookup(I);

    for (const uint8_t* PartPtr = M68k::getMCInstrBeads(I);
         *PartPtr; ++PartPtr) {
      uint8_t Bead = *PartPtr;
      unsigned Ext = Bead >> 4;
      unsigned Op = Bead & 0xf;

      switch (Op) {
      case M68kBeads::Ctrl:
        switch (Ext) {
        case M68kBeads::Ignore:
          continue;

        default:
          // Term will have already been skipped by the loop.
          llvm_unreachable("unexpected command bead");
        }

      case M68kBeads::Bits1:
        Lookup.addBits(Ext, 1);
        break;

      case M68kBeads::Bits2:
        Lookup.addBits(Ext, 2);
        break;

      case M68kBeads::Bits3:
        Lookup.addBits(Ext, 3);
        break;

      case M68kBeads::Bits4:
        Lookup.addBits(Ext, 4);
        break;

      case M68kBeads::DAReg:
      case M68kBeads::DA:
      case M68kBeads::DReg:
      case M68kBeads::Reg:
        if (Op != M68kBeads::DA) {
          Lookup.addWildcard(3);
        }

        if (Op != M68kBeads::Reg && Op != M68kBeads::DReg) {
          Lookup.addWildcard(1);
        }
        break;

      case M68kBeads::Disp8:
        Lookup.addWildcard(8);
        break;

      case M68kBeads::Imm8:
      case M68kBeads::Imm16:
        Lookup.addWildcard(16);
        break;

      case M68kBeads::Imm32:
        Lookup.addWildcard(32);
        break;

      case M68kBeads::Imm3:
        Lookup.addWildcard(3);
        break;

      default:
        llvm_unreachable("unhandled bead type");
      }
    }

    assert(!(Lookup.Length & 0xf) && "lookups must be whole words");

    // Ignore instructions which are unmatchable (usually pseudo instructions).
    if (!Lookup.Mask) {
      continue;
    }

    Lookups.push_back(Lookup);
  }

#if 0
//#ifndef NDEBUG
  // TODO: This doesn't work because multiple instructions can have the same
  // machine code output. :(
  for (unsigned I = 0; I < NumInstr; ++I) {
    M68kInstructionLookup LookupA = Lookups[I];
    for (unsigned J = 0; J < NumInstr; ++J) {
      if (J == I) {
        continue;
      }

      M68kInstructionLookup LookupB = Lookups[J];
      if ((LookupA.Mask & LookupB.Mask) != LookupA.Mask) {
        continue;
      }

      if (LookupA.Value == LookupB.Value) {
        StringRef NameA = MCII->getName(LookupA.OpCode);
        StringRef NameB = MCII->getName(LookupB.OpCode);

        llvm::errs() << "multiple instructions with the same mask: \n"
          << "  " << NameA << " and " << NameB << "\n"
          << llvm::format("  %x %x", LookupA.Value, LookupB.Value) << "\n"
          << llvm::format("  %x %x", LookupA.Mask, LookupB.Mask) << "\n";

        llvm::errs() << "Beads: \n  ";
        for (const uint8_t *BeadPtr = M68k::getMCInstrBeads(LookupA.OpCode); *BeadPtr; ++BeadPtr) {
          llvm::errs() << format("%02x ", (unsigned)*BeadPtr);
        }
        llvm::errs() << "\n  ";
        for (const uint8_t *BeadPtr = M68k::getMCInstrBeads(LookupB.OpCode); *BeadPtr; ++BeadPtr) {
          llvm::errs() << format("%02x ", (unsigned)*BeadPtr);
        }
        llvm::errs() << "\n";
      }

      assert((LookupA.Value != LookupB.Value)
             && "Multiple instructions with the same mask");
    }
  }

#endif
}

unsigned M68kDisassembler::getRegOperandIndex(MCInst &Instr, unsigned Bead) const {
  auto Ext = Bead >> 4;

  const MCInstrDesc &Desc = MCII->get(Instr.getOpcode());
  auto MIOpIdx = M68k::getLogicalOperandIdx(Instr.getOpcode(), Ext & 7);

  if (M68kII::hasMultiMIOperands(Instr.getOpcode(), Ext & 7)) {
    bool IsPCRel = Desc.OpInfo[MIOpIdx].OperandType == MCOI::OPERAND_PCREL;
    if (IsPCRel) {
      MIOpIdx += M68k::PCRelIndex;
    } else if (Ext & 8) {
      MIOpIdx += M68k::MemIndex;
    } else {
      MIOpIdx += M68k::MemBase;
    }
  }

  return MIOpIdx;
}

unsigned M68kDisassembler::getImmOperandIndex(MCInst &Instr, unsigned Bead) const {
  auto Ext = Bead >> 4;

  const MCInstrDesc &Desc = MCII->get(Instr.getOpcode());
  auto MIOpIdx = M68k::getLogicalOperandIdx(Instr.getOpcode(), Ext & 7);

  if (M68kII::hasMultiMIOperands(Instr.getOpcode(), Ext & 7)) {
    bool IsPCRel = Desc.OpInfo[MIOpIdx].OperandType == MCOI::OPERAND_PCREL;
    if (IsPCRel) {
      MIOpIdx += M68k::PCRelDisp;
    } else if (Ext & 8) {
      MIOpIdx += M68k::MemOuter;
    } else {
      MIOpIdx += M68k::MemDisp;
    }
  }

  return MIOpIdx;
}

void M68kDisassembler::decodeReg(
    MCInst &Instr, unsigned Bead,
    uint64_t Value, unsigned &NumRead,
    unsigned &Scratch) const {
  unsigned Op = Bead & 0xf;
  LLVM_DEBUG(errs() << format("decodeReg %" PRIx64 " >> %d - %x\n",
                              Value, (unsigned)NumRead, Bead));

  if (Op != M68kBeads::DA) {
    Scratch = (Scratch &~ 7) | ((Value >> NumRead) & 7);
    NumRead += 3;
  }

  if (Op != M68kBeads::Reg) {
    bool DA = (Op != M68kBeads::DReg) && ((Value >> NumRead) & 1);
    if (!DA) {
      Scratch |= 8;
    } else {
      Scratch &=~ 8;
    }

    if (Op != M68kBeads::DReg) {
      ++NumRead;
    }
  }
}

void M68kDisassembler::decodeImm(MCInst &Instr, unsigned Bead,
                                 uint64_t Value, unsigned &NumRead,
                                 unsigned &Scratch) const {
  unsigned Op = Bead & 0xf;
  LLVM_DEBUG(errs() << format("decodeImm %" PRIx64 " >> %d - %x\n",
                              Value, (unsigned)NumRead, Bead));

  unsigned NumToRead;
  switch (Op) {
  case M68kBeads::Disp8:
    NumToRead = 8;
    break;
  case M68kBeads::Imm8:
  case M68kBeads::Imm16:
    NumToRead = 16;
    break;
  case M68kBeads::Imm32:
    NumToRead = 32;
    break;
  case M68kBeads::Imm3:
    NumToRead = 3;
    break;
  default:
    llvm_unreachable("invalid imm");
  }

  uint64_t Mask = (((uint64_t)1) << NumToRead) - 1;
  uint64_t Bits = (Value >> NumRead) & Mask;
  Scratch = (Scratch << NumToRead) | Bits;
  NumRead += NumToRead;
}

DecodeStatus M68kDisassembler::getInstruction(MCInst &Instr, uint64_t &Size,
                            ArrayRef<uint8_t> Bytes, uint64_t Address,
                            raw_ostream &CStream) const {
  // Read and shift the input (fetch as much as we can for now).
  unsigned AvailableLength = 0;
  uint64_t Value = 0;
  for (unsigned I = 0; I < sizeof(uint64_t)/sizeof(uint16_t); ++I) {
    unsigned Offset = I * 2;
    if (Bytes.size() < Offset + 2) {
      break;
    }

    uint64_t Hi = Bytes[Offset];
    uint64_t Lo = Bytes[Offset + 1];
    uint64_t Word = (Hi << 8) | Lo;
    Value |= Word << AvailableLength;
    AvailableLength += 16;
  }
  LLVM_DEBUG(errs() << format("Read bits %" PRIx64 " (%d)\n", Value, AvailableLength));

  // Check through our lookup table.
  bool Found = false;
  for (unsigned I = 0; I < Lookups.size(); ++I) {
    const M68kInstructionLookup &Lookup = Lookups[I];
    if (!Lookup.matches(Value, AvailableLength)) {
      continue;
    }

    Found = true;
    Size = Lookup.Length >> 3;
    Instr.setOpcode(Lookup.OpCode);

    LLVM_DEBUG(errs() << "decoding instruction "
                      << MCII->getName(Lookup.OpCode) << "\n");
    break;
  }

  if (!Found) {
    return Fail;
  }

  const MCInstrDesc& Desc = MCII->get(Instr.getOpcode());
  unsigned NumOperands = Desc.NumOperands;
  unsigned NumRead = 0;

  // Now use the beads to decode the operands.
  enum class OperandType {
    Invalid,
    Reg,
    Imm,
  };

  SmallVector<OperandType, 6> OpType(NumOperands, OperandType::Invalid);
  SmallVector<unsigned, 6> Scratch(NumOperands, 0);
  for (const uint8_t* PartPtr = M68k::getMCInstrBeads(Instr.getOpcode());
       *PartPtr; ++PartPtr) {
    uint8_t Bead = *PartPtr;
    unsigned Ext = Bead >> 4;
    unsigned Op = Bead & 0xf;
    unsigned MIOpIdx;

    switch (Op) {
    case M68kBeads::Ctrl:
      switch (Ext) {
      case M68kBeads::Ignore:
        continue;

      default:
        // Term will have already been skipped by the loop.
        llvm_unreachable("unexpected command bead");
      }

      // These bits are constant - if we're here we've already matched them.
    case M68kBeads::Bits1:
      NumRead += 1;
      break;
    case M68kBeads::Bits2:
      NumRead += 2;
      break;
    case M68kBeads::Bits3:
      NumRead += 3;
      break;
    case M68kBeads::Bits4:
      NumRead += 4;
      break;

    case M68kBeads::DAReg:
    case M68kBeads::DA:
    case M68kBeads::DReg:
    case M68kBeads::Reg:
      MIOpIdx = getRegOperandIndex(Instr, Bead);
      assert(((OpType[MIOpIdx] == OperandType::Invalid) ||
              (OpType[MIOpIdx] == OperandType::Reg)) &&
             "operands cannot change type");
      OpType[MIOpIdx] = OperandType::Reg;
      decodeReg(Instr, Bead, Value, NumRead, Scratch[MIOpIdx]);
      break;

    case M68kBeads::Disp8:
    case M68kBeads::Imm8:
    case M68kBeads::Imm16:
    case M68kBeads::Imm32:
    case M68kBeads::Imm3:
      MIOpIdx = getImmOperandIndex(Instr, Bead);
      assert(((OpType[MIOpIdx] == OperandType::Invalid) ||
             (OpType[MIOpIdx] == OperandType::Imm)) &&
             "operands cannot change type");
      OpType[MIOpIdx] = OperandType::Imm;
      decodeImm(Instr, Bead, Value, NumRead, Scratch[MIOpIdx]);
      break;

    default:
      llvm_unreachable("unhandled bead type");
    }
  }

  // Create the operands from our scratch space.
  for (unsigned O = 0; O < NumOperands; ++O) {
    switch (OpType[O]) {
    case OperandType::Invalid:
      assert(false && "operand not parsed");

    case OperandType::Imm:
      Instr.addOperand(MCOperand::createImm(Scratch[O]));
      break;

    case OperandType::Reg:
      Instr.addOperand(MCOperand::createReg(RegisterDecode[Scratch[O]]));
      break;
    }
  }

  assert((NumRead == Size * 8) && "wrong number of bits consumed");
  return Success;
}

static MCDisassembler *createM68kDisassembler(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              MCContext &Ctx) {
  return new M68kDisassembler(STI, Ctx, T.createMCInstrInfo());
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeM68kDisassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheM68kTarget(),
                                         createM68kDisassembler);
}
