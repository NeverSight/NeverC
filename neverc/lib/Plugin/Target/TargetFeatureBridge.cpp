#include "neverc/Plugin/Host/PluginTargetInfo.h"
#include "neverc/Foundation/Diagnostic/DiagnosticFrontend.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

using namespace llvm;

namespace neverc::plugin {
namespace {

NevercTaskHandle taskHandle(const PluginTargetInfo &Target) {
  return Target.task() ? Target.task()->handle() : NevercTaskHandle{};
}

template <typename Callback>
Expected<NevercStatus>
invokeTargetCallback(const PluginTargetInfo &Target,
                     StringRef CallbackName, Callback &&Invoke) {
  if (Target.task())
    return Target.task()->invokeCallback(
        Target.record().PluginID, CallbackName,
        std::forward<Callback>(Invoke));
  return Invoke();
}

std::optional<std::string> copyCallbackString(NevercStringView View) {
  if (View.Length > 4096 || (!View.Data && View.Length != 0))
    return std::nullopt;
  StringRef Text(View.Data ? View.Data : "",
                 static_cast<size_t>(View.Length));
  if (Text.empty() || Text.contains('\0'))
    return std::nullopt;
  return Text.str();
}

std::optional<std::string>
canonicalCPU(const PluginTargetInfo &Target, StringRef Name) {
  const auto &Record = Target.record();
  if (!Record.CanonicalizeCPU)
    return Name.str();

  NevercStringView Canonical{};
  const NevercStringView Input{Name.data(), Name.size()};
  auto Status = invokeTargetCallback(Target, "CanonicalizeTargetCPU", [&] {
    return Record.CanonicalizeCPU(
        taskHandle(Target), Input, Record.TargetUserData, &Canonical);
  });
  if (!Status) {
    consumeError(Status.takeError());
    return std::nullopt;
  }
  if (Status->Code != NEVERC_STATUS_OK)
    return std::nullopt;
  return copyCallbackString(Canonical);
}

bool callbackAcceptsCPU(const PluginTargetInfo &Target, StringRef Name) {
  const auto &Record = Target.record();
  if (!Record.ValidateCPU)
    return false;
  NevercBool Valid = NEVERC_FALSE;
  const NevercStringView Input{Name.data(), Name.size()};
  auto Status = invokeTargetCallback(Target, "ValidateTargetCPU", [&] {
    return Record.ValidateCPU(
        taskHandle(Target), Input, Record.TargetUserData, &Valid);
  });
  if (!Status) {
    consumeError(Status.takeError());
    return false;
  }
  return Status->Code == NEVERC_STATUS_OK &&
         (Valid == NEVERC_FALSE || Valid == NEVERC_TRUE) &&
         Valid == NEVERC_TRUE;
}

bool validateResolvedFeatures(
    const PluginTargetInfo &Target, NevercStructArrayView States,
    StringMap<bool> &Features) {
  const auto &Known = Target.record().Machine.Features;
  constexpr size_t Required =
      offsetof(NevercTargetFeatureState, Reserved) +
      sizeof(NevercTargetFeatureState::Reserved);
  if (States.Count != Known.size() ||
      (States.Count != 0 &&
       (!States.Data || States.ElementStride < Required ||
        States.ElementStride > std::numeric_limits<size_t>::max() ||
        States.ElementStride >
            std::numeric_limits<size_t>::max() / States.Count)))
    return false;

  const auto *Bytes = static_cast<const uint8_t *>(States.Data);
  for (uint64_t I = 0; I != States.Count; ++I) {
    const auto *State = reinterpret_cast<const NevercTargetFeatureState *>(
        Bytes + static_cast<size_t>(I * States.ElementStride));
    if (State->Header.StructSize < Required ||
        State->Header.Major != NEVERC_TARGET_API_MAJOR ||
        State->Header.Minor > NEVERC_TARGET_API_MINOR ||
        State->Header.Flags != 0 ||
        llvm::any_of(State->Reserved, [](uint8_t Byte) {
          return Byte != 0;
        }) ||
        (State->Enabled != NEVERC_FALSE &&
         State->Enabled != NEVERC_TRUE))
      return false;
    auto Name = copyCallbackString(State->Name);
    if (!Name || *Name != Known[static_cast<size_t>(I)].Name)
      return false;
    Features[*Name] = State->Enabled == NEVERC_TRUE;
  }
  return true;
}

bool validateFeatureGraph(
    ArrayRef<VerifiedTargetFeature> Known,
    const StringMap<bool> &Features,
    DiagnosticsEngine &Diags) {
  for (const VerifiedTargetFeature &Feature : Known) {
    if (!Features.lookup(Feature.Name))
      continue;
    for (const std::string &Implied : Feature.Implies)
      if (!Features.lookup(Implied)) {
        Diags.Report(diag::warn_invalid_feature_combination)
            << (Feature.Name + " requires " + Implied);
        return false;
      }
    for (const std::string &Conflict : Feature.Conflicts)
      if (Features.lookup(Conflict)) {
        Diags.Report(diag::warn_invalid_feature_combination)
            << (Feature.Name + " " + Conflict);
        return false;
      }
  }
  return true;
}

} // namespace

bool PluginTargetInfo::setCPU(const std::string &Name) {
  auto Canonical = canonicalCPU(*this, Name);
  if (!Canonical)
    return false;
  if (Record.ValidateCPU) {
    if (!callbackAcceptsCPU(*this, *Canonical))
      return false;
  } else if (!isValidCPUName(*Canonical)) {
    return false;
  }
  CPU = std::move(*Canonical);
  return true;
}

bool PluginTargetInfo::isValidCPUName(StringRef Name) const {
  auto Canonical = canonicalCPU(*this, Name);
  if (!Canonical)
    return false;
  if (Record.ValidateCPU)
    return callbackAcceptsCPU(*this, *Canonical);
  if (Canonical->empty())
    return false;
  if (Record.Machine.CPUs.empty())
    return true;
  return std::binary_search(Record.Machine.CPUs.begin(),
                            Record.Machine.CPUs.end(), *Canonical);
}

bool PluginTargetInfo::isValidTuneCPUName(StringRef Name) const {
  return isValidCPUName(Name);
}

void PluginTargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  if (Record.ListCPUs && !CallbackCPUsLoaded) {
    CallbackCPUsLoaded = true;
    NevercStringArrayView Result{};
    auto Status = invokeTargetCallback(*this, "ListTargetCPUs", [&] {
      return Record.ListCPUs(
          taskHandle(*this), Record.TargetUserData, &Result);
    });
    if (!Status) {
      consumeError(Status.takeError());
    } else if (Status->Code == NEVERC_STATUS_OK &&
        Result.Count <= 4096 &&
        (Result.Count == 0 ||
         (Result.Data &&
          Result.ElementStride >= sizeof(NevercStringView)))) {
      std::vector<std::string> Candidate;
      const auto *Bytes =
          reinterpret_cast<const uint8_t *>(Result.Data);
      for (uint64_t I = 0; I != Result.Count; ++I) {
        const auto *Item = reinterpret_cast<const NevercStringView *>(
            Bytes + static_cast<size_t>(I * Result.ElementStride));
        auto Text = copyCallbackString(*Item);
        if (!Text) {
          Candidate.clear();
          break;
        }
        Candidate.push_back(std::move(*Text));
      }
      if (std::is_sorted(Candidate.begin(), Candidate.end()) &&
          std::adjacent_find(Candidate.begin(), Candidate.end()) ==
              Candidate.end())
        CallbackCPUs = std::move(Candidate);
    }
  }

