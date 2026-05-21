#include "Linker/COFF/InputFiles.h"
#include "Linker/COFF/COFFLinkerContext.h"
#include "Linker/COFF/Chunks.h"
#include "Linker/COFF/Config.h"
#include "Linker/COFF/Driver.h"
#include "Linker/COFF/SymbolTable.h"
#include "Linker/COFF/Symbols.h"
#include "Linker/Core/Support/Dwarf.h"
#include "neverc/Foundation/OverrideNames.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"
#include "llvm/TargetParser/Triple.h"
#include <optional>
#include <utility>

using namespace llvm;
using namespace llvm::COFF;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace linker;
using namespace linker::coff;

using llvm::Triple;
using llvm::support::ulittle32_t;

// ===----------------------------------------------------------------------===
// Input file processing
// ===----------------------------------------------------------------------===

namespace {
StringRef getBasename(StringRef path) {
  return sys::path::filename(path, sys::path::Style::windows);
}
} // namespace

// Returns a string in the format of "foo.obj" or "foo.obj(bar.lib)".
std::string linker::toString(const coff::InputFile *file) {
  if (!file)
    return "<internal>";
  if (file->parentName.empty() || file->kind() == coff::InputFile::ImportKind)
    return std::string(file->getName());

  return (getBasename(file->parentName) + "(" + getBasename(file->getName()) +
          ")")
      .str();
}

/// Checks that Source is compatible with being a weak alias to Target.
/// If Source is Undefined and has no weak alias set, makes it a weak
/// alias to Target.
namespace {
void checkAndSetWeakAlias(COFFLinkerContext &ctx, InputFile *f, Symbol *source,
                          Symbol *target) {
  if (auto *u = dyn_cast<Undefined>(source)) {
    if (u->weakAlias && u->weakAlias != target) {
      ctx.symtab.reportDuplicate(source, f);
    }
    u->weakAlias = target;
  }
}

bool ignoredSymbolName(StringRef name) {
  return name == "@feat.00" || name == "@comp.id";
}
} // namespace

ArchiveFile::ArchiveFile(COFFLinkerContext &ctx, MemoryBufferRef m)
    : InputFile(ctx, ArchiveKind, m) {}

void ArchiveFile::parse() {
  // Parse a MemoryBufferRef as an archive file.
  file = CHECK(Archive::create(mb), this);

  // Read the symbol table to construct Lazy objects.
  for (const Archive::Symbol &sym : file->symbols())
    ctx.symtab.addLazyArchive(this, sym);
}

// Returns a buffer pointing to a member file containing a given symbol.
void ArchiveFile::addMember(const Archive::Symbol &sym) {
  const Archive::Child &c =
      CHECK(sym.getMember(),
            "could not get the member for symbol " + toCOFFString(ctx, sym));

  // Return an empty buffer if we have already returned the same buffer.
  if (!seen.insert(c.getChildOffset()).second)
    return;

  ctx.driver.enqueueArchiveMember(c, sym, getName());
}

std::vector<MemoryBufferRef> linker::coff::getArchiveMembers(Archive *file) {
  std::vector<MemoryBufferRef> v;
  Error err = Error::success();
  for (const Archive::Child &c : file->children(err)) {
    MemoryBufferRef mbref =
        CHECK(c.getMemoryBufferRef(),
              file->getFileName() +
                  ": could not get the buffer for a child of the archive");
    v.push_back(mbref);
  }
  if (err)
    fatal(file->getFileName() +
          ": Archive::children failed: " + toString(std::move(err)));
  return v;
}

void ObjFile::parseLazy() {
  // Native object file.
  std::unique_ptr<Binary> coffObjPtr = CHECK(createBinary(mb), this);
  COFFObjectFile *coffObj = cast<COFFObjectFile>(coffObjPtr.get());
  uint32_t numSymbols = coffObj->getNumberOfSymbols();
  for (uint32_t i = 0; i < numSymbols; ++i) {
    COFFSymbolRef coffSym = check(coffObj->getSymbol(i));
    if (coffSym.isUndefined() || !coffSym.isExternal() ||
        coffSym.isWeakExternal())
      continue;
    StringRef name = check(coffObj->getSymbolName(coffSym));
    if (coffSym.isAbsolute() && ignoredSymbolName(name))
      continue;
    ctx.symtab.addLazyObject(this, name);
    i += coffSym.getNumberOfAuxSymbols();
  }
}

