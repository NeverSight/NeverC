#include "neverc/Invoke/OptionUtils.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/DiagnosticDriver.h"
#include "llvm/Option/ArgList.h"

using namespace neverc;
using namespace llvm::opt;

namespace {
template <typename IntTy>
IntTy getLastArgIntValueImpl(const ArgList &Args, OptSpecifier Id,
                             IntTy Default, DiagnosticsEngine *Diags,
                             unsigned Base) {
  IntTy Res = Default;
  if (Arg *A = Args.getLastArg(Id)) {
    if (llvm::StringRef(A->getValue()).getAsInteger(Base, Res)) {
      if (Diags)
        Diags->Report(diag::err_drv_invalid_int_value)
            << A->getAsString(Args) << A->getValue();
    }
  }
  return Res;
}
} // namespace

namespace neverc {

int getLastArgIntValue(const ArgList &Args, OptSpecifier Id, int Default,
                       DiagnosticsEngine *Diags, unsigned Base) {
  return getLastArgIntValueImpl<int>(Args, Id, Default, Diags, Base);
}

uint64_t getLastArgUInt64Value(const ArgList &Args, OptSpecifier Id,
                               uint64_t Default, DiagnosticsEngine *Diags,
                               unsigned Base) {
  return getLastArgIntValueImpl<uint64_t>(Args, Id, Default, Diags, Base);
}

} // namespace neverc
