#include "neverc/Plugin/Host/PluginTargetInfo.h"
#include "neverc/Foundation/Builtin/Builtins.h"
#include "neverc/Foundation/Core/MacroBuilder.h"
#include "neverc/Foundation/Diagnostic/DiagnosticFrontend.h"
#include "llvm/IR/DataLayout.h"
#include <algorithm>

using namespace llvm;

namespace neverc::plugin {
namespace {

std::string selectedTriple(
    const PluginTargetSnapshot::TargetRecord &Record,
    StringRef Requested) {
  const std::string Normalized =
      Requested.trim().lower().str().str();
  if (Normalized.empty() ||
      StringRef(Record.CanonicalName).lower() == Normalized ||
      std::find(Record.Aliases.begin(), Record.Aliases.end(),
                Normalized) != Record.Aliases.end())
    return Record.Machine.RawTriple;
  return Requested.str();
}

TargetInfo::GCCRegAlias makeRegisterAlias(
    const VerifiedTargetRegister &Register) {
  const auto Alias = [&](size_t Index) -> const char * {
    return Index < Register.Aliases.size()
               ? Register.Aliases[Index].c_str()
               : nullptr;
  };
  return {{Alias(0), Alias(1), Alias(2), Alias(3), nullptr},
          Register.Name.c_str()};
}

TargetInfo::AddlRegName makeAdditionalRegisterName(
    const VerifiedTargetRegister &Register) {
  const auto Name = [&](size_t Index) -> const char * {
    return Index < Register.AdditionalNames.size()
               ? Register.AdditionalNames[Index].c_str()
               : nullptr;
  };
  return {{Name(0), Name(1), Name(2), Name(3), Name(4)},
          Register.RegisterNumber};
}

} // namespace

PluginTargetInfo::PluginTargetInfo(
    const PluginTargetSnapshot::TargetRecord &RecordValue,
    StringRef RequestedTriple,
    const PluginTargetSnapshot::NamedRecord *ABIValue,
    const PluginTargetSnapshot::NamedRecord *CallingConventionValue,
    PluginTaskContext *TaskValue)
    : TargetInfo(
          llvm::Triple(selectedTriple(RecordValue, RequestedTriple))),
      Record(RecordValue),
      ABI(ABIValue ? std::optional(*ABIValue) : std::nullopt),
      CallingConvention(
          CallingConventionValue
              ? std::optional(*CallingConventionValue)
              : std::nullopt),
      Task(TaskValue), CPU(Record.Machine.DefaultCPU),
      MaximumPointerWidth(Record.Machine.PointerWidth) {
  const VerifiedTargetMachineDescriptor &Machine = Record.Machine;
  const DataLayout Layout(Machine.DataLayout);
  const auto AlignmentFor = [&](unsigned Width) {
    return static_cast<unsigned char>(
        Layout.getABIIntegerTypeAlignment(Width).value() * 8);
  };
  const auto SignedTypeForWidth = [&](unsigned Width) {
    if (Machine.IntWidth == Width)
      return SignedInt;
    if (Machine.LongWidth == Width)
      return SignedLong;
    if (Machine.LongLongWidth == Width)
      return SignedLongLong;
    return NoInt;
  };
  const auto UnsignedTypeForWidth = [&](unsigned Width) {
    if (Machine.IntWidth == Width)
      return UnsignedInt;
    if (Machine.LongWidth == Width)
      return UnsignedLong;
    if (Machine.LongLongWidth == Width)
      return UnsignedLongLong;
    return NoInt;
  };

  PointerWidth = static_cast<unsigned char>(Machine.PointerWidth);
  PointerAlign = static_cast<unsigned char>(
      Layout.getPointerABIAlignment(0).value() * 8);
  IntWidth = static_cast<unsigned char>(Machine.IntWidth);
  IntAlign = AlignmentFor(Machine.IntWidth);
  LongWidth = static_cast<unsigned char>(Machine.LongWidth);
  LongAlign = AlignmentFor(Machine.LongWidth);
  LongLongWidth = static_cast<unsigned char>(Machine.LongLongWidth);
  LongLongAlign = AlignmentFor(Machine.LongLongWidth);
  Int128Align = AlignmentFor(128);
  BFloat16Width = BFloat16Align = 16;
  BFloat16Format = &APFloat::BFloat();
  SuitableAlign = static_cast<unsigned short>(Machine.StackAlignment);
  MaxAtomicPromoteWidth = MaxAtomicInlineWidth =
      static_cast<unsigned char>(Machine.MaximumAtomicWidth);
  MaxVectorAlign = Machine.MaximumVectorAlignment;
  TLSSupported = Machine.TLSSupported;
  HasBuiltinMSVaList =
      Machine.BuiltinVaListKind == NEVERC_TARGET_VA_LIST_AARCH64 ||
      Machine.BuiltinVaListKind == NEVERC_TARGET_VA_LIST_X86_64;

  SizeType = UnsignedTypeForWidth(Machine.PointerWidth);
  PtrDiffType = SignedTypeForWidth(Machine.PointerWidth);
  IntPtrType = PtrDiffType;
  Int64Type = SignedTypeForWidth(64);
  IntMaxType = SignedTypeForWidth(
      std::max({Machine.IntWidth, Machine.LongWidth,
                Machine.LongLongWidth}));

  for (const VerifiedTargetAddressSpace &AddressSpace :
       Machine.AddressSpaces)
    MaximumPointerWidth =
        std::max<uint64_t>(MaximumPointerWidth,
                           AddressSpace.PointerWidth);

  BuiltinInfos.reserve(Record.Builtins.size());
  for (const VerifiedTargetBuiltin &Builtin : Record.Builtins)
    BuiltinInfos.push_back(
        {StringRef(Builtin.Name), Builtin.TypeEncoding.c_str(),
         Builtin.Attributes.c_str(),
         Builtin.RequiredFeatures.empty()
             ? nullptr
             : Builtin.RequiredFeatures.c_str(),
         HeaderDesc::NO_HEADER,
         static_cast<LanguageID>(Builtin.Languages),
         Builtin.HeaderName.empty() ? nullptr
                                    : Builtin.HeaderName.c_str()});

  RegisterNames.reserve(Record.Registers.size());
  RegisterAliases.reserve(Record.Registers.size());
  RegisterAdditionalNames.reserve(Record.Registers.size());
  for (const VerifiedTargetRegister &Register : Record.Registers) {
    RegisterNames.push_back(Register.Name.c_str());
    if (!Register.Aliases.empty())
      RegisterAliases.push_back(makeRegisterAlias(Register));
    if (!Register.AdditionalNames.empty())
      RegisterAdditionalNames.push_back(
          makeAdditionalRegisterName(Register));
  }

  for (const VerifiedTargetFeature &Feature : Machine.Features)
    if (Feature.EnabledByDefault)
      ActiveFeatures.insert(Feature.Name);

  resetDataLayout(Machine.DataLayout,
                  Record.Machine.GlobalLabelPrefix.c_str());
}

TargetInfo::BuiltinVaListKind
PluginTargetInfo::getBuiltinVaListKind() const {
  switch (Record.Machine.BuiltinVaListKind) {
  case NEVERC_TARGET_VA_LIST_CHAR_POINTER:
    return CharPtrBuiltinVaList;
  case NEVERC_TARGET_VA_LIST_AARCH64:
    return AArch64ABIBuiltinVaList;
  case NEVERC_TARGET_VA_LIST_X86_64:
    return X86_64ABIBuiltinVaList;
  case NEVERC_TARGET_VA_LIST_VOID_POINTER:
  default:
    return VoidPtrBuiltinVaList;
  }
}

uint64_t PluginTargetInfo::getMaxPointerWidth() const {
  return MaximumPointerWidth;
}

const VerifiedTargetAddressSpace *
PluginTargetInfo::findAddressSpace(LangAS AddressSpace) const {
  const unsigned Number = getTargetAddressSpace(AddressSpace);
  const auto It = std::lower_bound(
      Record.Machine.AddressSpaces.begin(),
      Record.Machine.AddressSpaces.end(), Number,
      [](const VerifiedTargetAddressSpace &Entry, unsigned Value) {
        return Entry.AddressSpace < Value;
      });
  if (It == Record.Machine.AddressSpaces.end() ||
      It->AddressSpace != Number)
    return nullptr;
  return &*It;
}

uint64_t PluginTargetInfo::getPointerWidthV(
    LangAS AddressSpace) const {
  const VerifiedTargetAddressSpace *Entry =
      findAddressSpace(AddressSpace);
  return Entry ? Entry->PointerWidth : PointerWidth;
}

uint64_t PluginTargetInfo::getPointerAlignV(
    LangAS AddressSpace) const {
  const VerifiedTargetAddressSpace *Entry =
      findAddressSpace(AddressSpace);
  return Entry ? Entry->ABIAlignment : PointerAlign;
}

} // namespace neverc::plugin
