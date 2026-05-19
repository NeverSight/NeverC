//===-- AArch64TargetParser - Parser for AArch64 features -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a target parser to recognise AArch64 hardware features
// such as FPU/CPU/ARCH and extension names.
//
//===----------------------------------------------------------------------===//

#include "llvm/TargetParser/AArch64TargetParser.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <cctype>

using namespace llvm;

StringRef AArch64::getArchSynonym(StringRef Arch) {
  return StringSwitch<StringRef>(Arch)
      .Cases("v8", "v8a", "v8l", "aarch64", "arm64", "v8-a")
      .Case("v8.1a", "v8.1-a")
      .Case("v8.2a", "v8.2-a")
      .Case("v8.3a", "v8.3-a")
      .Case("v8.4a", "v8.4-a")
      .Case("v8.5a", "v8.5-a")
      .Case("v8.6a", "v8.6-a")
      .Case("v8.7a", "v8.7-a")
      .Case("v8.8a", "v8.8-a")
      .Case("v8.9a", "v8.9-a")
      .Case("v8r", "v8-r")
      .Cases("v9", "v9a", "v9-a")
      .Case("v9.1a", "v9.1-a")
      .Case("v9.2a", "v9.2-a")
      .Case("v9.3a", "v9.3-a")
      .Case("v9.4a", "v9.4-a")
      .Case("v9.5a", "v9.5-a")
      .Default(Arch);
}

StringRef AArch64::getCanonicalArchName(StringRef Arch) {
  size_t Offset = StringRef::npos;
  StringRef A = Arch;
  StringRef Error = "";

  if (A.starts_with("arm64"))
    Offset = 5;
  else if (A.starts_with("armv8") || A.starts_with("armv9"))
    Offset = 3;
  else if (A.starts_with("arm") || A.starts_with("thumb"))
    return Error;
  else if (A.starts_with("aarch64")) {
    Offset = 7;
    if (A.contains("eb") || A.substr(Offset, 3) == "_be")
      return Error;
  }

  if (Offset != StringRef::npos)
    A = A.substr(Offset);

  if (A.empty())
    return Arch;

  if (Offset != StringRef::npos) {
    if (A.size() < 2 || A[0] != 'v' || (A[1] != '8' && A[1] != '9') ||
        A.contains("eb"))
      return Error;
  }

  return A;
}

bool AArch64::parseBranchProtection(StringRef Spec, ParsedBranchProtection &PBP,
                                    StringRef &Err) {
  PBP = {"none", "a_key", false, false};
  if (Spec == "none")
    return true;

  if (Spec == "standard") {
    PBP.Scope = "non-leaf";
    PBP.BranchTargetEnforcement = true;
    return true;
  }

  SmallVector<StringRef, 4> Opts;
  Spec.split(Opts, "+");
  for (int I = 0, E = Opts.size(); I != E; ++I) {
    StringRef Opt = Opts[I].trim();
    if (Opt == "bti") {
      PBP.BranchTargetEnforcement = true;
      continue;
    }
    if (Opt == "pac-ret") {
      PBP.Scope = "non-leaf";
      for (; I + 1 != E; ++I) {
        StringRef PACOpt = Opts[I + 1].trim();
        if (PACOpt == "leaf")
          PBP.Scope = "all";
        else if (PACOpt == "b-key")
          PBP.Key = "b_key";
        else if (PACOpt == "pc")
          PBP.BranchProtectionPAuthLR = true;
        else
          break;
      }
      continue;
    }
    if (Opt == "")
      Err = "<empty>";
    else
      Err = Opt;
    return false;
  }

  return true;
}

static unsigned checkArchVersion(llvm::StringRef Arch) {
  if (Arch.size() >= 2 && Arch[0] == 'v' && std::isdigit(Arch[1]))
    return (Arch[1] - 48);
  return 0;
}

