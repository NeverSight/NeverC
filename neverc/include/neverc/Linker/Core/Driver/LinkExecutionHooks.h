#ifndef LINKER_CORE_DRIVER_LINKEXECUTIONHOOKS_H
#define LINKER_CORE_DRIVER_LINKEXECUTIONHOOKS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
class raw_ostream;
}

namespace linker {

struct LinkerDriverConfig;

struct LinkExecutionID {
  uint64_t High = 0;
  uint64_t Low = 0;
};

enum class LinkExecutionInputKind : uint32_t {
  Unknown = 0,
  Object = 1,
  Archive = 2,
  SharedLibrary = 3,
  Bitcode = 4,
  Script = 5,
  Blob = 6,
};

enum class LinkExecutionOutputKind : uint32_t {
  Relocatable = 1,
  Executable = 2,
  SharedLibrary = 3,
  Bundle = 4,
};

struct LinkExecutionInput {
  LinkExecutionInputKind Kind = LinkExecutionInputKind::Unknown;
  uint64_t Flags = 0;
  uint64_t Ordinal = 0;
  std::string LogicalURI;
  std::vector<uint8_t> AuthorizedBlob;
  LinkExecutionID Artifact{};
};

/// Driver-produced immutable semantic request. Raw argv is provenance only.
struct LinkExecutionRequest {
  std::string TargetTriple;
  LinkExecutionID TargetID{};
  LinkExecutionID InputFormat{};
  LinkExecutionID OutputFormat{};
  LinkExecutionOutputKind OutputKind = LinkExecutionOutputKind::Executable;
  std::string OutputURI;
  std::vector<LinkExecutionInput> Inputs;
  std::vector<std::string> ArgumentProvenance;
};

enum class LinkHookDisposition : uint8_t {
  ContinueBuiltin,
  Completed,
  Failed,
};

struct LinkHookResult {
  LinkHookDisposition Disposition = LinkHookDisposition::ContinueBuiltin;
  int ExitCode = 0;
};

/// Nullable linker-core injection seam. Implementations live outside Core.
class LinkExecutionHooks {
public:
  virtual ~LinkExecutionHooks() = default;

  virtual llvm::Expected<LinkHookResult>
  execute(const LinkExecutionRequest &Request,
          const LinkerDriverConfig &Config, llvm::raw_ostream &Stdout,
          llvm::raw_ostream &Stderr) = 0;

  virtual void complete(bool Success) noexcept = 0;
};

} // namespace linker

#endif
