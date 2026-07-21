#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/TargetParser/Triple.h"
#include <array>
#include <cstdint>
#include <string>

using namespace llvm;

namespace neverc::plugin {
namespace {

constexpr uint64_t BuiltinTargetIDHigh = UINT64_C(0x4e43544255494c54);
constexpr uint64_t BuiltinABIIDHigh = UINT64_C(0x4e43414249425549);
constexpr uint64_t BuiltinMCSchemaIDHigh = UINT64_C(0x4e434d435343484d);
constexpr uint64_t BuiltinObjectFormatIDHigh = UINT64_C(0x4e434f424a464d54);

constexpr NevercTargetID targetID(uint64_t Route) {
  return {BuiltinTargetIDHigh, Route};
}

constexpr NevercTargetABIID abiID(uint64_t Route) {
  return {BuiltinABIIDHigh, Route};
}

constexpr NevercInterfaceID mcSchemaID(bool IsAArch64) {
  return {BuiltinMCSchemaIDHigh, IsAArch64 ? UINT64_C(2) : UINT64_C(1)};
}

constexpr NevercInterfaceID objectFormatID(BuiltinObjectFormat Format) {
  return {
      BuiltinObjectFormatIDHigh,
      Format == BuiltinObjectFormat::ELF
          ? UINT64_C(1)
          : (Format == BuiltinObjectFormat::COFF ? UINT64_C(2) : UINT64_C(3))};
}

const std::array<BuiltinTargetRoute, 9> Routes = {{
    {"builtin.x86_64.macos", "x86_64-apple-macosx", "generic", targetID(1),
     abiID(1), mcSchemaID(false), objectFormatID(BuiltinObjectFormat::MachO),
     BuiltinTargetABIKind::X86_64SysV, BuiltinObjectFormat::MachO, true, true,
     true, false},
    {"builtin.aarch64.macos", "aarch64-apple-macosx", "generic", targetID(2),
     abiID(2), mcSchemaID(true), objectFormatID(BuiltinObjectFormat::MachO),
     BuiltinTargetABIKind::AArch64DarwinPCS, BuiltinObjectFormat::MachO, true,
     true, true, false},
    {"builtin.x86_64.linux", "x86_64-unknown-linux-gnu", "generic", targetID(3),
     abiID(3), mcSchemaID(false), objectFormatID(BuiltinObjectFormat::ELF),
     BuiltinTargetABIKind::X86_64SysV, BuiltinObjectFormat::ELF, true, true,
     true, false},
    {"builtin.aarch64.linux", "aarch64-unknown-linux-gnu", "generic",
     targetID(4), abiID(4), mcSchemaID(true),
     objectFormatID(BuiltinObjectFormat::ELF),
     BuiltinTargetABIKind::AArch64AAPCS, BuiltinObjectFormat::ELF, true, true,
     true, false},
    {"builtin.x86_64.android", "x86_64-unknown-linux-android29", "generic",
     targetID(5), abiID(5), mcSchemaID(false),
     objectFormatID(BuiltinObjectFormat::ELF), BuiltinTargetABIKind::X86_64SysV,
     BuiltinObjectFormat::ELF, true, true, true, false},
    {"builtin.aarch64.android", "aarch64-unknown-linux-android29", "generic",
     targetID(6), abiID(6), mcSchemaID(true),
     objectFormatID(BuiltinObjectFormat::ELF),
     BuiltinTargetABIKind::AArch64AAPCS, BuiltinObjectFormat::ELF, true, true,
     true, false},
    {"builtin.x86_64.windows", "x86_64-pc-windows-msvc", "generic", targetID(7),
     abiID(7), mcSchemaID(false), objectFormatID(BuiltinObjectFormat::COFF),
     BuiltinTargetABIKind::X86_64Win64, BuiltinObjectFormat::COFF, true, true,
     true, false},
    {"builtin.aarch64.windows", "aarch64-pc-windows-msvc", "generic",
     targetID(8), abiID(8), mcSchemaID(true),
     objectFormatID(BuiltinObjectFormat::COFF),
     BuiltinTargetABIKind::AArch64Win64, BuiltinObjectFormat::COFF, true, true,
     true, false},
    {"builtin.aarch64.ios", "aarch64-apple-ios", "generic", targetID(9),
     abiID(9), mcSchemaID(true), objectFormatID(BuiltinObjectFormat::MachO),
     BuiltinTargetABIKind::AArch64DarwinPCS, BuiltinObjectFormat::MachO, true,
     true, true, false},
}};

} // namespace

ArrayRef<BuiltinTargetRoute> builtinTargetRoutes() { return Routes; }

const BuiltinTargetRoute *findBuiltinTargetRoute(StringRef TripleText) {
  const Triple Parsed(Triple::normalize(TripleText));
  const bool IsX86 = Parsed.getArch() == Triple::x86_64;
  const bool IsAArch64 = Parsed.getArch() == Triple::aarch64;
  if (!IsX86 && !IsAArch64)
    return nullptr;

  if (Parsed.isMacOSX())
    return &Routes[IsAArch64 ? 1 : 0];
  if (Parsed.isiOS())
    return IsAArch64 ? &Routes[8] : nullptr;
  if (Parsed.getOS() == Triple::Linux) {
    if (Parsed.isAndroid())
      return &Routes[IsAArch64 ? 5 : 4];
    return &Routes[IsAArch64 ? 3 : 2];
  }
  if (Parsed.getOS() == Triple::Win32 &&
      Parsed.getEnvironment() == Triple::MSVC)
    return &Routes[IsAArch64 ? 7 : 6];
  return nullptr;
}

Expected<OwnedTargetKey>
createBuiltinTargetKey(const BuiltinTargetRoute &Route, StringRef TripleText,
                       StringRef CPU,
                       NevercTargetRelocationModel RelocationModel,
                       NevercTargetCodeModel CodeModel,
                       NevercTargetExecutionLevel ExecutionLevel) {
  const BuiltinTargetRoute *Selected = findBuiltinTargetRoute(TripleText);
  if (!Selected || Selected->TargetID.High != Route.TargetID.High ||
      Selected->TargetID.Low != Route.TargetID.Low)
    return createStringError(inconvertibleErrorCode(),
                             "built-in target route does not match triple '" +
                                 TripleText + "'");

  const Triple Parsed(Triple::normalize(TripleText));
  const std::string SelectedCPU =
      CPU.empty() ? Route.DefaultCPU.str() : CPU.str();
  return TargetKeyBuilder()
      .setTargetID(Route.TargetID)
      .setTriple(TripleText.str(), Parsed.getArchName().str(),
                 Parsed.getVendorName().str(), Parsed.getOSName().str(),
                 Parsed.getEnvironmentName().str())
      .setCPU(SelectedCPU, SelectedCPU)
      .setFeatures({})
      .setABI(Route.ABIID)
      .setCallingConvention({UINT64_C(0x4e43504243430001), Route.TargetID.Low})
      .setObjectFormat(Route.ObjectFormatID)
      .setCodeGeneration(RelocationModel, CodeModel)
      .setExecution(ExecutionLevel, 64, NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(std::string(64, '0'))
      .build();
}

} // namespace neverc::plugin
