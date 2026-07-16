#include "neverc/Foundation/Core/OutputPathLease.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include <utility>

namespace neverc {

OutputPathLease::OutputPathLease(OutputCoordinator &OwnerValue,
                                 std::string CanonicalPathValue)
    : Owner(&OwnerValue), CanonicalPath(std::move(CanonicalPathValue)) {}

OutputPathLease::~OutputPathLease() { release(); }

OutputPathLease::OutputPathLease(OutputPathLease &&Other) noexcept
    : Owner(std::exchange(Other.Owner, nullptr)),
      CanonicalPath(std::move(Other.CanonicalPath)) {}

OutputPathLease &
OutputPathLease::operator=(OutputPathLease &&Other) noexcept {
  if (this == &Other)
    return *this;
  release();
  Owner = std::exchange(Other.Owner, nullptr);
  CanonicalPath = std::move(Other.CanonicalPath);
  return *this;
}

void OutputPathLease::release() {
  if (!Owner)
    return;
  OutputCoordinator *CurrentOwner = std::exchange(Owner, nullptr);
  CurrentOwner->release(CanonicalPath);
  CanonicalPath.clear();
}

} // namespace neverc
