#ifndef NEVERC_LIB_PLUGIN_LINK_LINKREQUEST_H
#define NEVERC_LIB_PLUGIN_LINK_LINKREQUEST_H

#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "neverc/Plugin/PluginLink.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neverc::plugin {

struct OwnedRawLinkInput {
  NevercLinkInputKind Kind = NEVERC_LINK_INPUT_UNKNOWN;
  NevercLinkInputFlags Flags = NEVERC_LINK_INPUT_FLAG_NONE;
  uint64_t Ordinal = 0;
  std::string LogicalURI;
  std::vector<uint8_t> AuthorizedBlob;
  NevercObjectGraphHandle ObjectGraph{};
  NevercArtifactHandle Artifact{};
};

struct LinkRequestOptions {
  NevercLinkOptionFlags Flags = NEVERC_LINK_OPTION_NONE;
  std::string EntrySymbol;
  std::string InstallName;
  std::string Soname;
  uint64_t ImageBase = 0;
  uint64_t PageSize = 0;
  uint32_t ThreadBudget = 0;
  std::vector<std::string> SearchPaths;
  std::vector<std::string> Libraries;
};

struct LinkRequestData {
  NevercLinkRequestHandle Request{};
  NevercTaskHandle Task{};
  OwnedTargetKey Target;
  NevercObjectFormatID InputFormat{};
  NevercObjectFormatID OutputFormat{};
  NevercLinkOutputKind OutputKind = 0;
  std::string OutputURI;
  LinkRequestOptions Options;
  std::vector<OwnedRawLinkInput> Inputs;
  std::array<uint8_t, 32> RequestDigest{};
};

/// Immutable, owning request snapshot used for route selection and callbacks.
class LinkRequest final {
public:
  static llvm::Expected<std::shared_ptr<const LinkRequest>>
  create(LinkRequestData Data);

  NevercLinkRequestHandle handle() const { return Data.Request; }
  NevercTaskHandle task() const { return Data.Task; }
  NevercTargetKey target() const { return Data.Target.view(); }
  const OwnedTargetKey &ownedTarget() const { return Data.Target; }
  NevercObjectFormatID inputFormat() const { return Data.InputFormat; }
  NevercObjectFormatID outputFormat() const { return Data.OutputFormat; }
  NevercLinkOutputKind outputKind() const { return Data.OutputKind; }
  llvm::StringRef outputURI() const { return Data.OutputURI; }
  const LinkRequestOptions &options() const { return Data.Options; }
  llvm::ArrayRef<OwnedRawLinkInput> inputs() const { return Data.Inputs; }
  llvm::ArrayRef<uint8_t> digest() const { return Data.RequestDigest; }

  /// Returns a borrowed C view valid for the lifetime of this request.
  const NevercLinkRequest &cView() const { return CView; }

private:
  explicit LinkRequest(LinkRequestData Data);
  void rebuildCView();

  LinkRequestData Data;
  std::vector<NevercRawLinkInput> CInputs;
  std::vector<NevercStringView> CSearchPaths;
  std::vector<NevercStringView> CLibraries;
  NevercLinkRequest CView{};
};

} // namespace neverc::plugin

#endif
