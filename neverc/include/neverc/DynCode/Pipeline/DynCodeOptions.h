#ifndef NEVERC_DYNCODE_DYNCODEOPTIONS_H
#define NEVERC_DYNCODE_DYNCODEOPTIONS_H

#include "neverc/DynCode/Pipeline/TargetDesc.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace neverc {
namespace dyncode {

struct DynCodeOptions {
  bool Enabled = false;
  bool AllBlr = false;
  bool SyscallInlining = false;
  bool WindowsPEBImport = false;
  ExecutionLevel Level = ExecutionLevel::User;
  std::string KeepObjPath;
  std::string EntrySymbol;
  std::string BadByteProfile;
  std::vector<uint8_t> BadBytes;
  bool BadByteRewrite = true;
  std::string Charset;
  std::optional<uint64_t> MaxLength;
  uint32_t Align = 1;
  std::optional<uint8_t> PadByte;
  bool HeapArena = true;
  bool InlineAll = false;
  bool Verbose = false;
  std::string ObfuscateSpec;
  std::string MirObfuscateSpec;
  TargetDesc Target;
  /// Normalized target triple the request was frozen for.  The format-agnostic
  /// extractor uses it to build the built-in TargetKey that the object Reader
  /// matches against; TargetDesc only keeps the OS/arch/format enums.
  std::string TargetTriple;
  /// Requested CPU (``-mcpu``) or empty for the route default.
  std::string CPU;
  /// Optional side-output path for the canonical dyncode report JSON
  /// (``-fdyncode-report=<path>``).
  std::string ReportPath;
};

}
}

#endif
