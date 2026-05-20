#ifndef NEVERC_INVOKE_OPTIONUTILS_H
#define NEVERC_INVOKE_OPTIONUTILS_H

#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "llvm/Option/OptSpecifier.h"

namespace llvm {

namespace opt {

class ArgList;

} // namespace opt

} // namespace llvm

namespace neverc {
int getLastArgIntValue(const llvm::opt::ArgList &Args,
                       llvm::opt::OptSpecifier Id, int Default,
                       DiagnosticsEngine *Diags = nullptr, unsigned Base = 0);

inline int getLastArgIntValue(const llvm::opt::ArgList &Args,
                              llvm::opt::OptSpecifier Id, int Default,
                              DiagnosticsEngine &Diags, unsigned Base = 0) {
  return getLastArgIntValue(Args, Id, Default, &Diags, Base);
}

uint64_t getLastArgUInt64Value(const llvm::opt::ArgList &Args,
                               llvm::opt::OptSpecifier Id, uint64_t Default,
                               DiagnosticsEngine *Diags = nullptr,
                               unsigned Base = 0);

inline uint64_t getLastArgUInt64Value(const llvm::opt::ArgList &Args,
                                      llvm::opt::OptSpecifier Id,
                                      uint64_t Default,
                                      DiagnosticsEngine &Diags,
                                      unsigned Base = 0) {
  return getLastArgUInt64Value(Args, Id, Default, &Diags, Base);
}

} // namespace neverc

#endif // NEVERC_INVOKE_OPTIONUTILS_H
