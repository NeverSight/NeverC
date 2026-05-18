#ifndef NEVERC_AST_PRETTYPRINTER_H
#define NEVERC_AST_PRETTYPRINTER_H

#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

namespace neverc {

class Stmt;

class PrinterHelper {
public:
  virtual ~PrinterHelper();
  virtual bool handledStmt(Stmt *E, llvm::raw_ostream &OS) = 0;
};

class PrintingCallbacks {
protected:
  ~PrintingCallbacks() = default;

public:
  virtual std::string remapPath(llvm::StringRef Path) const {
    return std::string(Path);
  }
};

struct PrintingPolicy {
  PrintingPolicy(const LangOptions &LO)
      : Indentation(2), SuppressSpecifiers(false), SuppressTagKeyword(false),
        IncludeTagDefinition(false), SuppressScope(false),
        AnonymousTagLocations(true), Bool(LO.Bool), Nullptr(LO.C23),
        Restrict(LO.C99), UnderscoreAlignof(LO.C11), UseVoidForZeroParams(true),
        MSWChar(LO.MicrosoftExt && !LO.WChar), MSVCFormatting(false),
        PrintCanonicalTypes(false), EntireContentsOfLargeArray(true),
        UseEnumerators(true) {}

  unsigned Indentation : 8;

  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressSpecifiers : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressTagKeyword : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned IncludeTagDefinition : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressScope : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned AnonymousTagLocations : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned Bool : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned Nullptr : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned Restrict : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned UnderscoreAlignof : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned UseVoidForZeroParams : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned MSWChar : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned MSVCFormatting : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned PrintCanonicalTypes : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned EntireContentsOfLargeArray : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned UseEnumerators : 1;

  const PrintingCallbacks *Callbacks = nullptr;
};

} // end namespace neverc

#endif
