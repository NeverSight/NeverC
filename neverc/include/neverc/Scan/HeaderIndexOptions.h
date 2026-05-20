#ifndef NEVERC_SCAN_HEADERINDEXOPTIONS_H
#define NEVERC_SCAN_HEADERINDEXOPTIONS_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/HashBuilder.h"
#include <string>
#include <vector>

namespace neverc {

namespace frontend {

enum IncludeDirGroup {
  Quoted = 0,

  Angled = 1,

  IndexHeaderMap = 2,

  System = 3,

  ExternCSystem = 4,

  CSystem = 5,

  After = 7,
};

} // namespace frontend

class HeaderIndexOptions {
public:
  struct Entry {
    std::string Path;
    frontend::IncludeDirGroup Group;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsFramework : 1;

    /// IgnoreSysRoot - This is false if an absolute path should be treated
    /// relative to the sysroot, or true if it should always be the absolute
    /// path.
    LLVM_PREFERRED_TYPE(bool)
    unsigned IgnoreSysRoot : 1;

    Entry(llvm::StringRef path, frontend::IncludeDirGroup group,
          bool isFramework, bool ignoreSysRoot)
        : Path(path), Group(group), IsFramework(isFramework),
          IgnoreSysRoot(ignoreSysRoot) {}
  };

  struct SystemHeaderPrefix {
    /// A prefix to be matched against paths in \#include directives.
    std::string Prefix;

    /// True if paths beginning with this prefix should be treated as system
    /// headers.
    bool IsSystemHeader;

    SystemHeaderPrefix(llvm::StringRef Prefix, bool IsSystemHeader)
        : Prefix(Prefix), IsSystemHeader(IsSystemHeader) {}
  };

  std::string Sysroot;

  std::vector<Entry> UserEntries;

  std::vector<SystemHeaderPrefix> SystemHeaderPrefixes;

  std::string ResourceDir;

  std::vector<std::string> VFSOverlayFiles;

  LLVM_PREFERRED_TYPE(bool)
  unsigned UseBuiltinIncludes : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned UseStandardSystemIncludes : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned Verbose : 1;

  HeaderIndexOptions(llvm::StringRef _Sysroot = "/")
      : Sysroot(_Sysroot), UseBuiltinIncludes(true),
        UseStandardSystemIncludes(true), Verbose(false) {}

  void AddPath(llvm::StringRef Path, frontend::IncludeDirGroup Group,
               bool IsFramework, bool IgnoreSysRoot) {
    UserEntries.emplace_back(Path, Group, IsFramework, IgnoreSysRoot);
  }

  void AddSystemHeaderPrefix(llvm::StringRef Prefix, bool IsSystemHeader) {
    SystemHeaderPrefixes.emplace_back(Prefix, IsSystemHeader);
  }

  void AddVFSOverlayFile(llvm::StringRef Name) {
    VFSOverlayFiles.push_back(std::string(Name));
  }
};

inline llvm::hash_code hash_value(const HeaderIndexOptions::Entry &E) {
  return llvm::hash_combine(E.Path, E.Group, E.IsFramework, E.IgnoreSysRoot);
}

template <typename HasherT, llvm::endianness Endianness>
inline void addHash(llvm::HashBuilder<HasherT, Endianness> &HBuilder,
                    const HeaderIndexOptions::Entry &E) {
  HBuilder.add(E.Path, E.Group, E.IsFramework, E.IgnoreSysRoot);
}

inline llvm::hash_code
hash_value(const HeaderIndexOptions::SystemHeaderPrefix &SHP) {
  return llvm::hash_combine(SHP.Prefix, SHP.IsSystemHeader);
}

template <typename HasherT, llvm::endianness Endianness>
inline void addHash(llvm::HashBuilder<HasherT, Endianness> &HBuilder,
                    const HeaderIndexOptions::SystemHeaderPrefix &SHP) {
  HBuilder.add(SHP.Prefix, SHP.IsSystemHeader);
}

} // namespace neverc

#endif // NEVERC_SCAN_HEADERINDEXOPTIONS_H
