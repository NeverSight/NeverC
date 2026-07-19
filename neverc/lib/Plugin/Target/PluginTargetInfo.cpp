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
  for (const VerifiedTargetRegister &Register : Record.Registers) {
    RegisterNames.push_back(Register.Name.c_str());
    if (!Register.Aliases.empty())
      RegisterAliases.push_back(makeRegisterAlias(Register));
  }

  for (const VerifiedTargetFeature &Feature : Machine.Features)
    if (Feature.EnabledByDefault)
      ActiveFeatures.insert(Feature.Name);

  resetDataLayout(Machine.DataLayout,
                  Record.Machine.GlobalLabelPrefix.c_str());
}

void PluginTargetInfo::getTargetDefines(const LangOptions &,
                                        MacroBuilder &Builder) const {
  for (const VerifiedTargetMacro &Macro : Record.Macros) {
    if (Macro.Undefine) {
      Builder.undefineMacro(Macro.Name);
      continue;
    }
    if (Macro.Value.empty())
      Builder.defineMacro(Macro.Name);
    else
      Builder.defineMacro(Macro.Name, Macro.Value);
  }
}

ArrayRef<Builtin::Info> PluginTargetInfo::getTargetBuiltins() const {
  return BuiltinInfos;
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

bool PluginTargetInfo::validateAsmConstraint(
    const char *&Name, ConstraintInfo &Info) const {
  const StringRef Input(Name);
  for (const VerifiedTargetConstraint &Constraint :
       Record.Constraints) {
    if (!Input.starts_with(Constraint.Spelling))
      continue;
    if ((Constraint.Flags &
         NEVERC_TARGET_CONSTRAINT_ALLOWS_MEMORY) != 0)
      Info.setAllowsMemory();
    if ((Constraint.Flags &
         NEVERC_TARGET_CONSTRAINT_ALLOWS_REGISTER) != 0)
      Info.setAllowsRegister();
    if ((Constraint.Flags & NEVERC_TARGET_CONSTRAINT_IMMEDIATE) != 0)
      Info.setRequiresImmediate(Constraint.ImmediateMinimum,
                                Constraint.ImmediateMaximum);
    Name += Constraint.Spelling.size() - 1;
    return true;
  }
  return false;
}

std::string PluginTargetInfo::convertConstraint(
    const char *&ConstraintValue) const {
  const StringRef Input(ConstraintValue);
  for (const VerifiedTargetConstraint &Constraint :
       Record.Constraints) {
    if (!Input.starts_with(Constraint.Spelling))
      continue;
    ConstraintValue += Constraint.Spelling.size() - 1;
    return Constraint.ConvertedConstraint.empty()
               ? Constraint.Spelling
               : Constraint.ConvertedConstraint;
  }
  return TargetInfo::convertConstraint(ConstraintValue);
}

std::string_view PluginTargetInfo::getClobbers() const {
  return Record.Clobbers;
}

bool PluginTargetInfo::setCPU(const std::string &Name) {
  if (!isValidCPUName(Name))
    return false;
  CPU = Name;
  return true;
}

bool PluginTargetInfo::isValidCPUName(StringRef Name) const {
  if (Name.empty())
    return false;
  if (Record.Machine.CPUs.empty())
    return true;
  return std::binary_search(Record.Machine.CPUs.begin(),
                            Record.Machine.CPUs.end(), Name);
}

bool PluginTargetInfo::isValidTuneCPUName(StringRef Name) const {
  return isValidCPUName(Name);
}

void PluginTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  if (Record.Machine.CPUs.empty()) {
    Values.push_back(Record.Machine.DefaultCPU);
    return;
  }
  for (const std::string &Name : Record.Machine.CPUs)
    Values.push_back(Name);
}

void PluginTargetInfo::fillValidTuneCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  fillValidCPUList(Values);
}

bool PluginTargetInfo::initFeatureMap(
    StringMap<bool> &Features, DiagnosticsEngine &Diags,
    StringRef CPUValue,
    const std::vector<std::string> &FeatureVec) const {
  for (const VerifiedTargetFeature &Feature : Record.Machine.Features)
    if (Feature.EnabledByDefault)
      Features[Feature.Name] = true;
  for (const std::string &Spelling : FeatureVec) {
    StringRef Value(Spelling);
    if (Value.size() < 2 ||
        (Value.front() != '+' && Value.front() != '-') ||
        !isValidFeatureName(Value.drop_front())) {
      Diags.Report(diag::warn_fe_backend_invalid_feature_flag)
          << Value;
      return false;
    }
  }
  if (!TargetInfo::initFeatureMap(Features, Diags, CPUValue,
                                  FeatureVec))
    return false;

  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (const VerifiedTargetFeature &Feature : Record.Machine.Features) {
      if (!Features.lookup(Feature.Name))
        continue;
      for (const std::string &Implied : Feature.Implies)
        if (!Features.lookup(Implied)) {
          Features[Implied] = true;
          Changed = true;
        }
    }
  }
  for (const VerifiedTargetFeature &Feature : Record.Machine.Features) {
    if (!Features.lookup(Feature.Name))
      continue;
    for (const std::string &Conflict : Feature.Conflicts)
      if (Features.lookup(Conflict)) {
        Diags.Report(diag::warn_invalid_feature_combination)
            << (Feature.Name + " " + Conflict);
        return false;
      }
  }
  return true;
}

bool PluginTargetInfo::isValidFeatureName(StringRef Feature) const {
  const auto It = std::lower_bound(
      Record.Machine.Features.begin(), Record.Machine.Features.end(),
      Feature,
      [](const VerifiedTargetFeature &Entry, StringRef Value) {
        return Entry.Name < Value;
      });
  return It != Record.Machine.Features.end() && It->Name == Feature;
}

bool PluginTargetInfo::handleTargetFeatures(
    std::vector<std::string> &Features, DiagnosticsEngine &) {
  ActiveFeatures.clear();
  for (const std::string &Spelling : Features) {
    StringRef Value(Spelling);
    if (Value.size() < 2)
      continue;
    if (Value.front() == '+')
      ActiveFeatures.insert(Value.drop_front());
    else if (Value.front() == '-')
      ActiveFeatures.erase(Value.drop_front());
  }
  return true;
}

bool PluginTargetInfo::hasFeature(StringRef Feature) const {
  return ActiveFeatures.contains(Feature);
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

ArrayRef<const char *> PluginTargetInfo::getGCCRegNames() const {
  return RegisterNames;
}

ArrayRef<TargetInfo::GCCRegAlias>
PluginTargetInfo::getGCCRegAliases() const {
  return RegisterAliases;
}

} // namespace neverc::plugin
