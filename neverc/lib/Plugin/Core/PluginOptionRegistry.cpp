#include "neverc/Plugin/Host/PluginOptionRegistry.h"
#include "neverc/Plugin/Host/PluginRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/JSON.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <set>
#include <unordered_set>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error optionError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

Expected<std::string> copyString(NevercStringView View, const Twine &Field,
                                 bool AllowEmpty) {
  if (View.Length > std::numeric_limits<size_t>::max())
    return optionError(Field + " is too large");
  if (!View.Data && View.Length != 0)
    return optionError(Field + " has null data");
  StringRef Text(View.Data ? View.Data : "", static_cast<size_t>(View.Length));
  if ((!AllowEmpty && Text.empty()) || Text.contains('\0') ||
      !json::isUTF8(Text))
    return optionError(Field + " is not a valid UTF-8 string");
  return Text.str();
}

Expected<std::vector<std::string>>
copyStringList(NevercStringList List, const Twine &Field) {
  std::vector<std::string> Result;
  if (List.Count == 0)
    return Result;
  if (!List.Data || List.ElementStride < sizeof(NevercStringView) ||
      List.Count >
          std::numeric_limits<size_t>::max() / List.ElementStride)
    return optionError(Field + " has an invalid array shape");
  Result.reserve(static_cast<size_t>(List.Count));
  const auto *Bytes = reinterpret_cast<const unsigned char *>(List.Data);
  for (uint64_t I = 0; I != List.Count; ++I) {
    const auto *Item =
        reinterpret_cast<const NevercStringView *>(Bytes +
                                                   I * List.ElementStride);
    auto Text = copyString(*Item, Field, false);
    if (!Text)
      return Text.takeError();
    Result.push_back(std::move(*Text));
  }
  return Result;
}

bool validBool(NevercBool Value) {
  return Value == NEVERC_FALSE || Value == NEVERC_TRUE;
}

bool isReservedBootstrapSpelling(StringRef Spelling) {
  return Spelling.starts_with("-fplugin");
}

StringRef normalizedOptionKey(StringRef Spelling) {
  while (Spelling.starts_with("-"))
    Spelling = Spelling.drop_front();
  if (Spelling.ends_with("="))
    Spelling = Spelling.drop_back();
  return Spelling;
}

bool optionAppliesToTarget(const OwnedPluginOption &Option,
                           StringRef TargetTriple) {
  return Option.TargetPredicate.empty() ||
         Option.TargetPredicate == TargetTriple;
}

Error validateValue(const OwnedPluginOption &Option, StringRef Value,
                    StringRef TargetTriple, uint64_t Occurrence) {
  switch (Option.ValueType) {
  case NEVERC_OPTION_BOOL:
    if (Value != "true" && Value != "false" && Value != "1" && Value != "0")
      return optionError("option '" + Option.Spelling +
                         "' requires a boolean value");
    break;
  case NEVERC_OPTION_INT: {
    int64_t Parsed = 0;
    if (Value.getAsInteger(0, Parsed))
      return optionError("option '" + Option.Spelling +
                         "' requires a signed integer");
    break;
  }
  case NEVERC_OPTION_UINT: {
    uint64_t Parsed = 0;
    if (Value.starts_with("-") || Value.getAsInteger(0, Parsed))
      return optionError("option '" + Option.Spelling +
                         "' requires an unsigned integer");
    break;
  }
  case NEVERC_OPTION_ENUM:
    if (!llvm::any_of(Option.EnumValues, [&](const OwnedOptionEnumValue &Item) {
          return Item.Name == Value;
        }))
      return optionError("option '" + Option.Spelling +
                         "' has an unknown enum value");
    break;
  case NEVERC_OPTION_STRING:
  case NEVERC_OPTION_PATH:
    if (Value.contains('\0'))
      return optionError("option '" + Option.Spelling +
                         "' contains an embedded NUL");
    break;
  default:
    return optionError("option '" + Option.Spelling +
                       "' has an invalid value type");
  }

  if (Option.Validator) {
    NevercOptionValidationContext Context{};
    Context.Header = {sizeof(Context), NEVERC_DRIVER_API_MAJOR,
                      NEVERC_DRIVER_API_MINOR, 0};
    Context.PluginID = {Option.PluginID.data(), Option.PluginID.size()};
    Context.Spelling = {Option.Spelling.data(), Option.Spelling.size()};
    Context.TargetTriple = {TargetTriple.data(), TargetTriple.size()};
    Context.Occurrence = Occurrence;
    NevercStringView ValueView{Value.data(), Value.size()};
    NevercStatus Status =
        Option.Validator(&Context, ValueView, Option.UserData);
    if (Status.Code != NEVERC_STATUS_OK || Status.Flags != 0 ||
        Status.Detail != 0)
      return optionError("validator rejected option '" + Option.Spelling + "'");
  }
  return Error::success();
}

} // namespace