void ObjFile::parse() {
  // Parse a memory buffer as a COFF file.
  std::unique_ptr<Binary> bin = CHECK(createBinary(mb), this);

  if (auto *obj = dyn_cast<COFFObjectFile>(bin.get())) {
    bin.release();
    coffObj.reset(obj);
  } else {
    fatal(toString(this) + " is not a COFF file");
  }

  // Read section and symbol tables.
  initializeChunks();
  initializeSymbols();
}

const coff_section *ObjFile::getSection(uint32_t i) {
  auto sec = coffObj->getSection(i);
  if (!sec)
    fatal("getSection failed: #" + Twine(i) + ": " + toString(sec.takeError()));
  return *sec;
}

// We set SectionChunk pointers in the SparseChunks vector to this value
// temporarily to mark comdat sections as having an unknown resolution. As we
// walk the object file's symbol table, once we visit either a leader symbol or
// an associative section definition together with the parent comdat's leader,
// we set the pointer to either nullptr (to mark the section as discarded) or a
// valid SectionChunk for that section.
namespace {
SectionChunk *const pendingComdat = reinterpret_cast<SectionChunk *>(1);
} // namespace

void ObjFile::initializeChunks() {
  uint32_t numSections = coffObj->getNumberOfSections();
  sparseChunks.resize(numSections + 1);
  for (uint32_t i = 1; i < numSections + 1; ++i) {
    const coff_section *sec = getSection(i);
    if (sec->Characteristics & IMAGE_SCN_LNK_COMDAT)
      sparseChunks[i] = pendingComdat;
    else
      sparseChunks[i] = readSection(i, nullptr, "");
  }
}

SectionChunk *ObjFile::readSection(uint32_t sectionNumber,
                                   const coff_aux_section_definition *def,
                                   StringRef leaderName) {
  const coff_section *sec = getSection(sectionNumber);

  StringRef name;
  if (Expected<StringRef> e = coffObj->getSectionName(sec))
    name = *e;
  else
    fatal("getSectionName failed: #" + Twine(sectionNumber) + ": " +
          toString(e.takeError()));

  if (name == ".drectve") {
    ArrayRef<uint8_t> data;
    cantFail(coffObj->getSectionContents(sec, data));
    directives = StringRef((const char *)data.data(), data.size());
    return nullptr;
  }

  if (name == ".llvm_addrsig") {
    addrsigSec = sec;
    return nullptr;
  }

  if (name == ".llvm.call-graph-profile") {
    callgraphSec = sec;
    return nullptr;
  }

  if (name == neverc::OverrideNames::ELFSectionName) {
    // LTO emits the section again into its native output, but the bitcode
    // source already populated overrideSymbols via marker symbols. Skip
    // re-parsing to avoid spurious "marked in multiple files" warnings.
    if (!builtFromBitcode) {
      ArrayRef<uint8_t> data;
      cantFail(coffObj->getSectionContents(sec, data));
      StringRef payload(reinterpret_cast<const char *>(data.data()),
                        data.size());
      while (!payload.empty()) {
        auto [sym, rest] = payload.split('\0');
        if (!sym.empty()) {
          StringRef saved = saver().save(sym);
          auto [it, inserted] = ctx.overrideSymbols.try_emplace(saved, this);
          if (!inserted && it->second != nullptr && it->second != this) {
            warn("symbol '" + saved +
                 "' marked override in multiple files; last definition wins"
                 "\n>>> " +
                 toString(it->second) + "\n>>> " +
                 toString(static_cast<const InputFile *>(this)));
            it->second = this;
          }
        }
        payload = rest;
      }
    }
    return nullptr;
  }

  // Ignore DWARF debug info unless requested to be included.
  if (!ctx.config.includeDwarfChunks && name.starts_with(".debug_"))
    return nullptr;

  if (sec->Characteristics & llvm::COFF::IMAGE_SCN_LNK_REMOVE)
    return nullptr;
  auto *c = make<SectionChunk>(this, sec);
  if (def)
    c->checksum = def->CheckSum;

  if (c->isCodeView())
    return nullptr;
  if (name == ".gfids$y")
    guardFidChunks.push_back(c);
  else if (name == ".giats$y")
    guardIATChunks.push_back(c);
  else if (name == ".gljmp$y")
    guardLJmpChunks.push_back(c);
  else if (name == ".gehcont$y")
    guardEHContChunks.push_back(c);
  else if (ctx.config.tailMerge && sec->NumberOfRelocations == 0 &&
           name == ".rdata" && leaderName.starts_with("??_C@"))
    // COFF sections that look like string literal sections (i.e. no
    // relocations, in .rdata, leader symbol name matches the MSVC name mangling
    // for string literals) are subject to string tail merging.
    MergeChunk::addSection(ctx, c);
  else if (name == ".rsrc" || name.starts_with(".rsrc$"))
    resourceChunks.push_back(c);
  else
    chunks.push_back(c);

  return c;
}

