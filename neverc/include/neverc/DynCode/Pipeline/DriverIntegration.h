#ifndef NEVERC_DYNCODE_DRIVERINTEGRATION_H
#define NEVERC_DYNCODE_DRIVERINTEGRATION_H

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "llvm/ADT/SmallVector.h"
#include <set>
#include <string>

namespace neverc {
namespace dyncode {

struct CompilationState {
  DynCodeOptions Opts;
  std::string OutputBin;
  std::string TmpObj;

  std::set<std::string> StringPool;
  bool enabled() const { return Opts.Enabled; }
  bool PrintOnly = false;
};

int configureCompilation(llvm::SmallVectorImpl<const char *> &Args,
                         CompilationState &State);
int finalizeCompilation(const CompilationState &State, int CompilationRes);

}
}

#endif