Expected<OwnedPluginOption>
copyPluginOptionDescriptor(StringRef PluginID,
                           const NevercOptionDescriptor &Descriptor) {
  constexpr uint64_t Required =
      offsetof(NevercOptionDescriptor, DestroyUserData) +
      sizeof(NevercOptionDescriptor::DestroyUserData);
  if (!isCanonicalPluginID(PluginID))
    return optionError("plugin option has a non-canonical plugin ID");
  if (Descriptor.Header.StructSize < Required ||
      Descriptor.Header.Major != NEVERC_DRIVER_API_MAJOR ||
      Descriptor.Header.Minor > NEVERC_DRIVER_API_MINOR ||
      Descriptor.Header.Flags != 0)
    return optionError("plugin option descriptor has an invalid ABI header");
  if (Descriptor.Form > NEVERC_OPTION_MULTI_ARG ||
      Descriptor.ValueType > NEVERC_OPTION_PATH ||
      Descriptor.Multiplicity > NEVERC_OPTION_APPEND ||
      !validBool(Descriptor.Required) || !validBool(Descriptor.Hidden))
    return optionError("plugin option descriptor has an invalid discriminant");
  if (Descriptor.Form == NEVERC_OPTION_MULTI_ARG) {
    if (Descriptor.ArgumentCount == 0)
      return optionError("multi-argument plugin option has zero arity");
  } else if (Descriptor.ArgumentCount != 0) {
    return optionError("non-multi plugin option has a non-zero arity");
  }
  if (Descriptor.Form == NEVERC_OPTION_FLAG &&
      Descriptor.ValueType != NEVERC_OPTION_BOOL)
    return optionError("flag plugin option must have boolean value type");

  OwnedPluginOption Result;
  Result.PluginID = PluginID.str();
  auto Spelling = copyString(Descriptor.Spelling, "option spelling", false);
  if (!Spelling)
    return Spelling.takeError();
  Result.Spelling = std::move(*Spelling);
  if (!StringRef(Result.Spelling).starts_with("-"))
    return optionError("plugin option spelling must begin with '-'");
  auto Aliases = copyStringList(Descriptor.Aliases, "option alias");
  if (!Aliases)
    return Aliases.takeError();
  Result.Aliases = std::move(*Aliases);
  for (const std::string &Alias : Result.Aliases)
    if (!StringRef(Alias).starts_with("-"))
      return optionError("plugin option alias must begin with '-'");

  Result.Form = Descriptor.Form;
  Result.ValueType = Descriptor.ValueType;
  Result.Multiplicity = Descriptor.Multiplicity;
  Result.ArgumentCount = Descriptor.ArgumentCount;
  Result.Required = Descriptor.Required == NEVERC_TRUE;
  Result.Hidden = Descriptor.Hidden == NEVERC_TRUE;
  auto Help = copyString(Descriptor.Help, "option help", true);
  if (!Help)
    return Help.takeError();
  Result.Help = std::move(*Help);
  auto Metavar = copyString(Descriptor.Metavar, "option metavar", true);
  if (!Metavar)
    return Metavar.takeError();
  Result.Metavar = std::move(*Metavar);
  auto Target =
      copyString(Descriptor.TargetPredicate, "target predicate", true);
  if (!Target)
    return Target.takeError();
  Result.TargetPredicate = std::move(*Target);

  if (Descriptor.EnumValues.Count != 0) {
    constexpr uint64_t EnumRequired =
        offsetof(NevercOptionEnumValue, Help) +
        sizeof(NevercOptionEnumValue::Help);
    if (!Descriptor.EnumValues.Data ||
        Descriptor.EnumValues.ElementStride < EnumRequired ||
        Descriptor.EnumValues.Count >
            std::numeric_limits<size_t>::max() /
                Descriptor.EnumValues.ElementStride)
      return optionError("option enum values have an invalid array shape");
    const auto *Bytes = static_cast<const unsigned char *>(
        Descriptor.EnumValues.Data);
    for (uint64_t I = 0; I != Descriptor.EnumValues.Count; ++I) {
      const auto *Item = reinterpret_cast<const NevercOptionEnumValue *>(
          Bytes + I * Descriptor.EnumValues.ElementStride);
      if (Item->Header.StructSize < EnumRequired ||
          Item->Header.Major != NEVERC_DRIVER_API_MAJOR ||
          Item->Header.Minor > NEVERC_DRIVER_API_MINOR ||
          Item->Header.Flags != 0)
        return optionError("option enum value has an invalid ABI header");
      auto Name = copyString(Item->Name, "option enum name", false);
      if (!Name)
        return Name.takeError();
      auto ItemHelp = copyString(Item->Help, "option enum help", true);
      if (!ItemHelp)
        return ItemHelp.takeError();
      if (llvm::any_of(Result.EnumValues,
                       [&](const OwnedOptionEnumValue &Existing) {
                         return Existing.Name == *Name;
                       }))
        return optionError("option enum contains a duplicate name");
      Result.EnumValues.push_back(
          {std::move(*Name), Item->Value, std::move(*ItemHelp)});
    }
  }
  if (Result.ValueType == NEVERC_OPTION_ENUM && Result.EnumValues.empty())
    return optionError("enum plugin option has no values");

  auto Conflicts = copyStringList(Descriptor.Conflicts, "option conflict");
  if (!Conflicts)
    return Conflicts.takeError();
  Result.Conflicts = std::move(*Conflicts);
  auto Requires = copyStringList(Descriptor.Requires, "option requirement");
  if (!Requires)
    return Requires.takeError();
  Result.Requires = std::move(*Requires);
  Result.Validator = Descriptor.Validator;
  Result.UserData = Descriptor.UserData;
  return Result;
}