void ObjFile::includeResourceChunks() {
  chunks.insert(chunks.end(), resourceChunks.begin(), resourceChunks.end());
}

void ObjFile::readAssociativeDefinition(
    COFFSymbolRef sym, const coff_aux_section_definition *def) {
  readAssociativeDefinition(sym, def, def->getNumber(sym.isBigObj()));
}

void ObjFile::readAssociativeDefinition(COFFSymbolRef sym,
                                        const coff_aux_section_definition *def,
                                        uint32_t parentIndex) {
  SectionChunk *parent = sparseChunks[parentIndex];
  int32_t sectionNumber = sym.getSectionNumber();

  auto diag = [&]() {
    StringRef name = check(coffObj->getSymbolName(sym));

    StringRef parentName;
    const coff_section *parentSec = getSection(parentIndex);
    if (Expected<StringRef> e = coffObj->getSectionName(parentSec))
      parentName = *e;
    error(toString(this) + ": associative comdat " + name + " (sec " +
          Twine(sectionNumber) + ") has invalid reference to section " +
          parentName + " (sec " + Twine(parentIndex) + ")");
  };

  if (parent == pendingComdat) {
    // This can happen if an associative comdat refers to another associative
    // comdat that appears after it (invalid per COFF spec) or to a section
    // without any symbols.
    diag();
    return;
  }

  // Check whether the parent is prevailing. If it is, so are we, and we read
  // the section; otherwise mark it as discarded.
  if (parent) {
    SectionChunk *c = readSection(sectionNumber, def, "");
    sparseChunks[sectionNumber] = c;
    if (c) {
      c->selection = IMAGE_COMDAT_SELECT_ASSOCIATIVE;
      parent->addAssociative(c);
    }
  } else {
    sparseChunks[sectionNumber] = nullptr;
  }
}

Symbol *ObjFile::createRegular(COFFSymbolRef sym) {
  SectionChunk *sc = sparseChunks[sym.getSectionNumber()];
  if (sym.isExternal()) {
    StringRef name = check(coffObj->getSymbolName(sym));
    if (sc)
      return ctx.symtab.addRegular(this, name, sym.getGeneric(), sc,
                                   sym.getValue());
    return ctx.symtab.addUndefined(name, this, false);
  }
  if (sc)
    return make<DefinedRegular>(this, /*Name*/ "", /*IsCOMDAT*/ false,
                                /*IsExternal*/ false, sym.getGeneric(), sc);
  return nullptr;
}

