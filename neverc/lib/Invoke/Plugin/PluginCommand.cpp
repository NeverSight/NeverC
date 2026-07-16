#include "Plugin/PluginCommand.h"
#include "Plugin/ActionGraph.h"
#include "neverc/Invoke/Action.h"
#include "neverc/Invoke/Compilation.h"
#include "neverc/Invoke/Tool.h"
#include "neverc/Invoke/ToolChain.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "llvm/Support/Error.h"

using namespace llvm;

namespace neverc::driver {
namespace {

class PluginCommandTool final : public Tool {
public:
  explicit PluginCommandTool(const ToolChain &TC)
      : Tool("neverc::plugin-job", "plugin job", TC) {}

  bool hasIntegratedCPP() const override { return false; }

  void ConstructJob(Compilation &, const JobAction &, const InputInfo &,
                    const InputInfoList &, const llvm::opt::ArgList &,
                    const char *) const override {
    llvm_unreachable("plugin command tool does not construct builtin jobs");
  }
};

} // namespace

Tool &Compilation::getPluginCommandTool() {
  if (!PluginCommandCreator)
    PluginCommandCreator =
        std::make_unique<PluginCommandTool>(DefaultToolChain);
  return *PluginCommandCreator;
}

PluginCommand::PluginCommand(
    const Action &Source, const Tool &Creator, NevercJobID JobIDValue,
    std::string CallbackIDValue,
    std::shared_ptr<plugin::PluginSession> SessionValue,
    const llvm::opt::ArgStringList &Arguments,
    llvm::ArrayRef<InputInfo> Inputs, llvm::ArrayRef<InputInfo> Outputs)
    : Command(Source, Creator, ResponseFileSupport::None(), "<plugin-job>",
              Arguments, Inputs, Outputs),
      JobID(JobIDValue), CallbackID(std::move(CallbackIDValue)),
      Session(std::move(SessionValue)) {
  InProcess = true;
}

void PluginCommand::Print(raw_ostream &OS, const char *Terminator, bool,
                          CrashReportInfo *) const {
  OS << " (plugin job) " << CallbackID << Terminator;
}

int PluginCommand::Execute(ArrayRef<StringRef>,
                           SmallVectorImpl<char> *ErrMsg,
                           bool *ExecutionFailed,
                           llvm::sys::ProcessInfo &PI) const {
  if (ExecutionFailed)
    *ExecutionFailed = false;
  if (!Session) {
    if (ExecutionFailed)
      *ExecutionFailed = true;
    if (ErrMsg)
      ErrMsg->assign({'p', 'l', 'u', 'g', 'i', 'n', ' ',
                      's', 'e', 's', 's', 'i', 'o', 'n',
                      ' ', 'i', 's', ' ', 'u', 'n', 'a',
                      'v', 'a', 'i', 'l', 'a', 'b', 'l', 'e'});
    PI.ReturnCode = 1;
    return 1;
  }

  std::vector<NevercStringView> ArgumentViews;
  ArgumentViews.reserve(getArguments().size());
  for (const char *Argument : getArguments()) {
    StringRef Value(Argument);
    ArgumentViews.push_back({Value.data(), Value.size()});
  }
  std::vector<NevercStringView> EnvironmentViews;
  EnvironmentViews.reserve(getEnvironment().size());
  for (const char *Entry : getEnvironment()) {
    StringRef Value(Entry);
    EnvironmentViews.push_back({Value.data(), Value.size()});
  }

  std::vector<NevercJobFile> Inputs;
  Inputs.reserve(getInputInfos().size());
  for (const InputInfo &Input : getInputInfos()) {
    auto PublicType = toPublicDriverType(Input.getType());
    if (!PublicType)
      continue;
    StringRef Path(Input.getFilename());
    NevercJobFile File{};
    File.Header = {sizeof(File), NEVERC_DRIVER_API_MAJOR,
                   NEVERC_DRIVER_API_MINOR, 0};
    File.Path = {Path.data(), Path.size()};
    File.Type = *PublicType;
    Inputs.push_back(File);
  }

  std::vector<NevercJobFile> Outputs;
  Outputs.reserve(getOutputFilenames().size());
  auto OutputType = toPublicDriverType(getSource().getType());
  if (OutputType) {
    for (const std::string &Output : getOutputFilenames()) {
      NevercJobFile File{};
      File.Header = {sizeof(File), NEVERC_DRIVER_API_MAJOR,
                     NEVERC_DRIVER_API_MINOR, 0};
      File.Path = {Output.data(), Output.size()};
      File.Type = *OutputType;
      Outputs.push_back(File);
    }
  } else {
    consumeError(OutputType.takeError());
  }

  NevercPluginJobContext Context{};
  Context.Header = {sizeof(Context), NEVERC_DRIVER_API_MAJOR,
                    NEVERC_DRIVER_API_MINOR, 0};
  Context.Session = Session->handle();
  Context.Job = JobID;
  Context.CallbackID = {CallbackID.data(), CallbackID.size()};
  Context.Arguments = {ArgumentViews.data(), ArgumentViews.size(),
                       sizeof(NevercStringView)};
  Context.Environment = {EnvironmentViews.data(), EnvironmentViews.size(),
                         sizeof(NevercStringView)};
  Context.Inputs = {Inputs.data(), Inputs.size(), sizeof(NevercJobFile)};
  Context.Outputs = {Outputs.data(), Outputs.size(),
                     sizeof(NevercJobFile)};

  int32_t ExitCode = 0;
  auto Result = Session->invokeDeferredCallback(
      "neverc.driver.job", CallbackID, &Context, &ExitCode);
  if (!Result || Result->Code != NEVERC_STATUS_OK) {
    std::string Message;
    if (!Result)
      Message = toString(Result.takeError()).str().str();
    else
      Message = "plugin job callback failed with status code " +
                std::to_string(Result->Code);
    if (ErrMsg)
      ErrMsg->assign(Message.begin(), Message.end());
    if (ExecutionFailed)
      *ExecutionFailed = true;
    PI.ReturnCode = 1;
    return 1;
  }
  PI.ReturnCode = ExitCode;
  return ExitCode;
}

} // namespace neverc::driver