const ParsedPluginOption *
PluginOptionParseResult::find(StringRef PluginID, StringRef Spelling) const {
  auto It = llvm::find_if(Options, [&](const ParsedPluginOption &Option) {
    return Option.PluginID == PluginID && Option.Spelling == Spelling;
  });
  return It == Options.end() ? nullptr : &*It;
}

PluginOptionRegistry::PluginOptionRegistry(
    ArrayRef<StringRef> StaticSpellingsValue) {
  StaticSpellings.reserve(StaticSpellingsValue.size());
  for (StringRef Spelling : StaticSpellingsValue)
    StaticSpellings.push_back(Spelling.str());
}

Error PluginOptionRegistry::registerBatch(
    std::vector<OwnedPluginOption> Options) {
  if (Frozen)
    return optionError("plugin option registry is frozen");

  std::unordered_set<std::string> PendingSpellings;
  for (const OwnedPluginOption &Option : Options) {
    SmallVector<StringRef, 4> Spellings;
    Spellings.push_back(Option.Spelling);
    for (const std::string &Alias : Option.Aliases)
      Spellings.push_back(Alias);
    for (StringRef Spelling : Spellings) {
      if (isReservedBootstrapSpelling(Spelling))
        return optionError("plugin option spelling '" + Spelling +
                           "' uses a reserved bootstrap prefix");
      if (llvm::is_contained(StaticSpellings, Spelling))
        return optionError("plugin option spelling '" + Spelling +
                           "' conflicts with a static NeverC option");
      if (SpellingIndex.count(Spelling.str()) != 0 ||
          !PendingSpellings.insert(Spelling.str()).second)
        return optionError("plugin option spelling conflict for '" + Spelling +
                           "'");
    }
  }

  Registered.reserve(Registered.size() + Options.size());
  for (OwnedPluginOption &Option : Options)
    Registered.push_back(std::move(Option));
  return rebuildIndex();
}

Error PluginOptionRegistry::freeze() {
  if (Frozen)
    return Error::success();
  for (const OwnedPluginOption &Option : Registered) {
    for (const std::string &Related :
         llvm::concat<const std::string>(Option.Conflicts, Option.Requires)) {
      auto It = SpellingIndex.find(Related);
      if (It == SpellingIndex.end() ||
          Registered[It->second].PluginID != Option.PluginID)
        return optionError("option '" + Option.Spelling +
                           "' references unknown related option '" + Related +
                           "'");
    }
  }
  Frozen = true;
  return Error::success();
}