void ObjFile::initializeSymbols() {
  uint32_t numSymbols = coffObj->getNumberOfSymbols();
  symbols.resize(numSymbols);

  SmallVector<std::pair<Symbol *, uint32_t>, 8> weakAliases;
  std::vector<uint32_t> pendingIndexes;
  pendingIndexes.reserve(numSymbols);

  std::vector<const coff_aux_section_definition *> comdatDefs(
      coffObj->getNumberOfSections() + 1);

  for (uint32_t i = 0; i < numSymbols; ++i) {
    COFFSymbolRef coffSym = check(coffObj->getSymbol(i));
    bool prevailingComdat;
    if (coffSym.isUndefined()) {
      symbols[i] = createUndefined(coffSym);
    } else if (coffSym.isWeakExternal()) {
      symbols[i] = createUndefined(coffSym);
      uint32_t tagIndex = coffSym.getAux<coff_aux_weak_external>()->TagIndex;
      weakAliases.emplace_back(symbols[i], tagIndex);
    } else if (std::optional<Symbol *> optSym =
                   createDefined(coffSym, comdatDefs, prevailingComdat)) {
      symbols[i] = *optSym;
    } else {
      // createDefined() returns std::nullopt if a symbol belongs to a section
      // that was pending at the point when the symbol was read. This can happen
      // in two cases:
      // 1) section definition symbol for a comdat leader;
      // 2) symbol belongs to a comdat section associated with another section.
      // In both of these cases, we can expect the section to be resolved by
      // the time we finish visiting the remaining symbols in the symbol
      // table. So we postpone the handling of this symbol until that time.
      pendingIndexes.push_back(i);
    }
    i += coffSym.getNumberOfAuxSymbols();
  }

  for (uint32_t i : pendingIndexes) {
    COFFSymbolRef sym = check(coffObj->getSymbol(i));
    if (const coff_aux_section_definition *def = sym.getSectionDefinition()) {
      if (def->Selection == IMAGE_COMDAT_SELECT_ASSOCIATIVE)
        readAssociativeDefinition(sym, def);
    }
    if (sparseChunks[sym.getSectionNumber()] == pendingComdat) {
      StringRef name = check(coffObj->getSymbolName(sym));
      log("comdat section " + name +
          " without leader and unassociated, discarding");
      continue;
    }
    symbols[i] = createRegular(sym);
  }

  for (auto &kv : weakAliases) {
    Symbol *sym = kv.first;
    uint32_t idx = kv.second;
    checkAndSetWeakAlias(ctx, this, sym, symbols[idx]);
  }

  // Free the memory used by sparseChunks now that symbol loading is finished.
  decltype(sparseChunks)().swap(sparseChunks);
}

Symbol *ObjFile::createUndefined(COFFSymbolRef sym) {
  StringRef name = check(coffObj->getSymbolName(sym));
  return ctx.symtab.addUndefined(name, this, sym.isWeakExternal());
}

