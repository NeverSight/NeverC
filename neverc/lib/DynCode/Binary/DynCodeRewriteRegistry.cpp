// The dyncode bad-byte rewrite registry: see DynCodeRewriteRegistry.h.

#include "Binary/DynCodeRewriteRegistry.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include <limits>

using namespace llvm;

namespace neverc {
namespace dyncode {

llvm::Error
DynCodeRewriteRegistry::registerProvider(DynCodeRewriteProvider Provider) {
  if (Provider.ID.empty())
    return createStringError(errc::invalid_argument,
                             "dyncode rewrite registry: provider needs an ID");
  if (!Provider.Rewrite)
    return createStringError(
        errc::invalid_argument,
        "dyncode rewrite registry: provider '%s' has no callback",
        Provider.ID.c_str());
  for (const DynCodeRewriteProvider &Existing : Providers)
    if (Existing.ID == Provider.ID)
      return createStringError(
          errc::invalid_argument,
          "dyncode rewrite registry: duplicate rewrite provider '%s'",
          Provider.ID.c_str());
  Providers.push_back(std::move(Provider));
  return Error::success();
}

llvm::Error DynCodeRewriteRegistry::runChain(DynCodeImage &Image,
                                             ArrayRef<uint8_t> BadBytes,
                                             uint64_t &OutChanges) const {
  OutChanges = 0;
  const size_t N = Providers.size();
  if (N == 0)
    return Error::success();

  StringMap<size_t> IdToIndex;
  for (size_t I = 0; I < N; ++I)
    IdToIndex[Providers[I].ID] = I;

  // Edge u -> v means u must run before v.
  std::vector<std::vector<size_t>> Adjacency(N);
  std::vector<unsigned> InDegree(N, 0);
  auto addEdge = [&](size_t U, size_t V) {
    Adjacency[U].push_back(V);
    ++InDegree[V];
  };
  for (size_t I = 0; I < N; ++I) {
    for (const std::string &B : Providers[I].Before) {
      auto It = IdToIndex.find(B);
      if (It != IdToIndex.end())
        addEdge(I, It->second);
    }
    for (const std::string &A : Providers[I].After) {
      auto It = IdToIndex.find(A);
      if (It != IdToIndex.end())
        addEdge(It->second, I);
    }
  }

  // Kahn's algorithm with a stable tie-break on registration index so the same
  // provider set always produces the same order.
  std::vector<size_t> Order;
  Order.reserve(N);
  std::vector<bool> Done(N, false);
  for (size_t Step = 0; Step < N; ++Step) {
    size_t Pick = std::numeric_limits<size_t>::max();
    for (size_t I = 0; I < N; ++I)
      if (!Done[I] && InDegree[I] == 0) {
        Pick = I;
        break;
      }
    if (Pick == std::numeric_limits<size_t>::max())
      return createStringError(
          errc::invalid_argument,
          "dyncode rewrite registry: before/after constraints form a cycle");
    Done[Pick] = true;
    Order.push_back(Pick);
    for (size_t V : Adjacency[Pick])
      --InDegree[V];
  }

  for (size_t Index : Order) {
    const DynCodeRewriteProvider &P = Providers[Index];
    uint64_t Before = Image.size();
    Expected<uint64_t> Changed = P.Rewrite(Image, BadBytes);
    if (!Changed)
      return Changed.takeError();
    uint64_t After = Image.size();
    if (After > Before && (After - Before) > P.MaxGrowth)
      return createStringError(
          errc::invalid_argument,
          "dyncode rewrite registry: provider '%s' grew the image by %llu "
          "bytes but declared a max growth of %llu",
          P.ID.c_str(), (unsigned long long)(After - Before),
          (unsigned long long)P.MaxGrowth);
    OutChanges += *Changed;
  }
  return Error::success();
}

} // namespace dyncode
} // namespace neverc
