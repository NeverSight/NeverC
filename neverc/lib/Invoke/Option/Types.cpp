#include "neverc/Invoke/Types.h"
#include "neverc/Invoke/Driver.h"
#include "neverc/Invoke/DriverDiagnostic.h"
#include "neverc/Invoke/Options.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Option/Arg.h"
#include <cassert>
#include <cstring>

using namespace neverc::driver;
using namespace neverc::driver::types;

// ===----------------------------------------------------------------------===
// Type table
// ===----------------------------------------------------------------------===

struct TypeInfo {
  const char *Name;
  const char *TempSuffix;
  ID PreprocessedType;
  class PhasesBitSet {
    unsigned Bits = 0;

  public:
    constexpr PhasesBitSet(std::initializer_list<phases::ID> Phases) {
      for (auto Id : Phases)
        Bits |= 1 << Id;
    }
    bool contains(phases::ID Id) const { return Bits & (1 << Id); }
  } Phases;
};

namespace {
constexpr TypeInfo TypeInfos[] = {
#define TYPE(NAME, ID, PP_TYPE, TEMP_SUFFIX, ...)                              \
  {                                                                            \
      NAME,                                                                    \
      TEMP_SUFFIX,                                                             \
      TY_##PP_TYPE,                                                            \
      {__VA_ARGS__},                                                           \
  },
#include "neverc/Invoke/Types.def"
#undef TYPE
};
const unsigned numTypes = std::size(TypeInfos);

const TypeInfo &getInfo(unsigned id) {
  assert(id > 0 && id - 1 < numTypes && "Invalid Type ID.");
  return TypeInfos[id - 1];
}
} // namespace

const char *types::getTypeName(ID Id) { return getInfo(Id).Name; }

types::ID types::getPreprocessedType(ID Id) {
  ID PPT = getInfo(Id).PreprocessedType;
  assert((getInfo(Id).Phases.contains(phases::Preprocess) !=
          (PPT == TY_INVALID)) &&
         "Unexpected Preprocess Type.");
  return PPT;
}

const char *types::getTypeTempSuffix(ID Id, bool CLStyle) {
  if (CLStyle) {
    switch (Id) {
    case TY_Object:
    case TY_LTO_BC:
      return "obj";
    case TY_Image:
      return "exe";
    case TY_PP_Asm:
      return "asm";
    default:
      break;
    }
  }
  return getInfo(Id).TempSuffix;
}

bool types::canTypeBeUserSpecified(ID Id) {
  static const neverc::driver::types::ID kStaticLangageTypes[] = {
      TY_LTO_IR, TY_LTO_BC, TY_Object, TY_Image, TY_dSYM, TY_Dependencies};
  return !llvm::is_contained(kStaticLangageTypes, Id);
}

bool types::appendSuffixForType(ID Id) { return Id == TY_dSYM; }

bool types::canLipoType(ID Id) {
  return (Id == TY_Nothing || Id == TY_Image || Id == TY_Object ||
          Id == TY_LTO_BC);
}

bool types::isAcceptedByNeverC(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_Asm:
  case TY_C:
  case TY_PP_C:
  case TY_CHeader:
  case TY_LLVM_IR:
  case TY_LLVM_BC:
    return true;
  }
}

bool types::isDerivedFromC(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_PP_C:
  case TY_C:
  case TY_CHeader:
    return true;
  }
}

bool types::isLLVMIR(ID Id) {
  switch (Id) {
  default:
    return false;

  case TY_LLVM_IR:
  case TY_LLVM_BC:
  case TY_LTO_IR:
  case TY_LTO_BC:
    return true;
  }
}

bool types::isSrcFile(ID Id) {
  return Id != TY_Object && getPreprocessedType(Id) != TY_INVALID;
}

types::ID types::lookupTypeForExtension(llvm::StringRef Ext) {
  return llvm::StringSwitch<types::ID>(Ext)
      .Case("c", TY_C)
      .Case("nc", TY_C)
      .Case("h", TY_CHeader)
      .Case("i", TY_PP_C)
      .Case("o", TY_Object)
      .Case("S", TY_Asm)
      .Case("s", TY_PP_Asm)
      .Case("bc", TY_LLVM_BC)
      .Case("ll", TY_LLVM_IR)
      .Case("asm", TY_PP_Asm)
      .Case("lib", TY_Object)
      .Case("obj", TY_Object)
      .Default(TY_INVALID);
}

types::ID types::lookupTypeForTypeSpecifier(const char *Name) {
  for (unsigned i = 0; i < numTypes; ++i) {
    types::ID Id = (types::ID)(i + 1);
    if (canTypeBeUserSpecified(Id) && strcmp(Name, getInfo(Id).Name) == 0)
      return Id;
  }
  return TY_INVALID;
}

llvm::SmallVector<phases::ID, phases::MaxNumberOfPhases>
types::getCompilationPhases(ID Id, phases::ID LastPhase) {
  llvm::SmallVector<phases::ID, phases::MaxNumberOfPhases> P;
  const auto &Info = getInfo(Id);
  for (int I = 0; I <= LastPhase; ++I)
    if (Info.Phases.contains(static_cast<phases::ID>(I)))
      P.push_back(static_cast<phases::ID>(I));
  assert(P.size() <= phases::MaxNumberOfPhases && "Too many phases in list");
  return P;
}

llvm::SmallVector<phases::ID, phases::MaxNumberOfPhases>
types::getCompilationPhases(const neverc::driver::Driver &Driver,
                            llvm::opt::DerivedArgList &DAL, ID Id) {
  return types::getCompilationPhases(Id, Driver.getFinalPhase(DAL));
}

ID types::lookupHeaderTypeForSourceType(ID Id) {
  switch (Id) {
  default:
    return Id;
  case types::TY_C:
    return types::TY_CHeader;
  }
}