void ObjFile::handleComdatSelection(
    COFFSymbolRef sym, COMDATType &selection, bool &prevailing,
    DefinedRegular *leader,
    const llvm::object::coff_aux_section_definition *def) {
  if (prevailing)
    return;
  // There's already an existing comdat for this symbol: `Leader`.
  // Use the comdats's selection field to determine if the new
  // symbol in `Sym` should be discarded, produce a duplicate symbol
  // error, etc.

  SectionChunk *leaderChunk = leader->getChunk();
  COMDATType leaderSelection = leaderChunk->selection;

  assert(leader->data && "Comdat leader without SectionChunk?");
  if (isa<BitcodeFile>(leader->file)) {
    // If the leader is only a LTO symbol, we don't know e.g. its final size
    // yet, so we can't do the full strict comdat selection checking yet.
    selection = leaderSelection = IMAGE_COMDAT_SELECT_ANY;
  }

  if ((selection == IMAGE_COMDAT_SELECT_ANY &&
       leaderSelection == IMAGE_COMDAT_SELECT_LARGEST) ||
      (selection == IMAGE_COMDAT_SELECT_LARGEST &&
       leaderSelection == IMAGE_COMDAT_SELECT_ANY)) {
    // cl.exe picks "any" for vftables when building with /GR- and
    // "largest" when building with /GR. To be able to link object files
    // compiled with each flag, "any" and "largest" are merged as "largest".
    leaderSelection = selection = IMAGE_COMDAT_SELECT_LARGEST;
  }

  // GCCs __declspec(selectany) doesn't actually pick "any" but "same size as".
  // NeverC on the other hand picks "any". To be able to link two object files
  // with a __declspec(selectany) declaration, one compiled with gcc and the
  // other with NeverC, we merge them as proper "same size as"
  // Comdat selections must match.  This is a bit more
  // strict than link.exe which allows merging "any" and "largest" if "any"
  // is the first symbol the linker sees, and it allows merging "largest"
  // with everything (!) if "largest" is the first symbol the linker sees.
  // Making this symmetric independent of which selection is seen first
  // seems better though.
  // (This behavior matches ModuleLinker::getComdatResult().)
  if (selection != leaderSelection) {
    log(("conflicting comdat type for " + toString(ctx, *leader) + ": " +
         Twine((int)leaderSelection) + " in " + toString(leader->getFile()) +
         " and " + Twine((int)selection) + " in " + toString(this))
            .str());
    ctx.symtab.reportDuplicate(leader, this);
    return;
  }

  switch (selection) {
  case IMAGE_COMDAT_SELECT_NODUPLICATES:
    ctx.symtab.reportDuplicate(leader, this);
    break;

  case IMAGE_COMDAT_SELECT_ANY:
    // Nothing to do.
    break;

  case IMAGE_COMDAT_SELECT_SAME_SIZE:
    if (leaderChunk->getSize() != getSection(sym)->SizeOfRawData)
      ctx.symtab.reportDuplicate(leader, this);
    break;

  case IMAGE_COMDAT_SELECT_EXACT_MATCH: {
    SectionChunk newChunk(this, getSection(sym));
    // link.exe only compares section contents here and doesn't complain
    // if the two comdat sections have e.g. different alignment.
    // Match that.
    if (leaderChunk->getContents() != newChunk.getContents())
      ctx.symtab.reportDuplicate(leader, this, &newChunk, sym.getValue());
    break;
  }

  case IMAGE_COMDAT_SELECT_ASSOCIATIVE:
    // createDefined() is never called for IMAGE_COMDAT_SELECT_ASSOCIATIVE.
    // (This means the linker doesn't produce duplicate symbol errors for
    // associative comdats while link.exe does, but associate comdats
    // are never extern in practice.)
    llvm_unreachable("createDefined not called for associative comdats");

  case IMAGE_COMDAT_SELECT_LARGEST:
    if (leaderChunk->getSize() < getSection(sym)->SizeOfRawData) {
      // Replace the existing comdat symbol with the new one.
      StringRef name = check(coffObj->getSymbolName(sym));
      replaceSymbol<DefinedRegular>(leader, this, name, /*IsCOMDAT*/ true,
                                    /*IsExternal*/ true, sym.getGeneric(),
                                    nullptr);
      prevailing = true;
    }
    break;

  case IMAGE_COMDAT_SELECT_NEWEST:
    llvm_unreachable("should have been rejected earlier");
  }
}

