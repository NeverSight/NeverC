#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <limits>
#include <map>
#include <set>
#include <string>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

Expected<std::string> copyString(NevercStringView View, StringRef Field,
                                 bool AllowEmpty = false) {
  if (View.Length > std::numeric_limits<size_t>::max() ||
      (!View.Data && View.Length != 0))
    return createStringError(inconvertibleErrorCode(),
                             Field + " has an invalid string view");
  StringRef Text(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  if ((!AllowEmpty && Text.empty()) || Text.contains('\0'))
    return createStringError(inconvertibleErrorCode(),
                             Field + " has an invalid value");
  return Text.str();
}

bool validHeader(
    const NevercABITableHeader &Header, size_t Required,
    uint16_t ExpectedMajor = NEVERC_TARGET_API_MAJOR,
    uint16_t MaximumMinor = NEVERC_TARGET_API_MINOR) {
  return Header.StructSize >= Required &&
         Header.Major == ExpectedMajor &&
         Header.Minor <= MaximumMinor && Header.Flags == 0;
}

bool validCIdentifier(StringRef Value) {
  if (Value.empty() ||
      !(std::isalpha(static_cast<unsigned char>(Value.front())) ||
        Value.front() == '_'))
    return false;
  return llvm::all_of(Value.drop_front(), [](char C) {
    return std::isalnum(static_cast<unsigned char>(C)) || C == '_';
  });
}

bool validMacroReplacement(StringRef Value) {
  return llvm::none_of(Value, [](char C) {
    const unsigned char Byte = static_cast<unsigned char>(C);
    return C == '\r' || C == '\n' || (Byte < 0x20 && C != '\t');
  });
}

std::string normalizeComponent(StringRef Value) {
  auto Lower = Value.trim().lower();
  std::string Result = Lower.str().str();
  if (Result == "amd64" || Result == "x86-64")
    return "x86_64";
  if (Result == "arm64")
    return "aarch64";
  return Result;
}

Expected<std::vector<std::string>>
copyStrings(NevercStringArrayView Values, StringRef Field) {
  if (Values.Count > 1024 ||
      (Values.Count != 0 &&
       (!Values.Data || Values.ElementStride < sizeof(NevercStringView))))
    return createStringError(inconvertibleErrorCode(),
                             Field + " has an invalid array view");
  std::vector<std::string> Result;
  Result.reserve(static_cast<size_t>(Values.Count));
  const auto *Bytes = reinterpret_cast<const uint8_t *>(Values.Data);
  for (uint64_t I = 0; I != Values.Count; ++I) {
    const auto *Value = reinterpret_cast<const NevercStringView *>(
        Bytes + I * Values.ElementStride);
    auto Text = copyString(*Value, Field);
    if (!Text)
      return Text.takeError();
    Result.push_back(normalizeComponent(*Text));
  }
  return Result;
}

Expected<std::vector<NevercInterfaceID>>
copyIDs(NevercInterfaceIDArrayView Values, StringRef Field) {
  if (Values.Count > 1024 ||
      (Values.Count != 0 &&
       (!Values.Data || Values.ElementStride < sizeof(NevercInterfaceID))))
    return createStringError(inconvertibleErrorCode(),
                             Field + " has an invalid array view");
  std::vector<NevercInterfaceID> Result;
  Result.reserve(static_cast<size_t>(Values.Count));
  const auto *Bytes = reinterpret_cast<const uint8_t *>(Values.Data);
  for (uint64_t I = 0; I != Values.Count; ++I) {
    const auto *Value = reinterpret_cast<const NevercInterfaceID *>(
        Bytes + I * Values.ElementStride);
    if (!nonzero(*Value))
      return createStringError(inconvertibleErrorCode(),
                               Field + " contains a zero ID");
    Result.push_back(*Value);
  }
  return Result;
}

Expected<std::vector<PluginTargetSnapshot::TripleMatcher>>
copyMatchers(NevercStructArrayView Values) {
  constexpr size_t Required =
      offsetof(NevercTargetTripleMatcher, Reserved) +
      sizeof(NevercTargetTripleMatcher::Reserved);
  if (Values.Count > 1024 ||
      (Values.Count != 0 &&
       (!Values.Data || Values.ElementStride < Required)))
    return createStringError(inconvertibleErrorCode(),
                             "Target triple matcher has an invalid array view");

  std::vector<PluginTargetSnapshot::TripleMatcher> Result;
  Result.reserve(static_cast<size_t>(Values.Count));
  const auto *Bytes = static_cast<const uint8_t *>(Values.Data);
  for (uint64_t I = 0; I != Values.Count; ++I) {
    const auto *Value = reinterpret_cast<const NevercTargetTripleMatcher *>(
        Bytes + I * Values.ElementStride);
    if (!validHeader(Value->Header, Required) || Value->Reserved != 0)
      return createStringError(inconvertibleErrorCode(),
                               "Target triple matcher is invalid");
    auto Architecture =
        copyString(Value->Architecture, "matcher architecture", true);
    auto Vendor = copyString(Value->Vendor, "matcher vendor", true);
    auto OperatingSystem =
        copyString(Value->OperatingSystem, "matcher operating system", true);
    auto Environment =
        copyString(Value->Environment, "matcher environment", true);
    if (!Architecture)
      return Architecture.takeError();
    if (!Vendor)
      return Vendor.takeError();
    if (!OperatingSystem)
      return OperatingSystem.takeError();
    if (!Environment)
      return Environment.takeError();
    Result.push_back(
        {normalizeComponent(*Architecture), normalizeComponent(*Vendor),
         normalizeComponent(*OperatingSystem),
         normalizeComponent(*Environment), Value->Priority});
  }
  return Result;
}

bool validStructArray(NevercStructArrayView Values, size_t Required,
                      uint64_t MaximumCount = 4096) {
  return Values.Count <= MaximumCount &&
         (Values.Count == 0 ||
          (Values.Data && Values.ElementStride >= Required &&
           Values.ElementStride <= std::numeric_limits<size_t>::max() &&
           Values.ElementStride <=
               std::numeric_limits<size_t>::max() / Values.Count));
}

Expected<std::vector<VerifiedTargetMacro>>
copyMacros(NevercStructArrayView Values) {
  constexpr size_t Required =
      offsetof(NevercTargetMacroDescriptor, Flags) +
      sizeof(NevercTargetMacroDescriptor::Flags);
  if (!validStructArray(Values, Required))
    return createStringError(inconvertibleErrorCode(),
                             "Target macro array is invalid");
  std::vector<VerifiedTargetMacro> Result;
  std::set<std::string> Names;
  const auto *Bytes = static_cast<const uint8_t *>(Values.Data);
  for (uint64_t I = 0; I != Values.Count; ++I) {
    const auto *Macro = reinterpret_cast<
        const NevercTargetMacroDescriptor *>(
        Bytes + static_cast<size_t>(I * Values.ElementStride));
    if (!validHeader(Macro->Header, Required) ||
        (Macro->Flags & ~NEVERC_TARGET_MACRO_UNDEFINE) != 0)
      return createStringError(inconvertibleErrorCode(),
                               "Target macro descriptor is invalid");
    auto Name = copyString(Macro->Name, "Target macro name");
    auto Value =
        copyString(Macro->Value, "Target macro value", true);
    if (!Name)
      return Name.takeError();
    if (!Value)
      return Value.takeError();
    if (!validCIdentifier(*Name) || !validMacroReplacement(*Value) ||
        ((Macro->Flags & NEVERC_TARGET_MACRO_UNDEFINE) != 0 &&
         !Value->empty()))
      return createStringError(inconvertibleErrorCode(),
                               "Target macro descriptor has invalid tokens");
    if (!Names.insert(*Name).second)
      return createStringError(inconvertibleErrorCode(),
                               "duplicate Target macro '" + *Name + "'");
    Result.push_back(
        {std::move(*Name), std::move(*Value),
         (Macro->Flags & NEVERC_TARGET_MACRO_UNDEFINE) != 0});
  }
  return Result;
}

Expected<std::vector<VerifiedTargetBuiltin>>
copyBuiltins(NevercStructArrayView Values) {
  constexpr size_t Required =
      offsetof(NevercTargetBuiltinDescriptor, Lower) +
      sizeof(NevercTargetBuiltinDescriptor::Lower);
  constexpr uint32_t KnownLanguages =
      NEVERC_TARGET_BUILTIN_LANGUAGE_GNU |
      NEVERC_TARGET_BUILTIN_LANGUAGE_C |
      NEVERC_TARGET_BUILTIN_LANGUAGE_MS;
  if (!validStructArray(Values, Required))
    return createStringError(inconvertibleErrorCode(),
                             "Target builtin array is invalid");
  std::vector<VerifiedTargetBuiltin> Result;
  std::set<std::string> Names;
  const auto *Bytes = static_cast<const uint8_t *>(Values.Data);
  for (uint64_t I = 0; I != Values.Count; ++I) {
    const auto *Builtin = reinterpret_cast<
        const NevercTargetBuiltinDescriptor *>(
        Bytes + static_cast<size_t>(I * Values.ElementStride));
    if (!validHeader(Builtin->Header, Required) ||
        Builtin->Reserved != 0 || Builtin->Languages == 0 ||
        (Builtin->Languages & ~KnownLanguages) != 0 ||
        Builtin->Lower == nullptr)
      return createStringError(inconvertibleErrorCode(),
                               "Target builtin descriptor is invalid");
    auto Name = copyString(Builtin->Name, "Target builtin name");
    auto Type =
        copyString(Builtin->TypeEncoding, "Target builtin type");
    auto Attributes = copyString(Builtin->Attributes,
                                 "Target builtin attributes", true);
    auto Features = copyString(Builtin->RequiredFeatures,
                               "Target builtin features", true);
    auto Header = copyString(Builtin->HeaderName,
                             "Target builtin header", true);
    if (!Name)
      return Name.takeError();
    if (!Type)
      return Type.takeError();
    if (!Attributes)
      return Attributes.takeError();
    if (!Features)
      return Features.takeError();
    if (!Header)
      return Header.takeError();
    if (!Names.insert(*Name).second)
      return createStringError(inconvertibleErrorCode(),
                               "duplicate Target builtin '" + *Name +
                                   "'");
    Result.push_back(
        {std::move(*Name), std::move(*Type), std::move(*Attributes),
         std::move(*Features), std::move(*Header),
         Builtin->Languages, Builtin->Lower});
  }
  return Result;
}

Expected<std::vector<VerifiedTargetRegister>>
copyRegisters(NevercStructArrayView Values) {
  constexpr size_t Required =
      offsetof(NevercTargetRegisterDescriptor, Flags) +
      sizeof(NevercTargetRegisterDescriptor::Flags);
  if (!validStructArray(Values, Required))
    return createStringError(inconvertibleErrorCode(),
                             "Target register array is invalid");
  std::vector<VerifiedTargetRegister> Result;
  std::set<std::string> Names;
  const auto *Bytes = static_cast<const uint8_t *>(Values.Data);
  for (uint64_t I = 0; I != Values.Count; ++I) {
    const auto *Register = reinterpret_cast<
        const NevercTargetRegisterDescriptor *>(
        Bytes + static_cast<size_t>(I * Values.ElementStride));
    if (!validHeader(Register->Header, Required) ||
        Register->Reserved != 0 || Register->Flags != 0 ||
        Register->Aliases.Count > 4 ||
        (Register->Aliases.Count != 0 &&
         (!Register->Aliases.Data ||
          Register->Aliases.ElementStride <
              sizeof(NevercStringView))) ||
        Register->AdditionalNames.Count > 5 ||
        (Register->AdditionalNames.Count != 0 &&
         (!Register->AdditionalNames.Data ||
          Register->AdditionalNames.ElementStride <
              sizeof(NevercStringView))))
      return createStringError(inconvertibleErrorCode(),
                               "Target register descriptor is invalid");
    auto Name = copyString(Register->Name, "Target register name");
    if (!Name)
      return Name.takeError();
    if (!Names.insert(*Name).second)
      return createStringError(inconvertibleErrorCode(),
                               "duplicate Target register '" + *Name +
                                   "'");
    VerifiedTargetRegister Verified;
    Verified.Name = std::move(*Name);
    const auto *AliasBytes = reinterpret_cast<const uint8_t *>(
        Register->Aliases.Data);
    for (uint64_t AliasIndex = 0;
         AliasIndex != Register->Aliases.Count; ++AliasIndex) {
      const auto *Alias =
          reinterpret_cast<const NevercStringView *>(
              AliasBytes + static_cast<size_t>(
                               AliasIndex *
                               Register->Aliases.ElementStride));
      auto Text = copyString(*Alias, "Target register alias");
      if (!Text)
        return Text.takeError();
      if (!Names.insert(*Text).second)
        return createStringError(
            inconvertibleErrorCode(),
            "duplicate Target register name or alias '" + *Text + "'");
      Verified.Aliases.push_back(std::move(*Text));
    }
    const auto *AdditionalNameBytes = reinterpret_cast<const uint8_t *>(
        Register->AdditionalNames.Data);
    for (uint64_t AdditionalIndex = 0;
         AdditionalIndex != Register->AdditionalNames.Count;
         ++AdditionalIndex) {
      const auto *AdditionalName =
          reinterpret_cast<const NevercStringView *>(
              AdditionalNameBytes +
              static_cast<size_t>(
                  AdditionalIndex *
                  Register->AdditionalNames.ElementStride));
      auto Text =
          copyString(*AdditionalName, "additional Target register name");
      if (!Text)
        return Text.takeError();
      if (!Names.insert(*Text).second)
        return createStringError(
            inconvertibleErrorCode(),
            "duplicate Target register name or alias '" + *Text + "'");
      Verified.AdditionalNames.push_back(std::move(*Text));
    }
    Verified.RegisterNumber = Register->RegisterNumber;
    Result.push_back(std::move(Verified));
  }
  return Result;
}

Expected<std::vector<VerifiedTargetConstraint>>
copyConstraints(NevercStructArrayView Values) {
  constexpr size_t Required =
      offsetof(NevercTargetConstraintDescriptor, Reserved) +
      sizeof(NevercTargetConstraintDescriptor::Reserved);
  constexpr uint64_t KnownFlags =
      NEVERC_TARGET_CONSTRAINT_ALLOWS_MEMORY |
      NEVERC_TARGET_CONSTRAINT_ALLOWS_REGISTER |
      NEVERC_TARGET_CONSTRAINT_IMMEDIATE;
  if (!validStructArray(Values, Required))
    return createStringError(inconvertibleErrorCode(),
                             "Target constraint array is invalid");
  std::vector<VerifiedTargetConstraint> Result;
  std::set<std::string> Spellings;
  const auto *Bytes = static_cast<const uint8_t *>(Values.Data);
  for (uint64_t I = 0; I != Values.Count; ++I) {
    const auto *Constraint = reinterpret_cast<
        const NevercTargetConstraintDescriptor *>(
        Bytes + static_cast<size_t>(I * Values.ElementStride));
    if (!validHeader(Constraint->Header, Required) ||
        Constraint->Reserved != 0 || Constraint->Flags == 0 ||
        (Constraint->Flags & ~KnownFlags) != 0 ||
        Constraint->MatchingOperand < -1 ||
        (Constraint->RegisterClassID != 0 &&
         (Constraint->Flags &
          NEVERC_TARGET_CONSTRAINT_ALLOWS_REGISTER) == 0) ||
        Constraint->ImmediateValues.Count > 64 ||
        (Constraint->ImmediateValues.Count != 0 &&
         (!Constraint->ImmediateValues.Data ||
          Constraint->ImmediateValues.ElementStride < sizeof(int32_t))) ||
        (Constraint->ImmediateValues.Count != 0 &&
         (Constraint->Flags & NEVERC_TARGET_CONSTRAINT_IMMEDIATE) == 0) ||
        ((Constraint->Flags & NEVERC_TARGET_CONSTRAINT_IMMEDIATE) !=
             0 &&
         Constraint->ImmediateMinimum >
             Constraint->ImmediateMaximum))
      return createStringError(inconvertibleErrorCode(),
                               "Target constraint descriptor is invalid");
    auto Spelling =
        copyString(Constraint->Spelling, "Target constraint spelling");
    auto Converted = copyString(Constraint->ConvertedConstraint,
                                "converted Target constraint", true);
    if (!Spelling)
      return Spelling.takeError();
    if (!Converted)
      return Converted.takeError();
    if (!Spellings.insert(*Spelling).second)
      return createStringError(
          inconvertibleErrorCode(),
          "duplicate Target constraint '" + *Spelling + "'");
    std::vector<int32_t> ImmediateValues;
    const auto *ImmediateBytes = reinterpret_cast<const uint8_t *>(
        Constraint->ImmediateValues.Data);
    for (uint64_t ValueIndex = 0;
         ValueIndex != Constraint->ImmediateValues.Count; ++ValueIndex) {
      const auto *Value = reinterpret_cast<const int32_t *>(
          ImmediateBytes +
          static_cast<size_t>(
              ValueIndex * Constraint->ImmediateValues.ElementStride));
      ImmediateValues.push_back(*Value);
    }
    if (!llvm::is_sorted(ImmediateValues) ||
        std::adjacent_find(ImmediateValues.begin(),
                           ImmediateValues.end()) != ImmediateValues.end())
      return createStringError(
          inconvertibleErrorCode(),
          "Target constraint immediate set must be sorted and unique");
    Result.push_back({std::move(*Spelling),
                      Constraint->Flags,
                      Constraint->ImmediateMinimum,
                      Constraint->ImmediateMaximum,
                      std::move(ImmediateValues),
                      Constraint->RegisterClassID,
                      Constraint->MatchingOperand,
                      std::move(*Converted)});
  }
  llvm::sort(Result, [](const VerifiedTargetConstraint &Left,
                        const VerifiedTargetConstraint &Right) {
    if (Left.Spelling.size() != Right.Spelling.size())
      return Left.Spelling.size() > Right.Spelling.size();
    return Left.Spelling < Right.Spelling;
  });
  return Result;
}

Expected<std::vector<PluginTargetSnapshot::MCSchemaValueRecord>>
copyMCSchemaValues(NevercStructArrayView Values, StringRef Field) {
  constexpr size_t Required =
      offsetof(NevercMCSchemaValueDescriptor, Flags) +
      sizeof(NevercMCSchemaValueDescriptor::Flags);
  if (!validStructArray(Values, Required))
    return createStringError(inconvertibleErrorCode(),
                             Field + " array is invalid");
  std::vector<PluginTargetSnapshot::MCSchemaValueRecord> Result;
  std::set<uint32_t> StableIDs;
  std::set<uint32_t> BackendValues;
  std::set<std::string> Names;
  const auto *Bytes = static_cast<const uint8_t *>(Values.Data);
  for (uint64_t I = 0; I != Values.Count; ++I) {
    const auto *Value =
        reinterpret_cast<const NevercMCSchemaValueDescriptor *>(
            Bytes + static_cast<size_t>(I * Values.ElementStride));
    if (Value->Header.StructSize < Required ||
        Value->Header.Major != NEVERC_MC_API_MAJOR ||
        Value->Header.Minor > NEVERC_MC_API_MINOR ||
        Value->Header.Flags != 0 || Value->StableID == 0)
      return createStringError(inconvertibleErrorCode(),
                               Field + " descriptor is invalid");
    const std::string NameField = (Field + " name").str();
    auto Name = copyString(Value->CanonicalName, NameField);
    if (!Name)
      return Name.takeError();
    if (!StableIDs.insert(Value->StableID).second)
      return createStringError(
          inconvertibleErrorCode(),
          Field + " has a duplicate stable ID");
    if (!BackendValues.insert(Value->BackendValue).second)
      return createStringError(
          inconvertibleErrorCode(),
          Field + " has a duplicate backend value");
    if (!Names.insert(*Name).second)
      return createStringError(inconvertibleErrorCode(),
                               Field + " has a duplicate name");
    Result.push_back({Value->StableID, Value->BackendValue,
                      std::move(*Name), Value->Flags});
  }
  return Result;
}

bool componentsOverlap(StringRef Left, StringRef Right) {
  return Left.empty() || Right.empty() || Left == Right;
}

bool matchersOverlap(const PluginTargetSnapshot::TripleMatcher &Left,
                     const PluginTargetSnapshot::TripleMatcher &Right) {
  return Left.Priority == Right.Priority &&
         componentsOverlap(Left.Architecture, Right.Architecture) &&
         componentsOverlap(Left.Vendor, Right.Vendor) &&
         componentsOverlap(Left.OperatingSystem, Right.OperatingSystem) &&
         componentsOverlap(Left.Environment, Right.Environment);
}

bool validProductKind(NevercCodeGenProductKind Kind) {
  return (Kind >= NEVERC_CODEGEN_PRODUCT_IR &&
          Kind <= NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE) ||
         Kind >= NEVERC_CODEGEN_PRODUCT_CUSTOM;
}

bool validDigest(StringRef Digest) {
  return Digest.size() == 64 &&
         std::all_of(Digest.begin(), Digest.end(), [](char C) {
           return (C >= '0' && C <= '9') || (C >= 'a' && C <= 'f');
         });
}

Expected<std::array<std::string, 4>> parseTriple(StringRef Triple) {
  std::array<std::string, 4> Components;
  StringRef Remaining = Triple.trim();
  if (Remaining.empty() || Remaining.size() > 256)
    return createStringError(inconvertibleErrorCode(),
                             "malformed target triple: empty or too long");
  for (size_t I = 0; I != Components.size(); ++I) {
    auto Part = Remaining.split('-');
    if (Part.first.empty() ||
        !std::all_of(Part.first.begin(), Part.first.end(), [](char C) {
          return std::isalnum(static_cast<unsigned char>(C)) || C == '_' ||
                 C == '.' || C == '+';
        }))
      return createStringError(inconvertibleErrorCode(),
                               "malformed target triple component");
    Components[I] = normalizeComponent(Part.first);
    Remaining = Part.second;
    if (Remaining.empty()) {
      if (I == 0)
        return createStringError(
            inconvertibleErrorCode(),
            "malformed target triple: expected at least arch-vendor");
      return Components;
    }
    if (I + 1 == Components.size())
      return createStringError(
          inconvertibleErrorCode(),
          "malformed target triple: too many components");
  }
  return createStringError(inconvertibleErrorCode(),
                           "malformed target triple");
}

bool matcherAccepts(
    const PluginTargetSnapshot::TripleMatcher &Matcher,
    const std::array<std::string, 4> &Triple) {
  return (Matcher.Architecture.empty() ||
          Matcher.Architecture == Triple[0]) &&
         (Matcher.Vendor.empty() || Matcher.Vendor == Triple[1]) &&
         (Matcher.OperatingSystem.empty() ||
          Matcher.OperatingSystem == Triple[2]) &&
         (Matcher.Environment.empty() ||
          Matcher.Environment == Triple[3]);
}

unsigned matcherSpecificity(
    const PluginTargetSnapshot::TripleMatcher &Matcher) {
  return static_cast<unsigned>(!Matcher.Architecture.empty()) +
         static_cast<unsigned>(!Matcher.Vendor.empty()) +
         static_cast<unsigned>(!Matcher.OperatingSystem.empty()) +
         static_cast<unsigned>(!Matcher.Environment.empty());
}

bool hasLayerOptions(const PluginTargetOptionLayer &Layer) {
  return !Layer.Triple.empty() || !Layer.CPU.empty() ||
         !Layer.Features.empty() || !Layer.ObjectFormat.empty() ||
         Layer.RelocationModel != 0 || Layer.CodeModel != 0 ||
         Layer.ExecutionLevel != 0;
}

Expected<std::vector<std::string>>
resolveFeatures(const PluginTargetSnapshot::TargetRecord &Target,
                const PluginTargetRequest &Request) {
  struct State {
    bool Enabled = false;
    unsigned Precedence = 0;
  };
  std::map<std::string, State> States;
  const auto FindFeature = [&](StringRef Name) {
    return std::lower_bound(
        Target.Machine.Features.begin(), Target.Machine.Features.end(), Name,
        [](const VerifiedTargetFeature &Feature, StringRef Value) {
          return Feature.Name < Value;
        });
  };
  const auto ApplyLayer = [&](StringRef Text, unsigned Precedence,
                              StringRef Layer) -> Error {
    if (Text.empty())
      return Error::success();
    std::map<std::string, bool> LayerValues;
    while (true) {
      auto Part = Text.split(',');
      StringRef Token = Part.first.trim();
      if (Token.empty())
        return createStringError(
            inconvertibleErrorCode(),
            "malformed " + Layer + " target feature list");
      bool Enabled = true;
      if (Token.front() == '+' || Token.front() == '-') {
        Enabled = Token.front() == '+';
        Token = Token.drop_front().trim();
      }
      if (Token.empty())
        return createStringError(
            inconvertibleErrorCode(),
            "malformed " + Layer + " target feature name");
      const auto Feature = FindFeature(Token);
      if (Feature == Target.Machine.Features.end() ||
          Feature->Name != Token)
        return createStringError(
            inconvertibleErrorCode(),
            "unknown target feature '" + Token + "' in " + Layer);
      auto [It, Inserted] =
          LayerValues.emplace(Token.str(), Enabled);
      if (!Inserted && It->second != Enabled)
        return createStringError(
            inconvertibleErrorCode(),
            "incompatible target feature overrides for '" + Token + "'");
      States[Token.str()] = {Enabled, Precedence};
      if (Part.second.empty())
        break;
      Text = Part.second;
    }
    return Error::success();
  };

  if (Error E = ApplyLayer(Request.Platform.Features, 1, "platform"))
    return std::move(E);
  for (const VerifiedTargetFeature &Feature : Target.Machine.Features)
    if (Feature.EnabledByDefault)
      States[Feature.Name] = {true, 2};
  if (Error E =
          ApplyLayer(Request.Configuration.Features, 3, "configuration"))
    return std::move(E);
  if (Error E = ApplyLayer(Request.Features, 4, "command-line"))
    return std::move(E);

  bool Changed = true;
  while (Changed) {
    Changed = false;
    for (const VerifiedTargetFeature &Feature : Target.Machine.Features) {
      auto Source = States.find(Feature.Name);
      if (Source == States.end() || !Source->second.Enabled)
        continue;
      for (const std::string &Implied : Feature.Implies) {
        auto Existing = States.find(Implied);
        if (Existing != States.end() && !Existing->second.Enabled &&
            Existing->second.Precedence >= Source->second.Precedence)
          return createStringError(
              inconvertibleErrorCode(),
              "incompatible target features: '" + Feature.Name +
                  "' requires disabled feature '" + Implied + "'");
        if (Existing == States.end() || !Existing->second.Enabled) {
          States[Implied] = {true, Source->second.Precedence};
          Changed = true;
        }
      }
    }
  }

  std::vector<std::string> Result;
  for (const VerifiedTargetFeature &Feature : Target.Machine.Features) {
    auto Value = States.find(Feature.Name);
    if (Value == States.end() || !Value->second.Enabled)
      continue;
    for (const std::string &Conflict : Feature.Conflicts) {
      auto Other = States.find(Conflict);
      if (Other != States.end() && Other->second.Enabled)
        return createStringError(
            inconvertibleErrorCode(),
            "incompatible target features '" + Feature.Name + "' and '" +
                Conflict + "'");
    }
    Result.push_back(Feature.Name);
  }
  return Result;
}

} // namespace

