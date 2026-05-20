#ifndef NEVERC_INVOKE_TYPES_H
#define NEVERC_INVOKE_TYPES_H

#include "neverc/Invoke/Phases.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Option/ArgList.h"

namespace llvm {
class StringRef;
}
namespace neverc {
namespace driver {
class Driver;
namespace types {
enum ID {
  TY_INVALID,
#define TYPE(NAME, ID, PP_TYPE, TEMP_SUFFIX, ...) TY_##ID,
#include "neverc/Invoke/Types.def"
#undef TYPE
  TY_LAST
};

const char *getTypeName(ID Id);

ID getPreprocessedType(ID Id);

const char *getTypeTempSuffix(ID Id, bool CLStyle = false);

bool canTypeBeUserSpecified(ID Id);

bool appendSuffixForType(ID Id);

bool canLipoType(ID Id);

bool isAcceptedByNeverC(ID Id);

bool isDerivedFromC(ID Id);

bool isLLVMIR(ID Id);

bool isSrcFile(ID Id);

ID lookupTypeForExtension(llvm::StringRef Ext);

ID lookupTypeForTypeSpecifier(const char *Name);

llvm::SmallVector<phases::ID, phases::MaxNumberOfPhases>
getCompilationPhases(ID Id, phases::ID LastPhase = phases::Link);
llvm::SmallVector<phases::ID, phases::MaxNumberOfPhases>
getCompilationPhases(const neverc::driver::Driver &Driver,
                     llvm::opt::DerivedArgList &DAL, ID Id);

ID lookupHeaderTypeForSourceType(ID Id);

} // end namespace types
} // end namespace driver
} // end namespace neverc

#endif
