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
};

}
}

#endif
