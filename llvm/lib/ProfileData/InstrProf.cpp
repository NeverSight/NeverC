//===- InstrProf.cpp - Instrumented profiling format support --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for clang's instrumentation based PGO and
// coverage.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/InstrProf.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/config.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SwapByteOrder.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace llvm;

static cl::opt<bool> StaticFuncFullModulePrefix(
    "static-func-full-module-prefix", cl::init(true), cl::Hidden,
    cl::desc("Use full module build paths in the profile counter names for "
             "static functions."));

// This option is tailored to users that have different top-level directory in
// profile-gen and profile-use compilation. Users need to specific the number
// of levels to strip. A value larger than the number of directories in the
// source file will strip all the directory names and only leave the basename.
//
// Note current LTO module importing for the indirect-calls assumes
// the source directory name not being stripped. A non-zero option value here
// can potentially prevent some inter-module indirect-call-promotions.
static cl::opt<unsigned> StaticFuncStripDirNamePrefix(
    "static-func-strip-dirname-prefix", cl::init(0), cl::Hidden,
    cl::desc("Strip specified level of directory name from source path in "
             "the profile counter name for static functions."));

static std::string getInstrProfErrString(instrprof_error Err,
                                         const std::string &ErrMsg = "") {
  std::string Msg;
  raw_string_ostream OS(Msg);

  switch (Err) {
  case instrprof_error::success:
    OS << "success";
    break;
  case instrprof_error::eof:
    OS << "end of File";
    break;
  case instrprof_error::unrecognized_format:
    OS << "unrecognized instrumentation profile encoding format";
    break;
  case instrprof_error::bad_magic:
    OS << "invalid instrumentation profile data (bad magic)";
    break;
  case instrprof_error::bad_header:
    OS << "invalid instrumentation profile data (file header is corrupt)";
    break;
  case instrprof_error::unsupported_version:
    OS << "unsupported instrumentation profile format version";
    break;
  case instrprof_error::unsupported_hash_type:
    OS << "unsupported instrumentation profile hash type";
    break;
  case instrprof_error::too_large:
    OS << "too much profile data";
    break;
  case instrprof_error::truncated:
    OS << "truncated profile data";
    break;
  case instrprof_error::malformed:
    OS << "malformed instrumentation profile data";
    break;
  case instrprof_error::missing_correlation_info:
    OS << "debug info/binary for correlation is required";
    break;
  case instrprof_error::unexpected_correlation_info:
    OS << "debug info/binary for correlation is not necessary";
    break;
  case instrprof_error::unable_to_correlate_profile:
    OS << "unable to correlate profile";
    break;
  case instrprof_error::invalid_prof:
    OS << "invalid profile created. Please file a bug "
          "at: " BUG_REPORT_URL
          " and include the profraw files that caused this error.";
    break;
  case instrprof_error::unknown_function:
    OS << "no profile data available for function";
    break;
  case instrprof_error::hash_mismatch:
    OS << "function control flow change detected (hash mismatch)";
    break;
  case instrprof_error::count_mismatch:
    OS << "function basic block count change detected (counter mismatch)";
    break;
  case instrprof_error::bitmap_mismatch:
    OS << "function bitmap size change detected (bitmap size mismatch)";
    break;
  case instrprof_error::counter_overflow:
    OS << "counter overflow";
    break;
  case instrprof_error::value_site_count_mismatch:
    OS << "function value site count change detected (counter mismatch)";
    break;
  case instrprof_error::compress_failed:
    OS << "failed to compress data (zlib)";
    break;
  case instrprof_error::uncompress_failed:
    OS << "failed to uncompress data (zlib)";
    break;
  case instrprof_error::empty_raw_profile:
    OS << "empty raw profile file";
    break;
  case instrprof_error::zlib_unavailable:
    OS << "profile uses zlib compression but the profile reader was built "
          "without zlib support";
    break;
  case instrprof_error::raw_profile_version_mismatch:
    OS << "raw profile version mismatch";
    break;
  case instrprof_error::counter_value_too_large:
    OS << "excessively large counter value suggests corrupted profile data";
    break;
  }

  // If optional error message is not empty, append it to the message.
  if (!ErrMsg.empty())
    OS << ": " << ErrMsg;

  return OS.str();
}

