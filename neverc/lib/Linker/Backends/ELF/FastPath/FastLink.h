//===----------------------------------------------------------------------===//
//
//  FastLink — a parallel ELF pipeline for common x86-64 executable links.
//
//  The pipeline keeps its state in flat per-file and per-name arrays and runs
//  every phase in parallel, producing output that does not depend on the
//  thread count. It handles dynamically linked x86-64 executables (PIE or
//  not) built from ELF objects, archives and shared libraries. Anything else
//  is declined, and the caller links with the full backend instead.
//
//===----------------------------------------------------------------------===//

#ifndef LINKER_ELF_FASTPATH_FASTLINK_H
#define LINKER_ELF_FASTPATH_FASTLINK_H

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace fastlink {

struct Input {
  std::string path; // a file, or a library name when isLibrary is set
  bool isLibrary = false;
  bool wholeArchive = false;
  bool asNeeded = false;
  bool lazy = false; // an object between --start-lib and --end-lib
  bool noShared = false; // given after -Bstatic: shared objects are errors
};

struct Request {
  std::string output = "a.out";
  std::string dynamicLinker;
  std::vector<std::string> rpaths;
  std::vector<std::string> libPaths;
  std::vector<Input> inputs;
  bool pie = false;
  bool gcSections = false;
  bool zNow = false;
  bool zRelro = false;
  bool ehFrameHdr = false;
  bool zNodelete = false;
  bool zOrigin = false;
  bool newDtags = true; // DT_RUNPATH rather than DT_RPATH
  // The entry symbol. A missing optional entry leaves the entry address
  // zero; a missing required one declines the link.
  std::string entry = "_start";
  bool entryOptional = false;
  // Names to treat as undefined references (-u): they extract archive
  // members and keep their sections.
  std::vector<std::string> undefined;
  // Size of the build id to derive from the output, or 0 for none. A
  // non-empty buildIdBytes is written as the build id instead.
  unsigned buildIdSize = 0;
  std::vector<unsigned char> buildIdBytes;
  bool discardLocals = false; // omit the inputs' local symbols from .symtab
  bool mmapOutput = true;     // write the output through a shared mapping
  bool stripSymbols = false;  // omit .symtab and .strtab
  bool exportDynamic = false; // export every global definition
  // Shared library output: the soname, which definitions stay preemptible
  // (-Bsymbolic: 0 all, 1 none, 2 data only, 3 weak ones, 4 weak data) and
  // whether undefined symbols are left to the dynamic loader.
  bool shared = false;
  std::string soname;
  unsigned bsymbolic = 0;
  bool allowUndefined = false;
  // Version script nodes by version index: 0 is local, 1 global, later
  // entries the named versions. Patterns may contain glob characters.
  struct VersionPattern {
    std::string name;
    bool wildcard = false;
  };
  struct VersionNode {
    std::string name;
    std::vector<VersionPattern> global, local;
  };
  std::vector<VersionNode> versions;
  bool undefinedVersion = false; // patterns may name undefined symbols
  bool stripDebug = false;    // omit the inputs' debug sections
  // Identical code folding: 0 none, 1 sections whose address is not taken,
  // 2 all code and the data whose address is not taken.
  unsigned icf = 0;
  // Supplies the contents of inputs that are not files on disk; returns false
  // for paths it does not know. The bytes must outlive the link.
  std::function<bool(const std::string &Path, const unsigned char *&Data,
                     size_t &Size)>
      openFile;
  unsigned threads = 0; // 0 selects the hardware concurrency, up to 16
  // Chooses the worker count from the inputs' total size and file count
  // when set; `threads` is then ignored.
  std::function<unsigned(unsigned long long Bytes, unsigned long long Files)>
      selectThreads;
  bool timing = false;  // print phase times to stderr
  // Receives the path of every file the link reads, linker scripts and the
  // files they name included, in command-line order.
  std::vector<std::string> *loadedPaths = nullptr;
};

enum class Status { Linked, Declined };

/// Links `Req`. A declined link leaves no complete output behind and sets
/// `Reason`; the caller then links with the full backend, which rewrites the
/// output. The process should exit soon after a link, which leaves the
/// pipeline's memory to process teardown.
Status link(const Request &Req, std::string &Reason);

} // namespace fastlink

#endif // LINKER_ELF_FASTPATH_FASTLINK_H
