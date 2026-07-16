#ifndef NEVERC_INVOKE_PLUGIN_PLUGINCOMMAND_H
#define NEVERC_INVOKE_PLUGIN_PLUGINCOMMAND_H

#include "neverc/Invoke/Job.h"
#include "neverc/Plugin/PluginDriver.h"
#include <memory>
#include <string>

namespace neverc::plugin {
class PluginSession;
}

namespace neverc::driver {

class PluginCommand final : public Command {
public:
  PluginCommand(const Action &Source, const Tool &Creator,
                NevercJobID JobID, std::string CallbackID,
                std::shared_ptr<plugin::PluginSession> Session,
                const llvm::opt::ArgStringList &Arguments,
                llvm::ArrayRef<InputInfo> Inputs,
                llvm::ArrayRef<InputInfo> Outputs);

  CommandKind getKind() const override { return CK_PluginCommand; }

  void Print(llvm::raw_ostream &OS, const char *Terminator, bool Quote,
             CrashReportInfo *CrashInfo = nullptr) const override;
  int Execute(llvm::ArrayRef<llvm::StringRef> Redirects,
              llvm::SmallVectorImpl<char> *ErrMsg, bool *ExecutionFailed,
              llvm::sys::ProcessInfo &PI) const override;

  NevercJobID getJobID() const { return JobID; }
  llvm::StringRef getCallbackID() const { return CallbackID; }

private:
  NevercJobID JobID = 0;
  std::string CallbackID;
  std::shared_ptr<plugin::PluginSession> Session;
};

} // namespace neverc::driver

#endif
