#include "LayoutVerifier.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <tuple>
#include <vector>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error layoutError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link layout verification: " + Message);
}

bool allowWritableExecutable(const PluginLinkGraph &Graph) {
  for (const PluginLinkConstraint &Constraint : Graph.constraints())
    if (Constraint.Kind == "allow-wx")
      return Constraint.Value != 0;
  return false;
}

bool addOverflows(uint64_t Left, uint64_t Right) {
  return Left > UINT64_MAX - Right;
}

// A zero-fill section (ELF SHT_NOBITS, and its Mach-O / COFF equivalents)
// occupies no bytes in the image file.  Its recorded file offset is only the
// conceptual placement the format asks for -- linkers set it to wherever the
// preceding section left the cursor and never pad it, so it routinely fails
// the alignment its memory image is held to.  Requiring otherwise rejects
// well-formed layouts: .bss is normally the section with the largest
// alignment in the graph and the one least likely to satisfy it.
bool isZeroFill(const PluginLinkSection &Section) {
  return Section.Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL ||
         Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL;
}

} // namespace

Error verifyLinkLayout(const PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_LAYOUT_COMPLETE)
    return layoutError("graph has no layout result");
  if (Error E = verifyPluginLinkGraph(Graph))
    return E;

  using Range = std::tuple<uint64_t, uint64_t, uint64_t>;
  std::vector<Range> AddressRanges;
  std::vector<Range> FileRanges;
  const bool AllowWX = allowWritableExecutable(Graph);
  for (const PluginLinkSection &Section : Graph.sections()) {
    if (!isPowerOf2_64(Section.Alignment) ||
        Section.Address % Section.Alignment != 0 ||
        (!isZeroFill(Section) && Section.FileOffset % Section.Alignment != 0))
      return layoutError("section alignment is invalid: " + Section.Name);
    if (addOverflows(Section.Address, Section.Size))
      return layoutError("section address range overflows: " + Section.Name);
    const bool Writable = (Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0;
    const bool Executable =
        (Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0;
    const bool Allocated =
        (Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0;
    if (!AllowWX && Writable && Executable)
      return layoutError("writable-executable section is forbidden: " +
                         Section.Name);
    // Non-allocated sections (for example ELF .symtab/.strtab) have no
    // runtime address. Multiple such sections legitimately use address zero,
    // so only allocated sections participate in the virtual-address proof.
    if (Allocated && Section.Size != 0)
      AddressRanges.emplace_back(Section.Address,
                                 Section.Address + Section.Size, Section.ID);

    uint64_t FileEnd = Section.FileOffset;
    for (const PluginLinkAtom &Atom : Graph.atoms()) {
      if (Atom.SectionID != Section.ID)
        continue;
      if ((Atom.Flags & NEVERC_LINK_ATOM_LIVE) == 0)
        continue;
      const uint64_t MemorySize = Atom.Content.size() + Atom.ZeroFillSize;
      if (!isPowerOf2_64(Atom.Alignment))
        return layoutError("atom alignment is invalid: " + Atom.Name);
      if (Atom.Address % Atom.Alignment != 0)
        return layoutError("atom address is misaligned: " + Atom.Name);
      if (Atom.Address < Section.Address ||
          addOverflows(Atom.Address, MemorySize) ||
          Atom.Address + MemorySize > Section.Address + Section.Size)
        return layoutError(Twine("atom '") + Atom.Name + "' at " +
                           Twine(Atom.Address) + " with size " +
                           Twine(MemorySize) + " is outside section '" +
                           Section.Name + "' at " + Twine(Section.Address) +
                           " with size " + Twine(Section.Size));
      if (!Atom.Content.empty()) {
        if (Atom.FileOffset < Section.FileOffset ||
            addOverflows(Atom.FileOffset, Atom.Content.size()))
          return layoutError("atom file range is invalid: " + Atom.Name);
        FileEnd = std::max(FileEnd, Atom.FileOffset + Atom.Content.size());
      }
    }
    if (FileEnd != Section.FileOffset)
      FileRanges.emplace_back(Section.FileOffset, FileEnd, Section.ID);
  }

  auto CheckDisjoint = [&](std::vector<Range> Ranges, StringRef Kind) -> Error {
    llvm::sort(Ranges);
    for (size_t Index = 1; Index < Ranges.size(); ++Index) {
      const Range &Previous = Ranges[Index - 1];
      const Range &Current = Ranges[Index];
      if (std::get<0>(Current) >= std::get<1>(Previous))
        continue;
      const PluginLinkSection *PreviousSection =
          Graph.findSection(std::get<2>(Previous));
      const PluginLinkSection *CurrentSection =
          Graph.findSection(std::get<2>(Current));
      return layoutError(
          Kind + " ranges overlap between '" +
          (PreviousSection ? PreviousSection->Name : "<unknown>") + "' and '" +
          (CurrentSection ? CurrentSection->Name : "<unknown>") + "'");
    }
    return Error::success();
  };
  if (Error E = CheckDisjoint(std::move(AddressRanges), "address"))
    return E;
  if (Error E = CheckDisjoint(std::move(FileRanges), "file"))
    return E;
  return Error::success();
}

} // namespace neverc::plugin
