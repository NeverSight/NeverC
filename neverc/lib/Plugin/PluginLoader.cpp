#include "neverc/Plugin/PluginLoader.h"
#include "BridgeCastHelpers.h"
#include "HostAPIBridge.h"
#include "csupport/ldynamic_llibrary.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "neverc-plugin"

using namespace llvm;

namespace neverc {
namespace plugin {

// ===----------------------------------------------------------------------===
//  Registration callbacks -- called by plugins during RegisterPasses
// ===----------------------------------------------------------------------===

namespace {

struct RegistrarContext {
  DenseMap<unsigned, SmallVector<RegisteredModulePass, 4>> *ModulePasses;
  DenseMap<unsigned, SmallVector<RegisteredMachinePass, 4>> *MachinePasses;
  DenseMap<unsigned, SmallVector<RegisteredBinaryPass, 4>> *BinaryPasses;
  DenseMap<unsigned, SmallVector<RegisteredLinkerPass, 4>> *LinkerPasses;
  std::string PluginPath;
};

bool isIRHookPoint(NevercHookPoint Hook) {
  switch (Hook) {
  case NEVERC_HOOK_PRE_OPT:
  case NEVERC_HOOK_POST_OPT:
  case NEVERC_HOOK_PIPELINE_START:
  case NEVERC_HOOK_PIPELINE_LAST:
  case NEVERC_HOOK_SC_BEFORE_PREP:
  case NEVERC_HOOK_SC_AFTER_PREP:
  case NEVERC_HOOK_SC_BEFORE_INLINING:
  case NEVERC_HOOK_SC_AFTER_INLINING:
  case NEVERC_HOOK_SC_AFTER_STACKIFY:
  case NEVERC_HOOK_SC_AFTER_FINAL_IR:
  case NEVERC_HOOK_LTO_PRE_OPT:
  case NEVERC_HOOK_LTO_POST_OPT:
    return true;
  default:
    return false;
  }
}

bool isMIRHookPoint(NevercHookPoint Hook) {
  switch (Hook) {
  case NEVERC_HOOK_BEFORE_CODEGEN_PREEMIT:
  case NEVERC_HOOK_AFTER_CODEGEN_FINAL_MIR:
  case NEVERC_HOOK_SC_BEFORE_PREEMIT:
  case NEVERC_HOOK_SC_AFTER_PREEMIT:
  case NEVERC_HOOK_SC_AFTER_FINAL_MIR:
    return true;
  default:
    return false;
  }
}

bool isBinaryHookPoint(NevercHookPoint Hook) {
  switch (Hook) {
  case NEVERC_HOOK_SC_POST_EXTRACT:
  case NEVERC_HOOK_SC_POST_FINALIZE:
    return true;
  default:
    return false;
  }
}

bool isLinkerHookPoint(NevercHookPoint Hook) {
  switch (Hook) {
  case NEVERC_HOOK_LINK_PRE_LAYOUT:
  case NEVERC_HOOK_LINK_POST_LAYOUT:
  case NEVERC_HOOK_LINK_POST_EMIT:
    return true;
  default:
    return false;
  }
}

void warnHookMismatch(const char *PassKind, NevercHookPoint Hook,
                      const char *PassName) {
  unsigned HookVal = static_cast<unsigned>(Hook);
  WithColor::warning(errs(), "neverc-plugin")
      << "pass '" << (PassName ? PassName : "<unnamed>") << "' registered "
      << PassKind << " pass at incompatible hook point 0x"
      << Twine::utohexstr(HookVal) << "; it will never run\n";
}

void registrarRegisterModulePass(void *Reg, NevercHookPoint Hook,
                                 NevercModulePassFn Fn, void *UserData,
                                 const char *PassName) {
  auto *Ctx = static_cast<RegistrarContext *>(Reg);
  if (!isIRHookPoint(Hook)) {
    warnHookMismatch("IR module", Hook, PassName);
    return;
  }
  (*Ctx->ModulePasses)[static_cast<unsigned>(Hook)].push_back(
      {Hook, Fn, UserData, nameStr(PassName), Ctx->PluginPath});
}

void registrarRegisterMachinePass(void *Reg, NevercHookPoint Hook,
                                  NevercMachinePassFn Fn, void *UserData,
                                  const char *PassName) {
  auto *Ctx = static_cast<RegistrarContext *>(Reg);
  if (!isMIRHookPoint(Hook)) {
    warnHookMismatch("MIR machine", Hook, PassName);
    return;
  }
  (*Ctx->MachinePasses)[static_cast<unsigned>(Hook)].push_back(
      {Hook, Fn, UserData, nameStr(PassName), Ctx->PluginPath});
}

void registrarRegisterBinaryPass(void *Reg, NevercHookPoint Hook,
                                 NevercBinaryPassFn Fn, void *UserData,
                                 const char *PassName) {
  auto *Ctx = static_cast<RegistrarContext *>(Reg);
  if (!isBinaryHookPoint(Hook)) {
    warnHookMismatch("binary", Hook, PassName);
    return;
  }
  (*Ctx->BinaryPasses)[static_cast<unsigned>(Hook)].push_back(
      {Hook, Fn, UserData, nameStr(PassName), Ctx->PluginPath});
}

void registrarRegisterLinkerPass(void *Reg, NevercHookPoint Hook,
                                 NevercLinkerPassFn Fn, void *UserData,
                                 const char *PassName) {
  auto *Ctx = static_cast<RegistrarContext *>(Reg);
  if (!isLinkerHookPoint(Hook)) {
    warnHookMismatch("linker", Hook, PassName);
    return;
  }
  (*Ctx->LinkerPasses)[static_cast<unsigned>(Hook)].push_back(
      {Hook, Fn, UserData, nameStr(PassName), Ctx->PluginPath});
}

} // namespace

// ===----------------------------------------------------------------------===
//  PluginLoader
// ===----------------------------------------------------------------------===

void PluginLoader::ensureHostAPIBuilt() {
  if (HostAPIReady)
    return;
  HostAPI = buildHostAPI();
  HostAPIReady = true;
}

PluginLoader::~PluginLoader() {
  ModulePasses.clear();
  MachinePasses.clear();
  BinaryPasses.clear();
  LinkerPasses.clear();
  for (auto It = Plugins.rbegin(); It != Plugins.rend(); ++It) {
    if (It->Info.Destroy)
      It->Info.Destroy();
    if (It->Handle)
      csupport_dlclose(It->Handle);
  }
}

bool PluginLoader::loadPlugin(StringRef Path, std::string &ErrMsg) {
  for (const auto &P : Plugins) {
    if (P.Path == Path) {
      LLVM_DEBUG(dbgs() << "neverc: plugin '" << Path
                        << "' already loaded; skipping\n");
      return true;
    }
  }

  char ErrBuf[512];
  void *Handle =
      csupport_dlopen_local(Path.str().c_str(), ErrBuf, sizeof(ErrBuf));
  if (!Handle) {
    ErrMsg =
        ("failed to load plugin '" + Path + "': " + ErrBuf).str();
    return false;
  }

  using GetInfoFn = NevercPluginInfo (*)();
  auto *GetInfo = reinterpret_cast<GetInfoFn>(
      csupport_dlsym(Handle, NEVERC_PLUGIN_ENTRY_POINT));
  if (!GetInfo) {
    ErrMsg = ("plugin '" + Path +
              "' does not export '" NEVERC_PLUGIN_ENTRY_POINT "'")
                 .str();
    csupport_dlclose(Handle);
    return false;
  }

  NevercPluginInfo Info = GetInfo();

  LLVM_DEBUG(dbgs() << "neverc: plugin '" << Path << "' reports API v"
                    << Info.APIVersion << ", name='"
                    << (Info.PluginName ? Info.PluginName : "<null>")
                    << "', version='"
                    << (Info.PluginVersion ? Info.PluginVersion : "<null>")
                    << "'\n");

  if (Info.APIVersion == 0 || Info.APIVersion > NEVERC_PLUGIN_API_VERSION) {
    ErrMsg = ("plugin '" + Path + "' has API version " +
              Twine(Info.APIVersion) + " (host supports 1.." +
              Twine(NEVERC_PLUGIN_API_VERSION) + ")")
                 .str();
    csupport_dlclose(Handle);
    return false;
  }

  if (!Info.RegisterPasses) {
    ErrMsg = ("plugin '" + Path + "' has NULL RegisterPasses").str();
    csupport_dlclose(Handle);
    return false;
  }

  ensureHostAPIBuilt();

  NevercHostAPI RegAPI = HostAPI;
  RegAPI.RegisterModulePass = registrarRegisterModulePass;
  RegAPI.RegisterMachinePass = registrarRegisterMachinePass;
  RegAPI.RegisterBinaryPass = registrarRegisterBinaryPass;
  RegAPI.RegisterLinkerPass = registrarRegisterLinkerPass;

  RegistrarContext Ctx;
  Ctx.ModulePasses = &ModulePasses;
  Ctx.MachinePasses = &MachinePasses;
  Ctx.BinaryPasses = &BinaryPasses;
  Ctx.LinkerPasses = &LinkerPasses;
  Ctx.PluginPath = Path.str();

  size_t ModBefore = 0, MachBefore = 0, BinBefore = 0, LinkBefore = 0;
  for (const auto &E : ModulePasses) ModBefore += E.second.size();
  for (const auto &E : MachinePasses) MachBefore += E.second.size();
  for (const auto &E : BinaryPasses) BinBefore += E.second.size();
  for (const auto &E : LinkerPasses) LinkBefore += E.second.size();

  Info.RegisterPasses(&RegAPI, &Ctx);

  Plugins.push_back({Handle, Info, Path.str()});

  {
    size_t ModAfter = 0, MachAfter = 0, BinAfter = 0, LinkAfter = 0;
    for (const auto &E : ModulePasses) ModAfter += E.second.size();
    for (const auto &E : MachinePasses) MachAfter += E.second.size();
    for (const auto &E : BinaryPasses) BinAfter += E.second.size();
    for (const auto &E : LinkerPasses) LinkAfter += E.second.size();
    LLVM_DEBUG(dbgs() << "neverc: plugin registered "
                      << (ModAfter - ModBefore) << " module, "
                      << (MachAfter - MachBefore) << " machine, "
                      << (BinAfter - BinBefore) << " binary, "
                      << (LinkAfter - LinkBefore) << " linker passes\n");
  }

  LLVM_DEBUG(dbgs() << "neverc: loaded plugin '"
                    << (Info.PluginName ? Info.PluginName : Path.str().c_str())
                    << "' v" << (Info.PluginVersion ? Info.PluginVersion : "?")
                    << " (API v" << Info.APIVersion << ")\n");

  return true;
}

SmallVector<const RegisteredModulePass *, 4>
PluginLoader::getModulePasses(NevercHookPoint Hook) const {
  SmallVector<const RegisteredModulePass *, 4> Result;
  auto It = ModulePasses.find(static_cast<unsigned>(Hook));
  if (It != ModulePasses.end())
    for (const auto &P : It->second)
      Result.push_back(&P);
  return Result;
}

SmallVector<const RegisteredMachinePass *, 4>
PluginLoader::getMachinePasses(NevercHookPoint Hook) const {
  SmallVector<const RegisteredMachinePass *, 4> Result;
  auto It = MachinePasses.find(static_cast<unsigned>(Hook));
  if (It != MachinePasses.end())
    for (const auto &P : It->second)
      Result.push_back(&P);
  return Result;
}

SmallVector<const RegisteredBinaryPass *, 4>
PluginLoader::getBinaryPasses(NevercHookPoint Hook) const {
  SmallVector<const RegisteredBinaryPass *, 4> Result;
  auto It = BinaryPasses.find(static_cast<unsigned>(Hook));
  if (It != BinaryPasses.end())
    for (const auto &P : It->second)
      Result.push_back(&P);
  return Result;
}

SmallVector<const RegisteredLinkerPass *, 4>
PluginLoader::getLinkerPasses(NevercHookPoint Hook) const {
  SmallVector<const RegisteredLinkerPass *, 4> Result;
  auto It = LinkerPasses.find(static_cast<unsigned>(Hook));
  if (It != LinkerPasses.end())
    for (const auto &P : It->second)
      Result.push_back(&P);
  return Result;
}

PluginLoader &getGlobalPluginLoader() {
  static PluginLoader Instance;
  return Instance;
}

// ===----------------------------------------------------------------------===
//  Linker backend accessor table + linker pass execution
// ===----------------------------------------------------------------------===

// Installed by a linker backend (ELF/COFF/MachO) for the duration of its
// link-pass invocation.  Single-threaded by construction: links run
// sequentially and the writer phase that fires linker hooks is not reentrant,
// so a plain pointer is sufficient (and avoids any TLS overhead).
static const NevercLinkerBackend *CurrentLinkerBackend = nullptr;

void setLinkerBackend(const NevercLinkerBackend *Backend) {
  CurrentLinkerBackend = Backend;
}

const NevercLinkerBackend *getLinkerBackend() { return CurrentLinkerBackend; }

void clearLinkerBackend() { CurrentLinkerBackend = nullptr; }

void runLinkerPasses(NevercHookPoint Hook, PluginLoader &Loader) {
  if (!Loader.hasPlugins())
    return;

  const NevercHostAPI &API = Loader.getHostAPI();
  for (const RegisteredLinkerPass *P : Loader.getLinkerPasses(Hook)) {
    if (!P->Fn)
      continue;
    SmallString<128> StackMsg("Plugin linker pass '");
    StackMsg += P->PassName;
    StackMsg += '\'';
    if (!P->PluginPath.empty()) {
      StackMsg += " from ";
      StackMsg += P->PluginPath;
    }
    PrettyStackTraceString CrashInfo(StackMsg.c_str());
    LLVM_DEBUG(dbgs() << "neverc: running plugin linker pass '" << P->PassName
                      << "'\n");
    P->Fn(&API, P->UserData);
  }
}

} // namespace plugin
} // namespace neverc
