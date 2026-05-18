//===- ELFObjectFile.cpp - ELF object file implementation -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Part of the ELFObjectFile class implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ELFObjectFile.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace llvm;
using namespace object;

const EnumEntry<unsigned> llvm::object::ElfSymbolTypes[NumElfSymbolTypes] = {
    {"None", "NOTYPE", ELF::STT_NOTYPE},
    {"Object", "OBJECT", ELF::STT_OBJECT},
    {"Function", "FUNC", ELF::STT_FUNC},
    {"Section", "SECTION", ELF::STT_SECTION},
    {"File", "FILE", ELF::STT_FILE},
    {"Common", "COMMON", ELF::STT_COMMON},
    {"TLS", "TLS", ELF::STT_TLS},
    {"Unknown", "<unknown>: 7", 7},
    {"Unknown", "<unknown>: 8", 8},
    {"Unknown", "<unknown>: 9", 9},
    {"GNU_IFunc", "IFUNC", ELF::STT_GNU_IFUNC},
    {"OS Specific", "<OS specific>: 11", 11},
    {"OS Specific", "<OS specific>: 12", 12},
    {"Proc Specific", "<processor specific>: 13", 13},
    {"Proc Specific", "<processor specific>: 14", 14},
    {"Proc Specific", "<processor specific>: 15", 15}};

ELFObjectFileBase::ELFObjectFileBase(unsigned int Type, MemoryBufferRef Source)
    : ObjectFile(Type, Source) {}

template <class ELFT>
static Expected<std::unique_ptr<ELFObjectFile<ELFT>>>
createPtr(MemoryBufferRef Object, bool InitContent) {
  auto Ret = ELFObjectFile<ELFT>::create(Object, InitContent);
  if (Error E = Ret.takeError())
    return std::move(E);
  return std::make_unique<ELFObjectFile<ELFT>>(std::move(*Ret));
}

Expected<std::unique_ptr<ObjectFile>>
ObjectFile::createELFObjectFile(MemoryBufferRef Obj, bool InitContent) {
  std::pair<unsigned char, unsigned char> Ident =
      getElfArchType(Obj.getBuffer());
  std::size_t MaxAlignment =
      1ULL << llvm::countr_zero(
          reinterpret_cast<uintptr_t>(Obj.getBufferStart()));

  if (MaxAlignment < 2)
    return createError("Insufficient alignment");

  if (Ident.first == ELF::ELFCLASS32) {
    if (Ident.second == ELF::ELFDATA2LSB)
      return createPtr<ELF32LE>(Obj, InitContent);
    else if (Ident.second == ELF::ELFDATA2MSB)
      return createPtr<ELF32BE>(Obj, InitContent);
    else
      return createError("Invalid ELF data");
  } else if (Ident.first == ELF::ELFCLASS64) {
    if (Ident.second == ELF::ELFDATA2LSB)
      return createPtr<ELF64LE>(Obj, InitContent);
    else if (Ident.second == ELF::ELFDATA2MSB)
      return createPtr<ELF64BE>(Obj, InitContent);
    else
      return createError("Invalid ELF data");
  }
  return createError("Invalid ELF class");
}

Expected<SubtargetFeatures> ELFObjectFileBase::getFeatures() const {
  return SubtargetFeatures();
}

std::optional<StringRef> ELFObjectFileBase::tryGetCPUName() const {
  return std::nullopt;
}

