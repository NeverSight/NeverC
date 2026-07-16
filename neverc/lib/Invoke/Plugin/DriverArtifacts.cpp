#include "Plugin/DriverArtifacts.h"
#include "neverc/Plugin/PluginPhaseSchema.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/JSON.h"
#include <limits>
#include <new>
#include <utility>

using namespace llvm;

namespace neverc::driver {
namespace {

constexpr uint64_t MaximumArgumentBytes = UINT64_C(1) << 20;
constexpr uint64_t MaximumArgumentStreamBytes = UINT64_C(1) << 26;

Error argumentError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

Error verifyTokensImpl(ArrayRef<DriverArgumentToken> Tokens) {
  if (Tokens.empty())
    return argumentError("raw argument artifact has no program name");
  uint64_t TotalBytes = 0;
  unsigned EndOfOptionsCount = 0;
  for (size_t Index = 0; Index != Tokens.size(); ++Index) {
    const DriverArgumentToken &Token = Tokens[Index];
    if (Token.Value.size() > MaximumArgumentBytes ||
        !json::isUTF8(Token.Value) || StringRef(Token.Value).contains('\0'))
      return argumentError("raw argument token is not valid bounded UTF-8");
    if (Token.Source.size() > MaximumArgumentBytes ||
        !json::isUTF8(Token.Source) || StringRef(Token.Source).contains('\0'))
      return argumentError("raw argument origin is not valid bounded UTF-8");
    if (TotalBytes > MaximumArgumentStreamBytes - Token.Value.size())
      return argumentError("raw argument stream exceeds the size limit");
    TotalBytes += Token.Value.size();
    if (Token.Origin != NEVERC_ARGUMENT_ORIGIN_COMMAND_LINE &&
        Token.Origin != NEVERC_ARGUMENT_ORIGIN_CONFIGURATION &&
        Token.Origin != NEVERC_ARGUMENT_ORIGIN_PLUGIN)
      return argumentError("raw argument token has an invalid origin");
    if (Token.EndOfOptions) {
      ++EndOfOptionsCount;
      if (Token.Value != "--")
        return argumentError("raw argument end-of-options marker changed");
    } else if (Token.Value == "--") {
      return argumentError("raw argument mutation forged '--'");
    }
    if (Index != 0 && isProtectedDriverBootstrapArgument(Token.Value) &&
        !Token.Protected)
      return argumentError(
          "raw argument mutation forged a plugin bootstrap token");
  }
  if (Tokens.front().Value.empty())
    return argumentError("raw argument program name is empty");
  if (Tokens.front().Origin != NEVERC_ARGUMENT_ORIGIN_COMMAND_LINE)
    return argumentError("raw argument program name has an invalid origin");
  if (Tokens.front().Protected == false)
    return argumentError("raw argument program name must be protected");
  if (EndOfOptionsCount > 1)
    return argumentError("raw argument stream has multiple '--' markers");
  return Error::success();
}

} // namespace

DriverRawArgumentsArtifact::DriverRawArgumentsArtifact(
    std::vector<DriverArgumentToken> TokensValue)
    : Tokens(std::move(TokensValue)) {}

DriverRawArgumentsArtifact::DriverRawArgumentsArtifact(
    const DriverRawArgumentsArtifact &Other) {
  std::lock_guard<std::mutex> Lock(Other.Mutex);
  Tokens = Other.Tokens;
}

Error DriverRawArgumentsArtifact::verify() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  return verifyTokensImpl(Tokens);
}

Expected<std::vector<DriverArgumentToken>>
DriverRawArgumentsArtifact::beginMutation() {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (MutationActive)
    return argumentError("raw argument artifact already has a mutation");
  MutationActive = true;
  return Tokens;
}

Error DriverRawArgumentsArtifact::commitMutation(
    std::vector<DriverArgumentToken> NewTokens) {
  if (Error E = verifyTokensImpl(NewTokens))
    return E;
  std::lock_guard<std::mutex> Lock(Mutex);
  if (!MutationActive)
    return argumentError("raw argument mutation is no longer active");
  Tokens = std::move(NewTokens);
  MutationActive = false;
  return Error::success();
}

