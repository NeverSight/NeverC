#include "BuiltinObjectWriterPreflight.h"

#include "neverc/Plugin/Host/AssemblySymbolName.h"
#include "neverc/Plugin/Host/ObjectSectionRole.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

Error invalid(StringRef Boundary, const Twine &Message) {
  return createStringError(errc::invalid_argument,
                           (Boundary + ": " + Message).str());
}

Error verifyName(StringRef Kind, StringRef Name, StringRef Boundary) {
  if (expressibleName(Name))
    return Error::success();
  return invalid(Boundary, Kind + " name cannot be represented by the "
                                  "built-in assembly writer");
}

Triple graphTriple(const PluginObjectGraph &Graph) {
  const NevercTargetKey Key = Graph.targetKey();
  const StringRef TripleText(Key.RawTriple.Data ? Key.RawTriple.Data : "",
                             static_cast<size_t>(Key.RawTriple.Length));
  return Triple(Triple::normalize(TripleText));
}

} // namespace

Error verifyBuiltinObjectWriterComdatRepresentability(
    const PluginObjectComdat &Comdat, StringRef Boundary) {
  return verifyName("COMDAT", Comdat.Name, Boundary);
}

Error verifyBuiltinObjectWriterSectionRepresentability(
    const PluginObjectSection &Section, const Triple &Target,
    StringRef Boundary) {
  if (Error E = verifyName("section", Section.Name, Boundary))
    return E;
  if (Section.Alignment > 1 &&
      (!isPowerOf2_64(Section.Alignment) || Log2_64(Section.Alignment) > 31))
    return invalid(Boundary, "section '" + Twine(Section.Name) +
                                 "' alignment cannot be represented by the "
                                 "built-in assembly writer");
  const bool TLS = Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_DATA ||
                   Section.Kind == NEVERC_OBJECT_SECTION_KIND_TLS_ZERO_FILL ||
                   (Section.Flags & NEVERC_OBJECT_SECTION_TLS) != 0;
  if (Target.isOSBinFormatELF() &&
      !elfNameAgreesWithFlags(
          Section.Name, (Section.Flags & NEVERC_OBJECT_SECTION_ALLOCATED) != 0,
          (Section.Flags & NEVERC_OBJECT_SECTION_WRITABLE) != 0,
          (Section.Flags & NEVERC_OBJECT_SECTION_EXECUTABLE) != 0, TLS))
    return invalid(Boundary, "section '" + Twine(Section.Name) +
                                 "' name implies native flags absent from "
                                 "the ObjectGraph");
  return Error::success();
}

Error verifyBuiltinObjectWriterSymbolNameRepresentability(StringRef Name,
                                                          const Triple &Target,
                                                          StringRef Boundary) {
  if (Error E = verifyName("symbol", Name, Boundary))
    return E;
  if (isPrivateLabelName(Name, Target))
    return invalid(Boundary, "symbol '" + Twine(Name) +
                                 "' uses an assembler-reserved private-label "
                                 "spelling");
  return Error::success();
}

Error verifyBuiltinObjectWriterGraphRepresentability(
    const PluginObjectGraph &Graph, StringRef Boundary) {
  const Triple Target = graphTriple(Graph);
  for (const PluginObjectComdat &Comdat : Graph.comdats())
    if (Error E =
            verifyBuiltinObjectWriterComdatRepresentability(Comdat, Boundary))
      return E;
  for (const PluginObjectSection &Section : Graph.sections())
    if (Error E = verifyBuiltinObjectWriterSectionRepresentability(
            Section, Target, Boundary))
      return E;
  for (const PluginObjectSymbol &Symbol : Graph.symbols())
    if (Error E = verifyBuiltinObjectWriterSymbolNameRepresentability(
            Symbol.Name, Target, Boundary))
      return E;
  return Error::success();
}

} // namespace neverc::plugin
