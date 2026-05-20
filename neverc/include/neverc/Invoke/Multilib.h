#ifndef NEVERC_INVOKE_MULTILIB_H
#define NEVERC_INVOKE_MULTILIB_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace neverc {
namespace driver {

class Multilib {
public:
  using flags_list = std::vector<std::string>;

private:
  std::string GCCSuffix;
  std::string OSSuffix;
  std::string IncludeSuffix;
  flags_list Flags;

  // Optionally, a multilib can be assigned a string tag indicating that it's
  // part of a group of mutually exclusive possibilities. If two or more
  // multilibs have the same non-empty value of ExclusiveGroup, then only the
  // last matching one of them will be selected.
  //
  // Setting this to the empty string is a special case, indicating that the
  // directory is not mutually exclusive with anything else.
  std::string ExclusiveGroup;

public:
  Multilib(llvm::StringRef GCCSuffix = {}, llvm::StringRef OSSuffix = {},
           llvm::StringRef IncludeSuffix = {},
           const flags_list &Flags = flags_list(),
           llvm::StringRef ExclusiveGroup = {});

  const std::string &gccSuffix() const { return GCCSuffix; }

  const std::string &osSuffix() const { return OSSuffix; }

  const std::string &includeSuffix() const { return IncludeSuffix; }

  const flags_list &flags() const { return Flags; }

  const std::string &exclusiveGroup() const { return ExclusiveGroup; }

  LLVM_DUMP_METHOD void dump() const;
  void print(llvm::raw_ostream &OS) const;

  bool isDefault() const {
    return GCCSuffix.empty() && OSSuffix.empty() && IncludeSuffix.empty();
  }

  bool operator==(const Multilib &Other) const;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const Multilib &M);

class MultilibSet {
public:
  using multilib_list = std::vector<Multilib>;
  using const_iterator = multilib_list::const_iterator;
  using IncludeDirsFunc =
      std::function<std::vector<std::string>(const Multilib &M)>;
  using FilterCallback = llvm::function_ref<bool(const Multilib &)>;

  struct FlagMatcher {
    std::string Match;
    std::vector<std::string> Flags;
  };

private:
  multilib_list Multilibs;
  std::vector<FlagMatcher> FlagMatchers;
  IncludeDirsFunc IncludeCallback;
  IncludeDirsFunc FilePathsCallback;

public:
  MultilibSet() = default;
  MultilibSet(multilib_list &&Multilibs,
              std::vector<FlagMatcher> &&FlagMatchers = {})
      : Multilibs(Multilibs), FlagMatchers(FlagMatchers) {}

  const multilib_list &getMultilibs() { return Multilibs; }

  MultilibSet &FilterOut(FilterCallback F);

  void push_back(const Multilib &M);

  const_iterator begin() const { return Multilibs.begin(); }
  const_iterator end() const { return Multilibs.end(); }

  bool select(const Multilib::flags_list &Flags,
              llvm::SmallVectorImpl<Multilib> &) const;

  unsigned size() const { return Multilibs.size(); }

  llvm::StringSet<> expandFlags(const Multilib::flags_list &) const;

  LLVM_DUMP_METHOD void dump() const;
  void print(llvm::raw_ostream &OS) const;

  MultilibSet &setIncludeDirsCallback(IncludeDirsFunc F) {
    IncludeCallback = std::move(F);
    return *this;
  }

  const IncludeDirsFunc &includeDirsCallback() const { return IncludeCallback; }

  MultilibSet &setFilePathsCallback(IncludeDirsFunc F) {
    FilePathsCallback = std::move(F);
    return *this;
  }

  const IncludeDirsFunc &filePathsCallback() const { return FilePathsCallback; }

  static llvm::ErrorOr<MultilibSet>
  parseYaml(llvm::MemoryBufferRef, llvm::SourceMgr::DiagHandlerTy = nullptr,
            void *DiagHandlerCtxt = nullptr);
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, const MultilibSet &MS);

} // namespace driver
} // namespace neverc

#endif // NEVERC_INVOKE_MULTILIB_H
