//===--- M68k.h - Declare M68k target feature support -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares M68k TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#ifndef M680X0_H_LTNCIPAD
#define M680X0_H_LTNCIPAD

#include "OSTargets.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/Compiler.h"

namespace clang {
namespace targets {

class LLVM_LIBRARY_VISIBILITY M68kTargetInfo : public TargetInfo {
  static const char *const GCCRegNames[];

  enum CPUKind {
    CK_Unknown,
    CK_68000,
    CK_68010,
    CK_68020,
    CK_68030,
    CK_68040,
    CK_68060
  } CPU = CK_Unknown;

public:
  M68kTargetInfo(const llvm::Triple &Triple, const TargetOptions &);

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
  ArrayRef<Builtin::Info> getTargetBuiltins() const override;
  bool hasFeature(StringRef Feature) const override;
  ArrayRef<const char *> getGCCRegNames() const override;
  ArrayRef<TargetInfo::GCCRegAlias> getGCCRegAliases() const override;
  bool validateAsmConstraint(const char *&Name,
                             TargetInfo::ConstraintInfo &info) const override;
  const char *getClobbers() const override;
  BuiltinVaListKind getBuiltinVaListKind() const override;
  bool setCPU(const std::string &Name) override;
};

} // namespace targets
} // namespace clang

#endif /* end of include guard: M680X0_H_LTNCIPAD */