void DriverRawArgumentsArtifact::abortMutation() {
  std::lock_guard<std::mutex> Lock(Mutex);
  MutationActive = false;
}

Expected<std::unique_ptr<DriverArgumentMutation>>
DriverArgumentMutation::create(DriverRawArgumentsArtifact &Target) {
  auto Tokens = Target.beginMutation();
  if (!Tokens)
    return Tokens.takeError();
  auto *Mutation =
      new (std::nothrow) DriverArgumentMutation(Target, std::move(*Tokens));
  if (!Mutation) {
    Target.abortMutation();
    return argumentError("cannot allocate a raw argument mutation");
  }
  return std::unique_ptr<DriverArgumentMutation>(Mutation);
}

DriverArgumentMutation::DriverArgumentMutation(
    DriverRawArgumentsArtifact &TargetValue,
    std::vector<DriverArgumentToken> TokensValue)
    : Target(TargetValue), Tokens(std::move(TokensValue)) {}

DriverArgumentMutation::~DriverArgumentMutation() { abort(); }

Error DriverArgumentMutation::insert(uint64_t Index, StringRef Value) {
  if (Finished)
    return argumentError("raw argument mutation is finished");
  if (Index == 0 || Index > Tokens.size() ||
      Index > std::numeric_limits<size_t>::max())
    return argumentError("raw argument insertion index is invalid");
  if (Value == "--" || isProtectedDriverBootstrapArgument(Value))
    return argumentError(
        "raw argument mutation cannot insert a reserved token");
  DriverArgumentToken Token;
  Token.Value = Value.str();
  Token.Origin = NEVERC_ARGUMENT_ORIGIN_PLUGIN;
  Token.Source = "plugin";
  Tokens.insert(Tokens.begin() + static_cast<size_t>(Index), std::move(Token));
  return Error::success();
}

Error DriverArgumentMutation::replace(uint64_t Index, StringRef Value) {
  if (Finished)
    return argumentError("raw argument mutation is finished");
  if (Index >= Tokens.size())
    return argumentError("raw argument replacement index is invalid");
  DriverArgumentToken &Token = Tokens[static_cast<size_t>(Index)];
  if (Index == 0 || Token.Protected || Token.EndOfOptions)
    return argumentError("raw argument mutation cannot replace this token");
  if (Value == "--" || isProtectedDriverBootstrapArgument(Value))
    return argumentError(
        "raw argument mutation cannot replace with a reserved token");
  Token.Value = Value.str();
  Token.Origin = NEVERC_ARGUMENT_ORIGIN_PLUGIN;
  Token.Source = "plugin";
  Token.Position = 0;
  return Error::success();
}

Error DriverArgumentMutation::erase(uint64_t Index) {
  if (Finished)
    return argumentError("raw argument mutation is finished");
  if (Index >= Tokens.size())
    return argumentError("raw argument erase index is invalid");
  const DriverArgumentToken &Token = Tokens[static_cast<size_t>(Index)];
  if (Index == 0 || Token.Protected || Token.EndOfOptions)
    return argumentError("raw argument mutation cannot erase this token");
  Tokens.erase(Tokens.begin() + static_cast<size_t>(Index));
  return Error::success();
}

Error DriverArgumentMutation::commit() {
  if (Finished)
    return argumentError("raw argument mutation is finished");
  if (Error E = Target.commitMutation(std::move(Tokens)))
    return E;
  Finished = true;
  return Error::success();
}

void DriverArgumentMutation::abort() {
  if (Finished)
    return;
  Target.abortMutation();
  Finished = true;
}

DriverParsedOptionOccurrence::DriverParsedOptionOccurrence(
    const DriverParsedOptionOccurrence &Other)
    : ID(Other.ID), Spelling(Other.Spelling), Values(Other.Values),
      Origin(Other.Origin), Start(Other.Start), End(Other.End) {
  rebuildValueViews();
}

DriverParsedOptionOccurrence &DriverParsedOptionOccurrence::operator=(
    const DriverParsedOptionOccurrence &Other) {
  if (this == &Other)
    return *this;
  ID = Other.ID;
  Spelling = Other.Spelling;
  Values = Other.Values;
  Origin = Other.Origin;
  Start = Other.Start;
  End = Other.End;
  rebuildValueViews();
  return *this;
}

