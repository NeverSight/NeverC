// Fuzz the bounded dyncode image builder.
//
// DynCodeImage replaces the old raw ``uint8_t** + Len + Capacity`` path with
// checked read/write/insert/append/resize/replace-range helpers whose arithmetic
// is overflow- and budget-checked, plus a small state machine.  This fuzzer
// drives a random sequence of those operations against an arbitrary (optionally
// budgeted) image and asserts two invariants: no operation ever crashes, and the
// image size never exceeds a declared budget.  Only the host builder runs.

#include "PluginFrontendFuzzSupport.h"
#include "neverc/DynCode/Extractor/DynCodeImage.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cstdint>
#include <optional>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::dyncode;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);
  DynCodeImage Image;

  if (Input.takeByte() & 1)
    Image.setBudget(Input.takeU64() % 65536);
  else
    Image.setBudget(std::nullopt);

  const unsigned Ops = std::min<unsigned>(Input.takeByte(), 96);
  for (unsigned I = 0; I != Ops && !Input.empty(); ++I) {
    switch (Input.takeByte() % 9) {
    case 0:
      consume(Image.append(Input.takeBytes(64)));
      break;
    case 1: {
      const uint64_t Offset = Input.takeU64();
      consume(Image.write(Offset, Input.takeBytes(64)));
      break;
    }
    case 2: {
      const uint64_t Offset = Input.takeU64();
      consume(Image.insert(Offset, Input.takeBytes(64)));
      break;
    }
    case 3: {
      // resize is the only absolute allocation driver; bound the requested size
      // so an unbudgeted image cannot ask for a genuinely huge (OOM) allocation
      // -- the real pipeline always sizes images against a MaxLength budget.
      const uint64_t NewSize = Input.takeU64() % (UINT64_C(1) << 20);
      consume(Image.resize(NewSize, Input.takeByte()));
      break;
    }
    case 4: {
      const uint64_t Offset = Input.takeU64();
      const uint64_t Length = Input.takeU64();
      consume(Image.replaceRange(Offset, Length, Input.takeBytes(64)));
      break;
    }
    case 5: {
      const uint64_t Offset = Input.takeU64();
      const uint64_t Length = Input.takeU64();
      if (auto R = Image.read(Offset, Length))
        (void)*R;
      else
        consume(R.takeError());
      break;
    }
    case 6:
      consume(Image.setOutputAlignment(Input.takeU64()));
      break;
    case 7:
      Image.setEntry(Input.takeU64(), "entry");
      Image.setPaddingSize(Input.takeU64());
      break;
    case 8:
      Image.republish();
      (void)Image.digest();
      break;
    }

    // Every growth operation is budget-checked, so the image must never exceed
    // its declared budget.
    if (std::optional<uint64_t> Budget = Image.budget())
      if (Image.size() > *Budget)
        abort();
  }

  switch (Input.takeByte() % 4) {
  case 0:
    consume(Image.markVerified());
    break;
  case 1:
    consume(Image.markVerified());
    consume(Image.markCommitted());
    break;
  case 2:
    consume(Image.markAborted());
    break;
  case 3:
    Image.markFailedPartial();
    break;
  }
  return 0;
}
