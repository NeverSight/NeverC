//===- OSDefines.cpp -- platform predefined macros (Darwin / Windows) -----===//
//
// Cross-target OS predefined-macro emitters, shared by every per-arch
// translation unit (`TargetsAArch64.cpp`, `TargetsX86.cpp`, ...).
//
// Public entry points (declared in `Targets/OSTargets.h`):
//   * `getDarwinDefines`   used by `DarwinTargetInfo<T>::getOSDefines`.
//   * `addWindowsDefines`  used by `WindowsTargetInfo<T>::getOSDefines`.
//
// Internal helper (`addVisualCDefines`) lives in an anonymous namespace;
// it is only ever reached through `addWindowsDefines`.
//
// Split out of the historical `TargetsX86AndOS.cpp`.
//

#include "OSTargets.h"
#include "Targets.h"
#include "neverc/Foundation/Core/MacroBuilder.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>

using namespace neverc;
using namespace neverc::targets;

namespace neverc {
namespace targets {

// ===----------------------------------------------------------------------===
// Darwin / Apple platform defines
// ===----------------------------------------------------------------------===

void getDarwinDefines(MacroBuilder &Builder, const LangOptions &Opts,
                      const llvm::Triple &Triple, llvm::StringRef &PlatformName,
                      llvm::VersionTuple &PlatformMinVersion) {
  Builder.defineMacro("__APPLE_CC__", "6000");
  Builder.defineMacro("__APPLE__");
  Builder.defineMacro("__STDC_NO_THREADS__");

  if (Opts.Static)
    Builder.defineMacro("__STATIC__");
  else
    Builder.defineMacro("__DYNAMIC__");

  if (Opts.POSIXThreads)
    Builder.defineMacro("_REENTRANT");

  llvm::VersionTuple OsVersion;
  if (Triple.isMacOSX()) {
    Triple.getMacOSXVersion(OsVersion);
    PlatformName = "macos";
  } else {
    OsVersion = Triple.getOSVersion();
    PlatformName = llvm::Triple::getOSTypeName(Triple.getOS());
  }

  // If -target arch-pc-win32-macho option specified, we're
  // generating code for Win32 ABI. No need to emit
  // __ENVIRONMENT_XX_OS_VERSION_MIN_REQUIRED__.
  if (PlatformName == "win32") {
    PlatformMinVersion = OsVersion;
    return;
  }

  assert(OsVersion < llvm::VersionTuple(100) && "Invalid version!");
  char Str[7];
  unsigned Maj = OsVersion.getMajor();
  unsigned Min = OsVersion.getMinor();
  unsigned Sub = OsVersion.getSubminor();
  bool IsMacOSX = Triple.isMacOSX();
  bool ShortForm = IsMacOSX && OsVersion < llvm::VersionTuple(10, 10);
  bool SingleDigitMajor = !IsMacOSX && Maj < 10;

  unsigned Pos = 0;
  if (ShortForm) {
    Str[Pos++] = '0' + (Maj / 10);
    Str[Pos++] = '0' + (Maj % 10);
    Str[Pos++] = '0' + std::min(Min, 9U);
    Str[Pos++] = '0' + std::min(Sub, 9U);
  } else if (SingleDigitMajor) {
    Str[Pos++] = '0' + Maj;
    Str[Pos++] = '0' + (Min / 10);
    Str[Pos++] = '0' + (Min % 10);
    Str[Pos++] = '0' + (Sub / 10);
    Str[Pos++] = '0' + (Sub % 10);
  } else {
    Str[Pos++] = '0' + (Maj / 10);
    Str[Pos++] = '0' + (Maj % 10);
    Str[Pos++] = '0' + (Min / 10);
    Str[Pos++] = '0' + (Min % 10);
    Str[Pos++] = '0' + (Sub / 10);
    Str[Pos++] = '0' + (Sub % 10);
  }
  Str[Pos] = '\0';

  // Set the appropriate OS version define. NeverC supports macOS and iOS
  // (the latter only for syntax-only cross-compilation).
  if (Triple.isiOS())
    Builder.defineMacro("__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__", Str);
  else if (Triple.isMacOSX())
    Builder.defineMacro("__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__", Str);

  if (Triple.isOSDarwin()) {
    // Any darwin OS defines a general darwin OS version macro in addition
    // to the other OS specific macros.
    assert(OsVersion.getMinor() < 100 && OsVersion.getSubminor() < 100 &&
           "Invalid version!");
    Builder.defineMacro("__ENVIRONMENT_OS_VERSION_MIN_REQUIRED__", Str);

    // Tell users about the kernel if there is one.
    Builder.defineMacro("__MACH__");
  }

  PlatformMinVersion = OsVersion;
}

// ===----------------------------------------------------------------------===
// Windows-family defines (MSVC)
// ===----------------------------------------------------------------------===

namespace {
void addVisualCDefines(const LangOptions &Opts, MacroBuilder &Builder) {
  struct ConditionalMacro {
    bool Condition;
    const char *Name;
    const char *Value;
  };
  const ConditionalMacro SimpleMacros[] = {
      {static_cast<bool>(Opts.Bool), "__BOOL_DEFINED", nullptr},
      {!Opts.CharIsSigned, "_CHAR_UNSIGNED", nullptr},
      {Opts.getDefaultFPContractMode() != LangOptions::FPModeKind::FPM_Off,
       "_M_FP_CONTRACT", nullptr},
      {Opts.getDefaultExceptionMode() ==
           LangOptions::FPExceptionModeKind::FPE_Strict,
       "_M_FP_EXCEPT", nullptr},
      {static_cast<bool>(Opts.POSIXThreads), "_MT", nullptr},
      {static_cast<bool>(Opts.MicrosoftExt), "_MSC_EXTENSIONS", nullptr},
      {!Opts.MSVolatile, "_ISO_VOLATILE", nullptr},
      {static_cast<bool>(Opts.Kernel), "_KERNEL_MODE", nullptr},
  };
  for (const auto &M : SimpleMacros) {
    if (M.Condition)
      Builder.defineMacro(M.Name, M.Value ? M.Value : "");
  }

  const bool AnyImprecise =
      Opts.FastMath | Opts.FiniteMathOnly | Opts.UnsafeFPMath |
      Opts.AllowFPReassoc | Opts.NoHonorNaNs | Opts.NoHonorInfs |
      Opts.NoSignedZero | Opts.AllowRecip | Opts.ApproxFunc;

  auto RM = Opts.getDefaultRoundingMode();
  if (RM == LangOptions::RoundingMode::NearestTiesToEven)
    Builder.defineMacro(AnyImprecise ? "_M_FP_FAST" : "_M_FP_PRECISE");
  else if (!AnyImprecise && RM == LangOptions::RoundingMode::Dynamic)
    Builder.defineMacro("_M_FP_STRICT");

  if (Opts.MSCompatibilityVersion) {
    Builder.defineMacro("_MSC_VER",
                        llvm::Twine(Opts.MSCompatibilityVersion / 100000));
    Builder.defineMacro("_MSC_FULL_VER",
                        llvm::Twine(Opts.MSCompatibilityVersion));
    Builder.defineMacro("_MSC_BUILD", llvm::Twine(1));
  }

  Builder.defineMacro("_INTEGRAL_MAX_BITS", "64");
  Builder.defineMacro("__STDC_NO_THREADS__");
  Builder.defineMacro("_MSVC_EXECUTION_CHARACTER_SET", "65001");
}
} // namespace

void addWindowsDefines(const llvm::Triple &Triple, const LangOptions &Opts,
                       MacroBuilder &Builder) {
  Builder.defineMacro("_WIN32");
  if (Triple.isArch64Bit())
    Builder.defineMacro("_WIN64");
  if (Triple.isKnownWindowsMSVCEnvironment())
    addVisualCDefines(Opts, Builder);
}

} // namespace targets
} // namespace neverc