  if (!CallbackCPUs.empty()) {
    for (const std::string &Name : CallbackCPUs)
      Values.push_back(Name);
    return;
  }
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

  SmallVector<NevercStringView, 16> RequestedViews;
  RequestedViews.reserve(FeatureVec.size());
  for (const std::string &Spelling : FeatureVec) {
    StringRef Value(Spelling);
    if (Value.size() < 2 ||
        (Value.front() != '+' && Value.front() != '-') ||
        !isValidFeatureName(Value.drop_front())) {
      Diags.Report(diag::warn_fe_backend_invalid_feature_flag) << Value;
      return false;
    }
    RequestedViews.push_back({Value.data(), Value.size()});
  }

  if (Record.ResolveFeatures) {
    NevercStructArrayView States{};
    const StringRef SelectedCPU =
        CPU.empty() ? CPUValue : StringRef(CPU);
    const NevercStringView CPUView{SelectedCPU.data(),
                                  SelectedCPU.size()};
    NevercStringArrayView Requested{
        RequestedViews.data(), RequestedViews.size(),
        sizeof(NevercStringView)};
    auto Status = invokeTargetCallback(*this, "ResolveTargetFeatures", [&] {
      return Record.ResolveFeatures(
          taskHandle(*this), CPUView, Requested, Record.TargetUserData,
          &States);
    });
    if (!Status) {
      consumeError(Status.takeError());
      return false;
    }
    if (Status->Code != NEVERC_STATUS_OK ||
        !validateResolvedFeatures(*this, States, Features))
      return false;
  } else {
    if (!TargetInfo::initFeatureMap(Features, Diags, CPUValue,
                                    FeatureVec))
      return false;
    bool Changed = true;
    while (Changed) {
      Changed = false;
      for (const VerifiedTargetFeature &Feature :
           Record.Machine.Features) {
        if (!Features.lookup(Feature.Name))
          continue;
        for (const std::string &Implied : Feature.Implies)
          if (!Features.lookup(Implied)) {
            Features[Implied] = true;
            Changed = true;
          }
      }
    }
  }

  return validateFeatureGraph(Record.Machine.Features, Features, Diags);
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

} // namespace neverc::plugin
