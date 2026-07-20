#ifndef NEVERC_PLUGIN_LINK_LINKERSCRIPTPROVIDER_H
#define NEVERC_PLUGIN_LINK_LINKERSCRIPTPROVIDER_H

#include "neverc/Plugin/Host/PluginTargetDescriptor.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginTaskContext;

struct LinkerScriptOption {
  std::string Name;
  std::string Value;
};

struct LinkerScriptInput {
  std::string LogicalURI;
  bool InGroup = false;
  bool AsNeeded = false;
};

struct LinkerScriptLayoutConstraint {
  std::string Kind;
  std::string Expression;
};

struct LinkerScriptResult {
  std::string ProviderRoute;
  std::vector<LinkerScriptOption> Options;
  std::vector<LinkerScriptInput> Inputs;
  std::vector<LinkerScriptLayoutConstraint> LayoutConstraints;
};

/// A typed linker-script parser. Implementations return options, input
/// references, and layout constraints; script bytes are never sent through an
/// ObjectReader.
class LinkerScriptProvider {
public:
  virtual ~LinkerScriptProvider() = default;

  virtual llvm::Expected<LinkerScriptResult>
  parse(PluginTaskContext &Task, llvm::StringRef LogicalURI,
        llvm::ArrayRef<uint8_t> Bytes,
        const OwnedTargetKey &Target) const = 0;
};

const LinkerScriptProvider &builtinLinkerScriptProvider();

} // namespace neverc::plugin

#endif
