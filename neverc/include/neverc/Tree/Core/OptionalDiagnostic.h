#ifndef NEVERC_AST_OPTIONALDIAGNOSTIC_H
#define NEVERC_AST_OPTIONALDIAGNOSTIC_H

#include "neverc/Foundation/Diagnostic/PartialDiagnostic.h"
#include "neverc/Tree/Core/APValue.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace neverc {

class OptionalDiagnostic {
  PartialDiagnostic *Diag;

public:
  explicit OptionalDiagnostic(PartialDiagnostic *Diag = nullptr) : Diag(Diag) {}

  template <typename T> OptionalDiagnostic &operator<<(const T &v) {
    if (Diag)
      *Diag << v;
    return *this;
  }

  OptionalDiagnostic &operator<<(const llvm::APSInt &I) {
    if (Diag) {
      llvm::SmallVector<char, 32> Buffer;
      I.toString(Buffer);
      *Diag << llvm::StringRef(Buffer.data(), Buffer.size());
    }
    return *this;
  }

  OptionalDiagnostic &operator<<(const llvm::APFloat &F) {
    if (Diag) {
      unsigned precision = llvm::APFloat::semanticsPrecision(F.getSemantics());
      precision = (precision * 59 + 195) / 196;
      llvm::SmallVector<char, 32> Buffer;
      F.toString(Buffer, precision);
      *Diag << llvm::StringRef(Buffer.data(), Buffer.size());
    }
    return *this;
  }

  OptionalDiagnostic &operator<<(const llvm::APFixedPoint &FX) {
    if (Diag) {
      llvm::SmallVector<char, 32> Buffer;
      FX.toString(Buffer);
      *Diag << llvm::StringRef(Buffer.data(), Buffer.size());
    }
    return *this;
  }
};

} // namespace neverc

#endif