Error PluginOptionRegistry::removePlugin(StringRef PluginID) {
  Registered.erase(
      std::remove_if(Registered.begin(), Registered.end(),
                     [&](const OwnedPluginOption &Option) {
                       return Option.PluginID == PluginID;
                     }),
      Registered.end());
  return rebuildIndex();
}

Error PluginOptionRegistry::rebuildIndex() {
  SpellingIndex.clear();
  for (size_t I = 0; I != Registered.size(); ++I) {
    if (!SpellingIndex.emplace(Registered[I].Spelling, I).second)
      return optionError("duplicate plugin option spelling");
    for (const std::string &Alias : Registered[I].Aliases)
      if (!SpellingIndex.emplace(Alias, I).second)
        return optionError("duplicate plugin option alias");
  }
  return Error::success();
}

Expected<PluginOptionParseResult>
PluginOptionRegistry::parse(ArrayRef<StringRef> Arguments,
                            StringRef TargetTriple) const {
  if (!Frozen)
    return optionError("plugin option registry is not frozen");

  PluginOptionParseResult Result;
  std::unordered_map<size_t, size_t> ParsedByOption;
  std::set<std::string> ActivePluginIDs;
  for (const OwnedPluginOption &Option : Registered)
    ActivePluginIDs.insert(Option.PluginID);

  auto addOccurrence = [&](size_t OptionIndex, ArrayRef<StringRef> Values,
                           uint64_t ArgumentIndex) -> Error {
    const OwnedPluginOption &Option = Registered[OptionIndex];
    if (!optionAppliesToTarget(Option, TargetTriple))
      return optionError("option '" + Option.Spelling +
                         "' is unavailable for target '" + TargetTriple + "'");
    for (StringRef Value : Values)
      if (Error E =
              validateValue(Option, Value, TargetTriple, ArgumentIndex))
        return std::move(E);

    auto Existing = ParsedByOption.find(OptionIndex);
    if (Existing != ParsedByOption.end() &&
        Option.Multiplicity == NEVERC_OPTION_SINGLE)
      return optionError("option '" + Option.Spelling +
                         "' may only appear once");

    if (Existing == ParsedByOption.end()) {
      ParsedPluginOption Parsed;
      Parsed.PluginID = Option.PluginID;
      Parsed.Spelling = Option.Spelling;
      for (StringRef Value : Values)
        Parsed.Values.push_back(Value.str());
      Parsed.ArgumentIndices.push_back(ArgumentIndex);
      ParsedByOption.emplace(OptionIndex, Result.Options.size());
      Result.Options.push_back(std::move(Parsed));
      return Error::success();
    }

    ParsedPluginOption &Parsed = Result.Options[Existing->second];
    if (Option.Multiplicity == NEVERC_OPTION_LAST_WINS) {
      Parsed.Values.clear();
      Parsed.ArgumentIndices.clear();
    }
    for (StringRef Value : Values)
      Parsed.Values.push_back(Value.str());
    Parsed.ArgumentIndices.push_back(ArgumentIndex);
    return Error::success();
  };

  auto findNamespaced = [&](StringRef PluginID,
                            StringRef Key) -> std::optional<size_t> {
    for (size_t I = 0; I != Registered.size(); ++I) {
      const OwnedPluginOption &Option = Registered[I];
      if (Option.PluginID != PluginID)
        continue;
      if (normalizedOptionKey(Option.Spelling) == Key)
        return I;
      for (const std::string &Alias : Option.Aliases)
        if (normalizedOptionKey(Alias) == Key)
          return I;
    }
    return std::nullopt;
  };

  for (size_t I = 0; I != Arguments.size(); ++I) {
    StringRef Argument = Arguments[I];
    if (Argument.starts_with("-fplugin-arg=")) {
      StringRef Payload = Argument.drop_front(strlen("-fplugin-arg="));
      auto PluginAndValue = Payload.split(':');
      StringRef PluginID;
      StringRef KeyValue;
      if (!PluginAndValue.second.empty()) {
        PluginID = PluginAndValue.first;
        KeyValue = PluginAndValue.second;
      } else {
        if (ActivePluginIDs.size() != 1)
          return optionError(
              "unqualified -fplugin-arg requires an explicit plugin ID");
        PluginID = *ActivePluginIDs.begin();
        KeyValue = PluginAndValue.first;
      }
      auto Split = KeyValue.split('=');
      StringRef Key = Split.first;
      StringRef Value = Split.second;
      auto OptionIndex = findNamespaced(PluginID, Key);
      if (!OptionIndex)
        return optionError("unknown namespaced plugin option '" + Key + "'");
      const OwnedPluginOption &Option = Registered[*OptionIndex];
      if (Option.Form == NEVERC_OPTION_MULTI_ARG)
        return optionError(
            "multi-argument option cannot use -fplugin-arg syntax");
      if (Option.Form == NEVERC_OPTION_FLAG && Split.second.empty())
        Value = "true";
      else if (Split.second.empty())
        return optionError("namespaced plugin option requires '=value'");
      if (Error E = addOccurrence(*OptionIndex, Value, I))
        return std::move(E);
      continue;
    }

    std::optional<size_t> OptionIndex;
    StringRef JoinedValue;
    auto Exact = SpellingIndex.find(Argument.str());
    if (Exact != SpellingIndex.end())
      OptionIndex = Exact->second;
    if (!OptionIndex) {
      size_t BestLength = 0;
      for (const auto &Entry : SpellingIndex) {
        const OwnedPluginOption &Option = Registered[Entry.second];
        if (Option.Form != NEVERC_OPTION_JOINED ||
            !Argument.starts_with(Entry.first) ||
            Entry.first.size() <= BestLength)
          continue;
        OptionIndex = Entry.second;
        BestLength = Entry.first.size();
        JoinedValue = Argument.drop_front(BestLength);
      }
    }
    if (!OptionIndex) {
      Result.RemainingArguments.push_back(Argument.str());
      Result.RemainingArgumentIndices.push_back(I);
      continue;
    }

    const OwnedPluginOption &Option = Registered[*OptionIndex];
    SmallVector<StringRef, 4> Values;
    switch (Option.Form) {
    case NEVERC_OPTION_FLAG:
      Values.push_back("true");
      break;
    case NEVERC_OPTION_JOINED:
      if (JoinedValue.empty())
        return optionError("joined plugin option '" + Option.Spelling +
                           "' has no value");
      Values.push_back(JoinedValue);
      break;
    case NEVERC_OPTION_SEPARATE:
      if (I + 1 >= Arguments.size())
        return optionError("plugin option '" + Option.Spelling +
                           "' is missing its value");
      Values.push_back(Arguments[++I]);
      break;
    case NEVERC_OPTION_MULTI_ARG:
      if (Option.ArgumentCount > Arguments.size() - I - 1)
        return optionError("plugin option '" + Option.Spelling +
                           "' is missing arguments");
      for (uint32_t N = 0; N != Option.ArgumentCount; ++N)
        Values.push_back(Arguments[++I]);
      break;
    default:
      return optionError("plugin option has an invalid form");
    }
    if (Error E = addOccurrence(*OptionIndex, Values, I))
      return std::move(E);
  }

  for (size_t I = 0; I != Registered.size(); ++I) {
    const OwnedPluginOption &Option = Registered[I];
    bool Present = ParsedByOption.count(I) != 0;
    if (Option.Required && optionAppliesToTarget(Option, TargetTriple) &&
        !Present)
      return optionError("required plugin option '" + Option.Spelling +
                         "' is missing");
    if (!Present)
      continue;
    for (const std::string &Conflict : Option.Conflicts) {
      auto It = SpellingIndex.find(Conflict);
      if (It != SpellingIndex.end() && ParsedByOption.count(It->second) != 0)
        return optionError("plugin option '" + Option.Spelling +
                           "' conflicts with '" + Conflict + "'");
    }
    for (const std::string &Requirement : Option.Requires) {
      auto It = SpellingIndex.find(Requirement);
      if (It == SpellingIndex.end() ||
          ParsedByOption.count(It->second) == 0)
        return optionError("plugin option '" + Option.Spelling +
                           "' requires '" + Requirement + "'");
    }
  }
  return Result;
}

} // namespace neverc::plugin
