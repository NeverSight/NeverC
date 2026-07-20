#include "PluginLinkRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include <limits>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool nonzero(NevercInterfaceID ID) {
  return ID.High != 0 || ID.Low != 0;
}

Error routeError(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

template <typename Record>
bool forcedProviderMatches(const Record &Candidate,
                           const LinkRouteRequest &Request) {
  return Request.ForcedProvider.empty() ||
         Candidate.ProviderID == Request.ForcedProvider;
}

std::optional<unsigned>
linkerSpecificity(const PluginLinkSnapshot::LinkerProviderRecord &Candidate,
                  const LinkRouteRequest &Request) {
  if (!forcedProviderMatches(Candidate, Request))
    return std::nullopt;

  unsigned Score = Candidate.Builtin ? 0 : (1U << 5);
  if (nonzero(Candidate.TargetID)) {
    if (!sameID(Candidate.TargetID, Request.TargetID))
      return std::nullopt;
    Score |= 1U << 4;
  }
  if (nonzero(Candidate.InputFormat)) {
    if (!sameID(Candidate.InputFormat, Request.InputFormat))
      return std::nullopt;
    Score |= 1U << 3;
  }
  if (nonzero(Candidate.OutputFormat)) {
    if (!sameID(Candidate.OutputFormat, Request.OutputFormat))
      return std::nullopt;
    Score |= 1U << 2;
  }
  if (Candidate.OutputKind != 0) {
    if (Candidate.OutputKind != Request.OutputKind)
      return std::nullopt;
    Score |= 1U << 1;
  }
  if (!Candidate.CompatibilityKey.empty()) {
    if (Candidate.CompatibilityKey != Request.CompatibilityKey)
      return std::nullopt;
    Score |= 1U;
  }
  return Score;
}

std::optional<unsigned> mergeSpecificity(
    const PluginLinkSnapshot::ObjectMergeProviderRecord &Candidate,
    const LinkRouteRequest &Request) {
  if (!forcedProviderMatches(Candidate, Request))
    return std::nullopt;

  unsigned Score = Candidate.Builtin ? 0 : (1U << 3);
  if (nonzero(Candidate.TargetID)) {
    if (!sameID(Candidate.TargetID, Request.TargetID))
      return std::nullopt;
    Score |= 1U << 2;
  }
  if (nonzero(Candidate.FormatID)) {
    if (!sameID(Candidate.FormatID, Request.OutputFormat))
      return std::nullopt;
    Score |= 1U << 1;
  }
  if (!Candidate.CompatibilityKey.empty()) {
    if (Candidate.CompatibilityKey != Request.CompatibilityKey)
      return std::nullopt;
    Score |= 1U;
  }
  return Score;
}

template <typename Record, typename Specificity>
Expected<std::pair<const Record *, unsigned>>
chooseBest(ArrayRef<Record> Candidates, const LinkRouteRequest &Request,
           Specificity ScoreFor, StringRef RouteName) {
  const Record *Best = nullptr;
  unsigned BestScore = 0;
  bool Ambiguous = false;
  for (const Record &Candidate : Candidates) {
    std::optional<unsigned> Score = ScoreFor(Candidate, Request);
    if (!Score)
      continue;
    if (!Best || *Score > BestScore) {
      Best = &Candidate;
      BestScore = *Score;
      Ambiguous = false;
    } else if (*Score == BestScore) {
      Ambiguous = true;
    }
  }

  if (!Best) {
    std::string Message =
        ("missing " + RouteName + " route for target/format/output kind").str();
    if (!Request.ForcedProvider.empty())
      Message += " and forced provider '" + Request.ForcedProvider + "'";
    return routeError(Message);
  }
  if (Ambiguous)
    return routeError("ambiguous " + RouteName +
                      " route: equal host-defined specificity");
  return std::make_pair(Best, BestScore);
}

} // namespace

Expected<PlannedLinkRoute> LinkRoutePlanner::plan(
    ArrayRef<PluginLinkSnapshot::LinkerProviderRecord> Linkers,
    ArrayRef<PluginLinkSnapshot::ObjectMergeProviderRecord> Mergers,
    const LinkRouteRequest &Request) {
  if (!nonzero(Request.TargetID) || !nonzero(Request.OutputFormat) ||
      Request.OutputKind < NEVERC_LINK_OUTPUT_RELOCATABLE ||
      Request.OutputKind > NEVERC_LINK_OUTPUT_BUNDLE)
    return routeError("invalid link route request");

  PlannedLinkRoute Result;
  if (Request.OutputKind == NEVERC_LINK_OUTPUT_RELOCATABLE) {
    auto Selected = chooseBest(
        Mergers, Request, mergeSpecificity, "link.object_merge");
    if (!Selected)
      return Selected.takeError();
    Result.RouteKind = PlannedLinkRoute::Kind::ObjectMerge;
    Result.ObjectMergeProvider = Selected->first;
    Result.Specificity = Selected->second;
    return Result;
  }

  auto Selected =
      chooseBest(Linkers, Request, linkerSpecificity, "link.full");
  if (!Selected)
    return Selected.takeError();
  Result.RouteKind = PlannedLinkRoute::Kind::FullLink;
  Result.LinkerProvider = Selected->first;
  Result.Specificity = Selected->second;
  return Result;
}

Expected<PlannedLinkRoute>
LinkRoutePlanner::plan(const PluginLinkSnapshot &Snapshot,
                       const LinkRouteRequest &Request) {
  return plan(Snapshot.linkerProviders(),
              Snapshot.objectMergeProviders(), Request);
}

} // namespace neverc::plugin
