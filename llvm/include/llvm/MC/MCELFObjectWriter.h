//===- llvm/MC/MCELFObjectWriter.h - ELF Object Writer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCELFOBJECTWRITER_H
#define LLVM_MC_MCELFOBJECTWRITER_H

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdint>
#include <vector>

namespace llvm {

class MCAssembler;
class MCContext;
class MCFixup;
class MCSymbol;
class MCSymbolELF;
class MCValue;

struct ELFRelocationEntry {
  uint64_t Offset;           // Where is the relocation.
  const MCSymbolELF *Symbol; // The symbol to relocate with.
  unsigned Type;             // The type of the relocation.
  uint64_t Addend;           // The addend to use.
  const MCSymbolELF
      *OriginalSymbol;     // The original value of Symbol if we changed it.
  uint64_t OriginalAddend; // The original value of addend.

  ELFRelocationEntry(uint64_t Offset, const MCSymbolELF *Symbol, unsigned Type,
                     uint64_t Addend, const MCSymbolELF *OriginalSymbol,
                     uint64_t OriginalAddend)
      : Offset(Offset), Symbol(Symbol), Type(Type), Addend(Addend),
        OriginalSymbol(OriginalSymbol), OriginalAddend(OriginalAddend) {}

  void print(raw_ostream &Out) const {
    Out << "Off=" << Offset << ", Sym=" << Symbol << ", Type=" << Type
        << ", Addend=" << Addend << ", OriginalSymbol=" << OriginalSymbol
        << ", OriginalAddend=" << OriginalAddend;
  }

  LLVM_DUMP_METHOD void dump() const { print(errs()); }
};

class MCELFObjectTargetWriter : public MCObjectTargetWriter {
  const uint8_t OSABI;
  const uint8_t ABIVersion;
  const uint16_t EMachine;
  const unsigned HasRelocationAddend : 1;

protected:
  MCELFObjectTargetWriter(uint8_t OSABI_, uint16_t EMachine_,
                          bool HasRelocationAddend_, uint8_t ABIVersion_ = 0);

public:
  virtual ~MCELFObjectTargetWriter() = default;

  Triple::ObjectFormatType getFormat() const override { return Triple::ELF; }
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::ELF;
  }

  static uint8_t getOSABI(Triple::OSType OSType) {
    switch (OSType) {
    case Triple::HermitCore:
      return ELF::ELFOSABI_STANDALONE;
    case Triple::PS4:
    case Triple::FreeBSD:
      return ELF::ELFOSABI_FREEBSD;
    case Triple::Solaris:
      return ELF::ELFOSABI_SOLARIS;
    default:
      return ELF::ELFOSABI_NONE;
    }
  }

  virtual unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                                const MCFixup &Fixup, bool IsPCRel) const = 0;

  virtual bool needsRelocateWithSymbol(const MCValue &Val, const MCSymbol &Sym,
                                       unsigned Type) const;

  virtual void sortRelocs(const MCAssembler &Asm,
                          std::vector<ELFRelocationEntry> &Relocs);

  virtual void addTargetSectionFlags(MCContext &Ctx, MCSectionELF &Sec);

  /// \name Accessors
  /// @{
  uint8_t getOSABI() const { return OSABI; }
  uint8_t getABIVersion() const { return ABIVersion; }
  uint16_t getEMachine() const { return EMachine; }
  bool hasRelocationAddend() const { return HasRelocationAddend; }
  /// @}

  // On AArch64, return a new section to be added to the ELF object that
  // contains relocations used to describe every symbol that should have memory
  // tags applied. Returns nullptr if no such section is necessary (i.e. there's
  // no tagged globals).
  virtual MCSectionELF *getMemtagRelocsSection(MCContext &Ctx) const {
    return nullptr;
  }
};

/// Construct a new ELF writer instance.
///
/// \param MOTW - The target specific ELF writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
std::unique_ptr<MCObjectWriter>
createELFObjectWriter(std::unique_ptr<MCELFObjectTargetWriter> MOTW,
                      raw_pwrite_stream &OS, bool IsLittleEndian);

std::unique_ptr<MCObjectWriter>
createELFDwoObjectWriter(std::unique_ptr<MCELFObjectTargetWriter> MOTW,
                         raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS,
                         bool IsLittleEndian);

} // end namespace llvm

#endif // LLVM_MC_MCELFOBJECTWRITER_H
