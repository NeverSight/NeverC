#ifndef NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEREQUEST_H
#define NEVERC_LIB_DYNCODE_PLUGIN_DYNCODEREQUEST_H

// Host-side normalized, frozen DynCodeRequest.
//
// The driver parses -fdyncode options into a plain `DynCodeOptions`. Before any
// task runs, `freezeDynCodeRequest` normalizes those options against a resolved
// TargetKey/object format into an immutable `FrozenDynCodeRequest` and
// computes a deterministic 32-byte digest. Child tasks only borrow the frozen
// snapshot; there is no process-global "current dyncode options".

#include "neverc/DynCode/Pipeline/DynCodeOptions.h"
#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "neverc/Plugin/PluginDynCode.h"
#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace neverc {
namespace dyncode {

/// Immutable, normalized dyncode request. Owns all backing storage so the ABI
/// views produced by `fillRequestInfo` stay valid for the lifetime of the
/// object.
struct FrozenDynCodeRequest {
  plugin::OwnedTargetKey Target;
  NevercObjectFormatID ObjectFormat{};
  NevercDynCodeExecutionLevel ExecutionLevel = NEVERC_DYNCODE_LEVEL_USER;

  NevercDynCodeEntryPolicyKind EntryKind = NEVERC_DYNCODE_ENTRY_CANDIDATE_LIST;
  std::string EntrySymbol;
  std::vector<std::string> EntryCandidates;

  NevercDynCodePICFlags PICFlags = 0;
  NevercDynCodeRequestFlags Flags = 0;

  uint64_t MaxLength = 0;
  uint64_t Alignment = 1;
  uint32_t PadByte = 0;
  bool HasPadByte = false;

  std::vector<uint8_t> BadBytes; // sorted, unique
  std::string BadByteProfile;
  std::string CharsetProviderID;
  std::string TransformConfigNamespace;
  std::string MainOutputSinkID;
  std::string ObjectOutputSinkID;
  std::string ReportOutputSinkID;

  std::array<uint8_t, 32> Digest{};
};

/// Normalizes parsed driver options + a resolved TargetKey/object format into a
/// frozen request. Fails on internally inconsistent options (e.g. a pad byte
/// that is also a bad byte). Deterministic: identical inputs yield an identical
/// `Digest`.
llvm::Expected<FrozenDynCodeRequest>
freezeDynCodeRequest(const DynCodeOptions &Opts,
                     const plugin::OwnedTargetKey &Target,
                     NevercObjectFormatID ObjectFormat);

/// Fills an ABI request-info view that borrows storage from `Req`.
void fillRequestInfo(const FrozenDynCodeRequest &Req,
                     NevercDynCodeRequestInfo &Out,
                     std::vector<NevercStringView> &CandidateScratch);

} // namespace dyncode
} // namespace neverc

#endif
