#ifndef NEVERC_COMPILER_UTILS_H
#define NEVERC_COMPILER_UTILS_H

#include "neverc/Compiler/DependencyOutputOptions.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Invoke/OptionUtils.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace neverc {

class DiagnosticsEngine;
class FrontendOptions;
class PrepEngine;
class PrepOptions;
class PrepOutputOptions;

void InitializePrepEngine(PrepEngine &PP, const PrepOptions &PPOpts,
                          const FrontendOptions &FEOpts);

void DoPrintPreprocessedInput(PrepEngine &PP, llvm::raw_ostream *OS,
                              const PrepOutputOptions &Opts);

class DependencyCollector {
public:
  virtual ~DependencyCollector();

  virtual void attachToPrepEngine(PrepEngine &PP);
  llvm::ArrayRef<std::string> getDependencies() const { return Dependencies; }

  virtual bool sawDependency(llvm::StringRef Filename, bool IsSystem,
                             bool IsMissing);

  virtual void finishedMainFile(DiagnosticsEngine &Diags) {}

  virtual bool needSystemDependencies() { return false; }

  virtual void maybeAddDependency(llvm::StringRef Filename, bool IsSystem,
                                  bool IsMissing);

protected:
  bool addDependency(llvm::StringRef Filename);

private:
  llvm::StringSet<> Seen;
  std::vector<std::string> Dependencies;
};

class DependencyFileGenerator : public DependencyCollector {
public:
  DependencyFileGenerator(const DependencyOutputOptions &Opts);

  void attachToPrepEngine(PrepEngine &PP) override;

  void finishedMainFile(DiagnosticsEngine &Diags) override;

  bool needSystemDependencies() final { return IncludeSystemHeaders; }

  bool sawDependency(llvm::StringRef Filename, bool IsSystem,
                     bool IsMissing) final;

protected:
  void outputDependencyFile(llvm::raw_ostream &OS);

private:
  void outputDependencyFile(DiagnosticsEngine &Diags);

  std::string OutputFile;
  std::vector<std::string> Targets;
  bool IncludeSystemHeaders;
  bool PhonyTarget;
  bool AddMissingHeaderDeps;
  bool SeenMissingHeader;
  DependencyOutputFormat OutputFormat;
  unsigned InputFileIndex;
};

void AttachHeaderIncludeGen(PrepEngine &PP,
                            const DependencyOutputOptions &DepOpts,
                            bool ShowAllHeaders = false,
                            llvm::StringRef OutputPath = {},
                            bool ShowDepth = true, bool MSStyle = false);

} // namespace neverc

#endif // NEVERC_COMPILER_UTILS_H