namespace {

// FIXME: This class is only here to support the transition to llvm::Error. It
// will be removed once this transition is complete. Clients should prefer to
// deal with the Error value directly, rather than converting to error_code.
class InstrProfErrorCategoryType : public std::error_category {
  const char *name() const noexcept override { return "llvm.instrprof"; }

  std::string message(int IE) const override {
    return getInstrProfErrString(static_cast<instrprof_error>(IE));
  }
};

} // end anonymous namespace

const std::error_category &llvm::instrprof_category() {
  static InstrProfErrorCategoryType ErrorCategory;
  return ErrorCategory;
}

namespace {

const char *InstrProfSectNameCommon[] = {
#define INSTR_PROF_SECT_ENTRY(Kind, SectNameCommon, SectNameCoff, Prefix)      \
  SectNameCommon,
#include "llvm/ProfileData/InstrProfData.inc"
};

const char *InstrProfSectNameCoff[] = {
#define INSTR_PROF_SECT_ENTRY(Kind, SectNameCommon, SectNameCoff, Prefix)      \
  SectNameCoff,
#include "llvm/ProfileData/InstrProfData.inc"
};

const char *InstrProfSectNamePrefix[] = {
#define INSTR_PROF_SECT_ENTRY(Kind, SectNameCommon, SectNameCoff, Prefix)      \
  Prefix,
#include "llvm/ProfileData/InstrProfData.inc"
};

} // namespace

