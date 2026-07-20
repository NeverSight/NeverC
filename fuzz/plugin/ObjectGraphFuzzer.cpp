#include "PluginFrontendFuzzSupport.h"
#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace llvm;
using namespace neverc::fuzz;
using namespace neverc::plugin;

namespace {

void consume(Error E) {
  if (E)
    consumeError(std::move(E));
}

Expected<OwnedTargetKey> targetKey() {
  TargetKeyBuilder Builder;
  return Builder
      .setTargetID({UINT64_C(0x66757a7a74617267), 1})
      .setTriple("fuzz-unknown-none-none", "fuzz", "unknown",
                 "none", "none")
      .setCPU("generic", "")
      .setFeatures({})
      .setABI({UINT64_C(0x66757a7a61626900), 1})
      .setCallingConvention({UINT64_C(0x66757a7a63630000), 1})
      .setObjectFormat({UINT64_C(0x66757a7a6f626a00), 1})
      .setCodeGeneration(NEVERC_TARGET_RELOCATION_STATIC,
                         NEVERC_TARGET_CODE_MODEL_SMALL)
      .setExecution(NEVERC_TARGET_EXECUTION_USER, 64,
                    NEVERC_TARGET_ENDIAN_LITTLE)
      .setSchemaDigest(
          "0123456789abcdef0123456789abcdef"
          "0123456789abcdef0123456789abcdef")
      .build();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  ByteCursor Input(Data, Size);
  auto Key = targetKey();
  if (!Key) {
    consume(Key.takeError());
    return 0;
  }
  PluginObjectGraph Graph(std::move(*Key));
  std::vector<uint64_t> SectionIDs;

  static constexpr std::array<NevercObjectSectionKind, 5>
      SectionKinds = {
          NEVERC_OBJECT_SECTION_KIND_TEXT,
          NEVERC_OBJECT_SECTION_KIND_DATA,
          NEVERC_OBJECT_SECTION_KIND_ZERO_FILL,
          NEVERC_OBJECT_SECTION_KIND_DEBUG,
          NEVERC_OBJECT_SECTION_KIND_TLS_DATA};
  const unsigned SectionCount =
      std::min<unsigned>(Input.takeByte(), 12);
  for (unsigned I = 0; I != SectionCount; ++I) {
    PluginObjectSection Section;
    Section.ID = (Input.takeByte() & 1U) != 0
                     ? Graph.allocateEntityID()
                     : Input.takeU64();
    Section.Name = "section." + std::to_string(Input.takeU32());
    Section.Kind =
        SectionKinds[Input.takeByte() % SectionKinds.size()];
    Section.Flags = Input.takeU64();
    Section.Alignment = Input.takeU64();
    ArrayRef<uint8_t> Bytes = Input.takeBytes(128);
    Section.Data.assign(Bytes.begin(), Bytes.end());
    Section.ZeroFillSize = Input.takeU64();
    Section.ComdatID = Input.takeU64();
    SectionIDs.push_back(Section.ID);
    Graph.sections().push_back(std::move(Section));
  }

  const unsigned SymbolCount =
      std::min<unsigned>(Input.takeByte(), 16);
  for (unsigned I = 0; I != SymbolCount; ++I) {
    PluginObjectSymbol Symbol;
    Symbol.ID = (Input.takeByte() & 1U) != 0
                    ? Graph.allocateEntityID()
                    : Input.takeU64();
    Symbol.Name = "symbol." + std::to_string(Input.takeU32());
    Symbol.Binding =
        static_cast<NevercObjectSymbolBinding>(Input.takeU32());
    Symbol.Visibility =
        static_cast<NevercObjectSymbolVisibility>(Input.takeU32());
    Symbol.Type =
        static_cast<NevercObjectSymbolType>(Input.takeU32());
    Symbol.Definition =
        static_cast<NevercObjectSymbolDefinition>(Input.takeU32());
    if (!SectionIDs.empty())
      Symbol.SectionID =
          SectionIDs[Input.takeByte() % SectionIDs.size()];
    Symbol.Value = Input.takeU64();
    Symbol.Size = Input.takeU64();
    Symbol.Alignment = Input.takeU64();
    Symbol.Flags = Input.takeU64();
    Graph.symbols().push_back(std::move(Symbol));
  }

  const unsigned RelocationCount =
      std::min<unsigned>(Input.takeByte(), 16);
  for (unsigned I = 0; I != RelocationCount; ++I) {
    PluginObjectRelocation Relocation;
    Relocation.ID = (Input.takeByte() & 1U) != 0
                        ? Graph.allocateEntityID()
                        : Input.takeU64();
    if (!SectionIDs.empty())
      Relocation.SectionID =
          SectionIDs[Input.takeByte() % SectionIDs.size()];
    Relocation.Offset = Input.takeU64();
    Relocation.Kind =
        static_cast<NevercObjectRelocationKind>(Input.takeU32());
    Relocation.TargetKind =
        static_cast<NevercObjectRelocationTargetKind>(Input.takeU32());
    Relocation.Width = Input.takeU32();
    Relocation.IsPCRelative = (Input.takeByte() & 1U) != 0;
    Relocation.IsSigned = (Input.takeByte() & 1U) != 0;
    Relocation.Addend = static_cast<int64_t>(Input.takeU64());
    Relocation.TargetSymbolID = Input.takeU64();
    Relocation.TargetSectionID = Input.takeU64();
    Relocation.TargetValue = Input.takeU64();
    Graph.relocations().push_back(std::move(Relocation));
  }

  consume(verifyPluginObjectGraph(Graph));
  return 0;
}
