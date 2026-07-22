#include "MachOLinkGraphAdapter.h"

#include "Linker/MachO/InputFiles.h"
#include "neverc/Plugin/Host/BuiltinTargetProvider.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/SHA256.h"

using namespace llvm;
using namespace neverc::plugin;

namespace linker::macho {
namespace {

Error adapterError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "Mach-O LinkGraph adapter: " + Message);
}

NevercLinkInputKind inputKind(InputFile::Kind Kind) {
  switch (Kind) {
  case InputFile::ObjKind:
    return NEVERC_LINK_INPUT_OBJECT;
  case InputFile::ArchiveKind:
    return NEVERC_LINK_INPUT_ARCHIVE;
  case InputFile::DylibKind:
    return NEVERC_LINK_INPUT_SHARED_LIBRARY;
  case InputFile::BitcodeKind:
    return NEVERC_LINK_INPUT_BITCODE;
  case InputFile::OpaqueKind:
    return NEVERC_LINK_INPUT_BLOB;
  }
  return NEVERC_LINK_INPUT_UNKNOWN;
}

template <typename Map, typename Key, typename Add>
uint64_t ensureID(Map &IDs, Key Native, Add AddEntity) {
  auto It = IDs.find(Native);
  if (It != IDs.end())
    return It->second;
  const uint64_t ID = AddEntity().ID;
  IDs[Native] = ID;
  return ID;
}

} // namespace

MachOLinkGraphAdapter::MachOLinkGraphAdapter(
    PluginTaskContext &TaskValue, std::shared_ptr<PluginLinkGraph> GraphValue)
    : Task(TaskValue), Graph(std::move(GraphValue)) {}

MachOLinkGraphAdapter::~MachOLinkGraphAdapter() = default;

Expected<std::unique_ptr<MachOLinkGraphAdapter>>
MachOLinkGraphAdapter::create(PluginTaskContext &Task, StringRef TargetTriple,
                              StringRef CPU,
                              NevercTargetRelocationModel RelocationModel) {
  if (TargetTriple.empty())
    return adapterError("target triple is required");
  const BuiltinTargetRoute *Route = findBuiltinTargetRoute(TargetTriple);
  if (!Route || Route->ObjectFormat != BuiltinObjectFormat::MachO)
    return adapterError("target triple has no built-in Mach-O route");
  auto TargetKey =
      createBuiltinTargetKey(*Route, TargetTriple, CPU, RelocationModel);
  if (!TargetKey)
    return joinErrors(adapterError("could not create the target key"),
                      TargetKey.takeError());
  auto Graph = std::make_shared<PluginLinkGraph>(std::move(*TargetKey),
                                                 NEVERC_LINK_STATE_INITIAL);
  return std::unique_ptr<MachOLinkGraphAdapter>(
      new MachOLinkGraphAdapter(Task, std::move(Graph)));
}

Expected<std::shared_ptr<PluginLinkGraph>>
MachOLinkGraphAdapter::capture(const PluginLinkGraph &Previous,
                               NevercLinkState State) {
  auto Result = std::make_shared<PluginLinkGraph>(Previous);

  uint64_t Ordinal = 0;
  for (InputFile *File : machoInputFiles()) {
    if (!File)
      continue;
    const uint64_t ID = ensureID(InputIDs, File, [&]() -> PluginLinkInput & {
      return Result->addInput(PluginLinkInput{});
    });
    PluginLinkInput *Input = Result->findInput(ID);
    if (!Input)
      continue;
    Input->Kind = inputKind(File->kind());
    Input->Ordinal = Ordinal++;
    Input->LogicalURI =
        File->getName().empty() ? "<memory>" : File->getName().str();
    const StringRef Buffer = File->mb.getBuffer();
    Input->ContentDigest = SHA256::hash(ArrayRef<uint8_t>(
        reinterpret_cast<const uint8_t *>(Buffer.data()), Buffer.size()));
    Input->ReaderRoute = "neverc.builtin.macho";
    if (File->lazy)
      Input->Flags |= NEVERC_LINK_INPUT_FLAG_LAZY;
  }

  Result->advanceGeneration();
  Result->setState(State);
  if (Error E = verifyPluginLinkGraph(*Result))
    return joinErrors(adapterError("native projection is invalid"),
                      std::move(E));
  return Result;
}

Error MachOLinkGraphAdapter::applyDelta(const PluginLinkGraph &Before,
                                        const PluginLinkGraph &After,
                                        NevercLinkState State) {
  // This first-version adapter projects the frozen input set, which the native
  // Mach-O linker never mutates through the graph. A transparent link (no user
  // provider) therefore yields After == Before, so there is nothing to write
  // back. Section/symbol/relocation delta application mirrors the ELF and COFF
  // adapters and can be layered on as capture is enriched, at which point this
  // becomes strictly diff-based to preserve byte-identical native output.
  (void)Before;
  (void)After;
  (void)State;
  return Error::success();
}

} // namespace linker::macho
