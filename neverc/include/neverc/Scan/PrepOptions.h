#ifndef NEVERC_SCAN_PREPOPTIONS_H_
#define NEVERC_SCAN_PREPOPTIONS_H_

#include "neverc/Foundation/Core/FileEntry.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace neverc {

class PrepOptions {
public:
  std::vector<std::pair<std::string, bool /*isUndef*/>> Macros;
  std::vector<std::string> Includes;
  std::vector<std::string> MacroIncludes;

  bool UsePredefines = true;

  bool DefineTargetOSMacros = false;

  std::optional<uint64_t> SourceDateEpoch;

public:
  PrepOptions() = default;

  void addMacroDef(llvm::StringRef Name) {
    Macros.emplace_back(std::string(Name), false);
  }
  void addMacroUndef(llvm::StringRef Name) {
    Macros.emplace_back(std::string(Name), true);
  }
};

} // namespace neverc

#endif // NEVERC_SCAN_PREPOPTIONS_H_
