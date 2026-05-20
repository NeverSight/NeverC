#ifndef NEVERC_COMPILER_DEPENDENCYOUTPUTOPTIONS_H
#define NEVERC_COMPILER_DEPENDENCYOUTPUTOPTIONS_H

#include "neverc/Foundation/Core/HeaderInclude.h"
#include <string>
#include <vector>

namespace neverc {

enum class ShowIncludesDestination { None, Stdout, Stderr };

enum class DependencyOutputFormat { Make, NMake };

enum ExtraDepKind {
  EDK_DepFileEntry,
};

class DependencyOutputOptions {
public:
  LLVM_PREFERRED_TYPE(bool)
  unsigned IncludeSystemHeaders : 1; ///< Include system header dependencies.
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowHeaderIncludes : 1; ///< Show header inclusions (-H).
  LLVM_PREFERRED_TYPE(bool)
  unsigned UsePhonyTargets : 1; ///< Include phony targets for each
                                /// dependency, which can avoid some 'make'
                                /// problems.
  LLVM_PREFERRED_TYPE(bool)
  unsigned AddMissingHeaderDeps : 1; ///< Add missing headers to dependency list
  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowSkippedHeaderIncludes : 1; ///< With ShowHeaderIncludes, show
                                          /// also includes that were skipped
                                          /// due to the "include guard
                                          /// optimization" or #pragma once.

  HeaderIncludeFormatKind HeaderIncludeFormat = HIFMT_Textual;

  HeaderIncludeFilteringKind HeaderIncludeFiltering = HIFIL_None;

  ShowIncludesDestination ShowIncludesDest = ShowIncludesDestination::None;

  DependencyOutputFormat OutputFormat = DependencyOutputFormat::Make;

  std::string OutputFile;

  std::string HeaderIncludeOutputFile;

  std::vector<std::string> Targets;

  std::vector<std::pair<std::string, ExtraDepKind>> ExtraDeps;

public:
  DependencyOutputOptions()
      : IncludeSystemHeaders(0), ShowHeaderIncludes(0), UsePhonyTargets(0),
        AddMissingHeaderDeps(0), ShowSkippedHeaderIncludes(0),
        HeaderIncludeFormat(HIFMT_Textual), HeaderIncludeFiltering(HIFIL_None) {
  }
};

} // end namespace neverc

#endif