DriverParsedOptionOccurrence::DriverParsedOptionOccurrence(
    DriverParsedOptionOccurrence &&Other) noexcept
    : ID(Other.ID), Spelling(std::move(Other.Spelling)),
      Values(std::move(Other.Values)), Origin(Other.Origin), Start(Other.Start),
      End(Other.End) {
  rebuildValueViews();
}

DriverParsedOptionOccurrence &DriverParsedOptionOccurrence::operator=(
    DriverParsedOptionOccurrence &&Other) noexcept {
  if (this == &Other)
    return *this;
  ID = Other.ID;
  Spelling = std::move(Other.Spelling);
  Values = std::move(Other.Values);
  Origin = Other.Origin;
  Start = Other.Start;
  End = Other.End;
  rebuildValueViews();
  return *this;
}

void DriverParsedOptionOccurrence::rebuildValueViews() {
  ValueViews.clear();
  ValueViews.reserve(Values.size());
  for (const std::string &Value : Values)
    ValueViews.push_back({Value.data(), Value.size()});
}

DriverParsedArgumentsArtifact::DriverParsedArgumentsArtifact(
    std::vector<DriverArgumentToken> TokensValue,
    std::vector<DriverParsedOptionOccurrence> OccurrencesValue,
    RenderOption RenderValue)
    : Tokens(std::move(TokensValue)), Occurrences(std::move(OccurrencesValue)),
      Render(std::move(RenderValue)) {
  for (DriverParsedOptionOccurrence &Occurrence : Occurrences)
    Occurrence.rebuildValueViews();
}

DriverParsedArgumentsArtifact::DriverParsedArgumentsArtifact(
    const DriverParsedArgumentsArtifact &Other) {
  std::lock_guard<std::mutex> Lock(Other.Mutex);
  Tokens = Other.Tokens;
  Occurrences = Other.Occurrences;
  Render = Other.Render;
}

Error DriverParsedArgumentsArtifact::verify() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (Error E = verifyTokensImpl(Tokens))
    return E;
  if (!Render)
    return argumentError("parsed argument artifact has no option renderer");
  size_t PreviousEnd = 0;
  for (size_t Index = 0; Index != Occurrences.size(); ++Index) {
    const DriverParsedOptionOccurrence &Occurrence = Occurrences[Index];
    if (Occurrence.ID == 0 || Occurrence.Spelling.empty() ||
        !json::isUTF8(Occurrence.Spelling) ||
        StringRef(Occurrence.Spelling).contains('\0'))
      return argumentError("parsed option occurrence identity is invalid");
    for (size_t Other = 0; Other != Index; ++Other)
      if (Occurrences[Other].ID == Occurrence.ID)
        return argumentError("parsed option occurrence ID is duplicated");
    if (Occurrence.Start >= Occurrence.End || Occurrence.End > Tokens.size() ||
        (Index != 0 && Occurrence.Start < PreviousEnd))
      return argumentError("parsed option occurrence range is invalid");
    if (Occurrence.Origin != NEVERC_ARGUMENT_ORIGIN_COMMAND_LINE &&
        Occurrence.Origin != NEVERC_ARGUMENT_ORIGIN_CONFIGURATION &&
        Occurrence.Origin != NEVERC_ARGUMENT_ORIGIN_PLUGIN)
      return argumentError("parsed option occurrence origin is invalid");
    for (const std::string &Value : Occurrence.Values)
      if (Value.size() > MaximumArgumentBytes || !json::isUTF8(Value) ||
          StringRef(Value).contains('\0'))
        return argumentError("parsed option value is not valid UTF-8");
    PreviousEnd = Occurrence.End;
  }
  return Error::success();
}

Expected<DriverParsedArgumentsArtifact::MutationSnapshot>
DriverParsedArgumentsArtifact::beginMutation() {
  std::lock_guard<std::mutex> Lock(Mutex);
  if (MutationActive)
    return argumentError("parsed argument artifact already has a mutation");
  MutationActive = true;
  return MutationSnapshot{Tokens, Occurrences, Render};
}

