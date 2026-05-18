#ifndef NEVERC_BASIC_TARGETOPTIONS_H
#define NEVERC_BASIC_TARGETOPTIONS_H

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Target/TargetOptions.h"
#include <string>
#include <vector>

namespace neverc {

class TargetOptions {
public:
  std::string Triple;

  std::string CPU;

  std::string TuneCPU;

  std::string FPMath;

  std::string ABI;

  std::string LinkerVersion;

  std::vector<std::string> FeaturesAsWritten;

  std::vector<std::string> Features;

  llvm::StringMap<bool> FeatureMap;

  bool ForceEnableInt128 = false;

  // The code model to be used as specified by the user. Corresponds to
  // CodeModel::Model enum defined in include/llvm/Support/CodeGen.h, plus
  // "default" for the case when the user has not explicitly specified a
  // code model.
  std::string CodeModel;

  // The large data threshold used for certain code models on certain
  // architectures.
  uint64_t LargeDataThreshold;

  llvm::VersionTuple SDKVersion;
};

} // end namespace neverc

#endif