const PluginTargetSnapshot::TargetRecord *
PluginTargetSnapshot::findTarget(NevercTargetID ID) const {
  for (const TargetRecord &Target : Targets)
    if (sameID(Target.ID, ID))
      return &Target;
  return nullptr;
}

const PluginTargetSnapshot::NamedRecord *
PluginTargetSnapshot::findABI(NevercTargetABIID ID) const {
  for (const NamedRecord &ABI : ABIs)
    if (sameID(ABI.ID, ID))
      return &ABI;
  return nullptr;
}

const PluginTargetSnapshot::NamedRecord *
PluginTargetSnapshot::findCallingConvention(
    NevercCallingConventionID ID) const {
  for (const NamedRecord &CallingConvention : CallingConventions)
    if (sameID(CallingConvention.ID, ID))
      return &CallingConvention;
  return nullptr;
}

const PluginTargetSnapshot::NamedRecord *
PluginTargetSnapshot::findMCSchema(NevercInterfaceID ID) const {
  for (const NamedRecord &Schema : MCSchemas)
    if (sameID(Schema.ID, ID))
      return &Schema;
  return nullptr;
}

const PluginTargetSnapshot::ObjectFormatRecord *
PluginTargetSnapshot::findObjectFormat(NevercObjectFormatID ID) const {
  for (const ObjectFormatRecord &Format : ObjectFormats)
    if (sameID(Format.ID, ID))
      return &Format;
  return nullptr;
}

