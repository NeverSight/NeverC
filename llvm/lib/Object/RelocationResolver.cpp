//===- RelocationResolver.cpp ------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities to resolve relocations in object files.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/RelocationResolver.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>

namespace llvm {
namespace object {

static int64_t getELFAddend(RelocationRef R) {
  Expected<int64_t> AddendOrErr = ELFRelocationRef(R).getAddend();
  handleAllErrors(AddendOrErr.takeError(), [](const ErrorInfoBase &EI) {
    report_fatal_error(Twine(EI.message()));
  });
  return *AddendOrErr;
}

static bool supportsX86_64(uint64_t Type) {
  switch (Type) {
  case ELF::R_X86_64_NONE:
  case ELF::R_X86_64_64:
  case ELF::R_X86_64_DTPOFF32:
  case ELF::R_X86_64_DTPOFF64:
  case ELF::R_X86_64_PC32:
  case ELF::R_X86_64_PC64:
  case ELF::R_X86_64_32:
  case ELF::R_X86_64_32S:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveX86_64(uint64_t Type, uint64_t Offset, uint64_t S,
                              uint64_t LocData, int64_t Addend) {
  switch (Type) {
  case ELF::R_X86_64_NONE:
    return LocData;
  case ELF::R_X86_64_64:
  case ELF::R_X86_64_DTPOFF32:
  case ELF::R_X86_64_DTPOFF64:
    return S + Addend;
  case ELF::R_X86_64_PC32:
  case ELF::R_X86_64_PC64:
    return S + Addend - Offset;
  case ELF::R_X86_64_32:
  case ELF::R_X86_64_32S:
    return (S + Addend) & 0xFFFFFFFF;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsAArch64(uint64_t Type) {
  switch (Type) {
  case ELF::R_AARCH64_ABS32:
  case ELF::R_AARCH64_ABS64:
  case ELF::R_AARCH64_PREL16:
  case ELF::R_AARCH64_PREL32:
  case ELF::R_AARCH64_PREL64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveAArch64(uint64_t Type, uint64_t Offset, uint64_t S,
                               uint64_t /*LocData*/, int64_t Addend) {
  switch (Type) {
  case ELF::R_AARCH64_ABS32:
    return (S + Addend) & 0xFFFFFFFF;
  case ELF::R_AARCH64_ABS64:
    return S + Addend;
  case ELF::R_AARCH64_PREL16:
    return (S + Addend - Offset) & 0xFFFF;
  case ELF::R_AARCH64_PREL32:
    return (S + Addend - Offset) & 0xFFFFFFFF;
  case ELF::R_AARCH64_PREL64:
    return S + Addend - Offset;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsCOFFX86_64(uint64_t Type) {
  switch (Type) {
  case COFF::IMAGE_REL_AMD64_SECREL:
  case COFF::IMAGE_REL_AMD64_ADDR64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveCOFFX86_64(uint64_t Type, uint64_t Offset, uint64_t S,
                                  uint64_t LocData, int64_t /*Addend*/) {
  switch (Type) {
  case COFF::IMAGE_REL_AMD64_SECREL:
    return (S + LocData) & 0xFFFFFFFF;
  case COFF::IMAGE_REL_AMD64_ADDR64:
    return S + LocData;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsCOFFARM64(uint64_t Type) {
  switch (Type) {
  case COFF::IMAGE_REL_ARM64_SECREL:
  case COFF::IMAGE_REL_ARM64_ADDR64:
    return true;
  default:
    return false;
  }
}

static uint64_t resolveCOFFARM64(uint64_t Type, uint64_t Offset, uint64_t S,
                                 uint64_t LocData, int64_t /*Addend*/) {
  switch (Type) {
  case COFF::IMAGE_REL_ARM64_SECREL:
    return (S + LocData) & 0xFFFFFFFF;
  case COFF::IMAGE_REL_ARM64_ADDR64:
    return S + LocData;
  default:
    llvm_unreachable("Invalid relocation type");
  }
}

static bool supportsMachOX86_64(uint64_t Type) {
  return Type == MachO::X86_64_RELOC_UNSIGNED;
}

static uint64_t resolveMachOX86_64(uint64_t Type, uint64_t Offset, uint64_t S,
                                   uint64_t LocData, int64_t /*Addend*/) {
  if (Type == MachO::X86_64_RELOC_UNSIGNED)
    return S;
  llvm_unreachable("Invalid relocation type");
}

std::pair<SupportsRelocation, RelocationResolver>
getRelocationResolver(const ObjectFile &Obj) {
  if (Obj.isCOFF()) {
    switch (Obj.getArch()) {
    case Triple::x86_64:
      return {supportsCOFFX86_64, resolveCOFFX86_64};
    case Triple::aarch64:
      return {supportsCOFFARM64, resolveCOFFARM64};
    default:
      return {nullptr, nullptr};
    }
  } else if (Obj.isELF()) {
    switch (Obj.getArch()) {
    case Triple::x86_64:
      return {supportsX86_64, resolveX86_64};
    case Triple::aarch64:
      return {supportsAArch64, resolveAArch64};
    default:
      return {nullptr, nullptr};
    }
  } else if (Obj.isMachO()) {
    if (Obj.getArch() == Triple::x86_64)
      return {supportsMachOX86_64, resolveMachOX86_64};
    return {nullptr, nullptr};
  }

  llvm_unreachable("Invalid object file");
}

uint64_t resolveRelocation(RelocationResolver Resolver, const RelocationRef &R,
                           uint64_t S, uint64_t LocData) {
  if (const ObjectFile *Obj = R.getObject()) {
    int64_t Addend = 0;
    if (Obj->isELF()) {
      auto GetRelSectionType = [&]() -> unsigned {
        if (auto *Elf32LEObj = dyn_cast<ELF32LEObjectFile>(Obj))
          return Elf32LEObj->getRelSection(R.getRawDataRefImpl())->sh_type;
        if (auto *Elf64LEObj = dyn_cast<ELF64LEObjectFile>(Obj))
          return Elf64LEObj->getRelSection(R.getRawDataRefImpl())->sh_type;
        if (auto *Elf32BEObj = dyn_cast<ELF32BEObjectFile>(Obj))
          return Elf32BEObj->getRelSection(R.getRawDataRefImpl())->sh_type;
        auto *Elf64BEObj = cast<ELF64BEObjectFile>(Obj);
        return Elf64BEObj->getRelSection(R.getRawDataRefImpl())->sh_type;
      };

      if (GetRelSectionType() == ELF::SHT_RELA) {
        Addend = getELFAddend(R);
        LocData = 0;
      }
    }

    return Resolver(R.getType(), R.getOffset(), S, LocData, Addend);
  }

  return Resolver(/*Type=*/0, /*Offset=*/0, S, LocData,
                  R.getRawDataRefImpl().p);
}

} // namespace object
} // namespace llvm
