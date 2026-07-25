#include "DynCodeRegistry.h"

#include "neverc/DynCode/Pipeline/TargetDesc.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "llvm/TargetParser/Triple.h"

#include <cstring>

namespace neverc {
namespace dyncode {

namespace {

// Stable interface-ID name spaces for the built-in dyncode adapters. The low
// word reuses the built-in target route number so a dyncode target ID is 1:1
// with its underlying TargetKey.
constexpr uint64_t DynCodeTargetIDHigh = UINT64_C(0x4e43444354475401);   // NCDCTGT
constexpr uint64_t RelocApplicatorHigh = UINT64_C(0x4e43444352454c01);   // NCDCREL
constexpr uint64_t UserImportHigh = UINT64_C(0x4e434443554d5501);        // NCDCUMU
constexpr uint64_t KernelImportHigh = UINT64_C(0x4e434443554d4b01);      // NCDCUMK
constexpr uint64_t EntryABIHigh = UINT64_C(0x4e434443454e5401);          // NCDCENT

NevercStringView sv(const std::string &S) {
  return NevercStringView{S.data(), static_cast<uint64_t>(S.size())};
}

NevercDynCodeExecutionLevel toDynCodeLevel(ExecutionLevel Level) {
  return Level == ExecutionLevel::Kernel ? NEVERC_DYNCODE_LEVEL_KERNEL
                                         : NEVERC_DYNCODE_LEVEL_USER;
}

} // namespace

NevercDynCodeTargetDescriptor OwnedDynCodeTargetDescriptor::view() const {
  NevercDynCodeTargetDescriptor D;
  std::memset(&D, 0, sizeof(D));
  D.Header = {static_cast<uint32_t>(sizeof(NevercDynCodeTargetDescriptor)),
              NEVERC_DYNCODE_API_MAJOR, NEVERC_DYNCODE_API_MINOR, 0};
  D.DynCodeTargetID = DynCodeTargetID;
  D.Target = Target.view();
  D.TargetSchemaDigest = D.Target.SchemaDigest;
  D.ObjectFormat = ObjectFormat;
  D.CodeSectionRole = sv(CodeSectionRole);
  D.CodeSectionName = sv(CodeSectionName);
  D.DefaultFragmentAlignment = DefaultFragmentAlignment;
  D.Flags = Flags;
  D.RelocationApplicatorID = RelocationApplicatorID;
  D.UserImportStrategyID = UserImportStrategyID;
  D.KernelImportStrategyID = KernelImportStrategyID;
  D.EntryABIID = EntryABIID;
  D.PICConstraints = PICConstraints;
  return D;
}

llvm::Expected<OwnedDynCodeTargetDescriptor>
buildBuiltinDynCodeTarget(llvm::StringRef Triple,
                          NevercDynCodeExecutionLevel Level) {
  const plugin::BuiltinTargetRoute *Route =
      plugin::findBuiltinTargetRoute(Triple);
  if (!Route)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "-fdyncode does not support triple '" + Triple + "'");

  const NevercTargetExecutionLevel KeyLevel =
      Level == NEVERC_DYNCODE_LEVEL_KERNEL ? NEVERC_TARGET_EXECUTION_KERNEL
                                           : NEVERC_TARGET_EXECUTION_USER;
  llvm::Expected<plugin::OwnedTargetKey> KeyOr = plugin::createBuiltinTargetKey(
      *Route, Triple, /*CPU=*/"", NEVERC_TARGET_RELOCATION_PIC,
      NEVERC_TARGET_CODE_MODEL_SMALL, KeyLevel);
  if (!KeyOr)
    return KeyOr.takeError();

  const ExecutionLevel DescLevel = Level == NEVERC_DYNCODE_LEVEL_KERNEL
                                       ? ExecutionLevel::Kernel
                                       : ExecutionLevel::User;
  const llvm::Triple Parsed(llvm::Triple::normalize(Triple));
  const TargetDesc Desc = describeTriple(Parsed, DescLevel);

  OwnedDynCodeTargetDescriptor Out;
  Out.Target = std::move(*KeyOr);
  Out.UnderlyingTargetID = Route->TargetID;
  Out.DynCodeTargetID = {DynCodeTargetIDHigh, Route->TargetID.Low};
  Out.ObjectFormat = Route->ObjectFormatID;
  Out.CodeSectionRole = "text";
  Out.CodeSectionName = Desc.TextSectionName.str();
  Out.DefaultFragmentAlignment = 16;
  Out.Flags =
      NEVERC_DYNCODE_TARGET_SUPPORTS_USER | NEVERC_DYNCODE_TARGET_SUPPORTS_KERNEL;
  Out.RelocationApplicatorID = {RelocApplicatorHigh, Route->TargetID.Low};
  Out.UserImportStrategyID = {UserImportHigh, Route->TargetID.Low};
  Out.KernelImportStrategyID = {KernelImportHigh, Route->TargetID.Low};
  Out.EntryABIID = {EntryABIHigh, Route->TargetID.Low};
  Out.PICConstraints = NEVERC_DYNCODE_PIC_ALLOW_PC_RELATIVE |
                       NEVERC_DYNCODE_PIC_REQUIRE_ENTRY_AT_ZERO;
  (void)toDynCodeLevel; // reserved for future descriptor level filtering
  return Out;
}

} // namespace dyncode
} // namespace neverc