size_t PluginTargetSnapshot::builtinTargetCount() const {
  return builtinTargetRoutes().size();
}

ArrayRef<BuiltinTargetRoute>
PluginTargetSnapshot::builtinTargets() const {
  return builtinTargetRoutes();
}

const BuiltinTargetRoute *
PluginTargetSnapshot::matchBuiltinTarget(StringRef Selector) const {
  return findBuiltinTargetRoute(Selector);
}

const PluginTargetSnapshot::TargetRecord *
PluginTargetSnapshot::matchTarget(StringRef Selector) const {
  const std::string Requested = normalizeComponent(Selector);
  for (const TargetRecord &Target : Targets) {
    if (normalizeComponent(Target.CanonicalName) == Requested ||
        normalizeComponent(Target.Machine.RawTriple) == Requested ||
        std::any_of(Target.Aliases.begin(), Target.Aliases.end(),
                    [&](const std::string &Alias) {
                      return Alias == Requested;
                    }))
      return &Target;
  }

  auto Parsed = parseTriple(Selector);
  if (!Parsed) {
    consumeError(Parsed.takeError());
    return nullptr;
  }

  const TargetRecord *Best = nullptr;
  uint32_t BestPriority = 0;
  unsigned BestSpecificity = 0;
  for (const TargetRecord &Target : Targets) {
    for (const TripleMatcher &Matcher : Target.Matchers) {
      if (!matcherAccepts(Matcher, *Parsed))
        continue;
      const unsigned Specificity = matcherSpecificity(Matcher);
      if (Best &&
          (Matcher.Priority < BestPriority ||
           (Matcher.Priority == BestPriority &&
            Specificity <= BestSpecificity)))
        continue;
      Best = &Target;
      BestPriority = Matcher.Priority;
      BestSpecificity = Specificity;
    }
  }
  return Best;
}

