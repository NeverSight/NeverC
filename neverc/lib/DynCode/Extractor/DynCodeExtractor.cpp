#include "neverc/DynCode/Extractor/DynCodeExtractor.h"
#include "Extractor/DynCodeObjectPipeline.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <chrono>
#include <cstdlib>
using namespace llvm;

namespace neverc {
namespace dyncode {

// Reads the relocatable object into a volume-4 ObjectGraph and runs the single
// format-agnostic extraction pipeline (plan -> relocate -> binary phases ->
// sealed verify), then writes the verified image.  This replaces the old
// per-format extractELF / extractCOFF / extractMachO dispatch: nothing is read
// from disk a second time and there is no hard-coded format switch.
int extractDynCode(StringRef InputObj, StringRef OutputBin,
                     const DynCodeOptions &Opts) {
  auto Buf = MemoryBuffer::getFile(InputObj, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);
  if (!Buf) {
    errs() << "dyncode-extractor: cannot read '" << InputObj
           << "': " << Buf.getError().message() << "\n";
    return 1;
  }
  ArrayRef<uint8_t> ObjectBytes(
      reinterpret_cast<const uint8_t *>((*Buf)->getBufferStart()),
      (*Buf)->getBufferSize());

  auto Result = runDynCodeExtractionPipeline(ObjectBytes, InputObj, Opts);
  if (!Result) {
    errs() << "neverc: error: " << toString(Result.takeError()) << "\n";
    return 1;
  }

  std::error_code EC;
  raw_fd_ostream Os(OutputBin, EC, sys::fs::OF_None);
  if (EC) {
    errs() << "dyncode-extractor: cannot write '" << OutputBin
           << "': " << EC.message() << "\n";
    return 1;
  }
  ArrayRef<uint8_t> ImageBytes = Result->Image.bytes();
  Os.write(reinterpret_cast<const char *>(ImageBytes.data()),
           ImageBytes.size());
  Os.close();
  if (Os.has_error()) {
    errs() << "dyncode-extractor: write error on '" << OutputBin << "'\n";
    return 1;
  }

  if (!Opts.ReportPath.empty()) {
    auto Json = Result->Report.toCanonicalJSON();
    if (!Json) {
      errs() << "neverc: error: " << toString(Json.takeError()) << "\n";
      return 1;
    }
    std::error_code ReportEC;
    raw_fd_ostream ReportOs(Opts.ReportPath, ReportEC, sys::fs::OF_Text);
    if (ReportEC) {
      errs() << "dyncode-extractor: cannot write report '" << Opts.ReportPath
             << "': " << ReportEC.message() << "\n";
      return 1;
    }
    ReportOs << *Json;
  }

  return 0;
}

} // namespace dyncode
} // namespace neverc
