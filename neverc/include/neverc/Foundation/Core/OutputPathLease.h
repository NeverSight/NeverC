#ifndef NEVERC_FOUNDATION_CORE_OUTPUTPATHLEASE_H
#define NEVERC_FOUNDATION_CORE_OUTPUTPATHLEASE_H

#include <string>

namespace neverc {

class OutputCoordinator;

class OutputPathLease {
public:
  OutputPathLease() = default;
  ~OutputPathLease();

  OutputPathLease(OutputPathLease &&Other) noexcept;
  OutputPathLease &operator=(OutputPathLease &&Other) noexcept;

  OutputPathLease(const OutputPathLease &) = delete;
  OutputPathLease &operator=(const OutputPathLease &) = delete;

  explicit operator bool() const { return Owner != nullptr; }
  const std::string &canonicalPath() const { return CanonicalPath; }
  void release();

private:
  OutputPathLease(OutputCoordinator &Owner, std::string CanonicalPath);

  OutputCoordinator *Owner = nullptr;
  std::string CanonicalPath;

  friend class OutputCoordinator;
};

} // namespace neverc

#endif
