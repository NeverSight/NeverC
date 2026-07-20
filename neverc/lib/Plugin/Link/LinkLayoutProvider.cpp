#include "LinkLayoutProvider.h"
#include "LayoutVerifier.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <map>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error layoutError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link layout provider: " + Message);
}

Expected<uint64_t> aligned(uint64_t Value, uint64_t Alignment) {
  if (!isPowerOf2_64(Alignment))
    return layoutError("alignment is not a power of two");
  const uint64_t Mask = Alignment - 1;
  if (Value > UINT64_MAX - Mask)
    return layoutError("aligned range overflows");
  return alignTo(Value, Alignment);
}

bool checkedAdd(uint64_t &Value, uint64_t Increment) {
  if (Value > UINT64_MAX - Increment)
    return false;
  Value += Increment;
  return true;
}

} // namespace

Expected<LinkLayoutResult>
layoutLinkGraph(PluginLinkGraph &Graph,
                const LinkLayoutOptions &Options) {
  if (Graph.state() < NEVERC_LINK_STATE_THUNKS_RELAXED)
    return layoutError("thunk relaxation is not complete");
  if (Graph.state() > NEVERC_LINK_STATE_LAYOUT_COMPLETE)
    return layoutError(
        "later phases must be invalidated before layout");

  uint64_t ImageBase = Options.ImageBase;
  uint64_t FileBase = Options.FileBase;
  uint64_t PageSize = Options.PageSize;
  bool AllowWX = !Options.EnforceWritableXorExecutable;
  std::map<uint64_t, uint64_t> FixedAddress;
  std::map<uint64_t, uint64_t> FixedFileOffset;
  for (const PluginLinkConstraint &Constraint : Graph.constraints()) {
    if (Constraint.Kind == "image-base")
      ImageBase = Constraint.Value;
    else if (Constraint.Kind == "file-base")
      FileBase = Constraint.Value;
    else if (Constraint.Kind == "page-size")
      PageSize = Constraint.Value;
    else if (Constraint.Kind == "allow-wx")
      AllowWX = Constraint.Value != 0;
    else if (Constraint.Kind == "section-address")
      FixedAddress[Constraint.SubjectID] = Constraint.Value;
    else if (Constraint.Kind == "section-file-offset")
      FixedFileOffset[Constraint.SubjectID] = Constraint.Value;
    else if (Constraint.Required)
      return layoutError("required layout constraint is unknown: " +
                         Constraint.Kind);
  }
  if (!isPowerOf2_64(PageSize))
    return layoutError("page size is not a power of two");
  auto AddressStart = aligned(ImageBase, PageSize);
  auto FileStart = aligned(FileBase, PageSize);
  if (!AddressStart)
    return AddressStart.takeError();
  if (!FileStart)
    return FileStart.takeError();
  uint64_t AddressCursor = *AddressStart;
  uint64_t FileCursor = *FileStart;

  for (PluginLinkSection &Section : Graph.sections()) {
    const bool Writable =
        (Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0;
    const bool Executable =
        (Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0;
    if (!AllowWX && Writable && Executable)
      return layoutError("writable-executable section is forbidden: " +
                         Section.Name);
    auto NextAddress = aligned(AddressCursor, Section.Alignment);
    auto NextFile = aligned(FileCursor, Section.Alignment);
    if (!NextAddress)
      return NextAddress.takeError();
    if (!NextFile)
      return NextFile.takeError();
    if (auto It = FixedAddress.find(Section.ID);
        It != FixedAddress.end()) {
      if (It->second < *NextAddress ||
          It->second % Section.Alignment != 0)
        return layoutError("fixed section address is invalid: " +
                           Section.Name);
      *NextAddress = It->second;
    }
    if (auto It = FixedFileOffset.find(Section.ID);
        It != FixedFileOffset.end()) {
      if (It->second < *NextFile ||
          It->second % Section.Alignment != 0)
        return layoutError("fixed section file offset is invalid: " +
                           Section.Name);
      *NextFile = It->second;
    }
    Section.Address = *NextAddress;
    Section.FileOffset = *NextFile;

    uint64_t MemoryCursor = 0;
    uint64_t SectionFileSize = 0;
    for (PluginLinkAtom &Atom : Graph.atoms()) {
      if (Atom.SectionID != Section.ID)
        continue;
      auto AtomMemory = aligned(MemoryCursor, Atom.Alignment);
      auto AtomFile = aligned(SectionFileSize, Atom.Alignment);
      if (!AtomMemory)
        return AtomMemory.takeError();
      if (!AtomFile)
        return AtomFile.takeError();
      MemoryCursor = *AtomMemory;
      SectionFileSize = *AtomFile;
      if (Section.Address > UINT64_MAX - MemoryCursor ||
          Section.FileOffset > UINT64_MAX - SectionFileSize)
        return layoutError("atom coordinate overflows: " + Atom.Name);
      Atom.Address = Section.Address + MemoryCursor;
      Atom.FileOffset = Section.FileOffset + SectionFileSize;
      if (!checkedAdd(MemoryCursor,
                      Atom.Content.size() + Atom.ZeroFillSize) ||
          !checkedAdd(SectionFileSize, Atom.Content.size()))
        return layoutError("atom range overflows: " + Atom.Name);
    }
    Section.Size = std::max(Section.Size, MemoryCursor);
    AddressCursor = Section.Address;
    FileCursor = Section.FileOffset;
    if (!checkedAdd(AddressCursor, Section.Size) ||
        !checkedAdd(FileCursor, SectionFileSize))
      return layoutError("section range overflows: " + Section.Name);
  }

  Graph.advanceGeneration();
  Graph.setState(NEVERC_LINK_STATE_LAYOUT_COMPLETE);
  if (AllowWX) {
    bool Recorded = false;
    for (const PluginLinkConstraint &Constraint : Graph.constraints())
      Recorded |= Constraint.Kind == "allow-wx";
    if (!Recorded) {
      PluginLinkConstraint Constraint;
      Constraint.Kind = "allow-wx";
      Constraint.Value = 1;
      Graph.addConstraint(std::move(Constraint));
      Graph.advanceGeneration();
    }
  }
  if (Error E = verifyLinkLayout(Graph))
    return std::move(E);

  LinkLayoutResult Result;
  Result.GraphGeneration = Graph.generation();
  Result.ImageBase = *AddressStart;
  for (const PluginLinkSymbol &Symbol : Graph.symbols()) {
    if (!Symbol.IsRoot || Symbol.AtomID == 0)
      continue;
    if (const PluginLinkAtom *Atom = Graph.findAtom(Symbol.AtomID)) {
      Result.EntryAddress = Atom->Address + Symbol.Value;
      if (Symbol.Name == "entry")
        break;
    }
  }
  Result.RangeDigest = Graph.semanticDigest();
  return Result;
}

} // namespace neverc::plugin