Error DriverParsedArgumentsArtifact::commitMutation(MutationSnapshot Snapshot) {
  DriverParsedArgumentsArtifact Candidate(Snapshot.Tokens, Snapshot.Occurrences,
                                          Snapshot.Render);
  if (Error E = Candidate.verify())
    return E;
  std::lock_guard<std::mutex> Lock(Mutex);
  if (!MutationActive)
    return argumentError("parsed argument mutation is no longer active");
  Tokens = std::move(Snapshot.Tokens);
  Occurrences = std::move(Snapshot.Occurrences);
  Render = std::move(Snapshot.Render);
  for (DriverParsedOptionOccurrence &Occurrence : Occurrences)
    Occurrence.rebuildValueViews();
  MutationActive = false;
  return Error::success();
}

void DriverParsedArgumentsArtifact::abortMutation() {
  std::lock_guard<std::mutex> Lock(Mutex);
  MutationActive = false;
}

Expected<std::unique_ptr<DriverParsedArgumentMutation>>
DriverParsedArgumentMutation::create(DriverParsedArgumentsArtifact &Target) {
  auto Snapshot = Target.beginMutation();
  if (!Snapshot)
    return Snapshot.takeError();
  auto *Mutation = new (std::nothrow)
      DriverParsedArgumentMutation(Target, std::move(*Snapshot));
  if (!Mutation) {
    Target.abortMutation();
    return argumentError("cannot allocate a parsed argument mutation");
  }
  return std::unique_ptr<DriverParsedArgumentMutation>(Mutation);
}

DriverParsedArgumentMutation::DriverParsedArgumentMutation(
    DriverParsedArgumentsArtifact &TargetValue,
    DriverParsedArgumentsArtifact::MutationSnapshot SnapshotValue)
    : Target(TargetValue), Snapshot(std::move(SnapshotValue)) {}

DriverParsedArgumentMutation::~DriverParsedArgumentMutation() { abort(); }

DriverParsedOptionOccurrence *
DriverParsedArgumentMutation::find(uint64_t Occurrence) {
  auto It = llvm::find_if(Snapshot.Occurrences,
                          [&](const DriverParsedOptionOccurrence &Candidate) {
                            return Candidate.ID == Occurrence;
                          });
  return It == Snapshot.Occurrences.end() ? nullptr : &*It;
}

void DriverParsedArgumentMutation::adjustRanges(
    size_t Start, size_t End, size_t NewSize,
    DriverParsedOptionOccurrence *EditedOccurrence) {
  const size_t OldSize = End - Start;
  for (DriverParsedOptionOccurrence &Occurrence : Snapshot.Occurrences) {
    if (&Occurrence == EditedOccurrence) {
      Occurrence.Start = Start;
      Occurrence.End = Start + NewSize;
      continue;
    }
    if (Occurrence.Start >= End) {
      if (NewSize >= OldSize) {
        const size_t Delta = NewSize - OldSize;
        Occurrence.Start += Delta;
        Occurrence.End += Delta;
      } else {
        const size_t Delta = OldSize - NewSize;
        Occurrence.Start -= Delta;
        Occurrence.End -= Delta;
      }
    }
  }
}

Error DriverParsedArgumentMutation::replaceRange(
    size_t Start, size_t End, std::vector<DriverArgumentToken> Replacement,
    DriverParsedOptionOccurrence *EditedOccurrence) {
  if (Start > End || End > Snapshot.Tokens.size() || Replacement.empty())
    return argumentError("parsed option replacement range is invalid");
  for (size_t Index = Start; Index != End; ++Index)
    if (Snapshot.Tokens[Index].Protected || Snapshot.Tokens[Index].EndOfOptions)
      return argumentError(
          "parsed argument mutation cannot modify a protected token");
  const size_t NewSize = Replacement.size();
  Snapshot.Tokens.erase(Snapshot.Tokens.begin() + Start,
                        Snapshot.Tokens.begin() + End);
  Snapshot.Tokens.insert(Snapshot.Tokens.begin() + Start,
                         std::make_move_iterator(Replacement.begin()),
                         std::make_move_iterator(Replacement.end()));
  adjustRanges(Start, End, NewSize, EditedOccurrence);
  return Error::success();
}

