#ifndef NEVERC_PLUGIN_HOST_PLUGINTARGETINFO_H
#define NEVERC_PLUGIN_HOST_PLUGINTARGETINFO_H

#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include <cstdint>
#include <string>

namespace neverc::plugin {

class PluginTaskContext;

class PluginTargetInfo final : public TargetInfo {
public:
  explicit PluginTargetInfo(
      const PluginTargetSnapshot::TargetRecord &Record,
      llvm::StringRef RequestedTriple = {},
      const PluginTargetSnapshot::NamedRecord *ABI = nullptr,
      const PluginTargetSnapshot::NamedRecord *CallingConvention =
          nullptr,
      PluginTaskContext *Task = nullptr);

  const PluginTargetSnapshot::TargetRecord &record() const {
    return Record;
  }
  const PluginTargetSnapshot::NamedRecord *abi() const {
    return ABI ? &*ABI : nullptr;
  }
  const PluginTargetSnapshot::NamedRecord *
  callingConvention() const {
    return CallingConvention ? &*CallingConvention : nullptr;
  }
  PluginTaskContext *task() const { return Task; }
  llvm::StringRef selectedCPU() const { return CPU; }
  const VerifiedTargetBuiltin *getPluginBuiltin(unsigned BuiltinID) const;
  const VerifiedTargetConstraint *
  findPluginConstraint(llvm::StringRef Spelling) const;
  const PluginTargetInfo *getPluginTargetInfo() const override {
    return this;
  }

  void getTargetDefines(const LangOptions &Opts,
                        MacroBuilder &Builder) const override;
  llvm::ArrayRef<Builtin::Info> getTargetBuiltins() const override;
  BuiltinVaListKind getBuiltinVaListKind() const override;
  bool validateAsmConstraint(const char *&Name,
                             ConstraintInfo &Info) const override;
  std::string convertConstraint(
      const char *&Constraint) const override;
  std::string_view getClobbers() const override;

  bool setCPU(const std::string &Name) override;
  bool isValidCPUName(llvm::StringRef Name) const override;
  bool isValidTuneCPUName(llvm::StringRef Name) const override;
  void fillValidCPUList(
      llvm::SmallVectorImpl<llvm::StringRef> &Values) const override;
  void fillValidTuneCPUList(
      llvm::SmallVectorImpl<llvm::StringRef> &Values) const override;
  bool initFeatureMap(
      llvm::StringMap<bool> &Features, DiagnosticsEngine &Diags,
      llvm::StringRef CPU,
      const std::vector<std::string> &FeatureVec) const override;
  bool isValidFeatureName(llvm::StringRef Feature) const override;
  bool handleTargetFeatures(std::vector<std::string> &Features,
                            DiagnosticsEngine &Diags) override;
  bool hasFeature(llvm::StringRef Feature) const override;
  uint64_t getMaxPointerWidth() const override;

protected:
  uint64_t getPointerWidthV(LangAS AddressSpace) const override;
  uint64_t getPointerAlignV(LangAS AddressSpace) const override;
  llvm::ArrayRef<const char *> getGCCRegNames() const override;
  llvm::ArrayRef<GCCRegAlias> getGCCRegAliases() const override;
  llvm::ArrayRef<AddlRegName> getGCCAddlRegNames() const override;

private:
  const VerifiedTargetAddressSpace *
  findAddressSpace(LangAS AddressSpace) const;

  PluginTargetSnapshot::TargetRecord Record;
  std::optional<PluginTargetSnapshot::NamedRecord> ABI;
  std::optional<PluginTargetSnapshot::NamedRecord>
      CallingConvention;
  PluginTaskContext *Task = nullptr;
  std::string CPU;
  std::vector<Builtin::Info> BuiltinInfos;
  std::vector<const char *> RegisterNames;
  std::vector<GCCRegAlias> RegisterAliases;
  std::vector<AddlRegName> RegisterAdditionalNames;
  mutable std::vector<std::string> CallbackCPUs;
  mutable bool CallbackCPUsLoaded = false;
  llvm::StringSet<> ActiveFeatures;
  uint64_t MaximumPointerWidth = 0;
};

} // namespace neverc::plugin

#endif
