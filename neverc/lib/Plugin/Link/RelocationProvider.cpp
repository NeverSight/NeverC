#include "RelocationProvider.h"
#include "RelocationVerifier.h"
#include "neverc/Plugin/Host/BuiltinObjectExtension.h"
#include "neverc/Plugin/Host/NativeRelocationFacts.h"
#include "llvm/Support/Errc.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <optional>

using namespace llvm;

namespace neverc::plugin {
namespace {

Error relocationError(const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           "link relocation provider: " + Message);
}

// Applying a relocation replaces every byte it covers with the computed value,
// and that is only what the relocation asks for when those bytes are a field
// of their own. An AArch64 CALL26 says it is 32 bits wide because that is the
// size of the instruction word its 26-bit offset sits in; writing the offset
// over the word takes the opcode with it, and what was a `bl` becomes whatever
// the displacement happens to encode. Nothing downstream notices -- the value
// fits, the image lays out, and the bytes are simply different code.
//
// Which relocations those are is not something the graph's own Kind and Width
// can say: they describe the field, not its surroundings. The native type
// does, and the object reader records it in the relocation's extension. A
// graph built without one -- by a plugin, or by a producer other than the
// builtin reader -- keeps the earlier behaviour, since there is nothing to
// read and the graph's description is all there is.
std::optional<uint64_t> nativeRelocationType(const PluginLinkEdge &Edge,
                                             NevercObjectFormatID FormatID) {
  for (const PluginLinkExtensionData &Extension : Edge.Extensions.values()) {
    if (Extension.NamespaceID.High != FormatID.High ||
        Extension.NamespaceID.Low != FormatID.Low)
      continue;
    if (std::optional<uint64_t> Type =
            builtinext::nativeRelocationType(Extension.Payload))
      return Type;
  }
  return std::nullopt;
}

bool coversWholeBytes(const PluginLinkGraph &Graph,
                      const PluginLinkEdge &Edge) {
  const NevercTargetKey Key = Graph.targetKey();
  const std::optional<uint64_t> Type =
      nativeRelocationType(Edge, Key.ObjectFormatID);
  if (!Type)
    return true;
  const Triple Target(StringRef(Key.RawTriple.Data,
                                static_cast<size_t>(Key.RawTriple.Length)));
  // A target the tables say nothing about is not one whose objects this should
  // refuse to link -- the absent answer is about the tables, not the
  // relocation. Where they do cover the target, an unrecognised type is the
  // relocation's own answer, and one whose form is unknown cannot be shown to
  // be safe to overwrite.
  if (!haveNativeRelocationTable(Target))
    return true;
  return nativeRelocationFieldIsWholeBytes(Target, *Type).value_or(false);
}

uint64_t imageBase(const PluginLinkGraph &Graph) {
  uint64_t Result = UINT64_MAX;
  for (const PluginLinkSection &Section : Graph.sections())
    if ((Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0)
      Result = std::min(Result, Section.Address);
  return Result == UINT64_MAX ? 0 : Result;
}

uint64_t widthMask(uint32_t Width) {
  return Width >= 64 ? UINT64_MAX : (UINT64_C(1) << Width) - 1;
}

// Value carries the encoded fixup as a 64-bit two's-complement bit pattern.
// Width is guaranteed to be in [1, 64] by the caller, so 64-bit arithmetic is
// sufficient and portable across toolchains that lack a native 128-bit integer
// type (e.g. MSVC).
bool fitsValue(uint64_t Value, uint32_t Width, bool Signed) {
  if (Width == 0 || Width > 64)
    return false;
  if (Width == 64)
    return true;
  if (Signed) {
    const int64_t SignedValue = static_cast<int64_t>(Value);
    const int64_t Minimum = -(INT64_C(1) << (Width - 1));
    const int64_t Maximum = (INT64_C(1) << (Width - 1)) - 1;
    return SignedValue >= Minimum && SignedValue <= Maximum;
  }
  return (Value >> Width) == 0;
}

} // namespace

Expected<LinkRelocationValue>
evaluateLinkRelocation(const PluginLinkGraph &Graph,
                       const PluginLinkEdge &Edge) {
  const PluginLinkAtom *Source = Graph.findAtom(Edge.SourceAtomID);
  if (!Source)
    return relocationError("source atom is missing");
  if (Edge.Width == 0 || Edge.Width > 64 ||
      Edge.Width % 8 != 0 ||
      Edge.Offset > Source->Content.size() ||
      Edge.Width / 8 > Source->Content.size() - Edge.Offset)
    return relocationError("fixup is outside initialized source bytes");

  LinkRelocationValue Result;
  Result.Place = Source->Address + Edge.Offset;
  const PluginLinkSymbol *TargetSymbol =
      Graph.findSymbol(Edge.TargetSymbolID);
  const PluginLinkAtom *TargetAtom =
      Edge.TargetAtomID != 0
          ? Graph.findAtom(Edge.TargetAtomID)
          : TargetSymbol
                ? Graph.findAtom(TargetSymbol->AtomID)
                : nullptr;
  if ((TargetSymbol &&
       (TargetSymbol->Definition == NEVERC_LINK_SYMBOL_UNDEFINED ||
        TargetSymbol->IsImported)) ||
      Edge.RelocationKind == NEVERC_OBJECT_RELOCATION_TLS ||
      Edge.RelocationKind ==
          NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION) {
    Result.Dynamic = true;
    return Result;
  }
  if (!coversWholeBytes(Graph, Edge))
    return relocationError(
        "relocation patches a field inside an instruction, which cannot be "
        "written by replacing the bytes it covers");
  if (!TargetAtom)
    return relocationError("relocation target is missing");
  Result.Target = TargetAtom->Address;
  if (TargetSymbol)
    Result.Target += TargetSymbol->Value;

  // Relocation arithmetic is performed modulo 2^64. Field widths never exceed
  // 64 bits (validated above), so 64-bit two's-complement math reproduces the
  // encoded value on every toolchain, including those without a native 128-bit
  // integer type (e.g. MSVC).
  uint64_t Value = Result.Target + static_cast<uint64_t>(Edge.Addend);
  if (Edge.RelocationKind ==
          NEVERC_OBJECT_RELOCATION_PC_RELATIVE ||
      Edge.IsPCRelative)
    Value -= Result.Place;
  else if (Edge.RelocationKind ==
           NEVERC_OBJECT_RELOCATION_SECTION_RELATIVE) {
    const PluginLinkSection *Section =
        Graph.findSection(TargetAtom->SectionID);
    if (!Section)
      return relocationError("target section is missing");
    Value -= Section->Address;
  } else if (Edge.RelocationKind ==
             NEVERC_OBJECT_RELOCATION_IMAGE_RELATIVE)
    Value -= imageBase(Graph);

  if (!fitsValue(Value, Edge.Width, Edge.IsSigned))
    return relocationError("relocation value overflows its field");
  Result.EncodedValue = Value & widthMask(Edge.Width);
  return Result;
}

Expected<std::vector<LinkRelocationRecord>>
applyLinkRelocations(PluginLinkGraph &Graph) {
  if (Graph.state() < NEVERC_LINK_STATE_LAYOUT_COMPLETE)
    return relocationError("layout is not complete");
  if (Graph.state() > NEVERC_LINK_STATE_RELOCATIONS_APPLIED)
    return relocationError(
        "later phases must be invalidated before relocation");

  std::vector<LinkRelocationRecord> Records;
  const bool LittleEndian =
      Graph.targetKey().Endianness == NEVERC_TARGET_ENDIAN_LITTLE;
  for (PluginLinkEdge &Edge : Graph.edges()) {
    if (Edge.Kind != NEVERC_LINK_EDGE_RELOCATION)
      continue;
    auto Value = evaluateLinkRelocation(Graph, Edge);
    if (!Value)
      return Value.takeError();
    LinkRelocationRecord Record;
    Record.EdgeID = Edge.ID;
    Record.Place = Value->Place;
    Record.Target = Value->Target;
    Record.EncodedValue = Value->EncodedValue;
    Record.Width = Edge.Width;
    Record.Dynamic = Value->Dynamic;
    if (!Value->Dynamic) {
      PluginLinkAtom *Source = Graph.findAtom(Edge.SourceAtomID);
      const uint32_t ByteCount = Edge.Width / 8;
      for (uint32_t Index = 0; Index != ByteCount; ++Index) {
        const uint32_t Shift =
            LittleEndian ? Index * 8 : (ByteCount - Index - 1) * 8;
        Source->Content[Edge.Offset + Index] =
            static_cast<uint8_t>(Value->EncodedValue >> Shift);
      }
      Record.Applied = true;
    }
    Records.push_back(Record);
  }
  Graph.advanceGeneration();
  Graph.setState(NEVERC_LINK_STATE_RELOCATIONS_APPLIED);
  if (Error E = verifyLinkRelocations(Graph))
    return std::move(E);
  return Records;
}

} // namespace neverc::plugin
