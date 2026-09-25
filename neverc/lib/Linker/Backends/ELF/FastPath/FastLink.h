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
  // Size of the build id to derive from the output, or 0 for none. A
  // non-empty buildIdBytes is written as the build id instead.
  unsigned buildIdSize = 0;
  std::vector<unsigned char> buildIdBytes;
  bool discardLocals = false; // omit the inputs' local symbols from .symtab
  bool mmapOutput = true;     // write the output through a shared mapping
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
};

enum class Status { Linked, Declined };

/// Links `Req`. A declined link leaves no complete output behind and sets
/// `Reason`; the caller then links with the full backend, which rewrites the
/// output. The process should exit soon after a link, which leaves the
/// pipeline's memory to process teardown.
Status link(const Request &Req, std::string &Reason);

} // namespace fastlink

#endif // LINKER_ELF_FASTPATH_FASTLINK_H
