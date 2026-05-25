#ifndef NEVERC_FOUNDATION_BUILTINSTRING_H
#define NEVERC_FOUNDATION_BUILTINSTRING_H

#include "neverc/Foundation/Builtin/BuiltinStringNames.h"
#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace BuiltinString {

bool isMethodName(llvm::StringRef Name);
bool isRuntimeFunctionName(llvm::StringRef Name);

llvm::StringRef getDefaultMethodFunctionName(llvm::StringRef Name);

llvm::StringRef getMethodOverloadFunctionName(llvm::StringRef Name,
                                              unsigned DottedArgCount);

llvm::StringRef getMethodCharOverloadFunctionName(llvm::StringRef Name,
                                                  unsigned DottedArgCount);

llvm::StringRef resolveMethodFunctionName(llvm::StringRef Name,
                                          unsigned DottedArgCount,
                                          bool FirstArgIsCharLike = false);

enum class MethodDefaultArgKind {
  None,

#define NEVERC_BUILTIN_STRING_METHOD_DEFAULT_ARG_KIND(Kind) Kind,
#include "neverc/Foundation/Builtin/BuiltinStringMethodDefaultArgKinds.def"
#undef NEVERC_BUILTIN_STRING_METHOD_DEFAULT_ARG_KIND
};

MethodDefaultArgKind getMethodDefaultArgKind(llvm::StringRef Name,
                                             unsigned DottedArgCount);

bool isLValueDirectHelper(llvm::StringRef Name, bool InsideRuntime);

enum class MethodReceiverKind {
  ByValue,

#define NEVERC_BUILTIN_STRING_METHOD_RECEIVER_KIND(Kind) Kind,
#include "neverc/Foundation/Builtin/BuiltinStringMethodReceiverKindsRoster.def"
#undef NEVERC_BUILTIN_STRING_METHOD_RECEIVER_KIND
};

MethodReceiverKind getMethodReceiverKind(llvm::StringRef Name);

llvm::StringRef getBuiltinStringPrelude();

llvm::StringRef getBuiltinStringThinHeader();
llvm::StringRef getEmbeddedStringBitcode();

} // namespace BuiltinString
} // namespace neverc

#endif
