#include "Plugin/PluginBootstrap.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include <cstring>
#include <set>
#include <utility>

using namespace llvm;

namespace neverc::driver {
namespace {

Error bootstrapError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

} // namespace

PluginBootstrap::PluginBootstrap(
    std::string HostBuildIDValue, uint32_t LLVMMajorValue,
    std::vector<std::string> StaticOptionSpellingsValue)
    : HostBuildID(std::move(HostBuildIDValue)),
      LLVMMajor(LLVMMajorValue),
      StaticOptionSpellings(std::move(StaticOptionSpellingsValue)) {}

PluginBootstrap::~PluginBootstrap() {
  consumeError(shutdown());
}

bool PluginBootstrap::isReservedBootstrapToken(StringRef Token) {
  return Token.starts_with("-fplugin=") ||
         Token.starts_with("-fplugin-pass=") ||
         Token.starts_with("-fplugin-arg=") ||
         Token.starts_with("-fplugin-pass-arg=") ||
         Token.starts_with("-fplugin-provider=");
}

std::vector<std::string>
PluginBootstrap::collectStaticOptionSpellings(
    const opt::OptTable &Table) {
  std::set<std::string> Unique;
  for (unsigned ID = 1; ID < Table.getNumOptions(); ++ID) {
    opt::Option Option = Table.getOption(ID);
    if (!Option.isValid())
      continue;
    StringRef Spelling = Option.getPrefixedName();
    if (Spelling.starts_with("-"))
      Unique.insert(Spelling.str());
  }
  return {Unique.begin(), Unique.end()};
}

Error PluginBootstrap::discoverAndActivate(
    ArrayRef<PluginBootstrapToken> Tokens,
    InterfaceInitializer InitializeInterfaces) {
  if (Activated)
    return bootstrapError("plugin bootstrap is already activated");

  SmallVector<StringRef, 4> Paths;
  bool HasPluginControlArgument = false;
  for (const PluginBootstrapToken &Token : Tokens) {
    StringRef Argument = Token.Value;
    if (Argument == "-fplugin" || Argument == "-fplugin-pass")
      return bootstrapError("-fplugin requires '=path'");
    StringRef Path;
    if (Argument.starts_with("-fplugin="))
      Path = Argument.drop_front(strlen("-fplugin="));
    else if (Argument.starts_with("-fplugin-pass="))
      Path = Argument.drop_front(strlen("-fplugin-pass="));
    else {
      HasPluginControlArgument |=
          Argument.starts_with("-fplugin-arg=") ||
          Argument.starts_with("-fplugin-pass-arg=") ||
          Argument.starts_with("-fplugin-provider=");
      continue;
    }
    if (Path.empty())
      return bootstrapError("-fplugin path must not be empty");
    Paths.push_back(Path);
  }
  if (Paths.empty()) {
    if (HasPluginControlArgument)
      return bootstrapError(
          "plugin arguments or Provider selections require -fplugin");
    return Error::success();
  }

  SmallVector<StringRef, 64> StaticSpellings;
  StaticSpellings.reserve(StaticOptionSpellings.size());
  for (const std::string &Spelling : StaticOptionSpellings)
    StaticSpellings.push_back(Spelling);
  Services = std::make_unique<plugin::PluginProcessServices>(
      HostBuildID, LLVMMajor, StaticSpellings);
  if (InitializeInterfaces)
    if (Error E = InitializeInterfaces(*Services)) {
      Services.reset();
      return E;
    }
  if (Error E = Services->interfaces().freeze()) {
    Services.reset();
    return E;
  }

  for (StringRef Path : Paths) {
    auto Module = Services->registry().load(Path);
    if (!Module) {
      Services.reset();
      return Module.takeError();
    }
    StringRef PluginID = (*Module)->descriptor().PluginID;
    if (!llvm::is_contained(LoadedPluginIDs, PluginID))
      LoadedPluginIDs.push_back(PluginID.str());
  }

  SmallVector<StringRef, 4> SelectedIDs;
  SelectedIDs.reserve(LoadedPluginIDs.size());
  for (const std::string &PluginID : LoadedPluginIDs)
    SelectedIDs.push_back(PluginID);
  auto Activation = plugin::makePluginActivationPlan(
      Services->registry(), SelectedIDs);
  if (!Activation) {
    Services.reset();
    return Activation.takeError();
  }
  if (Error E = plugin::activatePluginPlan(*Services, *Activation)) {
    Services.reset();
    return E;
  }
  if (Error E = Services->options().freeze()) {
    Services.reset();
    return E;
  }
  Plan = std::make_unique<plugin::PluginActivationPlan>(
      std::move(*Activation));
  Activated = true;
  return Error::success();
}

Expected<plugin::PluginOptionParseResult>
PluginBootstrap::parsePluginOptions(ArrayRef<StringRef> Arguments,
                                    StringRef TargetTriple) const {
  if (!Activated || !Services)
    return bootstrapError(
        "cannot parse plugin options before plugin activation");
  return Services->options().parse(Arguments, TargetTriple);
}

Expected<std::unique_ptr<plugin::PluginSession>>
PluginBootstrap::createSession(
    plugin::PluginOptionParseResult Options) {
  if (!Activated || !Services || !Plan)
    return bootstrapError(
        "cannot create a plugin session before activation");
  return plugin::PluginSession::create(*Services, *Plan,
                                       std::move(Options));
}

Error PluginBootstrap::shutdown() {
  Plan.reset();
  Error Cleanup = Error::success();
  if (Services)
    Cleanup = Services->shutdown();
  Services.reset();
  LoadedPluginIDs.clear();
  Activated = false;
  return Cleanup;
}

} // namespace neverc::driver