namespace llvm {

cl::opt<bool> DoInstrProfNameCompression(
    "enable-name-compression",
    cl::desc("Enable name/filename string compression"), cl::init(true));

std::string getInstrProfSectionName(InstrProfSectKind IPSK,
                                    Triple::ObjectFormatType OF,
                                    bool AddSegmentInfo) {
  std::string SectName;

  if (OF == Triple::MachO && AddSegmentInfo)
    SectName = InstrProfSectNamePrefix[IPSK];

  if (OF == Triple::COFF)
    SectName += InstrProfSectNameCoff[IPSK];
  else
    SectName += InstrProfSectNameCommon[IPSK];

  if (OF == Triple::MachO && IPSK == IPSK_data && AddSegmentInfo)
    SectName += ",regular,live_support";

  return SectName;
}

std::string InstrProfError::message() const {
  return getInstrProfErrString(Err, Msg);
}

char InstrProfError::ID = 0;

std::string getPGOFuncName(StringRef Name, GlobalValue::LinkageTypes Linkage,
                           StringRef FileName,
                           uint64_t Version LLVM_ATTRIBUTE_UNUSED) {
  // Value names may be prefixed with a binary '1' to indicate
  // that the backend should not modify the symbols due to any platform
  // naming convention. Do not include that '1' in the PGO profile name.
  if (Name[0] == '\1')
    Name = Name.substr(1);

  std::string NewName = std::string(Name);
  if (llvm::GlobalValue::isLocalLinkage(Linkage)) {
    // For local symbols, prepend the main file name to distinguish them.
    // Do not include the full path in the file name since there's no guarantee
    // that it will stay the same, e.g., if the files are checked out from
    // version control in different locations.
    if (FileName.empty())
      NewName = NewName.insert(0, "<unknown>:");
    else
      NewName = NewName.insert(0, FileName.str() + ":");
  }
  return NewName;
}

// Strip NumPrefix level of directory name from PathNameStr. If the number of
// directory separators is less than NumPrefix, strip all the directories and
// leave base file name only.
static StringRef stripDirPrefix(StringRef PathNameStr, uint32_t NumPrefix) {
  uint32_t Count = NumPrefix;
  uint32_t Pos = 0, LastPos = 0;
  for (auto &CI : PathNameStr) {
    ++Pos;
    if (llvm::sys::path::is_separator(CI)) {
      LastPos = Pos;
      --Count;
    }
    if (Count == 0)
      break;
  }
  return PathNameStr.substr(LastPos);
}

static StringRef getStrippedSourceFileName(const GlobalObject &GO) {
  StringRef FileName(GO.getParent()->getSourceFileName());
  uint32_t StripLevel = StaticFuncFullModulePrefix ? 0 : (uint32_t)-1;
  if (StripLevel < StaticFuncStripDirNamePrefix)
    StripLevel = StaticFuncStripDirNamePrefix;
  if (StripLevel)
    FileName = stripDirPrefix(FileName, StripLevel);
  return FileName;
}

// The PGO name has the format [<filepath>;]<linkage-name> where <filepath>; is
// provided if linkage is local and <linkage-name> is the mangled function
// name. The filepath is used to discriminate possibly identical function names.
// ; is used because it is unlikely to be found in either <filepath> or
// <linkage-name>.
//
// Older compilers used getPGOFuncName() which has the format
// [<filepath>:]<function-name>. <filepath> is used to discriminate between
// possibly identical function names when linkage is local and <function-name>
// simply comes from F.getName(). This caused trouble for functions which
// commonly have :'s in their names. Also, since <function-name> is not
// mangled, they cannot be passed to Mach-O linkers via -order_file. We still
// need to compute this name to lookup functions from profiles built by older
// compilers.
static std::string
getIRPGONameForGlobalObject(const GlobalObject &GO,
                            GlobalValue::LinkageTypes Linkage,
                            StringRef FileName) {
  SmallString<64> Name;
  // FIXME: Mangler's handling is kept outside of `getGlobalIdentifier` for now.
  // For more details please check issue #74565.
  Mangler().getNameWithPrefix(Name, &GO, /*CannotUsePrivateLabel=*/true);
  return GlobalValue::getGlobalIdentifier(Name, Linkage, FileName);
}

static std::optional<std::string> lookupPGONameFromMetadata(MDNode *MD) {
  if (MD != nullptr) {
    StringRef S = cast<MDString>(MD->getOperand(0))->getString();
    return S.str();
  }
  return {};
}

// Returns the PGO object name. This function has some special handling
// when called in LTO optimization. The following only applies when calling in
// LTO passes (when \c InLTO is true): LTO's internalization privatizes many
// global linkage symbols. This happens after value profile annotation, but
// those internal linkage functions should not have a source prefix.
// Additionally, for LTO mode, exported internal functions are promoted
// and renamed. We need to ensure that the original internal PGO name is
// used when computing the GUID that is compared against the profiled GUIDs.
// To differentiate compiler generated internal symbols from original ones,
// PGOFuncName meta data are created and attached to the original internal
// symbols in the value profile annotation step
// (PGOUseFunc::annotateIndirectCallSites). If a symbol does not have the meta
// data, its original linkage must be non-internal.
static std::string getIRPGOObjectName(const GlobalObject &GO, bool InLTO,
                                      MDNode *PGONameMetadata) {
  if (!InLTO) {
    auto FileName = getStrippedSourceFileName(GO);
    return getIRPGONameForGlobalObject(GO, GO.getLinkage(), FileName);
  }

  // In LTO mode (when InLTO is true), first check if there is a meta data.
  if (auto IRPGOFuncName = lookupPGONameFromMetadata(PGONameMetadata))
    return *IRPGOFuncName;

  // If there is no meta data, the function must be a global before the value
  // profile annotation pass. Its current linkage may be internal if it is
  // internalized in LTO mode.
  return getIRPGONameForGlobalObject(GO, GlobalValue::ExternalLinkage, "");
}

// Returns the IRPGO function name and does special handling when called
// in LTO optimization. See the comments of `getIRPGOObjectName` for details.
std::string getIRPGOFuncName(const Function &F, bool InLTO) {
  return getIRPGOObjectName(F, InLTO, getPGOFuncNameMetadata(F));
}

// Please use getIRPGOFuncName for LLVM IR instrumentation. This function is
// for front-end (Clang, etc) instrumentation.
// The implementation is kept for profile matching from older profiles.
// This is similar to `getIRPGOFuncName` except that this function calls
// 'getPGOFuncName' to get a name and `getIRPGOFuncName` calls
// 'getIRPGONameForGlobalObject'. See the difference between two callees in the
// comments of `getIRPGONameForGlobalObject`.
std::string getPGOFuncName(const Function &F, bool InLTO, uint64_t Version) {
  if (!InLTO) {
    auto FileName = getStrippedSourceFileName(F);
    return getPGOFuncName(F.getName(), F.getLinkage(), FileName, Version);
  }

  // In LTO mode (when InLTO is true), first check if there is a meta data.
  if (auto PGOFuncName = lookupPGONameFromMetadata(getPGOFuncNameMetadata(F)))
    return *PGOFuncName;

  // If there is no meta data, the function must be a global before the value
  // profile annotation pass. Its current linkage may be internal if it is
  // internalized in LTO mode.
  return getPGOFuncName(F.getName(), GlobalValue::ExternalLinkage, "");
}

// See getIRPGOFuncName() for a discription of the format.
std::pair<StringRef, StringRef>
getParsedIRPGOFuncName(StringRef IRPGOFuncName) {
  auto [FileName, FuncName] = IRPGOFuncName.split(';');
  if (FuncName.empty())
    return std::make_pair(StringRef(), IRPGOFuncName);
  return std::make_pair(FileName, FuncName);
}

StringRef getFuncNameWithoutPrefix(StringRef PGOFuncName, StringRef FileName) {
  if (FileName.empty())
    return PGOFuncName;
  // Drop the file name including ':' or ';'. See getIRPGONameForGlobalObject as
  // well.
  if (PGOFuncName.starts_with(FileName))
    PGOFuncName = PGOFuncName.drop_front(FileName.size() + 1);
  return PGOFuncName;
}

// \p FuncName is the string used as profile lookup key for the function. A
// symbol is created to hold the name. Return the legalized symbol name.
std::string getPGOFuncNameVarName(StringRef FuncName,
                                  GlobalValue::LinkageTypes Linkage) {
  std::string VarName = std::string(getInstrProfNameVarPrefix());
  VarName += FuncName;

  if (!GlobalValue::isLocalLinkage(Linkage))
    return VarName;

  // Now fix up illegal chars in local VarName that may upset the assembler.
  const char InvalidChars[] = "-:;<>/\"'";
  size_t found = VarName.find_first_of(InvalidChars);
  while (found != std::string::npos) {
    VarName[found] = '_';
    found = VarName.find_first_of(InvalidChars, found + 1);
  }
  return VarName;
}

GlobalVariable *createPGOFuncNameVar(Module &M,
                                     GlobalValue::LinkageTypes Linkage,
                                     StringRef PGOFuncName) {
  // We generally want to match the function's linkage, but available_externally
  // and extern_weak both have the wrong semantics, and anything that doesn't
  // need to link across compilation units doesn't need to be visible at all.
  if (Linkage == GlobalValue::ExternalWeakLinkage)
    Linkage = GlobalValue::LinkOnceAnyLinkage;
  else if (Linkage == GlobalValue::AvailableExternallyLinkage)
    Linkage = GlobalValue::LinkOnceODRLinkage;
  else if (Linkage == GlobalValue::InternalLinkage ||
           Linkage == GlobalValue::ExternalLinkage)
    Linkage = GlobalValue::PrivateLinkage;

  auto *Value =
      ConstantDataArray::getString(M.getContext(), PGOFuncName, false);
  auto FuncNameVar =
      new GlobalVariable(M, Value->getType(), true, Linkage, Value,
                         getPGOFuncNameVarName(PGOFuncName, Linkage));

  // Hide the symbol so that we correctly get a copy for each executable.
  if (!GlobalValue::isLocalLinkage(FuncNameVar->getLinkage()))
    FuncNameVar->setVisibility(GlobalValue::HiddenVisibility);

  return FuncNameVar;
}

GlobalVariable *createPGOFuncNameVar(Function &F, StringRef PGOFuncName) {
  return createPGOFuncNameVar(*F.getParent(), F.getLinkage(), PGOFuncName);
}

Error InstrProfSymtab::create(Module &M, bool InLTO) {
  for (Function &F : M) {
    // Function may not have a name: like using asm("") to overwrite the name.
    // Ignore in this case.
    if (!F.hasName())
      continue;
    if (Error E = addFuncWithName(F, getIRPGOFuncName(F, InLTO)))
      return E;
    // Also use getPGOFuncName() so that we can find records from older profiles
    if (Error E = addFuncWithName(F, getPGOFuncName(F, InLTO)))
      return E;
  }
  Sorted = false;
  finalizeSymtab();
  return Error::success();
}

Error InstrProfSymtab::addFuncWithName(Function &F, StringRef PGOFuncName) {
  if (Error E = addFuncName(PGOFuncName))
    return E;
  MD5FuncMap.emplace_back(Function::getGUID(PGOFuncName), &F);
  // In LTO, local function may have been promoted to global and have
  // suffix ".llvm." added to the function name. We need to add the
  // stripped function name to the symbol table so that we can find a match
  // from profile.
  //
  // We may have other suffixes similar as ".llvm." which are needed to
  // be stripped before the matching, but ".__uniq." suffix which is used
  // to differentiate internal linkage functions in different modules
  // should be kept. Now this is the only suffix with the pattern ".xxx"
  // which is kept before matching.
  const std::string UniqSuffix = ".__uniq.";
  auto pos = PGOFuncName.find(UniqSuffix);
  // Search '.' after ".__uniq." if ".__uniq." exists, otherwise
  // search '.' from the beginning.
  if (pos != std::string::npos)
    pos += UniqSuffix.length();
  else
    pos = 0;
  pos = PGOFuncName.find('.', pos);
  if (pos != std::string::npos && pos != 0) {
    StringRef OtherFuncName = PGOFuncName.substr(0, pos);
    if (Error E = addFuncName(OtherFuncName))
      return E;
    MD5FuncMap.emplace_back(Function::getGUID(OtherFuncName), &F);
  }
  return Error::success();
}

void annotateValueSite(Module &M, Instruction &Inst,
                       ArrayRef<InstrProfValueData> VDs, uint64_t Sum,
                       InstrProfValueKind ValueKind, uint32_t MaxMDCount) {
  LLVMContext &Ctx = M.getContext();
  MDBuilder MDHelper(Ctx);
  SmallVector<Metadata *, 3> Vals;
  // Tag
  Vals.push_back(MDHelper.createString("VP"));
  // Value Kind
  Vals.push_back(MDHelper.createConstant(
      ConstantInt::get(Type::getInt32Ty(Ctx), ValueKind)));
  // Total Count
  Vals.push_back(
      MDHelper.createConstant(ConstantInt::get(Type::getInt64Ty(Ctx), Sum)));

  // Value Profile Data
  uint32_t MDCount = MaxMDCount;
  for (auto &VD : VDs) {
    Vals.push_back(MDHelper.createConstant(
        ConstantInt::get(Type::getInt64Ty(Ctx), VD.Value)));
    Vals.push_back(MDHelper.createConstant(
        ConstantInt::get(Type::getInt64Ty(Ctx), VD.Count)));
    if (--MDCount == 0)
      break;
  }
  Inst.setMetadata(LLVMContext::MD_prof, MDNode::get(Ctx, Vals));
}

bool getValueProfDataFromInst(const Instruction &Inst,
                              InstrProfValueKind ValueKind,
                              uint32_t MaxNumValueData,
                              InstrProfValueData ValueData[],
                              uint32_t &ActualNumValueData, uint64_t &TotalC,
                              bool GetNoICPValue) {
  MDNode *MD = Inst.getMetadata(LLVMContext::MD_prof);
  if (!MD)
    return false;

  unsigned NOps = MD->getNumOperands();

  if (NOps < 5)
    return false;

  // Operand 0 is a string tag "VP":
  MDString *Tag = cast<MDString>(MD->getOperand(0));
  if (!Tag)
    return false;

  if (!Tag->getString().equals("VP"))
    return false;

  // Now check kind:
  ConstantInt *KindInt = mdconst::dyn_extract<ConstantInt>(MD->getOperand(1));
  if (!KindInt)
    return false;
  if (KindInt->getZExtValue() != ValueKind)
    return false;

  // Get total count
  ConstantInt *TotalCInt = mdconst::dyn_extract<ConstantInt>(MD->getOperand(2));
  if (!TotalCInt)
    return false;
  TotalC = TotalCInt->getZExtValue();

  ActualNumValueData = 0;

  for (unsigned I = 3; I < NOps; I += 2) {
    if (ActualNumValueData >= MaxNumValueData)
      break;
    ConstantInt *Value = mdconst::dyn_extract<ConstantInt>(MD->getOperand(I));
    ConstantInt *Count =
        mdconst::dyn_extract<ConstantInt>(MD->getOperand(I + 1));
    if (!Value || !Count)
      return false;
    uint64_t CntValue = Count->getZExtValue();
    if (!GetNoICPValue && (CntValue == NOMORE_ICP_MAGICNUM))
      continue;
    ValueData[ActualNumValueData].Value = Value->getZExtValue();
    ValueData[ActualNumValueData].Count = CntValue;
    ActualNumValueData++;
  }
  return true;
}

MDNode *getPGOFuncNameMetadata(const Function &F) {
  return F.getMetadata(getPGOFuncNameMetadataName());
}

} // end namespace llvm