std::optional<AArch64::ArchInfo> AArch64::getArchForCpu(StringRef CPU) {
  if (CPU == "generic")
    return ARMV8A;

  // Note: this now takes cpu aliases into account
  std::optional<CpuInfo> Cpu = parseCpu(CPU);
  if (!Cpu)
    return {};
  return Cpu->Arch;
}

std::optional<AArch64::ArchInfo>
AArch64::ArchInfo::findBySubArch(StringRef SubArch) {
  for (const auto *A : AArch64::ArchInfos)
    if (A->getSubArch() == SubArch)
      return *A;
  return {};
}

uint64_t AArch64::getCpuSupportsMask(ArrayRef<StringRef> FeatureStrs) {
  uint64_t FeaturesMask = 0;
  for (const StringRef &FeatureStr : FeatureStrs) {
    for (const auto &E : llvm::AArch64::Extensions)
      if (FeatureStr == E.Name) {
        FeaturesMask |= (1ULL << E.CPUFeature);
        break;
      }
  }
  return FeaturesMask;
}

bool AArch64::getExtensionFeatures(const AArch64::ExtensionBitset &InputExts,
                                   std::vector<StringRef> &Features) {
  for (const auto &E : Extensions)
    /* INVALID and NONE have no feature name. */
    if (InputExts.test(E.ID) && !E.Feature.empty())
      Features.push_back(E.Feature);

  return true;
}

StringRef AArch64::resolveCPUAlias(StringRef Name) {
  for (const auto &A : CpuAliases)
    if (A.Alias == Name)
      return A.Name;
  return Name;
}

StringRef AArch64::getArchExtFeature(StringRef ArchExt) {
  if (ArchExt.starts_with("no")) {
    StringRef ArchExtBase(ArchExt.substr(2));
    for (const auto &AE : Extensions) {
      if (!AE.NegFeature.empty() && ArchExtBase == AE.Name)
        return AE.NegFeature;
    }
  }

  for (const auto &AE : Extensions)
    if (!AE.Feature.empty() && ArchExt == AE.Name)
      return AE.Feature;
  return StringRef();
}

void AArch64::fillValidCPUArchList(SmallVectorImpl<StringRef> &Values) {
  for (const auto &C : CpuInfos)
    Values.push_back(C.Name);

  for (const auto &Alias : CpuAliases)
    Values.push_back(Alias.Alias);
}

bool AArch64::isX18ReservedByDefault(const Triple &TT) {
  return TT.isAndroid() || TT.isOSDarwin() || TT.isOSWindows();
}

// Allows partial match, ex. "v8a" matches "armv8a".
std::optional<AArch64::ArchInfo> AArch64::parseArch(StringRef Arch) {
  Arch = getCanonicalArchName(Arch);
  if (checkArchVersion(Arch) < 8)
    return {};

  StringRef Syn = getArchSynonym(Arch);
  for (const auto *A : ArchInfos) {
    if (A->Name.ends_with(Syn))
      return *A;
  }
  return {};
}

std::optional<AArch64::ExtensionInfo>
AArch64::parseArchExtension(StringRef ArchExt) {
  for (const auto &A : Extensions) {
    if (ArchExt == A.Name)
      return A;
  }
  return {};
}

std::optional<AArch64::CpuInfo> AArch64::parseCpu(StringRef Name) {
  // Resolve aliases first.
  Name = resolveCPUAlias(Name);

  // Then find the CPU name.
  for (const auto &C : CpuInfos)
    if (Name == C.Name)
      return C;

  return {};
}

void AArch64::PrintSupportedExtensions(StringMap<StringRef> DescMap) {
  outs() << "All available -march extensions for AArch64\n\n"
         << "    " << left_justify("Name", 20)
         << (DescMap.empty() ? "\n" : "Description\n");
  for (const auto &Ext : Extensions) {
    // Extensions without a feature cannot be used with -march.
    if (!Ext.Feature.empty()) {
      std::string Description = DescMap[Ext.Name].str();
      outs() << "    "
             << format(Description.empty() ? "%s\n" : "%-20s%s\n",
                       Ext.Name.str().c_str(), Description.c_str());
    }
  }
}
