#include "neverc/DynCode/Extractor/DynCodeExtractor.h"
#include "Extractor/DynCodeObjectPipeline.h"
#include "neverc/Foundation/Core/OutputBundleTransaction.h"
#include "neverc/Foundation/Core/OutputCoordinator.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <vector>
using namespace llvm;

namespace neverc {
namespace dyncode {

// Reads the relocatable object into an ObjectGraph and runs the single
// format-agnostic extraction pipeline (plan -> relocate -> binary phases ->
// sealed verify), then atomically publishes the verified image (and optional
// report) through the OutputBundleTransaction.  This replaces the old
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

  // Stage the verified image and its optional report side output, then publish
  // them as one atomic bundle through the OutputBundleTransaction.
  // The sealed verify gate has already run, so nothing may mutate the bytes
  // after this point; a failure anywhere during publication rolls back to any
  // pre-existing content and leaves no partial file behind.
  std::vector<OutputBundleFile> Outputs;

  OutputBundleFile ImageFile;
  ImageFile.Name = "image";
  ImageFile.Path = OutputBin.str();
  ArrayRef<uint8_t> ImageBytes = Result->Image.bytes();
  ImageFile.Bytes.assign(ImageBytes.begin(), ImageBytes.end());
  ImageFile.Main = true;
  Outputs.push_back(std::move(ImageFile));

  if (!Opts.ReportPath.empty()) {
    auto Json = Result->Report.toCanonicalJSON();
    if (!Json) {
      errs() << "neverc: error: " << toString(Json.takeError()) << "\n";
      return 1;
    }
    OutputBundleFile ReportFile;
    ReportFile.Name = "report";
    ReportFile.Path = Opts.ReportPath;
    ReportFile.Bytes.assign(Json->begin(), Json->end());
    Outputs.push_back(std::move(ReportFile));
  }

  OutputCoordinator Coordinator;
  auto Transaction = OutputBundleTransaction::create(Coordinator, Outputs);
  if (!Transaction) {
    errs() << "neverc: error: " << toString(Transaction.takeError()) << "\n";
    return 1;
  }
  if (auto Summary = (*Transaction)->commit(); !Summary) {
    errs() << "neverc: error: dyncode output commit failed: "
           << toString(Summary.takeError()) << "\n";
    return 1;
  }

  return 0;
}

} // namespace dyncode
} // namespace neverc
