#ifndef NEVERC_TRANSLATE_ARTIFACTWRITER_H
#define NEVERC_TRANSLATE_ARTIFACTWRITER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem/UniqueID.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace neverc::translate {

struct ArtifactOptions {
  std::string Output;
  std::string OutputDirectory;
  std::string Report;
  // Project profiles publish a single combined source and its public header.
  bool Project = false;
};

struct Artifact {
  std::string Name;
  std::string Contents;
};

/// Owns temporary files and only the final paths this invocation created.
/// No existing destination is ever replaced, including at publication time.
class ArtifactWriter {
public:
  static llvm::Expected<std::unique_ptr<ArtifactWriter>>
  create(const ArtifactOptions &Options, llvm::StringRef Source);
  ~ArtifactWriter();

  llvm::StringRef stagingDirectory() const { return Staging; }
  llvm::StringRef sourceName() const { return SourceName; }
  llvm::StringRef mapName() const { return MapName; }
  llvm::StringRef manifestName() const { return ManifestName; }
  llvm::StringRef headerName() const { return HeaderName; }
  std::string stagePath(llvm::StringRef Name) const;
  llvm::Error writeStage(llvm::StringRef Name, llvm::StringRef Contents);
  llvm::Error publish(const std::vector<Artifact> &Artifacts,
                      llvm::StringRef Report,
                      std::function<bool()> IsCancelled = {});
  llvm::Error publishFailureReport(llvm::StringRef Report);

private:
  ArtifactWriter() = default;
  llvm::Error publishReport(llvm::StringRef Contents);
  llvm::Error publishFile(llvm::StringRef From, llvm::StringRef To);
  llvm::Error rollback();
  std::string Staging, Output, OutputDirectory, ReportPath;
  std::string SourceName, MapName, ManifestName, HeaderName, ReportName;
  bool ReportInsideDirectory = false;
  bool Committed = false;
  struct PublishedFile {
    std::string Path;
    llvm::sys::fs::UniqueID Identity;
  };
  std::vector<PublishedFile> Published;
};

llvm::Expected<std::string> readFile(llvm::StringRef Path,
                                     uint64_t Limit = 32 * 1024 * 1024);
std::string sha256(llvm::StringRef Contents);
llvm::Error writeExclusive(llvm::StringRef Path, llvm::StringRef Contents);

} // namespace neverc::translate
#endif
