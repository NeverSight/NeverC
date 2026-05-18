#include "neverc/Invoke/MultilibBuilder.h"
#include "ToolChains/CommonArgs.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Path.h"

using namespace neverc;
using namespace driver;

namespace {
void normalizePathSegment(std::string &Segment) {
  llvm::StringRef seg = Segment;

  // Prune trailing "/" or "./"
  while (true) {
    llvm::StringRef last = llvm::sys::path::filename(seg);
    if (last != ".")
      break;
    seg = llvm::sys::path::parent_path(seg);
  }

  if (seg.empty() || seg == "/") {
    Segment.clear();
    return;
  }

  // Add leading '/'
  if (seg.front() != '/') {
    Segment = "/" + seg.str();
  } else {
    Segment = std::string(seg);
  }
}
} // namespace

MultilibBuilder::MultilibBuilder(llvm::StringRef GCC, llvm::StringRef OS,
                                 llvm::StringRef Include)
    : GCCSuffix(GCC), OSSuffix(OS), IncludeSuffix(Include) {
  normalizePathSegment(GCCSuffix);
  normalizePathSegment(OSSuffix);
  normalizePathSegment(IncludeSuffix);
}

MultilibBuilder::MultilibBuilder(llvm::StringRef Suffix)
    : MultilibBuilder(Suffix, Suffix, Suffix) {}

MultilibBuilder &MultilibBuilder::gccSuffix(llvm::StringRef S) {
  GCCSuffix = std::string(S);
  normalizePathSegment(GCCSuffix);
  return *this;
}

MultilibBuilder &MultilibBuilder::osSuffix(llvm::StringRef S) {
  OSSuffix = std::string(S);
  normalizePathSegment(OSSuffix);
  return *this;
}

MultilibBuilder &MultilibBuilder::includeSuffix(llvm::StringRef S) {
  IncludeSuffix = std::string(S);
  normalizePathSegment(IncludeSuffix);
  return *this;
}

bool MultilibBuilder::isValid() const {
  llvm::StringMap<int> FlagSet;
  for (unsigned I = 0, N = Flags.size(); I != N; ++I) {
    llvm::StringRef Flag(Flags[I]);
    llvm::StringMap<int>::iterator SI = FlagSet.find(Flag.substr(1));

    assert(llvm::StringRef(Flag).front() == '-' ||
           llvm::StringRef(Flag).front() == '!');

    if (SI == FlagSet.end())
      FlagSet[Flag.substr(1)] = I;
    else if (Flags[I] != Flags[SI->getValue()])
      return false;
  }
  return true;
}

MultilibBuilder &MultilibBuilder::flag(llvm::StringRef Flag, bool Disallow) {
  tools::addMultilibFlag(!Disallow, Flag, Flags);
  return *this;
}

Multilib MultilibBuilder::makeMultilib() const {
  return Multilib(GCCSuffix, OSSuffix, IncludeSuffix, Flags);
}