std::vector<ELFPltEntry> ELFObjectFileBase::getPltEntries() const {
  std::string Err;
  const auto Triple = makeTriple();
  const auto *T = TargetRegistry::lookupTarget(Triple.str(), Err);
  if (!T)
    return {};
  uint32_t JumpSlotReloc = 0, GlobDatReloc = 0;
  switch (Triple.getArch()) {

  case Triple::x86_64:
    JumpSlotReloc = ELF::R_X86_64_JUMP_SLOT;
    GlobDatReloc = ELF::R_X86_64_GLOB_DAT;
    break;
  case Triple::aarch64:
    JumpSlotReloc = ELF::R_AARCH64_JUMP_SLOT;
    break;
  default:
    return {};
  }
  std::unique_ptr<const MCInstrInfo> MII(T->createMCInstrInfo());
  std::unique_ptr<const MCInstrAnalysis> MIA(
      T->createMCInstrAnalysis(MII.get()));
  if (!MIA)
    return {};
  std::vector<std::pair<uint64_t, uint64_t>> PltEntries;
  std::optional<SectionRef> RelaPlt, RelaDyn;
  for (const SectionRef &Section : sections()) {
    Expected<StringRef> NameOrErr = Section.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    StringRef Name = *NameOrErr;

    if (Name == ".rela.plt" || Name == ".rel.plt") {
      RelaPlt = Section;
    } else if (Name == ".rela.dyn" || Name == ".rel.dyn") {
      RelaDyn = Section;
    } else if (Name == ".plt" || Name == ".plt.got") {
      Expected<StringRef> PltContents = Section.getContents();
      if (!PltContents) {
        consumeError(PltContents.takeError());
        return {};
      }
      llvm::append_range(
          PltEntries,
          MIA->findPltEntries(Section.getAddress(),
                              arrayRefFromStringRef(*PltContents), Triple));
    }
  }

  // Build a map from GOT entry virtual address to PLT entry virtual address.
  DenseMap<uint64_t, uint64_t> GotToPlt;
  for (auto [Plt, GotPlt] : PltEntries)
    GotToPlt.insert(std::make_pair(GotPlt, Plt));

  // Find the relocations in the dynamic relocation table that point to
  // locations in the GOT for which we know the corresponding PLT entry.
  std::vector<ELFPltEntry> Result;
  auto handleRels = [&](iterator_range<relocation_iterator> Rels,
                        uint32_t RelType, StringRef PltSec) {
    for (const auto &R : Rels) {
      if (R.getType() != RelType)
        continue;
      auto PltEntryIter = GotToPlt.find(R.getOffset());
      if (PltEntryIter != GotToPlt.end()) {
        symbol_iterator Sym = R.getSymbol();
        if (Sym == symbol_end())
          Result.push_back(
              ELFPltEntry{PltSec, std::nullopt, PltEntryIter->second});
        else
          Result.push_back(ELFPltEntry{PltSec, Sym->getRawDataRefImpl(),
                                       PltEntryIter->second});
      }
    }
  };

  if (RelaPlt)
    handleRels(RelaPlt->relocations(), JumpSlotReloc, ".plt");

  // If a symbol needing a PLT entry also needs a GLOB_DAT relocation, GNU ld's
  // x86 port places the PLT entry in the .plt.got section.
  if (RelaDyn)
    handleRels(RelaDyn->relocations(), GlobDatReloc, ".plt.got");

  return Result;
}

template <class ELFT>
Expected<std::vector<BBAddrMap>> static readBBAddrMapImpl(
    const ELFFile<ELFT> &EF, std::optional<unsigned> TextSectionIndex,
    std::vector<PGOAnalysisMap> *PGOAnalyses) {
  using Elf_Shdr = typename ELFT::Shdr;
  bool IsRelocatable = EF.getHeader().e_type == ELF::ET_REL;
  std::vector<BBAddrMap> BBAddrMaps;
  if (PGOAnalyses)
    PGOAnalyses->clear();

  const auto &Sections = cantFail(EF.sections());
  auto IsMatch = [&](const Elf_Shdr &Sec) -> Expected<bool> {
    if (Sec.sh_type != ELF::SHT_LLVM_BB_ADDR_MAP &&
        Sec.sh_type != ELF::SHT_LLVM_BB_ADDR_MAP_V0)
      return false;
    if (!TextSectionIndex)
      return true;
    Expected<const Elf_Shdr *> TextSecOrErr = EF.getSection(Sec.sh_link);
    if (!TextSecOrErr)
      return createError("unable to get the linked-to section for " +
                         describe(EF, Sec) + ": " +
                         toString(TextSecOrErr.takeError()));
    if (*TextSectionIndex != std::distance(Sections.begin(), *TextSecOrErr))
      return false;
    return true;
  };

  Expected<MapVector<const Elf_Shdr *, const Elf_Shdr *>> SectionRelocMapOrErr =
      EF.getSectionAndRelocations(IsMatch);
  if (!SectionRelocMapOrErr)
    return SectionRelocMapOrErr.takeError();

  for (auto const &[Sec, RelocSec] : *SectionRelocMapOrErr) {
    if (IsRelocatable && !RelocSec)
      return createError("unable to get relocation section for " +
                         describe(EF, *Sec));
    Expected<std::vector<BBAddrMap>> BBAddrMapOrErr =
        EF.decodeBBAddrMap(*Sec, RelocSec, PGOAnalyses);
    if (!BBAddrMapOrErr) {
      if (PGOAnalyses)
        PGOAnalyses->clear();
      return createError("unable to read " + describe(EF, *Sec) + ": " +
                         toString(BBAddrMapOrErr.takeError()));
    }
    std::move(BBAddrMapOrErr->begin(), BBAddrMapOrErr->end(),
              std::back_inserter(BBAddrMaps));
  }
  return BBAddrMaps;
}