Expected<std::shared_ptr<const PluginTargetSnapshot>>
PluginTargetRegistry::freeze(
    ArrayRef<PluginTargetRegistrationView> Registrations,
    const PluginTargetRequest &Request) {
  auto Snapshot = std::make_shared<PluginTargetSnapshot>();

  for (const PluginTargetRegistrationView &Registration : Registrations) {
    if (Registration.PluginID.empty())
      return createStringError(inconvertibleErrorCode(),
                               "Target registration has no plugin ID");
    for (const NevercTargetDescriptor &Descriptor : Registration.Targets) {
      constexpr size_t Required =
          offsetof(NevercTargetDescriptor, DestroyUserData) +
          sizeof(NevercTargetDescriptor::DestroyUserData);
      if (!validHeader(Descriptor.Header, Required) ||
          !nonzero(Descriptor.TargetID))
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' has an invalid Target descriptor");
      const bool HasAnyCPUCallback =
          Descriptor.ValidateCPU || Descriptor.CanonicalizeCPU ||
          Descriptor.ListCPUs;
      if (HasAnyCPUCallback &&
          (!Descriptor.ValidateCPU || !Descriptor.CanonicalizeCPU ||
           !Descriptor.ListCPUs))
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' must provide the complete Target CPU callback set");

      auto Name = copyString(Descriptor.CanonicalName, "Target name");
      if (!Name)
        return Name.takeError();
      auto Machine = verifyTargetMachineDescriptor(Descriptor.Machine);
      if (!Machine)
        return joinErrors(
            createStringError(
                inconvertibleErrorCode(),
                "plugin '" + Registration.PluginID +
                    "' has an invalid Target machine descriptor"),
            Machine.takeError());
      auto Macros = copyMacros(Descriptor.Macros);
      if (!Macros)
        return Macros.takeError();
      auto Builtins = copyBuiltins(Descriptor.Builtins);
      if (!Builtins)
        return Builtins.takeError();
      auto Registers = copyRegisters(Descriptor.Registers);
      if (!Registers)
        return Registers.takeError();
      auto Constraints = copyConstraints(Descriptor.Constraints);
      if (!Constraints)
        return Constraints.takeError();
      auto Clobbers =
          copyString(Descriptor.Clobbers, "Target clobbers", true);
      if (!Clobbers)
        return Clobbers.takeError();
      if ((Descriptor.CreateTargetMachine == nullptr) !=
          (Descriptor.DestroyTargetMachine == nullptr))
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' must provide both TargetMachine lifecycle callbacks");

      for (const PluginTargetSnapshot::TargetRecord &Existing :
           Snapshot->Targets) {
        if (!sameID(Existing.ID, Descriptor.TargetID))
          continue;
        return createStringError(
            inconvertibleErrorCode(),
            "duplicate Target ID registered by plugins '" +
                Existing.PluginID + "' and '" + Registration.PluginID + "'");
      }

      PluginTargetSnapshot::TargetRecord Target;
      Target.PluginID = Registration.PluginID.str();
      Target.Owner = Registration.Owner;
      Target.ID = Descriptor.TargetID;
      Target.CanonicalName = std::move(*Name);
      auto Aliases = copyStrings(Descriptor.Aliases, "Target alias");
      if (!Aliases)
        return Aliases.takeError();
      Target.Aliases = std::move(*Aliases);
      auto Matchers = copyMatchers(Descriptor.TripleMatchers);
      if (!Matchers)
        return Matchers.takeError();
      Target.Matchers = std::move(*Matchers);
      Target.DefaultABI = Descriptor.DefaultABI;
      Target.DefaultCallingConvention =
          Descriptor.DefaultCallingConvention;
      Target.MCSchemaID = Descriptor.MCSchemaID;
      Target.DefaultObjectFormatID = Descriptor.DefaultObjectFormatID;
      Target.Machine = std::move(*Machine);
      Target.Macros = std::move(*Macros);
      Target.Builtins = std::move(*Builtins);
      Target.Registers = std::move(*Registers);
      Target.Constraints = std::move(*Constraints);
      Target.Clobbers = std::move(*Clobbers);
      Target.Flags = Descriptor.Flags;
      Target.ValidateCPU = Descriptor.ValidateCPU;
      Target.CanonicalizeCPU = Descriptor.CanonicalizeCPU;
      Target.ListCPUs = Descriptor.ListCPUs;
      Target.ResolveFeatures = Descriptor.ResolveFeatures;
      Target.CreateTargetMachine = Descriptor.CreateTargetMachine;
      Target.DestroyTargetMachine = Descriptor.DestroyTargetMachine;
      Target.TargetUserData = Descriptor.UserData;
      for (const PluginTargetSnapshot::TargetRecord &Existing :
           Snapshot->Targets)
        for (const PluginTargetSnapshot::TripleMatcher &ExistingMatcher :
             Existing.Matchers)
          for (const PluginTargetSnapshot::TripleMatcher &NewMatcher :
               Target.Matchers)
            if (matchersOverlap(ExistingMatcher, NewMatcher))
              return createStringError(
                  inconvertibleErrorCode(),
                  "overlapping triple matcher between plugins '" +
                      Existing.PluginID + "' and '" + Target.PluginID +
                      "' (architecture/vendor/operating-system/environment)");
      Snapshot->Targets.push_back(std::move(Target));
    }
  }

  for (size_t I = 0; I != Snapshot->Targets.size(); ++I) {
    const auto NamesFor = [](const PluginTargetSnapshot::TargetRecord &Target) {
      std::vector<std::string> Names;
      Names.push_back(normalizeComponent(Target.CanonicalName));
      Names.insert(Names.end(), Target.Aliases.begin(), Target.Aliases.end());
      return Names;
    };
    const std::vector<std::string> LeftNames = NamesFor(Snapshot->Targets[I]);
    if (std::set<std::string>(LeftNames.begin(), LeftNames.end()).size() !=
        LeftNames.size())
      return createStringError(
          inconvertibleErrorCode(),
          "Target '" + Snapshot->Targets[I].CanonicalName +
              "' contains duplicate normalized aliases");
    for (size_t J = I + 1; J != Snapshot->Targets.size(); ++J) {
      const std::vector<std::string> RightNames =
          NamesFor(Snapshot->Targets[J]);
      for (const std::string &Left : LeftNames)
        if (llvm::is_contained(RightNames, Left))
          return createStringError(
              inconvertibleErrorCode(),
              "ambiguous Target alias '" + Left + "' from plugins '" +
                  Snapshot->Targets[I].PluginID + "' and '" +
                  Snapshot->Targets[J].PluginID + "'");
    }
  }

  for (const PluginTargetRegistrationView &Registration : Registrations) {
    for (const NevercTargetABIDescriptor &Descriptor : Registration.ABIs) {
      constexpr size_t Required =
          offsetof(NevercTargetABIDescriptor, DestroyUserData) +
          sizeof(NevercTargetABIDescriptor::DestroyUserData);
      if (!validHeader(Descriptor.Header, Required) ||
          !nonzero(Descriptor.ABIID) || !nonzero(Descriptor.TargetID))
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' has an invalid target ABI descriptor");
      auto Name = copyString(Descriptor.CanonicalName, "target ABI name");
      auto Dependencies =
          copyIDs(Descriptor.Dependencies, "target ABI dependency");
      if (!Name)
        return Name.takeError();
      if (!Dependencies)
        return Dependencies.takeError();
      if (Descriptor.ClassifyFunction) {
        constexpr size_t RequiredVAArg =
            offsetof(NevercTargetVAArgDescriptor, Reserved) +
            sizeof(NevercTargetVAArgDescriptor::Reserved);
        if (!validHeader(Descriptor.VAArg.Header, RequiredVAArg) ||
            (Descriptor.VAArg.Kind != NEVERC_ABI_VA_ARG_LLVM &&
             Descriptor.VAArg.Kind !=
                 NEVERC_ABI_VA_ARG_VOID_POINTER) ||
            (Descriptor.VAArg.Kind ==
                 NEVERC_ABI_VA_ARG_VOID_POINTER &&
             (Descriptor.VAArg.SlotSize == 0 ||
              Descriptor.VAArg.SlotAlignment == 0 ||
              Descriptor.VAArg.SlotSize !=
                  Descriptor.VAArg.SlotAlignment)))
          return createStringError(
              inconvertibleErrorCode(),
              "plugin '" + Registration.PluginID +
                  "' has an invalid target ABI va_arg descriptor");
      }
      for (const PluginTargetSnapshot::NamedRecord &Existing :
           Snapshot->ABIs)
        if (sameID(Existing.ID, Descriptor.ABIID))
          return createStringError(
              inconvertibleErrorCode(),
              "duplicate target ABI ID registered by plugins '" +
                  Existing.PluginID + "' and '" + Registration.PluginID +
                  "'");
      PluginTargetSnapshot::NamedRecord ABI;
      ABI.PluginID = Registration.PluginID.str();
      ABI.Owner = Registration.Owner;
      ABI.ID = Descriptor.ABIID;
      ABI.TargetID = Descriptor.TargetID;
      ABI.CanonicalName = std::move(*Name);
      ABI.Dependencies = std::move(*Dependencies);
      ABI.ClassifyFunction = Descriptor.ClassifyFunction;
      ABI.VAArg = Descriptor.VAArg;
      ABI.Flags = Descriptor.Flags;
      ABI.CallbackUserData = Descriptor.UserData;
      Snapshot->ABIs.push_back(std::move(ABI));
    }

    for (const NevercCallingConventionDescriptor &Descriptor :
         Registration.CallingConventions) {
      constexpr size_t Required =
          offsetof(NevercCallingConventionDescriptor, DestroyUserData) +
          sizeof(NevercCallingConventionDescriptor::DestroyUserData);
      if (!validHeader(Descriptor.Header, Required,
                       NEVERC_CALLING_CONVENTION_API_MAJOR,
                       NEVERC_CALLING_CONVENTION_API_MINOR) ||
          !nonzero(Descriptor.CallingConventionID) ||
          !nonzero(Descriptor.TargetID))
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' has an invalid calling convention descriptor");
      auto Name =
          copyString(Descriptor.CanonicalName, "calling convention name");
      auto CalleeSaved = copyStrings(
          Descriptor.CalleeSavedRegisters,
          "calling convention callee-saved register");
      if (!Name)
        return Name.takeError();
      if (!CalleeSaved)
        return CalleeSaved.takeError();
      if (Descriptor.Reserved != 0 ||
          Descriptor.LLVMCallingConvention > UINT8_MAX ||
          (Descriptor.PlanCallingConvention &&
           Descriptor.LLVMCallingConvention != 0))
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' has an invalid LLVM calling convention");
      for (const PluginTargetSnapshot::NamedRecord &Existing :
           Snapshot->CallingConventions)
        if (sameID(Existing.ID, Descriptor.CallingConventionID))
          return createStringError(
              inconvertibleErrorCode(),
              "duplicate calling convention ID registered by plugins '" +
                  Existing.PluginID + "' and '" + Registration.PluginID +
                  "'");
      PluginTargetSnapshot::NamedRecord CallingConvention;
      CallingConvention.PluginID = Registration.PluginID.str();
      CallingConvention.Owner = Registration.Owner;
      CallingConvention.ID = Descriptor.CallingConventionID;
      CallingConvention.TargetID = Descriptor.TargetID;
      CallingConvention.CanonicalName = std::move(*Name);
      CallingConvention.CalleeSavedRegisters =
          std::move(*CalleeSaved);
      CallingConvention.LLVMCallingConvention =
          Descriptor.LLVMCallingConvention;
      CallingConvention.PlanCallingConvention =
          Descriptor.PlanCallingConvention;
      CallingConvention.Flags = Descriptor.Flags;
      CallingConvention.CallbackUserData = Descriptor.UserData;
      Snapshot->CallingConventions.push_back(
          std::move(CallingConvention));
    }

    for (const NevercMCSchemaDescriptor &Descriptor :
         Registration.MCSchemas) {
      constexpr size_t Required =
          offsetof(NevercMCSchemaDescriptor, DestroyUserData) +
          sizeof(NevercMCSchemaDescriptor::DestroyUserData);
      if (!validHeader(Descriptor.Header, Required) ||
          !nonzero(Descriptor.SchemaID) || !nonzero(Descriptor.TargetID))
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' has an invalid MC schema descriptor");
      auto Name = copyString(Descriptor.CanonicalName, "MC schema name");
      auto Digest = copyString(Descriptor.Digest, "MC schema digest");
      auto Opcodes =
          copyMCSchemaValues(Descriptor.Opcodes, "MC opcode");
      auto Registers =
          copyMCSchemaValues(Descriptor.Registers, "MC register");
      auto OperandKinds = copyMCSchemaValues(
          Descriptor.OperandKinds, "MC operand kind");
      auto Relocations = copyMCSchemaValues(
          Descriptor.Relocations, "MC relocation");
      auto Variants =
          copyMCSchemaValues(Descriptor.Variants, "MC variant");
      if (!Name)
        return Name.takeError();
      if (!Digest)
        return Digest.takeError();
      if (!Opcodes)
        return Opcodes.takeError();
      if (!Registers)
        return Registers.takeError();
      if (!OperandKinds)
        return OperandKinds.takeError();
      if (!Relocations)
        return Relocations.takeError();
      if (!Variants)
        return Variants.takeError();
      if (!validDigest(*Digest))
        return createStringError(inconvertibleErrorCode(),
                                 "MC schema digest must be lowercase SHA-256");
      for (const PluginTargetSnapshot::NamedRecord &Existing :
           Snapshot->MCSchemas)
        if (sameID(Existing.ID, Descriptor.SchemaID))
          return createStringError(
              inconvertibleErrorCode(),
              "duplicate MC schema ID registered by plugins '" +
                  Existing.PluginID + "' and '" + Registration.PluginID +
                  "'");
      PluginTargetSnapshot::NamedRecord Schema;
      Schema.PluginID = Registration.PluginID.str();
      Schema.Owner = Registration.Owner;
      Schema.ID = Descriptor.SchemaID;
      Schema.TargetID = Descriptor.TargetID;
      Schema.CanonicalName = std::move(*Name);
      Schema.Digest = std::move(*Digest);
      Schema.Opcodes = std::move(*Opcodes);
      Schema.SchemaRegisters = std::move(*Registers);
      Schema.OperandKinds = std::move(*OperandKinds);
      Schema.Relocations = std::move(*Relocations);
      Schema.Variants = std::move(*Variants);
      Schema.Flags = Descriptor.Flags;
      Snapshot->MCSchemas.push_back(std::move(Schema));
    }
  }

  for (const PluginTargetRegistrationView &Registration : Registrations) {
    for (const NevercObjectFormatDescriptor &Descriptor :
         Registration.ObjectFormats) {
      constexpr size_t Required =
          offsetof(NevercObjectFormatDescriptor, DestroyUserData) +
          sizeof(NevercObjectFormatDescriptor::DestroyUserData);
      if (!validHeader(Descriptor.Header, Required) ||
          !nonzero(Descriptor.FormatID))
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' has an invalid object Format descriptor");
      constexpr uint64_t KnownFormatFlags =
          NEVERC_OBJECT_FORMAT_CAN_PROBE |
          NEVERC_OBJECT_FORMAT_CAN_READ |
          NEVERC_OBJECT_FORMAT_CAN_WRITE;
      if ((Descriptor.Flags & ~KnownFormatFlags) != 0 ||
          (((Descriptor.Flags & NEVERC_OBJECT_FORMAT_CAN_PROBE) != 0) !=
           (Descriptor.Probe != nullptr)) ||
          (((Descriptor.Flags & NEVERC_OBJECT_FORMAT_CAN_READ) != 0) !=
           (Descriptor.Reader != nullptr)) ||
          (((Descriptor.Flags & NEVERC_OBJECT_FORMAT_CAN_WRITE) != 0) !=
           (Descriptor.Writer != nullptr)) ||
          (Descriptor.Reader != nullptr && Descriptor.Probe == nullptr))
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' has inconsistent object Format capabilities");
      auto Name = copyString(Descriptor.CanonicalName, "object Format name");
      auto Aliases = copyStrings(Descriptor.Aliases, "object Format alias");
      auto Targets =
          copyIDs(Descriptor.SupportedTargets, "object Format target");
      auto Extension = copyString(Descriptor.DefaultExtension,
                                  "object Format extension", true);
      if (!Name)
        return Name.takeError();
      if (!Aliases)
        return Aliases.takeError();
      if (!Targets)
        return Targets.takeError();
      if (!Extension)
        return Extension.takeError();
      for (const PluginTargetSnapshot::ObjectFormatRecord &Existing :
           Snapshot->ObjectFormats)
        if (sameID(Existing.ID, Descriptor.FormatID))
          return createStringError(
              inconvertibleErrorCode(),
              "duplicate object Format ID registered by plugins '" +
                  Existing.PluginID + "' and '" + Registration.PluginID +
                  "'");
      PluginTargetSnapshot::ObjectFormatRecord Format;
      Format.PluginID = Registration.PluginID.str();
      Format.Owner = Registration.Owner;
      Format.ID = Descriptor.FormatID;
      Format.CanonicalName = std::move(*Name);
      Format.Aliases = std::move(*Aliases);
      Format.SupportedTargets.assign(Targets->begin(), Targets->end());
      Format.DefaultExtension = std::move(*Extension);
      Format.Flags = Descriptor.Flags;
      Format.Probe = Descriptor.Probe;
      Format.Reader = Descriptor.Reader;
      Format.Writer = Descriptor.Writer;
      Format.CallbackUserData = Descriptor.UserData;
      Snapshot->ObjectFormats.push_back(std::move(Format));
    }
  }

  for (const PluginTargetRegistrationView &Registration : Registrations) {
    for (const NevercCodeGenEdgeDescriptor &Descriptor :
         Registration.CodeGenEdges) {
      constexpr size_t Required =
          offsetof(NevercCodeGenEdgeDescriptor, DestroyUserData) +
          sizeof(NevercCodeGenEdgeDescriptor::DestroyUserData);
      if (!validHeader(Descriptor.Header, Required) ||
          !nonzero(Descriptor.EdgeID) || !nonzero(Descriptor.TargetID) ||
          !validProductKind(Descriptor.InputKind) ||
          !validProductKind(Descriptor.OutputKind) ||
          Descriptor.InputKind == Descriptor.OutputKind)
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' has an invalid codegen edge descriptor");
      auto Name = copyString(Descriptor.CanonicalName, "codegen edge name");
      auto Dependencies =
          copyIDs(Descriptor.Dependencies, "codegen edge dependency");
      auto CompatibilityKey = copyString(
          Descriptor.CompatibilityKey, "codegen compatibility key", true);
      auto ProviderID =
          copyString(Descriptor.ProviderID, "codegen provider ID", true);
      if (!Name)
        return Name.takeError();
      if (!Dependencies)
        return Dependencies.takeError();
      if (!CompatibilityKey)
        return CompatibilityKey.takeError();
      if (!ProviderID)
        return ProviderID.takeError();
      constexpr NevercCodeGenEdgeFlags KnownEdgeFlags =
          NEVERC_CODEGEN_EDGE_COARSE | NEVERC_CODEGEN_EDGE_BUILTIN;
      if ((Descriptor.Flags & ~KnownEdgeFlags) != 0 ||
          ((Descriptor.Flags & NEVERC_CODEGEN_EDGE_COARSE) != 0 &&
           (Descriptor.Flags & NEVERC_CODEGEN_EDGE_BUILTIN) != 0) ||
          (Descriptor.CoarseLower &&
           (Descriptor.Flags & NEVERC_CODEGEN_EDGE_COARSE) == 0))
        return createStringError(
            inconvertibleErrorCode(),
            "plugin '" + Registration.PluginID +
                "' has invalid codegen edge flags or callbacks");
      for (const PluginTargetSnapshot::CodeGenEdgeRecord &Existing :
           Snapshot->CodeGenEdges)
        if (sameID(Existing.ID, Descriptor.EdgeID))
          return createStringError(
              inconvertibleErrorCode(),
              "duplicate codegen edge ID registered by plugins '" +
                  Existing.PluginID + "' and '" + Registration.PluginID +
                  "'");
      PluginTargetSnapshot::CodeGenEdgeRecord Edge;
      Edge.PluginID = Registration.PluginID.str();
      Edge.Owner = Registration.Owner;
      Edge.ID = Descriptor.EdgeID;
      Edge.TargetID = Descriptor.TargetID;
      Edge.CanonicalName = std::move(*Name);
      Edge.Dependencies = std::move(*Dependencies);
      Edge.Flags = Descriptor.Flags;
      Edge.InputKind = Descriptor.InputKind;
      Edge.OutputKind = Descriptor.OutputKind;
      Edge.ProductID =
          nonzero(Descriptor.ProductID) ? Descriptor.ProductID
                                        : Descriptor.EdgeID;
      Edge.CompatibilityKey = std::move(*CompatibilityKey);
      Edge.ProviderID = ProviderID->empty() ? Edge.CanonicalName
                                            : std::move(*ProviderID);
      Edge.CoarseLower = Descriptor.CoarseLower;
      Edge.VerifyProduct = Descriptor.VerifyProduct;
      Edge.CallbackUserData = Descriptor.UserData;
      Snapshot->CodeGenEdges.push_back(std::move(Edge));
    }
  }

  const auto FindNamed =
      [](const std::vector<PluginTargetSnapshot::NamedRecord> &Records,
         NevercInterfaceID ID)
      -> const PluginTargetSnapshot::NamedRecord * {
    for (const PluginTargetSnapshot::NamedRecord &Record : Records)
      if (sameID(Record.ID, ID))
        return &Record;
    return nullptr;
  };
  const auto FindFormat =
      [&](NevercInterfaceID ID)
      -> const PluginTargetSnapshot::ObjectFormatRecord * {
    for (const PluginTargetSnapshot::ObjectFormatRecord &Format :
         Snapshot->ObjectFormats)
      if (sameID(Format.ID, ID))
        return &Format;
    return nullptr;
  };

  for (const PluginTargetSnapshot::NamedRecord &ABI : Snapshot->ABIs) {
    if (!Snapshot->findTarget(ABI.TargetID))
      return createStringError(
          inconvertibleErrorCode(),
          "target ABI '" + ABI.CanonicalName +
              "' references an unknown Target ID");
    for (NevercInterfaceID Dependency : ABI.Dependencies) {
      const auto *RequiredABI = FindNamed(Snapshot->ABIs, Dependency);
      if (!RequiredABI || !sameID(RequiredABI->TargetID, ABI.TargetID))
        return createStringError(
            inconvertibleErrorCode(),
            "target ABI '" + ABI.CanonicalName +
                "' has an unknown or cross-target dependency");
    }
  }
  for (const PluginTargetSnapshot::NamedRecord &CallingConvention :
       Snapshot->CallingConventions)
    if (!Snapshot->findTarget(CallingConvention.TargetID))
      return createStringError(
          inconvertibleErrorCode(),
          "calling convention '" + CallingConvention.CanonicalName +
              "' references an unknown Target ID");
  for (const PluginTargetSnapshot::NamedRecord &Schema :
       Snapshot->MCSchemas)
    if (!Snapshot->findTarget(Schema.TargetID))
      return createStringError(
          inconvertibleErrorCode(),
          "MC schema '" + Schema.CanonicalName +
              "' references an unknown Target ID");
  for (const PluginTargetSnapshot::ObjectFormatRecord &Format :
       Snapshot->ObjectFormats)
    for (NevercTargetID TargetID : Format.SupportedTargets)
      if (!Snapshot->findTarget(TargetID))
        return createStringError(
            inconvertibleErrorCode(),
            "object Format '" + Format.CanonicalName +
                "' references an unknown Target ID");

  for (const PluginTargetSnapshot::TargetRecord &Target : Snapshot->Targets) {
    for (NevercTargetABIID ABIID : Target.Machine.ABIs) {
      const auto *ABI = FindNamed(Snapshot->ABIs, ABIID);
      if (!ABI || !sameID(ABI->TargetID, Target.ID))
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' machine descriptor references an unknown or cross-target ABI");
    }
    for (NevercCallingConventionID CallingConventionID :
         Target.Machine.CallingConventions) {
      const auto *CallingConvention =
          FindNamed(Snapshot->CallingConventions, CallingConventionID);
      if (!CallingConvention ||
          !sameID(CallingConvention->TargetID, Target.ID))
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' machine descriptor references an unknown or cross-target calling convention");
    }
    for (NevercInterfaceID FormatID : Target.Machine.ObjectFormats) {
      const auto *Format = FindFormat(FormatID);
      if (!Format)
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' machine descriptor references an unknown object Format ID");
      if (!Format->SupportedTargets.empty() &&
          std::none_of(Format->SupportedTargets.begin(),
                       Format->SupportedTargets.end(),
                       [&](NevercTargetID ID) {
                         return sameID(ID, Target.ID);
                       }))
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' machine descriptor references a cross-target object Format");
    }
    if (nonzero(Target.DefaultABI)) {
      const auto *ABI = FindNamed(Snapshot->ABIs, Target.DefaultABI);
      if (!ABI || !sameID(ABI->TargetID, Target.ID))
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' references an unknown target ABI ID");
      if (!ABI->ClassifyFunction)
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' default ABI has no function classifier");
      if (std::none_of(Target.Machine.ABIs.begin(),
                       Target.Machine.ABIs.end(),
                       [&](NevercTargetABIID ID) {
                         return sameID(ID, Target.DefaultABI);
                       }))
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' default ABI is absent from its machine descriptor");
    }
    if (nonzero(Target.DefaultCallingConvention)) {
      const auto *CallingConvention =
          FindNamed(Snapshot->CallingConventions,
                    Target.DefaultCallingConvention);
      if (!CallingConvention ||
          !sameID(CallingConvention->TargetID, Target.ID))
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' references an unknown calling convention ID");
      if (std::none_of(
              Target.Machine.CallingConventions.begin(),
              Target.Machine.CallingConventions.end(),
              [&](NevercCallingConventionID ID) {
                return sameID(ID, Target.DefaultCallingConvention);
              }))
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' default calling convention is absent from its machine descriptor");
    }
    if (nonzero(Target.MCSchemaID)) {
      const auto *Schema =
          FindNamed(Snapshot->MCSchemas, Target.MCSchemaID);
      if (!Schema || !sameID(Schema->TargetID, Target.ID))
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' references an unknown MC schema ID");
    }
    if (nonzero(Target.DefaultObjectFormatID)) {
      const auto *Format = FindFormat(Target.DefaultObjectFormatID);
      if (!Format)
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' references an unknown object Format ID");
      if (std::none_of(Target.Machine.ObjectFormats.begin(),
                       Target.Machine.ObjectFormats.end(),
                       [&](NevercInterfaceID ID) {
                         return sameID(ID, Target.DefaultObjectFormatID);
                       }))
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' default object Format is absent from its machine descriptor");
      if (!Format->SupportedTargets.empty() &&
          std::none_of(Format->SupportedTargets.begin(),
                       Format->SupportedTargets.end(),
                       [&](NevercTargetID ID) {
                         return sameID(ID, Target.ID);
                       }))
        return createStringError(
            inconvertibleErrorCode(),
            "Target '" + Target.CanonicalName +
                "' is not supported by its default object Format");
    }
  }

  std::vector<size_t> InDegree(Snapshot->CodeGenEdges.size(), 0);
  std::vector<std::vector<size_t>> Successors(Snapshot->CodeGenEdges.size());
  for (size_t I = 0; I != Snapshot->CodeGenEdges.size(); ++I) {
    const PluginTargetSnapshot::CodeGenEdgeRecord &Edge =
        Snapshot->CodeGenEdges[I];
    if (!Snapshot->findTarget(Edge.TargetID))
      return createStringError(
          inconvertibleErrorCode(),
          "codegen edge '" + Edge.CanonicalName +
              "' references an unknown Target ID");
    for (NevercInterfaceID Dependency : Edge.Dependencies) {
      size_t DependencyIndex = Snapshot->CodeGenEdges.size();
      for (size_t J = 0; J != Snapshot->CodeGenEdges.size(); ++J)
        if (sameID(Snapshot->CodeGenEdges[J].ID, Dependency)) {
          DependencyIndex = J;
          break;
        }
      if (DependencyIndex == Snapshot->CodeGenEdges.size())
        return createStringError(
            inconvertibleErrorCode(),
            "codegen edge '" + Edge.CanonicalName +
                "' references an unknown dependency");
      if (!sameID(Snapshot->CodeGenEdges[DependencyIndex].TargetID,
                  Edge.TargetID))
        return createStringError(
            inconvertibleErrorCode(),
            "codegen edge '" + Edge.CanonicalName +
                "' has a cross-target dependency");
      ++InDegree[I];
      Successors[DependencyIndex].push_back(I);
    }
  }

  struct EdgeOrder {
    const std::vector<PluginTargetSnapshot::CodeGenEdgeRecord> *Edges;
    bool operator()(size_t Left, size_t Right) const {
      const auto &L = (*Edges)[Left];
      const auto &R = (*Edges)[Right];
      if (L.CanonicalName != R.CanonicalName)
        return L.CanonicalName < R.CanonicalName;
      if (L.ID.High != R.ID.High)
        return L.ID.High < R.ID.High;
      return L.ID.Low < R.ID.Low;
    }
  };
  std::set<size_t, EdgeOrder> Ready{
      EdgeOrder{&Snapshot->CodeGenEdges}};
  for (size_t I = 0; I != InDegree.size(); ++I)
    if (InDegree[I] == 0)
      Ready.insert(I);
  while (!Ready.empty()) {
    size_t Current = *Ready.begin();
    Ready.erase(Ready.begin());
    Snapshot->Route.push_back(&Snapshot->CodeGenEdges[Current]);
    for (size_t Successor : Successors[Current])
      if (--InDegree[Successor] == 0)
        Ready.insert(Successor);
  }
  if (Snapshot->Route.size() != Snapshot->CodeGenEdges.size()) {
    std::string Message = "codegen dependency cycle includes:";
    for (size_t I = 0; I != InDegree.size(); ++I)
      if (InDegree[I] != 0)
        Message += " " + Snapshot->CodeGenEdges[I].CanonicalName;
    return createStringError(inconvertibleErrorCode(), Message);
  }

  const bool HasRequest =
      hasLayerOptions(Request) ||
      hasLayerOptions(Request.Configuration) ||
      hasLayerOptions(Request.Platform);
  if (HasRequest) {
    std::string Selector;
    if (!Request.Triple.empty())
      Selector = Request.Triple;
    else if (!Request.Configuration.Triple.empty())
      Selector = Request.Configuration.Triple;
    else if (Snapshot->Targets.size() == 1)
      Selector = Snapshot->Targets.front().CanonicalName;
    else
      Selector = Request.Platform.Triple;
    if (Selector.empty())
      return createStringError(
          inconvertibleErrorCode(),
          "target unavailable: no target selector was provided");

    uint32_t BestPriority = 0;
    unsigned BestSpecificity = 0;
    bool Found = false;
    bool SelectedByName = false;
    const std::string RequestedName = normalizeComponent(Selector);
    for (const PluginTargetSnapshot::TargetRecord &Target :
         Snapshot->Targets) {
      const bool Named =
          normalizeComponent(Target.CanonicalName) == RequestedName ||
          normalizeComponent(Target.Machine.RawTriple) == RequestedName ||
          std::any_of(Target.Aliases.begin(), Target.Aliases.end(),
                      [&](const std::string &Alias) {
                        return Alias == RequestedName;
                      });
      if (Named) {
        Snapshot->SelectedTarget = &Target;
        BestPriority = std::numeric_limits<uint32_t>::max();
        BestSpecificity = std::numeric_limits<unsigned>::max();
        Found = true;
        SelectedByName = true;
        break;
      }
    }
    Expected<std::array<std::string, 4>> RequestedTriple =
        std::array<std::string, 4>{};
    if (!Found) {
      RequestedTriple = parseTriple(Selector);
      if (!RequestedTriple)
        return RequestedTriple.takeError();
    }
    for (const PluginTargetSnapshot::TargetRecord &Target :
         Snapshot->Targets) {
      if (SelectedByName)
        break;
      for (const PluginTargetSnapshot::TripleMatcher &Matcher :
           Target.Matchers) {
        if (!matcherAccepts(Matcher, *RequestedTriple))
          continue;
        const unsigned Specificity = matcherSpecificity(Matcher);
        if (Found &&
            (Matcher.Priority < BestPriority ||
             (Matcher.Priority == BestPriority &&
              Specificity <= BestSpecificity)))
          continue;
        Snapshot->SelectedTarget = &Target;
        BestPriority = Matcher.Priority;
        BestSpecificity = Specificity;
        Found = true;
      }
    }
    if (!Found)
      return createStringError(
          inconvertibleErrorCode(),
          "unknown target architecture or unavailable target for '" +
              Selector + "'");
    Snapshot->Route.erase(
        std::remove_if(
            Snapshot->Route.begin(), Snapshot->Route.end(),
            [&](const PluginTargetSnapshot::CodeGenEdgeRecord *Edge) {
              return !sameID(Edge->TargetID,
                             Snapshot->SelectedTarget->ID);
            }),
        Snapshot->Route.end());

    const PluginTargetSnapshot::TargetRecord &Target =
        *Snapshot->SelectedTarget;
    std::string RawTriple;
    std::array<std::string, 4> KeyComponents;
    if (!SelectedByName && !Request.Triple.empty()) {
      RawTriple = Request.Triple;
      auto Parsed = parseTriple(RawTriple);
      if (!Parsed)
        return Parsed.takeError();
      KeyComponents = std::move(*Parsed);
    } else if (!SelectedByName &&
               !Request.Configuration.Triple.empty()) {
      RawTriple = Request.Configuration.Triple;
      auto Parsed = parseTriple(RawTriple);
      if (!Parsed)
        return Parsed.takeError();
      KeyComponents = std::move(*Parsed);
    } else {
      RawTriple = Target.Machine.RawTriple;
      KeyComponents = {
          Target.Machine.Architecture, Target.Machine.Vendor,
          Target.Machine.OperatingSystem, Target.Machine.Environment};
    }

    const std::string CPU =
        !Request.CPU.empty()
            ? Request.CPU
            : (!Request.Configuration.CPU.empty()
                   ? Request.Configuration.CPU
                   : (!Target.Machine.DefaultCPU.empty()
                          ? Target.Machine.DefaultCPU
                          : Request.Platform.CPU));
    if (CPU.empty())
      return createStringError(
          inconvertibleErrorCode(),
          "target unavailable: no CPU was selected for '" +
              Target.CanonicalName + "'");
    if (!Target.Machine.CPUs.empty() &&
        !std::binary_search(Target.Machine.CPUs.begin(),
                            Target.Machine.CPUs.end(), CPU))
      return createStringError(
          inconvertibleErrorCode(),
          "unknown CPU '" + CPU + "' for Target '" +
              Target.CanonicalName + "'");

    auto Features = resolveFeatures(Target, Request);
    if (!Features)
      return Features.takeError();

    const auto ResolveFormat =
        [&](StringRef Name)
        -> Expected<const PluginTargetSnapshot::ObjectFormatRecord *> {
      const std::string Normalized = normalizeComponent(Name);
      for (const PluginTargetSnapshot::ObjectFormatRecord &Format :
           Snapshot->ObjectFormats) {
        const bool Matches =
            normalizeComponent(Format.CanonicalName) == Normalized ||
            std::any_of(
                Format.Aliases.begin(), Format.Aliases.end(),
                [&](const std::string &Alias) {
                  return normalizeComponent(Alias) == Normalized;
                });
        if (!Matches)
          continue;
        if (!Format.SupportedTargets.empty() &&
            std::none_of(
                Format.SupportedTargets.begin(),
                Format.SupportedTargets.end(),
                [&](NevercTargetID ID) {
                  return sameID(ID, Target.ID);
                }))
          return createStringError(
              inconvertibleErrorCode(),
              "incompatible object Format '" + Name +
                  "' for selected Target");
        return &Format;
      }
      return createStringError(
          inconvertibleErrorCode(),
          "unknown object Format '" + Name + "'");
    };

    NevercInterfaceID ObjectFormatID{};
    if (!Request.ObjectFormat.empty()) {
      auto Format = ResolveFormat(Request.ObjectFormat);
      if (!Format)
        return Format.takeError();
      ObjectFormatID = (*Format)->ID;
    } else if (!Request.Configuration.ObjectFormat.empty()) {
      auto Format = ResolveFormat(Request.Configuration.ObjectFormat);
      if (!Format)
        return Format.takeError();
      ObjectFormatID = (*Format)->ID;
    } else if (nonzero(Target.DefaultObjectFormatID)) {
      ObjectFormatID = Target.DefaultObjectFormatID;
    } else if (!Request.Platform.ObjectFormat.empty()) {
      auto Format = ResolveFormat(Request.Platform.ObjectFormat);
      if (!Format)
        return Format.takeError();
      ObjectFormatID = (*Format)->ID;
    }

    if (!nonzero(Target.DefaultABI) ||
        !nonzero(Target.DefaultCallingConvention) ||
        !nonzero(ObjectFormatID))
      return createStringError(
          inconvertibleErrorCode(),
          "target unavailable: selected Target lacks a complete "
          "ABI/calling-convention/object-Format route");

    const NevercTargetRelocationModel RelocationModel =
        Request.RelocationModel != 0
            ? Request.RelocationModel
            : (Request.Configuration.RelocationModel != 0
                   ? Request.Configuration.RelocationModel
                   : (Target.Machine.DefaultRelocationModel != 0
                          ? Target.Machine.DefaultRelocationModel
                          : Request.Platform.RelocationModel));
    const NevercTargetCodeModel CodeModel =
        Request.CodeModel != 0
            ? Request.CodeModel
            : (Request.Configuration.CodeModel != 0
                   ? Request.Configuration.CodeModel
                   : (Target.Machine.DefaultCodeModel != 0
                          ? Target.Machine.DefaultCodeModel
                          : Request.Platform.CodeModel));
    const NevercTargetExecutionLevel ExecutionLevel =
        Request.ExecutionLevel != 0
            ? Request.ExecutionLevel
            : (Request.Configuration.ExecutionLevel != 0
                   ? Request.Configuration.ExecutionLevel
                   : (Target.Machine.DefaultExecutionLevel != 0
                          ? Target.Machine.DefaultExecutionLevel
                          : Request.Platform.ExecutionLevel));
    const auto ModelBit = [](uint32_t Value) {
      return Value == 0 || Value > 64
                 ? UINT64_C(0)
                 : UINT64_C(1) << (Value - 1);
    };
    if ((Target.Machine.SupportedRelocationModels &
         ModelBit(RelocationModel)) == 0 ||
        (Target.Machine.SupportedCodeModels &
         ModelBit(CodeModel)) == 0 ||
        ExecutionLevel == 0 ||
        (ExecutionLevel & (ExecutionLevel - 1)) != 0 ||
        (Target.Machine.ExecutionLevels & ExecutionLevel) == 0)
      return createStringError(
          inconvertibleErrorCode(),
          "incompatible target relocation/code/execution model");

    TargetKeyBuilder KeyBuilder;
    KeyBuilder.setTargetID(Target.ID)
        .setTriple(std::move(RawTriple), std::move(KeyComponents[0]),
                   std::move(KeyComponents[1]),
                   std::move(KeyComponents[2]),
                   std::move(KeyComponents[3]))
        .setCPU(CPU, Target.Machine.TuneCPU)
        .setFeatures(std::move(*Features))
        .setABI(Target.DefaultABI)
        .setCallingConvention(Target.DefaultCallingConvention)
        .setObjectFormat(ObjectFormatID)
        .setCodeGeneration(RelocationModel, CodeModel)
        .setExecution(ExecutionLevel, Target.Machine.PointerWidth,
                      Target.Machine.Endianness)
        .setSchemaDigest(Target.Machine.SchemaDigest);
    auto Key = KeyBuilder.build();
    if (!Key)
      return joinErrors(
          createStringError(
              inconvertibleErrorCode(),
              "incompatible target options for '" +
                  Target.CanonicalName + "'"),
          Key.takeError());
    Snapshot->SelectedKey =
        std::make_unique<OwnedTargetKey>(std::move(*Key));
  }

  return std::shared_ptr<const PluginTargetSnapshot>(std::move(Snapshot));
}