std::optional<Symbol *> ObjFile::createDefined(
    COFFSymbolRef sym,
    std::vector<const coff_aux_section_definition *> &comdatDefs,
    bool &prevailing) {
  prevailing = false;
  auto getName = [&]() { return check(coffObj->getSymbolName(sym)); };

  if (sym.isCommon()) {
    auto *c = make<CommonChunk>(sym);
    chunks.push_back(c);
    return ctx.symtab.addCommon(this, getName(), sym.getValue(),
                                sym.getGeneric(), c);
  }

  if (sym.isAbsolute()) {
    StringRef name = getName();

    if (name == "@feat.00")
      feat00Flags = sym.getValue();
    // Skip special symbols.
    if (ignoredSymbolName(name))
      return nullptr;

    if (sym.isExternal())
      return ctx.symtab.addAbsolute(name, sym);
    return make<DefinedAbsolute>(ctx, name, sym);
  }

  int32_t sectionNumber = sym.getSectionNumber();
  if (sectionNumber == llvm::COFF::IMAGE_SYM_DEBUG)
    return nullptr;

  if (llvm::COFF::isReservedSectionNumber(sectionNumber))
    fatal(toString(this) + ": " + getName() +
          " should not refer to special section " + Twine(sectionNumber));

  if ((uint32_t)sectionNumber >= sparseChunks.size())
    fatal(toString(this) + ": " + getName() +
          " should not refer to non-existent section " + Twine(sectionNumber));

  // Comdat handling.
  // A comdat symbol consists of two symbol table entries.
  // The first symbol entry has the name of the section (e.g. .text), fixed
  // values for the other fields, and one auxiliary record.
  // The second symbol entry has the name of the comdat symbol, called the
  // "comdat leader".
  // When this function is called for the first symbol entry of a comdat,
  // it sets comdatDefs and returns std::nullopt, and when it's called for the
  // second symbol entry it reads comdatDefs and then sets it back to nullptr.

  // Handle comdat leader.
  if (const coff_aux_section_definition *def = comdatDefs[sectionNumber]) {
    comdatDefs[sectionNumber] = nullptr;
    DefinedRegular *leader;

    if (sym.isExternal()) {
      std::tie(leader, prevailing) =
          ctx.symtab.addComdat(this, getName(), sym.getGeneric());
    } else {
      leader = make<DefinedRegular>(this, /*Name*/ "", /*IsCOMDAT*/ false,
                                    /*IsExternal*/ false, sym.getGeneric());
      prevailing = true;
    }

    if (def->Selection < (int)IMAGE_COMDAT_SELECT_NODUPLICATES ||
        // Intentionally ends at IMAGE_COMDAT_SELECT_LARGEST: link.exe
        // doesn't understand IMAGE_COMDAT_SELECT_NEWEST either.
        def->Selection > (int)IMAGE_COMDAT_SELECT_LARGEST) {
      fatal("unknown comdat type " + std::to_string((int)def->Selection) +
            " for " + getName() + " in " + toString(this));
    }
    COMDATType selection = (COMDATType)def->Selection;

    if (leader->isCOMDAT)
      handleComdatSelection(sym, selection, prevailing, leader, def);

    if (prevailing) {
      SectionChunk *c = readSection(sectionNumber, def, getName());
      sparseChunks[sectionNumber] = c;
      if (!c)
        return nullptr;
      c->sym = cast<DefinedRegular>(leader);
      c->selection = selection;
      cast<DefinedRegular>(leader)->data = &c->repl;
    } else {
      sparseChunks[sectionNumber] = nullptr;
    }
    return leader;
  }

  // Prepare to handle the comdat leader symbol by setting the section's
  // ComdatDefs pointer if we encounter a non-associative comdat.
  if (sparseChunks[sectionNumber] == pendingComdat) {
    if (const coff_aux_section_definition *def = sym.getSectionDefinition()) {
      if (def->Selection != IMAGE_COMDAT_SELECT_ASSOCIATIVE)
        comdatDefs[sectionNumber] = def;
    }
    return std::nullopt;
  }

  return createRegular(sym);
}

MachineTypes ObjFile::getMachineType() {
  if (coffObj)
    return static_cast<MachineTypes>(coffObj->getMachine());
  return IMAGE_FILE_MACHINE_UNKNOWN;
}

// Used only for DWARF debug info. This returns an optional pair of file name
// and line number for where the variable was defined.
std::optional<std::pair<StringRef, uint32_t>>
ObjFile::getVariableLocation(StringRef var) {
  if (!dwarf) {
    dwarf = make<DWARFCache>(DWARFContext::create(*getCOFFObj()));
    if (!dwarf)
      return std::nullopt;
  }
  std::optional<std::pair<std::string, unsigned>> ret =
      dwarf->getVariableLoc(var);
  if (!ret)
    return std::nullopt;
  return std::make_pair(saver().save(ret->first), ret->second);
}

// Used only for DWARF debug info.
std::optional<DILineInfo> ObjFile::getDILineInfo(uint32_t offset,
                                                 uint32_t sectionIndex) {
  if (!dwarf) {
    dwarf = make<DWARFCache>(DWARFContext::create(*getCOFFObj()));
    if (!dwarf)
      return std::nullopt;
  }

  return dwarf->getDILineInfo(offset, sectionIndex);
}

ImportFile::ImportFile(COFFLinkerContext &ctx, MemoryBufferRef m)
    : InputFile(ctx, ImportKind, m), live(!ctx.config.doGC), thunkLive(live) {}