Error DriverParsedArgumentMutation::add(StringRef Spelling,
                                        ArrayRef<StringRef> Values) {
  if (Finished)
    return argumentError("parsed argument mutation is finished");
  auto Rendered = Snapshot.Render(Spelling, Values);
  if (!Rendered)
    return Rendered.takeError();
  if (Rendered->empty())
    return argumentError("parsed option renderer returned no tokens");
  size_t InsertAt = Snapshot.Tokens.size();
  for (size_t Index = 0; Index != Snapshot.Tokens.size(); ++Index)
    if (Snapshot.Tokens[Index].EndOfOptions) {
      InsertAt = Index;
      break;
    }
  adjustRanges(InsertAt, InsertAt, Rendered->size(), nullptr);
  Snapshot.Tokens.insert(Snapshot.Tokens.begin() + InsertAt,
                         std::make_move_iterator(Rendered->begin()),
                         std::make_move_iterator(Rendered->end()));
  uint64_t NextID = 1;
  for (const DriverParsedOptionOccurrence &Occurrence : Snapshot.Occurrences) {
    if (Occurrence.ID == std::numeric_limits<uint64_t>::max())
      return argumentError("parsed option occurrence ID space is exhausted");
    NextID = std::max(NextID, Occurrence.ID + 1);
  }
  DriverParsedOptionOccurrence Added;
  Added.ID = NextID;
  Added.Spelling = Spelling.str();
  Added.Origin = NEVERC_ARGUMENT_ORIGIN_PLUGIN;
  Added.Start = InsertAt;
  Added.End = InsertAt + Rendered->size();
  for (StringRef Value : Values)
    Added.Values.push_back(Value.str());
  Added.rebuildValueViews();
  auto Position =
      llvm::find_if(Snapshot.Occurrences,
                    [&](const DriverParsedOptionOccurrence &Occurrence) {
                      return Occurrence.Start >= InsertAt;
                    });
  Snapshot.Occurrences.insert(Position, std::move(Added));
  return Error::success();
}

Error DriverParsedArgumentMutation::remove(uint64_t OccurrenceID) {
  if (Finished)
    return argumentError("parsed argument mutation is finished");
  DriverParsedOptionOccurrence *Occurrence = find(OccurrenceID);
  if (!Occurrence)
    return argumentError("parsed option occurrence is unknown");
  const size_t Start = Occurrence->Start;
  const size_t End = Occurrence->End;
  for (size_t Index = Start; Index != End; ++Index)
    if (Snapshot.Tokens[Index].Protected || Snapshot.Tokens[Index].EndOfOptions)
      return argumentError(
          "parsed argument mutation cannot remove a protected option");
  const size_t Position =
      static_cast<size_t>(Occurrence - Snapshot.Occurrences.data());
  Snapshot.Tokens.erase(Snapshot.Tokens.begin() + Start,
                        Snapshot.Tokens.begin() + End);
  Snapshot.Occurrences.erase(Snapshot.Occurrences.begin() + Position);
  adjustRanges(Start, End, 0, nullptr);
  return Error::success();
}

Error DriverParsedArgumentMutation::replace(uint64_t OccurrenceID,
                                            StringRef Spelling,
                                            ArrayRef<StringRef> Values) {
  if (Finished)
    return argumentError("parsed argument mutation is finished");
  DriverParsedOptionOccurrence *Occurrence = find(OccurrenceID);
  if (!Occurrence)
    return argumentError("parsed option occurrence is unknown");
  auto Rendered = Snapshot.Render(Spelling, Values);
  if (!Rendered)
    return Rendered.takeError();
  if (Error E = replaceRange(Occurrence->Start, Occurrence->End,
                             std::move(*Rendered), Occurrence))
    return E;
  Occurrence->Spelling = Spelling.str();
  Occurrence->Values.clear();
  for (StringRef Value : Values)
    Occurrence->Values.push_back(Value.str());
  Occurrence->Origin = NEVERC_ARGUMENT_ORIGIN_PLUGIN;
  Occurrence->rebuildValueViews();
  return Error::success();
}