Expected<std::shared_ptr<const PluginTargetSnapshot>>
PluginTargetRegistry::freeze(
    ArrayRef<std::shared_ptr<const PluginModule>> Modules,
    const PluginTargetRequest &Request) {
  struct MaterializedRegistration {
    std::string PluginID;
    std::shared_ptr<const PluginModule> Owner;
    std::vector<NevercTargetDescriptor> Targets;
    std::vector<std::vector<NevercStringView>> TargetAliases;
    std::vector<std::vector<NevercTargetTripleMatcher>> TargetMatchers;
    std::vector<NevercTargetABIDescriptor> ABIs;
    std::vector<NevercCallingConventionDescriptor> CallingConventions;
    std::vector<NevercMCSchemaDescriptor> MCSchemas;
    std::vector<NevercObjectFormatDescriptor> ObjectFormats;
    std::vector<std::vector<NevercStringView>> FormatAliases;
    std::vector<NevercCodeGenEdgeDescriptor> CodeGenEdges;
  };

  const auto StringView = [](const std::string &Value) {
    return NevercStringView{Value.data(),
                            static_cast<uint64_t>(Value.size())};
  };

  std::vector<MaterializedRegistration> Materialized;
  Materialized.reserve(Modules.size());
  for (const std::shared_ptr<const PluginModule> &Module : Modules) {
    const PluginPublishedRegistration *Published = Module->registration();
    if (!Published)
      continue;
    MaterializedRegistration Registration;
    Registration.PluginID = Module->descriptor().PluginID;
    Registration.Owner = Module;
    for (const PluginRegistrationRecord &Record : Published->records()) {
      switch (Record.Kind) {
      case PluginRegistrationKind::Target: {
        NevercTargetDescriptor Descriptor = Record.Target;
        Descriptor.CanonicalName = StringView(Record.CanonicalName);
        Registration.TargetAliases.emplace_back();
        for (const std::string &Alias : Record.Aliases)
          Registration.TargetAliases.back().push_back(StringView(Alias));
        Descriptor.Aliases = {
            Registration.TargetAliases.back().data(),
            static_cast<uint64_t>(
                Registration.TargetAliases.back().size()),
            sizeof(NevercStringView)};
        Registration.TargetMatchers.emplace_back();
        for (const OwnedTargetTripleMatcher &Owned :
             Record.TargetMatchers) {
          NevercTargetTripleMatcher Matcher{};
          Matcher.Header = {sizeof(Matcher), NEVERC_TARGET_API_MAJOR,
                            NEVERC_TARGET_API_MINOR, 0};
          Matcher.Architecture = StringView(Owned.Architecture);
          Matcher.Vendor = StringView(Owned.Vendor);
          Matcher.OperatingSystem = StringView(Owned.OperatingSystem);
          Matcher.Environment = StringView(Owned.Environment);
          Matcher.Priority = Owned.Priority;
          Registration.TargetMatchers.back().push_back(Matcher);
        }
        Descriptor.TripleMatchers = {
            Registration.TargetMatchers.back().data(),
            static_cast<uint64_t>(
                Registration.TargetMatchers.back().size()),
            sizeof(NevercTargetTripleMatcher)};
        Registration.Targets.push_back(Descriptor);
        break;
      }
      case PluginRegistrationKind::TargetABI: {
        NevercTargetABIDescriptor Descriptor = Record.TargetABI;
        Descriptor.CanonicalName = StringView(Record.CanonicalName);
        Descriptor.Dependencies = {
            Record.TargetReferences.data(),
            static_cast<uint64_t>(Record.TargetReferences.size()),
            sizeof(NevercInterfaceID)};
        Registration.ABIs.push_back(Descriptor);
        break;
      }
      case PluginRegistrationKind::CallingConvention: {
        NevercCallingConventionDescriptor Descriptor =
            Record.CallingConvention;
        Descriptor.CanonicalName = StringView(Record.CanonicalName);
        Registration.CallingConventions.push_back(Descriptor);
        break;
      }
      case PluginRegistrationKind::MCSchema: {
        NevercMCSchemaDescriptor Descriptor = Record.MCSchema;
        Descriptor.CanonicalName = StringView(Record.CanonicalName);
        Descriptor.Digest = StringView(Record.SchemaDigest);
        Registration.MCSchemas.push_back(Descriptor);
        break;
      }
      case PluginRegistrationKind::ObjectFormat: {
        NevercObjectFormatDescriptor Descriptor =
            Record.ObjectFormatDescriptor;
        Descriptor.CanonicalName = StringView(Record.CanonicalName);
        Registration.FormatAliases.emplace_back();
        for (const std::string &Alias : Record.Aliases)
          Registration.FormatAliases.back().push_back(StringView(Alias));
        Descriptor.Aliases = {
            Registration.FormatAliases.back().data(),
            static_cast<uint64_t>(
                Registration.FormatAliases.back().size()),
            sizeof(NevercStringView)};
        Descriptor.SupportedTargets = {
            Record.TargetReferences.data(),
            static_cast<uint64_t>(Record.TargetReferences.size()),
            sizeof(NevercInterfaceID)};
        Descriptor.DefaultExtension = StringView(Record.DefaultExtension);
        Registration.ObjectFormats.push_back(Descriptor);
        break;
      }
      case PluginRegistrationKind::CodeGenEdge: {
        NevercCodeGenEdgeDescriptor Descriptor = Record.CodeGenEdge;
        Descriptor.CanonicalName = StringView(Record.CanonicalName);
        Descriptor.CompatibilityKey =
            StringView(Record.CodeGenCompatibilityKey);
        Descriptor.ProviderID = StringView(Record.ProviderID);
        Descriptor.Dependencies = {
            Record.TargetReferences.data(),
            static_cast<uint64_t>(Record.TargetReferences.size()),
            sizeof(NevercInterfaceID)};
        Registration.CodeGenEdges.push_back(Descriptor);
        break;
      }
      case PluginRegistrationKind::Interface:
      case PluginRegistrationKind::Option:
      case PluginRegistrationKind::Phase:
      case PluginRegistrationKind::Observer:
      case PluginRegistrationKind::Interceptor:
      case PluginRegistrationKind::Provider:
      case PluginRegistrationKind::VFSProvider:
      case PluginRegistrationKind::IRPass:
      case PluginRegistrationKind::IRAnalysis:
      case PluginRegistrationKind::MIRPass:
      case PluginRegistrationKind::MCEncoder:
      case PluginRegistrationKind::MCDecoder:
      case PluginRegistrationKind::MCAsmBackend:
      case PluginRegistrationKind::LinkerProvider:
      case PluginRegistrationKind::ObjectMergeProvider:
      case PluginRegistrationKind::BinaryImageVerifier:
      case PluginRegistrationKind::LTOProvider:
        break;
      }
    }
    Materialized.push_back(std::move(Registration));
  }

  std::vector<PluginTargetRegistrationView> Views;
  Views.reserve(Materialized.size());
  for (const MaterializedRegistration &Registration : Materialized) {
    PluginTargetRegistrationView View;
    View.PluginID = Registration.PluginID;
    View.Owner = Registration.Owner;
    View.Targets = Registration.Targets;
    View.ABIs = Registration.ABIs;
    View.CallingConventions = Registration.CallingConventions;
    View.MCSchemas = Registration.MCSchemas;
    View.ObjectFormats = Registration.ObjectFormats;
    View.CodeGenEdges = Registration.CodeGenEdges;
    Views.push_back(std::move(View));
  }
  return freeze(Views, Request);
}

} // namespace neverc::plugin
