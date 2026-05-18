//===----------------------------------------------------------------------===//
//
//  ArgList — thin accessors over `llvm::opt::InputArgList`.
//
//  The backends pull option values through a handful of typed helpers
//  (`getInteger`, `getHex`, `getStrings`, `getZOptionValue`, ...) rather
//  than calling `Arg::getValue()` directly.  Centralising the error
//  reporting here keeps option-parsing diagnostics consistent across
//  every flavor.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Driver/ArgList.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace linker;

int linker::args::getCGOptLevel(int optLevelLTO) {
  return std::clamp(optLevelLTO, 2, 3);
}

namespace {
int64_t getInteger(opt::InputArgList &args, unsigned key, int64_t Default,
                   unsigned base) {
  auto *a = args.getLastArg(key);
  if (!a)
    return Default;

  int64_t v;
  StringRef s = a->getValue();
  if (base == 16 && (s.starts_with("0x") || s.starts_with("0X")))
    s = s.drop_front(2);
  if (to_integer(s, v, base))
    return v;

  StringRef spelling = args.getArgString(a->getIndex());
  error(spelling + ": number expected, but got '" + a->getValue() + "'");
  return 0;
}
} // namespace

int64_t linker::args::getInteger(opt::InputArgList &args, unsigned key,
                                 int64_t Default) {
  return ::getInteger(args, key, Default, 10);
}

int64_t linker::args::getHex(opt::InputArgList &args, unsigned key,
                             int64_t Default) {
  return ::getInteger(args, key, Default, 16);
}

SmallVector<StringRef, 0> linker::args::getStrings(opt::InputArgList &args,
                                                   int id) {
  SmallVector<StringRef, 0> v;
  for (auto *arg : args.filtered(id))
    v.push_back(arg->getValue());
  return v;
}

uint64_t linker::args::getZOptionValue(opt::InputArgList &args, int id,
                                       StringRef key, uint64_t Default) {
  for (auto *arg : args.filtered_reverse(id)) {
    std::pair<StringRef, StringRef> kv = StringRef(arg->getValue()).split('=');
    if (kv.first == key) {
      uint64_t result = Default;
      if (!to_integer(kv.second, result))
        error("invalid " + key + ": " + kv.second);
      return result;
    }
  }
  return Default;
}

std::vector<StringRef> linker::args::getLines(MemoryBufferRef mb) {
  SmallVector<StringRef, 0> arr;
  mb.getBuffer().split(arr, '\n');

  std::vector<StringRef> ret;
  for (StringRef s : arr) {
    s = s.trim();
    if (!s.empty() && s[0] != '#')
      ret.push_back(s);
  }
  return ret;
}

StringRef linker::args::getFilenameWithoutExe(StringRef path) {
  if (path.ends_with_insensitive(".exe"))
    return sys::path::stem(path);
  return sys::path::filename(path);
}