Error DriverParsedArgumentMutation::commit() {
  if (Finished)
    return argumentError("parsed argument mutation is finished");
  if (Error E = Target.commitMutation(std::move(Snapshot)))
    return E;
  Finished = true;
  return Error::success();
}

void DriverParsedArgumentMutation::abort() {
  if (Finished)
    return;
  Target.abortMutation();
  Finished = true;
}

NevercInterfaceID driverRawArgumentsArtifactID() {
  return {NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_INPUT_HIGH,
          NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_INPUT_LOW};
}

NevercInterfaceID driverRawArgumentsPhaseID() {
  return {NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
          NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW};
}

NevercInterfaceID driverParsedArgumentsArtifactID() {
  return {NEVERC_PHASE_DRIVER_PARSED_ARGUMENTS_INPUT_HIGH,
          NEVERC_PHASE_DRIVER_PARSED_ARGUMENTS_INPUT_LOW};
}

NevercInterfaceID driverParsedArgumentsPhaseID() {
  return {NEVERC_PHASE_DRIVER_PARSED_ARGUMENTS_HIGH,
          NEVERC_PHASE_DRIVER_PARSED_ARGUMENTS_LOW};
}

Expected<std::shared_ptr<const plugin::PluginArtifactType>>
registerDriverRawArgumentsArtifact(plugin::PluginArtifactRegistry &Registry) {
  plugin::PluginArtifactTypeDescriptor Descriptor;
  Descriptor.ID = driverRawArgumentsArtifactID();
  Descriptor.Name = "neverc.driver.argv";
  Descriptor.Ownership = plugin::PluginArtifactOwnership::Owned;
  Descriptor.Clone = [](const void *Payload) -> Expected<void *> {
    if (!Payload)
      return argumentError("cannot clone a null raw argument artifact");
    return static_cast<void *>(new DriverRawArgumentsArtifact(
        *static_cast<const DriverRawArgumentsArtifact *>(Payload)));
  };
  Descriptor.Destroy = [](void *Payload) {
    delete static_cast<DriverRawArgumentsArtifact *>(Payload);
  };
  Descriptor.Verify = [](const void *Payload) -> Error {
    if (!Payload)
      return argumentError("raw argument artifact payload is null");
    return static_cast<const DriverRawArgumentsArtifact *>(Payload)->verify();
  };
  return Registry.registerType(std::move(Descriptor));
}

Expected<std::shared_ptr<const plugin::PluginArtifactType>>
registerDriverParsedArgumentsArtifact(
    plugin::PluginArtifactRegistry &Registry) {
  plugin::PluginArtifactTypeDescriptor Descriptor;
  Descriptor.ID = driverParsedArgumentsArtifactID();
  Descriptor.Name = "neverc.driver.arguments";
  Descriptor.Ownership = plugin::PluginArtifactOwnership::Owned;
  Descriptor.Clone = [](const void *Payload) -> Expected<void *> {
    if (!Payload)
      return argumentError("cannot clone a null parsed argument artifact");
    return static_cast<void *>(new DriverParsedArgumentsArtifact(
        *static_cast<const DriverParsedArgumentsArtifact *>(Payload)));
  };
  Descriptor.Destroy = [](void *Payload) {
    delete static_cast<DriverParsedArgumentsArtifact *>(Payload);
  };
  Descriptor.Verify = [](const void *Payload) -> Error {
    if (!Payload)
      return argumentError("parsed argument artifact payload is null");
    return static_cast<const DriverParsedArgumentsArtifact *>(Payload)
        ->verify();
  };
  return Registry.registerType(std::move(Descriptor));
}

bool isProtectedDriverBootstrapArgument(StringRef Value) {
  return Value.starts_with("-fplugin=") || Value.starts_with("-fplugin-arg=") ||
         Value.starts_with("-fplugin-provider=");
}

Error verifyDriverArgumentTokens(ArrayRef<DriverArgumentToken> Tokens) {
  return verifyTokensImpl(Tokens);
}

} // namespace neverc::driver
