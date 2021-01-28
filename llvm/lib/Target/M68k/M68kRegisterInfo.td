//== M68kRegisterInfo.td - M68k register definitions ----*- tablegen -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file describes the M68k Register file, defining the registers
/// aliases between the registers, and the register classes built out of the
/// registers.
///
//===----------------------------------------------------------------------===//

class MxReg<string N, bits<16> ENC,
            list<Register> SUBREGS = [], list<SubRegIndex> SUBIDX,
            list<int> DWREGS = [], list<string> ALTNAMES = []>
    : Register<N>, DwarfRegNum<DWREGS> {
  let Namespace     = "M68k";
  let HWEncoding    = ENC;
  let SubRegs       = SUBREGS;
  let SubRegIndices = SUBIDX;
  let AltNames      = ALTNAMES;
}

// Subregister indices.
let Namespace = "M68k" in {
  def MxSubRegIndex8Lo  : SubRegIndex<8, 0>;
  def MxSubRegIndex16Lo : SubRegIndex<16, 0>;
}

multiclass MxDataRegister<int INDEX, string REG_NAME, list<string> ALTNAMES = []> {
  def "B"#NAME : MxReg<REG_NAME, INDEX, [], [], [INDEX], ALTNAMES>;
  def "W"#NAME
    : MxReg<REG_NAME, INDEX,
            [!cast<Register>("B"#NAME)], [MxSubRegIndex8Lo],
            [INDEX], ALTNAMES>;
  def NAME
    : MxReg<REG_NAME, INDEX,
            [!cast<Register>("W"#NAME)], [MxSubRegIndex16Lo],
            [INDEX], ALTNAMES>;
}

multiclass MxAddressRegister<int INDEX, string REG_NAME, list<string> ALTNAMES = []> {
  def "W"#NAME
    : MxReg<REG_NAME, INDEX, [], [], [!add(8,INDEX)], ALTNAMES>;
  def NAME
    : MxReg<REG_NAME, INDEX,
            [!cast<Register>("W"#NAME)], [MxSubRegIndex16Lo],
            [INDEX], ALTNAMES>;
}

defm D0 : MxDataRegister<0, "d0">;
defm D1 : MxDataRegister<1, "d1">;
defm D2 : MxDataRegister<2, "d2">;
defm D3 : MxDataRegister<3, "d3">;
defm D4 : MxDataRegister<4, "d4">;
defm D5 : MxDataRegister<5, "d5">;
defm D6 : MxDataRegister<6, "d6">;
defm D7 : MxDataRegister<7, "d7">;

defm A0 : MxAddressRegister<0, "a0">;
defm A1 : MxAddressRegister<1, "a1">;
defm A2 : MxAddressRegister<2, "a2">;
defm A3 : MxAddressRegister<3, "a3">;
defm A4 : MxAddressRegister<4, "a4">;
defm BP : MxAddressRegister<5, "a5", ["bp"]>;
defm FP : MxAddressRegister<6, "a6", ["fp"]>;
defm SP : MxAddressRegister<7, "sp", ["usp", "ssp", "isp", "a7"]>;

// TODO get rid of "pseudo" registers and use just MxReg variants and use
// HWEncoding's other 13 bits to encode type(and potentially other info) of
// register.

// Pseudo Registers
class MxPseudoReg<string N, list<Register> SUBREGS = [], list<SubRegIndex> SUBIDX = []>
    : MxReg<N, 0, SUBREGS, SUBIDX>;

def CCR : MxPseudoReg<"ccr">;
def SR  : MxPseudoReg<"sr", [], []>;

def PC  : MxPseudoReg<"pc">;

//===----------------------------------------------------------------------===//
// Register Classes
//===----------------------------------------------------------------------===//

class MxRegClass<list<ValueType> regTypes, int alignment, dag regList>
    : RegisterClass<"M68k", regTypes, alignment, regList>;

// Data Registers
def DR8  : MxRegClass<[i8],  16, (sequence "BD%u", 0, 7)>;
def DR16 : MxRegClass<[i16], 16, (sequence "WD%u", 0, 7)>;
def DR32 : MxRegClass<[i32], 32, (sequence "D%u",  0, 7)>;

// Address Registers
def AR16 : MxRegClass<[i16], 16, (add WA0, WA1, WA2, WA3, WA4, WBP, WFP, WSP)>;
def AR32 : MxRegClass<[i32], 32, (add A0, A1, A2, A3, A4, BP, FP, SP)>;

def AR32_NOSP : MxRegClass<[i32], 32, (add A0, A1, A2, A3, A4, BP, FP)>;

// Index Register Classes
// FIXME try alternative ordering like `D0, D1, A0, A1, ...`
def XR16 : MxRegClass<[i16], 16, (add DR16, AR16)>;
def XR32 : MxRegClass<[i32], 32, (add DR32, AR32)>;

def SPC  : MxRegClass<[i32], 32, (add SP)>;

let CopyCost = -1 in {
  def CCRC : MxRegClass<[i8],  16, (add CCR)>;
  def SRC  : MxRegClass<[i16], 16, (add SR)>;
}

let isAllocatable = 0 in {
  def PCC  : MxRegClass<[i32], 32, (add PC)>;
}

// Register used with tail call
def DR16_TC : MxRegClass<[i16], 16, (add D0, D1)>;
def DR32_TC : MxRegClass<[i32], 32, (add D0, D1)>;

def AR16_TC : MxRegClass<[i16], 16, (add A0, A1)>;
def AR32_TC : MxRegClass<[i32], 32, (add A0, A1)>;

def XR16_TC : MxRegClass<[i16], 16, (add DR16_TC, AR16_TC)>;
def XR32_TC : MxRegClass<[i32], 32, (add DR32_TC, AR32_TC)>;

// These classes provide spill/restore order if used with MOVEM instruction
def SPILL   : MxRegClass<[i32], 32, (add XR32)>;
def SPILL_R : MxRegClass<[i32], 32, (add SP, FP, BP, A4, A3, A2, A1, A0, D7, D6, D5, D4, D3, D2, D1, D0)>;