void ImportFile::parse() {
  const char *buf = mb.getBufferStart();
  const auto *hdr = reinterpret_cast<const coff_import_header *>(buf);

  // Check if the total size is valid.
  if (mb.getBufferSize() != sizeof(*hdr) + hdr->SizeOfData)
    fatal("broken import library");

  // Read names and create an __imp_ symbol.
  StringRef name = saver().save(StringRef(buf + sizeof(*hdr)));
  StringRef impName = saver().save("__imp_" + name);
  const char *nameStart = buf + sizeof(coff_import_header) + name.size() + 1;
  dllName = std::string(StringRef(nameStart));
  StringRef extName;
  switch (hdr->getNameType()) {
  case IMPORT_ORDINAL:
    extName = "";
    break;
  case IMPORT_NAME:
    extName = name;
    break;
  case IMPORT_NAME_NOPREFIX:
    extName = ltrim1(name, "?@_");
    break;
  case IMPORT_NAME_UNDECORATE:
    extName = ltrim1(name, "?@_");
    extName = extName.substr(0, extName.find('@'));
    break;
  }

  this->hdr = hdr;
  externalName = extName;

  impSym = ctx.symtab.addImportData(impName, this);
  // If this was a duplicate, we logged an error but may continue;
  // in this case, impSym is nullptr.
  if (!impSym) {
    // If possible, synthesize a thunk for <name> that jumps via the existing
    // __imp_<name> symbol, and mark this ImportFile as not contributing to the
    // import tables.
    if (hdr->getType() == llvm::COFF::IMPORT_CODE) {
      if (Symbol *existingImp = ctx.symtab.find(impName)) {
        if (auto *existingDef = dyn_cast<Defined>(existingImp)) {
          Chunk *thunk = nullptr;
          if (hdr->Machine == AMD64)
            thunk = make<ImportThunkChunkX64>(ctx, existingDef);
          else {
            assert(hdr->Machine == ARM64);
            thunk = make<ImportThunkChunkARM64>(ctx, existingDef);
          }
          Symbol *sym = ctx.symtab.find(name);
          if (!sym || isa<Undefined>(sym) || sym->isLazy()) {
            ctx.symtab.addSynthetic(name, thunk);
            ctx.symtab.extraImportThunkChunks.push_back(thunk);
          }
        }
      }
    }

    live = false;
    thunkLive = false;
    return;
  }

  if (hdr->getType() == llvm::COFF::IMPORT_CONST)
    static_cast<void>(ctx.symtab.addImportData(name, this));

  // If type is function, we need to create a thunk which jump to an
  // address pointed by the __imp_ symbol. (This allows you to call
  // DLL functions just like regular non-DLL functions.)
  if (hdr->getType() == llvm::COFF::IMPORT_CODE)
    thunkSym = ctx.symtab.addImportThunk(
        name, cast_or_null<DefinedImportData>(impSym), hdr->Machine);
}

BitcodeFile::BitcodeFile(COFFLinkerContext &ctx, MemoryBufferRef mb,
                         StringRef archiveName, uint64_t offsetInArchive,
                         bool lazy)
    : InputFile(ctx, BitcodeKind, mb, lazy) {
  std::string path = mb.getBufferIdentifier().str();

  // LTO assumes that all MemoryBufferRefs given to it have a unique
  // name. If two archives define two members with the same name, this
  // causes a collision which result in only one of the objects being taken
  // into consideration at LTO time (which very likely causes undefined
  // symbols later in the link stage). So we append file offset to make
  // filename unique.
  MemoryBufferRef mbref(mb.getBuffer(),
                        saver().save(archiveName.empty()
                                         ? path
                                         : archiveName +
                                               sys::path::filename(path) +
                                               utostr(offsetInArchive)));

  obj = check(lto::InputFile::create(mbref));
}

BitcodeFile::~BitcodeFile() = default;