template <class ELFT>
static Expected<std::vector<VersionEntry>>
readDynsymVersionsImpl(const ELFFile<ELFT> &EF,
                       ELFObjectFileBase::elf_symbol_iterator_range Symbols) {
  using Elf_Shdr = typename ELFT::Shdr;
  const Elf_Shdr *VerSec = nullptr;
  const Elf_Shdr *VerNeedSec = nullptr;
  const Elf_Shdr *VerDefSec = nullptr;
  // The user should ensure sections() can't fail here.
  for (const Elf_Shdr &Sec : cantFail(EF.sections())) {
    if (Sec.sh_type == ELF::SHT_GNU_versym)
      VerSec = &Sec;
    else if (Sec.sh_type == ELF::SHT_GNU_verdef)
      VerDefSec = &Sec;
    else if (Sec.sh_type == ELF::SHT_GNU_verneed)
      VerNeedSec = &Sec;
  }
  if (!VerSec)
    return std::vector<VersionEntry>();

  Expected<SmallVector<std::optional<VersionEntry>, 0>> MapOrErr =
      EF.loadVersionMap(VerNeedSec, VerDefSec);
  if (!MapOrErr)
    return MapOrErr.takeError();

  std::vector<VersionEntry> Ret;
  size_t I = 0;
  for (const ELFSymbolRef &Sym : Symbols) {
    ++I;
    Expected<const typename ELFT::Versym *> VerEntryOrErr =
        EF.template getEntry<typename ELFT::Versym>(*VerSec, I);
    if (!VerEntryOrErr)
      return createError("unable to read an entry with index " + Twine(I) +
                         " from " + describe(EF, *VerSec) + ": " +
                         toString(VerEntryOrErr.takeError()));

    Expected<uint32_t> FlagsOrErr = Sym.getFlags();
    if (!FlagsOrErr)
      return createError("unable to read flags for symbol with index " +
                         Twine(I) + ": " + toString(FlagsOrErr.takeError()));

    bool IsDefault;
    Expected<StringRef> VerOrErr = EF.getSymbolVersionByIndex(
        (*VerEntryOrErr)->vs_index, IsDefault, *MapOrErr,
        (*FlagsOrErr) & SymbolRef::SF_Undefined);
    if (!VerOrErr)
      return createError("unable to get a version for entry " + Twine(I) +
                         " of " + describe(EF, *VerSec) + ": " +
                         toString(VerOrErr.takeError()));

    Ret.push_back({(*VerOrErr).str(), IsDefault});
  }

  return Ret;
}

Expected<std::vector<VersionEntry>>
ELFObjectFileBase::readDynsymVersions() const {
  elf_symbol_iterator_range Symbols = getDynamicSymbolIterators();
  if (const auto *Obj = dyn_cast<ELF32LEObjectFile>(this))
    return readDynsymVersionsImpl(Obj->getELFFile(), Symbols);
  if (const auto *Obj = dyn_cast<ELF32BEObjectFile>(this))
    return readDynsymVersionsImpl(Obj->getELFFile(), Symbols);
  if (const auto *Obj = dyn_cast<ELF64LEObjectFile>(this))
    return readDynsymVersionsImpl(Obj->getELFFile(), Symbols);
  return readDynsymVersionsImpl(cast<ELF64BEObjectFile>(this)->getELFFile(),
                                Symbols);
}

Expected<std::vector<BBAddrMap>> ELFObjectFileBase::readBBAddrMap(
    std::optional<unsigned> TextSectionIndex,
    std::vector<PGOAnalysisMap> *PGOAnalyses) const {
  if (const auto *Obj = dyn_cast<ELF32LEObjectFile>(this))
    return readBBAddrMapImpl(Obj->getELFFile(), TextSectionIndex, PGOAnalyses);
  if (const auto *Obj = dyn_cast<ELF64LEObjectFile>(this))
    return readBBAddrMapImpl(Obj->getELFFile(), TextSectionIndex, PGOAnalyses);
  if (const auto *Obj = dyn_cast<ELF32BEObjectFile>(this))
    return readBBAddrMapImpl(Obj->getELFFile(), TextSectionIndex, PGOAnalyses);
  return readBBAddrMapImpl(cast<ELF64BEObjectFile>(this)->getELFFile(),
                           TextSectionIndex, PGOAnalyses);
}
