//===- M68k.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Symbols.h"
#include "Target.h"
#include "SyntheticSections.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Object/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class M68k final : public TargetInfo {
public:
  M68k();
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;

  void writeGotHeader(uint8_t *buf) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;

  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
};
} // namespace

M68k::M68k() {
  noneRel = R_68K_NONE;
  pltHeaderSize = 20;
  pltEntrySize = 20;
}

RelExpr M68k::getRelExpr(RelType type, const Symbol &s,
                        const uint8_t *loc) const {
  switch (type) {
  case R_68K_PC8:
  case R_68K_PC16:
  case R_68K_PC32:
    return R_PC;

  case R_68K_GOTPCREL8:
  case R_68K_GOTPCREL16:
  case R_68K_GOTPCREL32:
    return R_GOT_PC;

  case R_68K_GOTOFF8:
  case R_68K_GOTOFF16:
  case R_68K_GOTOFF32:
    return R_GOT_OFF;

  case R_68K_PLT8:
  case R_68K_PLT16:
  case R_68K_PLT32:
      return R_PLT;

  case R_68K_PLTOFF8:
  case R_68K_PLTOFF16:
  case R_68K_PLTOFF32:
    return R_PLT;

  default:
    return R_ABS;
  }
}

void M68k::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_68K_32:
  case R_68K_PC32:
  case R_68K_PLT32:
  case R_68K_PLTOFF32:
  case R_68K_GOTPCREL32:
    checkIntUInt(loc, val, 32, rel);
    write32be(loc, val);
    break;

  case R_68K_16:
  case R_68K_PC16:
  case R_68K_PLT16:
  case R_68K_PLTOFF16:
  case R_68K_GOTPCREL16:
    checkIntUInt(loc, val, 16, rel);
    write16be(loc, val);
    break;

  case R_68K_8:
  case R_68K_PC8:
  case R_68K_PLT8:
  case R_68K_PLTOFF8:
  case R_68K_GOTPCREL8:
    checkIntUInt(loc, val, 8, rel);
    *loc = val;
    break;

  default:
    error(getErrorLocation(loc) + "unrecognized relocation " +
          toString(rel.type));
  }
}

void M68k::writeGotHeader(uint8_t *buf) const {
  write32be(buf, mainPart->dynamic->getVA());
}

void M68k::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  write32be(buf, in.plt->getVA() + in.plt->headerSize + 4 * s.pltIndex);
}

void M68k::writePltHeader(uint8_t *buf) const {
  const uint8_t headerTemplate[] = {
      0x2f, 0x3b, 0x1, 0x70,  // move.l (gotPlt + 4, pc), -(sp)
      0, 0, 0, 0,             // Put (gotPlt + 4) - pc here.
      0x4e, 0xfb, 0x1, 0x71,  // jmp ([gotPlt + 8, pc])
      0, 0, 0, 0,             // Put (gotPlt + 8) - pc here.
  };

  uint64_t got = in.gotPlt->getVA();
  uint64_t plt = in.plt->getVA();
  memcpy(buf, headerTemplate, sizeof(headerTemplate));

  // +2 because PC points to the extension word during evaluation.
  write32(buf + 4, (got + 4) - (plt + 2));
  write32(buf + 12, (got + 8) - (plt + 10));
}

void M68k::writePlt(uint8_t *buf, const Symbol &sym, uint64_t pltEntryAddr) const {
  const uint8_t pltTemplate[] = {
      0x4e, 0xfb, 0x1, 0x71,  // jmp ([gotPlt, pc])
      0, 0, 0, 0,             // Put gotPlt - pc here.
      0x2f, 0x3c,             // move.l #offset, -(sp)
      0, 0, 0, 0,             // Put offset here.
      0x60, 0xff,             // bra.l plt
      0, 0, 0, 0,             // Put plt here.
  };

  unsigned relOff = in.relaPlt->entsize * sym.pltIndex;

  memcpy(buf, pltTemplate, sizeof(pltTemplate));
  write32be(buf + 4, sym.getGotPltVA() - (pltEntryAddr + 2));
  write32be(buf + 10, relOff);
  write32be(buf + 16, in.plt->getVA());
}

TargetInfo *elf::getM68kTargetInfo() {
  static M68k target;
  return &target;
}
