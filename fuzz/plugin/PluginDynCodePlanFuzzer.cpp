// Fuzz the typed dyncode extraction plan builder.
//
// DynCodeExtractionPlan::add*/setEntry/resolve validate every input with checked
// arithmetic, reject overlapping output ranges, allow only a single entry, and
// hand out typed generation handles.  This fuzzer drives them with arbitrary
// section/symbol/relocation/external/entry bytes plus random (possibly
// stale/wrong-kind) handle resolution, then optionally rebuilds the plan.  It
// asserts the builder never crashes and that a rebuild always invalidates every
// previously issued handle.  Only the host builder runs -- no native plugin code
// is generated from fuzz bytes.

#include "PluginFrontendFuzzSupport.h"
#include "neverc/DynCode/Extractor/DynCodeExtractionPlan.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::dyncode;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

std::string takeText(ByteCursor &Input, size_t Maximum) {
  ArrayRef<uint8_t> Bytes = Input.takeBytes(Maximum);
  return std::string(reinterpret_cast<const char *>(Bytes.data()), Bytes.size());
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);
  DynCodeExtractionPlan Plan;
  std::vector<DynCodeHandle> Handles;

  const unsigned Ops = std::min<unsigned>(Input.takeByte(), 64);
  for (unsigned I = 0; I != Ops && !Input.empty(); ++I) {
    switch (Input.takeByte() % 6) {
    case 0: {
      DynCodeSectionFragment Fragment;
      Fragment.SourceName = takeText(Input, 16);
      Fragment.SourceKind = Input.takeU32();
      Fragment.Disposition = (Input.takeByte() & 1)
                                 ? DynCodeSectionDisposition::Selected
                                 : DynCodeSectionDisposition::Discarded;
      Fragment.OutputOffset = Input.takeU64();
      Fragment.OutputSize = Input.takeU64();
      Fragment.Alignment = Input.takeU64();
      Fragment.Reason = takeText(Input, 8);
      if (auto H = Plan.addSectionFragment(std::move(Fragment)))
        Handles.push_back(*H);
      else
        consume(H.takeError());
      break;
    }
    case 1: {
      DynCodeSymbolMapping Mapping;
      Mapping.Name = takeText(Input, 16);
      Mapping.OutputOffset = Input.takeU64();
      Mapping.IsEntry = (Input.takeByte() & 1) != 0;
      if (auto H = Plan.addSymbolMapping(std::move(Mapping)))
        Handles.push_back(*H);
      else
        consume(H.takeError());
      break;
    }
    case 2: {
      DynCodeRelocationEntry Relocation;
      Relocation.SiteOffset = Input.takeU64();
      Relocation.TargetOffset = Input.takeU64();
      Relocation.Addend = static_cast<int64_t>(Input.takeU64());
      Relocation.Width = Input.takeU32();
      Relocation.IsPCRelative = (Input.takeByte() & 1) != 0;
      Relocation.Kind = Input.takeU32();
      Relocation.NativeType = Input.takeU64();
      Relocation.Resolved = (Input.takeByte() & 1) != 0;
      if (auto H = Plan.addRelocation(std::move(Relocation)))
        Handles.push_back(*H);
      else
        consume(H.takeError());
      break;
    }
    case 3: {
      DynCodeExternalContract Contract;
      Contract.Symbol = takeText(Input, 16);
      Contract.ImportKind = Input.takeU32();
      Contract.ResolverContract = takeText(Input, 8);
      Contract.ProviderID = takeText(Input, 8);
      if (auto H = Plan.addExternalContract(std::move(Contract)))
        Handles.push_back(*H);
      else
        consume(H.takeError());
      break;
    }
    case 4: {
      const DynCodeEntryPolicy Policy =
          static_cast<DynCodeEntryPolicy>(1 + (Input.takeByte() % 3));
      consume(Plan.setEntry(Policy, takeText(Input, 16), Input.takeU64()));
      break;
    }
    case 5: {
      DynCodeHandle Handle;
      if (!Handles.empty() && (Input.takeByte() & 1)) {
        Handle = Handles[Input.takeU32() % Handles.size()];
      } else {
        Handle.Kind = static_cast<DynCodeHandleKind>(Input.takeU32() % 7);
        Handle.Generation = Input.takeU32();
        Handle.Index = Input.takeU64();
      }
      const DynCodeHandleKind Expected =
          static_cast<DynCodeHandleKind>(1 + (Input.takeByte() % 4));
      if (auto R = Plan.resolve(Handle, Expected))
        (void)*R;
      else
        consume(R.takeError());
      break;
    }
    }
  }

  // Contract: rebuilding the plan bumps the generation and makes every handle
  // handed out before the rebuild stale.
  if (Input.takeByte() & 1) {
    const uint32_t Before = Plan.generation();
    Plan.rebuild();
    if (Plan.generation() == Before)
      abort();
    for (const DynCodeHandle &Handle : Handles) {
      if (auto R = Plan.resolve(Handle, Handle.Kind))
        abort();
      else
        consume(R.takeError());
    }
  }
  return 0;
}