void BitcodeFile::parse() {
  llvm::StringSaver &saver = linker::saver();

  std::vector<std::pair<Symbol *, bool>> comdat(obj->getComdatTable().size());
  for (size_t i = 0; i != obj->getComdatTable().size(); ++i)
    comdat[i] =
        ctx.symtab.addComdat(this, saver.save(obj->getComdatTable()[i].first));

  // Discover override symbols from per-symbol marker names emitted by the
  // NeverC frontend. Marker names are already linker-mangled. Markers are
  // extern_weak declarations with no section and no uses; they stay in the
  // LTO IR symbol table but are not inserted into the linker symbol table below
  // (nullptr slots), so BitcodeCompiler::add() can supply no-op LTO
  // resolutions and they are not emitted into native objects.
  constexpr StringRef overridePrefix = neverc::OverrideNames::SymbolPrefix;
  for (const lto::InputFile::Symbol &objSym : obj->symbols()) {
    StringRef name = objSym.getName();
    if (!name.consume_front(overridePrefix))
      continue;
    StringRef saved = saver.save(name);
    auto [it, inserted] = ctx.overrideSymbols.try_emplace(saved, this);
    if (!inserted && it->second != nullptr && it->second != this) {
      warn("symbol '" + saved +
           "' marked override in multiple files; last definition wins"
           "\n>>> " +
           toString(it->second) + "\n>>> " +
           toString(static_cast<const InputFile *>(this)));
      it->second = this;
    }
  }

  for (const lto::InputFile::Symbol &objSym : obj->symbols()) {
    StringRef symName = saver.save(objSym.getName());
    if (symName.starts_with(overridePrefix)) {
      // Marker IR globals are not real linkable entities; preserve the
      // index slot with a nullptr so BitcodeCompiler::add can hand LTO a
      // no-op resolution.
      symbols.push_back(nullptr);
      continue;
    }
    int comdatIndex = objSym.getComdatIndex();
    Symbol *sym;
    SectionChunk *fakeSC = nullptr;
    if (objSym.isExecutable())
      fakeSC = &ctx.ltoTextSectionChunk.chunk;
    else
      fakeSC = &ctx.ltoDataSectionChunk.chunk;
    if (objSym.isUndefined()) {
      sym = ctx.symtab.addUndefined(symName, this, false);
      if (objSym.isWeak())
        sym->deferUndefined = true;
      // If one LTO object file references (i.e. has an undefined reference to)
      // a symbol with an __imp_ prefix, the LTO compilation itself sees it
      // as unprefixed but with a dllimport attribute instead, and doesn't
      // understand the relation to a concrete IR symbol with the __imp_ prefix.
      //
      // For such cases, mark the symbol as used in a regular object (i.e. the
      // symbol must be retained) so that the linker can associate the
      // references in the end. If the symbol is defined in an import library
      // or in a regular object file, this has no effect, but if it is defined
      // in another LTO object file, this makes sure it is kept, to fulfill
      // the reference when linking the output of the LTO compilation.
      if (symName.starts_with("__imp_"))
        sym->isUsedInRegularObj = true;
    } else if (objSym.isCommon()) {
      sym = ctx.symtab.addCommon(this, symName, objSym.getCommonSize());
    } else if (objSym.isWeak() && objSym.isIndirect()) {
      // Weak external.
      sym = ctx.symtab.addUndefined(symName, this, true);
      std::string fallback = std::string(objSym.getCOFFWeakExternalFallback());
      Symbol *alias = ctx.symtab.addUndefined(saver.save(fallback));
      checkAndSetWeakAlias(ctx, this, sym, alias);
    } else if (comdatIndex != -1) {
      if (symName == obj->getComdatTable()[comdatIndex].first) {
        sym = comdat[comdatIndex].first;
        if (cast<DefinedRegular>(sym)->data == nullptr)
          cast<DefinedRegular>(sym)->data = &fakeSC->repl;
      } else if (comdat[comdatIndex].second) {
        sym = ctx.symtab.addRegular(this, symName, nullptr, fakeSC);
      } else {
        sym = ctx.symtab.addUndefined(symName, this, false);
      }
    } else {
      sym = ctx.symtab.addRegular(this, symName, nullptr, fakeSC, 0,
                                  objSym.isWeak());
    }
    symbols.push_back(sym);
    if (objSym.isUsed())
      ctx.config.gcroot.push_back(sym);
  }
  directives = obj->getCOFFLinkerOpts();
}

void BitcodeFile::parseLazy() {
  for (const lto::InputFile::Symbol &sym : obj->symbols())
    if (!sym.isUndefined() && !sym.getName().starts_with(neverc::OverrideNames::SymbolPrefix))
      ctx.symtab.addLazyObject(this, sym.getName());
}

MachineTypes BitcodeFile::getMachineType() {
  switch (Triple(obj->getTargetTriple()).getArch()) {
  case Triple::x86_64:
    return AMD64;
  case Triple::aarch64:
    return ARM64;
  default:
    return IMAGE_FILE_MACHINE_UNKNOWN;
  }
}
