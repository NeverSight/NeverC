#include "neverc/Plugin/Host/ObjectGraph.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <limits>
#include <set>
#include <string>

using namespace llvm;

namespace neverc::plugin {
namespace {

bool isNullID(NevercInterfaceID ID) {
  return ID.High == 0 && ID.Low == 0;
}

bool sameID(NevercInterfaceID Left, NevercInterfaceID Right) {
  return Left.High == Right.High && Left.Low == Right.Low;
}

bool isPowerOfTwo(uint64_t Value) {
  return Value != 0 && (Value & (Value - 1)) == 0;
}

bool isDigest(StringRef Value) {
  if (Value.size() != 64)
    return false;
  for (char Character : Value)
    if (!isHexDigit(Character))
      return false;
  return true;
}

Error invalid(const Twine &Message) {
  return createStringError(inconvertibleErrorCode(), Message);
}

Error verifyExtension(const PluginObjectExtension &Extension,
                      NevercObjectFormatID FormatID,
                      StringRef EntityName) {
  if (Extension.empty()) {
    if (!isNullID(Extension.Owner) || Extension.Version != 0)
      return invalid(EntityName +
                     " has extension metadata without a payload");
    return Error::success();
  }
  if (isNullID(Extension.Owner))
    return invalid(EntityName + " has an ownerless extension");
  if (!sameID(Extension.Owner, FormatID))
    return invalid(EntityName + " extension owner does not match format");
  return Error::success();
}

uint64_t sectionSize(const PluginObjectSection &Section) {
  return static_cast<uint64_t>(Section.Data.size()) +
         Section.ZeroFillSize;
}

bool knownSectionKind(NevercObjectSectionKind Kind) {
  return Kind >= NEVERC_OBJECT_SECTION_KIND_TEXT &&
         Kind <= NEVERC_OBJECT_SECTION_KIND_FORMAT_EXTENSION;
}

bool knownSymbolBinding(NevercObjectSymbolBinding Binding) {
  return Binding >= NEVERC_OBJECT_SYMBOL_BINDING_LOCAL &&
         Binding <= NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION;
}

bool knownSymbolVisibility(NevercObjectSymbolVisibility Visibility) {
  return Visibility >= NEVERC_OBJECT_SYMBOL_VISIBILITY_DEFAULT &&
         Visibility <= NEVERC_OBJECT_SYMBOL_VISIBILITY_FORMAT_EXTENSION;
}

bool knownSymbolType(NevercObjectSymbolType Type) {
  return Type >= NEVERC_OBJECT_SYMBOL_TYPE_NO_TYPE &&
         Type <= NEVERC_OBJECT_SYMBOL_TYPE_FORMAT_EXTENSION;
}

bool knownSymbolDefinition(NevercObjectSymbolDefinition Definition) {
  return Definition >= NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED &&
         Definition <= NEVERC_OBJECT_SYMBOL_DEFINITION_ABSOLUTE;
}

bool knownRelocationKind(NevercObjectRelocationKind Kind) {
  return Kind >= NEVERC_OBJECT_RELOCATION_ABSOLUTE &&
         Kind <= NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION;
}

bool knownComdatSelection(NevercObjectComdatSelection Selection) {
  return Selection >= NEVERC_OBJECT_COMDAT_ANY &&
         Selection <= NEVERC_OBJECT_COMDAT_ASSOCIATIVE;
}

} // namespace

Error verifyPluginObjectGraph(const PluginObjectGraph &Graph) {
  NevercTargetKey Target = Graph.targetKey();
  if (Target.Header.StructSize < sizeof(Target) ||
      Target.Header.Major != NEVERC_TARGET_API_MAJOR ||
      Target.Header.Minor > NEVERC_TARGET_API_MINOR)
    return invalid("ObjectGraph has an invalid TargetKey header");
  if (isNullID(Target.TargetID))
    return invalid("ObjectGraph target ID is null");
  if (isNullID(Target.ObjectFormatID))
    return invalid("ObjectGraph format ID is null");
  StringRef TargetDigest(Target.SchemaDigest.Data,
                         static_cast<size_t>(Target.SchemaDigest.Length));
  if (!isDigest(TargetDigest))
    return invalid("ObjectGraph target schema digest is invalid");

  std::set<uint64_t> EntityIDs;
  const auto VerifyID = [&](uint64_t ID, StringRef Name) -> Error {
    if (ID == 0 || !EntityIDs.insert(ID).second)
      return invalid("ObjectGraph has invalid or duplicate " + Name +
                     " ID");
    return Error::success();
  };

  std::set<std::string> ComdatNames;
  for (const PluginObjectComdat &Comdat : Graph.comdats()) {
    if (Error E = VerifyID(Comdat.ID, "COMDAT"))
      return E;
    if (Comdat.Name.empty() ||
        !ComdatNames.insert(Comdat.Name).second)
      return invalid("ObjectGraph has an empty or duplicate COMDAT name");
    if (!knownComdatSelection(Comdat.Selection))
      return invalid("ObjectGraph has an unknown COMDAT selection");
    if (Error E =
            verifyExtension(Comdat.Extension, Target.ObjectFormatID,
                            "COMDAT"))
      return E;
  }

  for (const PluginObjectComdat &Comdat : Graph.comdats()) {
    if (Comdat.Selection == NEVERC_OBJECT_COMDAT_ASSOCIATIVE) {
      if (Comdat.AssociatedComdatID == 0 ||
          Comdat.AssociatedComdatID == Comdat.ID ||
          !Graph.findComdat(Comdat.AssociatedComdatID))
        return invalid("associative COMDAT has an invalid parent");
    } else if (Comdat.AssociatedComdatID != 0) {
      return invalid("non-associative COMDAT has an associated parent");
    }
  }

  for (const PluginObjectSection &Section : Graph.sections()) {
    if (Error E = VerifyID(Section.ID, "section"))
      return E;
    if (Section.Name.empty() || !knownSectionKind(Section.Kind))
      return invalid("ObjectGraph section has invalid identity");
    if (!isPowerOfTwo(Section.Alignment))
      return invalid("ObjectGraph section alignment is not a power of two");
    if (Section.ZeroFillSize >
        std::numeric_limits<uint64_t>::max() - Section.Data.size())
      return invalid("ObjectGraph section size overflows");
    const bool IsZeroFill =
        Section.Kind == NEVERC_OBJECT_SECTION_KIND_ZERO_FILL ||
        Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL;
    if (IsZeroFill && !Section.Data.empty())
      return invalid("zero-fill section contains initialized bytes");
    if (!IsZeroFill && Section.ZeroFillSize != 0)
      return invalid("initialized section contains zero-fill storage");
    const bool IsTLS =
        Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA ||
        Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL;
    if (IsTLS !=
        ((Section.Flags & NEVERC_OBJECT_SECTION_TLS) != 0))
      return invalid("ObjectGraph section TLS kind and flags disagree");
    if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_DEBUG &&
        (Section.Flags & NEVERC_OBJECT_SECTION_DEBUG) == 0)
      return invalid("debug section is missing its stable debug flag");
    if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_UNWIND &&
        (Section.Flags & NEVERC_OBJECT_SECTION_UNWIND) == 0)
      return invalid("unwind section is missing its stable unwind flag");
    if (Section.ComdatID != 0 && !Graph.findComdat(Section.ComdatID))
      return invalid("ObjectGraph section references a missing COMDAT");
    if (Error E =
            verifyExtension(Section.Extension, Target.ObjectFormatID,
                            "section"))
      return E;
    if (Section.Kind == NEVERC_OBJECT_SECTION_KIND_FORMAT_EXTENSION &&
        Section.Extension.empty())
      return invalid("format-extension section has no extension payload");
  }

  std::set<std::string> StrongDefinitions;
  for (const PluginObjectSymbol &Symbol : Graph.symbols()) {
    if (Error E = VerifyID(Symbol.ID, "symbol"))
      return E;
    if (Symbol.Name.empty() ||
        !knownSymbolBinding(Symbol.Binding) ||
        !knownSymbolVisibility(Symbol.Visibility) ||
        !knownSymbolType(Symbol.Type) ||
        !knownSymbolDefinition(Symbol.Definition))
      return invalid("ObjectGraph symbol has invalid stable metadata");
    if (!isPowerOfTwo(Symbol.Alignment))
      return invalid("ObjectGraph symbol alignment is not a power of two");
    if (Symbol.ComdatID != 0 && !Graph.findComdat(Symbol.ComdatID))
      return invalid("ObjectGraph symbol references a missing COMDAT");
    const PluginObjectSection *Section =
        Symbol.SectionID == 0 ? nullptr
                              : Graph.findSection(Symbol.SectionID);
    if (Symbol.SectionID != 0 && !Section)
      return invalid("ObjectGraph symbol references a missing section");
    if (Symbol.Definition == NEVERC_OBJECT_SYMBOL_DEFINITION_DEFINED) {
      if (!Section)
        return invalid("defined ObjectGraph symbol has no section");
      const uint64_t Size = sectionSize(*Section);
      if (Symbol.Value > Size || Symbol.Size > Size - Symbol.Value)
        return invalid("ObjectGraph symbol exceeds its section");
    } else if (Section) {
      return invalid("non-section ObjectGraph symbol owns a section");
    }
    if (Symbol.Definition ==
            NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED &&
        (Symbol.Flags & NEVERC_OBJECT_SYMBOL_EXPORTED) != 0)
      return invalid("undefined ObjectGraph symbol is exported");
    if (Symbol.Definition !=
            NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED &&
        (Symbol.Flags & NEVERC_OBJECT_SYMBOL_IMPORTED) != 0)
      return invalid("defined ObjectGraph symbol is imported");
    if (Symbol.Type == NEVERC_OBJECT_SYMBOL_TYPE_TLS) {
      if (!Section ||
          (Section->Kind != NEVERC_OBJECT_SECTION_KIND_TLS_DATA &&
           Section->Kind !=
               NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL))
        return invalid("TLS symbol does not belong to a TLS section");
    }
    if (Error E =
            verifyExtension(Symbol.Extension, Target.ObjectFormatID,
                            "symbol"))
      return E;
    if ((Symbol.Binding ==
             NEVERC_OBJECT_SYMBOL_BINDING_FORMAT_EXTENSION ||
         Symbol.Visibility ==
             NEVERC_OBJECT_SYMBOL_VISIBILITY_FORMAT_EXTENSION ||
         Symbol.Type == NEVERC_OBJECT_SYMBOL_TYPE_FORMAT_EXTENSION) &&
        Symbol.Extension.empty())
      return invalid("format-extension symbol has no extension payload");
    const bool Strong =
        Symbol.Definition !=
            NEVERC_OBJECT_SYMBOL_DEFINITION_UNDEFINED &&
        Symbol.Definition != NEVERC_OBJECT_SYMBOL_DEFINITION_COMMON &&
        Symbol.Binding != NEVERC_OBJECT_SYMBOL_BINDING_LOCAL &&
        Symbol.Binding != NEVERC_OBJECT_SYMBOL_BINDING_WEAK;
    if (Strong && !StrongDefinitions.insert(Symbol.Name).second)
      return invalid("ObjectGraph has duplicate strong symbol definitions");
  }

  for (const PluginObjectRelocation &Relocation :
       Graph.relocations()) {
    if (Error E = VerifyID(Relocation.ID, "relocation"))
      return E;
    const PluginObjectSection *Section =
        Graph.findSection(Relocation.SectionID);
    if (!Section)
      return invalid("ObjectGraph relocation references a missing section");
    if (!knownRelocationKind(Relocation.Kind) ||
        Relocation.Width == 0 || Relocation.Width > 128 ||
        (Relocation.Width % 8) != 0)
      return invalid("ObjectGraph relocation has invalid width or kind");
    const uint64_t ByteWidth = Relocation.Width / 8;
    const uint64_t Size = sectionSize(*Section);
    if (Relocation.Offset > Size ||
        ByteWidth > Size - Relocation.Offset)
      return invalid("ObjectGraph relocation exceeds its section");
    if (Relocation.Kind == NEVERC_OBJECT_RELOCATION_PC_RELATIVE &&
        !Relocation.IsPCRelative)
      return invalid("PC-relative relocation is missing its stable flag");
    switch (Relocation.TargetKind) {
    case NEVERC_OBJECT_RELOCATION_TARGET_SYMBOL:
      if (Relocation.TargetSymbolID == 0 ||
          !Graph.findSymbol(Relocation.TargetSymbolID) ||
          Relocation.TargetSectionID != 0 ||
          Relocation.TargetExtensionKind != 0)
        return invalid("ObjectGraph relocation has an invalid symbol target");
      break;
    case NEVERC_OBJECT_RELOCATION_TARGET_SECTION:
      if (Relocation.TargetSectionID == 0 ||
          !Graph.findSection(Relocation.TargetSectionID) ||
          Relocation.TargetSymbolID != 0 ||
          Relocation.TargetExtensionKind != 0)
        return invalid("ObjectGraph relocation has an invalid section target");
      break;
    case NEVERC_OBJECT_RELOCATION_TARGET_ABSOLUTE:
      if (Relocation.TargetSymbolID != 0 ||
          Relocation.TargetSectionID != 0 ||
          Relocation.TargetExtensionKind != 0)
        return invalid("ObjectGraph relocation has an invalid absolute target");
      break;
    case NEVERC_OBJECT_RELOCATION_TARGET_FORMAT_EXTENSION:
      if (Relocation.TargetSymbolID != 0 ||
          Relocation.TargetSectionID != 0 ||
          Relocation.TargetExtensionKind == 0 ||
          Relocation.Extension.empty())
        return invalid("ObjectGraph relocation has an invalid format target");
      break;
    default:
      return invalid("ObjectGraph relocation has an unknown target kind");
    }
    if (Error E =
            verifyExtension(Relocation.Extension, Target.ObjectFormatID,
                            "relocation"))
      return E;
    if (Relocation.Kind ==
            NEVERC_OBJECT_RELOCATION_TARGET_EXTENSION &&
        Relocation.Extension.empty())
      return invalid("target relocation has no extension payload");
  }

  if (const PluginObjectLayoutProof *Proof = Graph.layoutProof()) {
    if (Proof->GraphGeneration != Graph.generation() ||
        !sameID(Proof->TargetID, Target.TargetID) ||
        !sameID(Proof->FormatID, Target.ObjectFormatID))
      return invalid("ObjectGraph layout proof is stale or foreign");
  }
  return Error::success();
}

} // namespace neverc::plugin
