#include "neverc/DynCode/Extractor/DynCodeExtractor.h"
#include "Extractor/ExtractorCommon.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace neverc {
namespace dyncode {

int extractDynCode(StringRef InputObj, StringRef OutputBin,
                     const DynCodeOptions &Opts) {
  const TargetDesc &Target = Opts.Target;
  switch (Target.Format) {
  case ObjectFormat::MachO:
    return extractMachO(InputObj, OutputBin, Opts);
  case ObjectFormat::ELF:
    return extractELF(InputObj, OutputBin, Opts);
  case ObjectFormat::COFF:
    return extractCOFF(InputObj, OutputBin, Opts);
  default:
    errs() << "dyncode-extractor: no extractor registered for "
           << triplePrettyName(Target) << "\n";
    return 1;
  }
}

} // namespace dyncode
} // namespace neverc
