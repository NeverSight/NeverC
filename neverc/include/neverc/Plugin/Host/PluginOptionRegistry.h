#ifndef NEVERC_PLUGIN_HOST_PLUGINOPTIONREGISTRY_H
#define NEVERC_PLUGIN_HOST_PLUGINOPTIONREGISTRY_H

#include "neverc/Plugin/PluginDriver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace neverc::plugin {

struct OwnedOptionEnumValue {
  std::string Name;
  int64_t Value = 0;
  std::string Help;
};

struct OwnedPluginOption {
  std::string PluginID;
  std::string Spelling;
  std::vector<std::string> Aliases;
  NevercOptionForm Form = NEVERC_OPTION_FLAG;
  NevercOptionValueType ValueType = NEVERC_OPTION_BOOL;
  NevercOptionMultiplicity Multiplicity = NEVERC_OPTION_SINGLE;
  uint32_t ArgumentCount = 0;
  bool Required = false;
  bool Hidden = false;
  std::string Help;
  std::string Metavar;
  std::vector<OwnedOptionEnumValue> EnumValues;
  std::vector<std::string> Conflicts;
  std::vector<std::string> Requires;
  std::string TargetPredicate;
  NevercOptionValidatorFn Validator = nullptr;
  void *UserData = nullptr;
};

llvm::Expected<OwnedPluginOption>
copyPluginOptionDescriptor(llvm::StringRef PluginID,
                           const NevercOptionDescriptor &Descriptor);

struct ParsedPluginOption {
  std::string PluginID;
  std::string Spelling;
  std::vector<std::string> Values;
  std::vector<uint64_t> ArgumentIndices;
};

class PluginOptionParseResult {
public:
  llvm::ArrayRef<std::string> remainingArguments() const {
    return RemainingArguments;
  }
  llvm::ArrayRef<uint64_t> remainingArgumentIndices() const {
    return RemainingArgumentIndices;
  }
  llvm::ArrayRef<ParsedPluginOption> options() const { return Options; }
  const ParsedPluginOption *find(llvm::StringRef PluginID,
                                 llvm::StringRef Spelling) const;

private:
  std::vector<std::string> RemainingArguments;
  std::vector<uint64_t> RemainingArgumentIndices;
  std::vector<ParsedPluginOption> Options;
  friend class PluginOptionRegistry;
};

class PluginOptionRegistry {
public:
  explicit PluginOptionRegistry(
      llvm::ArrayRef<llvm::StringRef> StaticSpellings = {});

  llvm::Error registerBatch(std::vector<OwnedPluginOption> Options);
  llvm::Error freeze();
  llvm::Error removePlugin(llvm::StringRef PluginID);

  llvm::Expected<PluginOptionParseResult>
  parse(llvm::ArrayRef<llvm::StringRef> Arguments,
        llvm::StringRef TargetTriple = "") const;

  bool isFrozen() const { return Frozen; }
  size_t size() const { return Registered.size(); }
  llvm::ArrayRef<OwnedPluginOption> options() const {
    return Registered;
  }

private:
  llvm::Error rebuildIndex();

  std::vector<std::string> StaticSpellings;
  std::vector<OwnedPluginOption> Registered;
  std::unordered_map<std::string, size_t> SpellingIndex;
  bool Frozen = false;
};

} // namespace neverc::plugin

#endif
