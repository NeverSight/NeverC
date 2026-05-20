#ifndef NEVERC_INVOKE_MULTILIBBUILDER_H
#define NEVERC_INVOKE_MULTILIBBUILDER_H

#include "neverc/Invoke/Multilib.h"

namespace neverc {
namespace driver {

class MultilibBuilder {
public:
  using flags_list = std::vector<std::string>;

private:
  std::string GCCSuffix;
  std::string OSSuffix;
  std::string IncludeSuffix;
  flags_list Flags;

public:
  MultilibBuilder(llvm::StringRef GCCSuffix, llvm::StringRef OSSuffix,
                  llvm::StringRef IncludeSuffix);

  MultilibBuilder(llvm::StringRef Suffix = {});

  const std::string &gccSuffix() const {
    assert(GCCSuffix.empty() ||
           (llvm::StringRef(GCCSuffix).front() == '/' && GCCSuffix.size() > 1));
    return GCCSuffix;
  }

  MultilibBuilder &gccSuffix(llvm::StringRef S);

  const std::string &osSuffix() const {
    assert(OSSuffix.empty() ||
           (llvm::StringRef(OSSuffix).front() == '/' && OSSuffix.size() > 1));
    return OSSuffix;
  }

  MultilibBuilder &osSuffix(llvm::StringRef S);

  const std::string &includeSuffix() const {
    assert(IncludeSuffix.empty() ||
           (llvm::StringRef(IncludeSuffix).front() == '/' &&
            IncludeSuffix.size() > 1));
    return IncludeSuffix;
  }

  MultilibBuilder &includeSuffix(llvm::StringRef S);

  const flags_list &flags() const { return Flags; }
  flags_list &flags() { return Flags; }

  MultilibBuilder &flag(llvm::StringRef Flag, bool Disallow = false);

  Multilib makeMultilib() const;

  bool isValid() const;

  bool isDefault() const {
    return GCCSuffix.empty() && OSSuffix.empty() && IncludeSuffix.empty();
  }
};

} // namespace driver
} // namespace neverc

#endif // NEVERC_INVOKE_MULTILIBBUILDER_H
