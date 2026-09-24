#include "Linker/COFF/SymbolTable.h"
#include "Linker/COFF/COFFLinkerContext.h"
#include "Linker/COFF/Config.h"
#include "Linker/COFF/Driver.h"
#include "Linker/COFF/LTO.h"
#include "Linker/COFF/Symbols.h"
#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/Core/Runtime/Stopwatch.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/WindowsMachineFlag.h"
#include "llvm/Support/raw_ostream.h"
#include <utility>

using namespace llvm;

// ===----------------------------------------------------------------------===
// Symbol table
// ===----------------------------------------------------------------------===

namespace linker::coff {

StringRef ltrim1(StringRef s, const char *chars) {
  if (!s.empty() && strchr(chars, s[0]))
    return s.substr(1);
  return s;
}

namespace {
bool compatibleMachineType(COFFLinkerContext &ctx, MachineTypes mt) {
  if (mt == IMAGE_FILE_MACHINE_UNKNOWN)
    return true;
  switch (ctx.config.machine) {
  case ARM64:
    return mt == ARM64;
  default:
    return ctx.config.machine == mt;
  }
}
} // namespace

void SymbolTable::addFile(InputFile *file) {
  log("Reading " + toString(file));
  if (file->lazy) {
    if (auto *f = dyn_cast<BitcodeFile>(file))
      f->parseLazy();
    else
      cast<ObjFile>(file)->parseLazy();
  } else {
    file->parse();
    if (auto *f = dyn_cast<ObjFile>(file)) {
      ctx.objFileInstances.push_back(f);
    } else if (auto *f = dyn_cast<BitcodeFile>(file)) {
      if (ltoCompilationDone) {
        error("LTO object file " + toString(file) +
              " linked in after "
              "doing LTO compilation.");
      }
      ctx.bitcodeFileInstances.push_back(f);
    } else if (auto *f = dyn_cast<ImportFile>(file)) {
      ctx.importFileInstances.push_back(f);
    }
  }

  MachineTypes mt = file->getMachineType();
  if (mt != IMAGE_FILE_MACHINE_UNKNOWN && mt != AMD64 && mt != ARM64) {
    error(toString(file) + ": " + machineToStr(mt) +
          " is not supported (only x86_64 and AArch64 are supported)");
    return;
  }
  if (ctx.config.machine == IMAGE_FILE_MACHINE_UNKNOWN) {
    ctx.config.machine = mt;
  } else if (!compatibleMachineType(ctx, mt)) {
    error(toString(file) + ": machine type " + machineToStr(mt) +
          " conflicts with " + machineToStr(ctx.config.machine));
    return;
  }

  ctx.driver.parseDirectives(file);
}

namespace {
void errorOrWarn(const Twine &s, bool forceUnresolved) {
  if (forceUnresolved)
    warn(s);
  else
    error(s);
}

// Causes the file associated with a lazy symbol to be linked in.
void forceLazy(Symbol *s) {
  s->pendingArchiveLoad = true;
  switch (s->kind()) {
  case Symbol::Kind::LazyArchiveKind: {
    auto *l = cast<LazyArchive>(s);
    l->file->addMember(l->sym);
    break;
  }
  case Symbol::Kind::LazyObjectKind: {
    InputFile *file = cast<LazyObject>(s)->file;
    file->ctx.symtab.addFile(file);
    break;
  }
  default:
    llvm_unreachable(
        "symbol passed to forceLazy is not a LazyArchive or LazyObject");
  }
}
} // namespace

// Returns the symbol in SC whose value is <= Addr that is closest to Addr.
// This is generally the global variable or function whose definition contains
// Addr.
namespace {
Symbol *getSymbol(SectionChunk *sc, uint32_t addr) {
  DefinedRegular *candidate = nullptr;

  for (Symbol *s : sc->file->getSymbols()) {
    auto *d = dyn_cast_or_null<DefinedRegular>(s);
    if (!d || !d->data || d->file != sc->file || d->getChunk() != sc ||
        d->getValue() > addr ||
        (candidate && d->getValue() < candidate->getValue()))
      continue;

    candidate = d;
  }

  return candidate;
}
} // namespace

namespace {
std::vector<std::string> getSymbolLocations(BitcodeFile *file) {
  std::string res("\n>>> referenced by ");
  StringRef source = file->obj->getSourceFileName();
  if (!source.empty())
    res += source.str() + "\n>>>               ";
  res += toString(file);
  return {res};
}

std::optional<std::pair<StringRef, uint32_t>> getFileLine(const SectionChunk *c,
                                                          uint32_t addr) {
  return std::nullopt;
}
} // namespace

// Given a file and the index of a symbol in that file, returns a description
// of all references to that symbol from that file. If no debug information is
// available, returns just the name of the file, else one string per actual
// reference as described in the debug info.
// Returns up to maxStrings string descriptions, along with the total number of
// locations found.
namespace {
std::pair<std::vector<std::string>, size_t>
getSymbolLocations(ObjFile *file, uint32_t symIndex, size_t maxStrings) {
  struct Location {
    Symbol *sym;
    std::pair<StringRef, uint32_t> fileLine;
  };
  std::vector<Location> locations;
  size_t numLocations = 0;

  for (Chunk *c : file->getChunks()) {
    auto *sc = dyn_cast<SectionChunk>(c);
    if (!sc)
      continue;
    for (const coff_relocation &r : sc->getRelocs()) {
      if (r.SymbolTableIndex != symIndex)
        continue;
      numLocations++;
      if (locations.size() >= maxStrings)
        continue;

      std::optional<std::pair<StringRef, uint32_t>> fileLine =
          getFileLine(sc, r.VirtualAddress);
      Symbol *sym = getSymbol(sc, r.VirtualAddress);
      if (fileLine)
        locations.push_back({sym, *fileLine});
      else if (sym)
        locations.push_back({sym, {"", 0}});
    }
  }

  if (maxStrings == 0)
    return std::make_pair(std::vector<std::string>(), numLocations);

  if (numLocations == 0)
    return std::make_pair(
        std::vector<std::string>{"\n>>> referenced by " + toString(file)}, 1);

  std::vector<std::string> symbolLocations(locations.size());
  size_t i = 0;
  for (Location loc : locations) {
    llvm::raw_string_ostream os(symbolLocations[i++]);
    os << "\n>>> referenced by ";
    if (!loc.fileLine.first.empty())
      os << loc.fileLine.first << ":" << loc.fileLine.second
         << "\n>>>               ";
    os << toString(file);
    if (loc.sym)
      os << ":(" << toString(file->ctx, *loc.sym) << ')';
  }
  return std::make_pair(symbolLocations, numLocations);
}
} // namespace

std::vector<std::string> getSymbolLocations(ObjFile *file, uint32_t symIndex) {
  return getSymbolLocations(file, symIndex, SIZE_MAX).first;
}

namespace {
std::pair<std::vector<std::string>, size_t>
getSymbolLocations(InputFile *file, uint32_t symIndex, size_t maxStrings) {
  if (auto *o = dyn_cast<ObjFile>(file))
    return getSymbolLocations(o, symIndex, maxStrings);
  if (auto *b = dyn_cast<BitcodeFile>(file)) {
    std::vector<std::string> symbolLocations = getSymbolLocations(b);
    size_t numLocations = symbolLocations.size();
    if (symbolLocations.size() > maxStrings)
      symbolLocations.resize(maxStrings);
    return std::make_pair(symbolLocations, numLocations);
  }
  llvm_unreachable("unsupported file type passed to getSymbolLocations");
  return std::make_pair(std::vector<std::string>(), (size_t)0);
}
} // namespace

// For an undefined symbol, stores all files referencing it and the index of
// the undefined symbol in each file.
struct UndefinedDiag {
  Symbol *sym;
  struct File {
    InputFile *file;
    uint32_t symIndex;
  };
  std::vector<File> files;
};

namespace {
void reportUndefinedSymbol(const COFFLinkerContext &ctx,
                           const UndefinedDiag &undefDiag) {
  std::string out;
  llvm::raw_string_ostream os(out);
  os << "undefined symbol: " << toString(ctx, *undefDiag.sym);

  const size_t maxUndefReferences = 3;
  size_t numDisplayedRefs = 0, numRefs = 0;
  for (const UndefinedDiag::File &ref : undefDiag.files) {
    auto [symbolLocations, totalLocations] = getSymbolLocations(
        ref.file, ref.symIndex, maxUndefReferences - numDisplayedRefs);

    numRefs += totalLocations;
    numDisplayedRefs += symbolLocations.size();
    for (const std::string &s : symbolLocations) {
      os << s;
    }
  }
  if (numDisplayedRefs < numRefs)
    os << "\n>>> referenced " << numRefs - numDisplayedRefs << " more times";
  errorOrWarn(os.str(), ctx.config.forceUnresolved);
}
} // namespace

/// Helper function for reportUnresolvable and resolveRemainingUndefines.
/// This function emits an "undefined symbol" diagnostic for each symbol in
/// undefs. If localImports is not nullptr, it also emits a "locally
/// defined symbol imported" diagnostic for symbols in localImports.
/// objFiles and bitcodeFiles (if not nullptr) are used to report where
/// undefined symbols are referenced.
namespace {
void reportProblemSymbols(const COFFLinkerContext &ctx,
                          const SmallPtrSetImpl<Symbol *> &undefs,
                          const DenseMap<Symbol *, Symbol *> *localImports,
                          bool needBitcodeFiles) {
  // Return early if there is nothing to report (which should be
  // the common case).
  if (undefs.empty() && (!localImports || localImports->empty()))
    return;

  for (Symbol *b : ctx.config.gcroot) {
    if (undefs.count(b))
      errorOrWarn("<root>: undefined symbol: " + toString(ctx, *b),
                  ctx.config.forceUnresolved);
    if (localImports)
      if (Symbol *imp = localImports->lookup(b))
        warn("<root>: locally defined symbol imported: " + toString(ctx, *imp) +
             " (defined in " + toString(imp->getFile()) + ") [LNK4217]");
  }

  std::vector<UndefinedDiag> undefDiags;
  DenseMap<Symbol *, int> firstDiag;

  auto processFile = [&](InputFile *file, ArrayRef<Symbol *> symbols) {
    uint32_t symIndex = (uint32_t)-1;
    for (Symbol *sym : symbols) {
      ++symIndex;
      if (!sym)
        continue;
      if (undefs.count(sym)) {
        auto it = firstDiag.find(sym);
        if (it == firstDiag.end()) {
          firstDiag[sym] = undefDiags.size();
          undefDiags.push_back({sym, {{file, symIndex}}});
        } else {
          undefDiags[it->second].files.push_back({file, symIndex});
        }
      }
      if (localImports)
        if (Symbol *imp = localImports->lookup(sym))
          warn(toString(file) +
               ": locally defined symbol imported: " + toString(ctx, *imp) +
               " (defined in " + toString(imp->getFile()) + ") [LNK4217]");
    }
  };

  for (ObjFile *file : ctx.objFileInstances)
    processFile(file, file->getSymbols());

  if (needBitcodeFiles)
    for (BitcodeFile *file : ctx.bitcodeFileInstances)
      processFile(file, file->getSymbols());

  for (const UndefinedDiag &undefDiag : undefDiags)
    reportUndefinedSymbol(ctx, undefDiag);
}
} // namespace

void SymbolTable::reportUnresolvable() {
  // Only set membership is observable here, so any iteration order will do.
  SmallPtrSet<Symbol *, 8> undefs;
  for (NameSlot<Symbol> *slot : insertionOrder) {
    Symbol *sym = slot->symbol;
    auto *undef = dyn_cast<Undefined>(sym);
    if (!undef || sym->deferUndefined)
      continue;
    if (undef->getWeakAlias())
      continue;
    StringRef name = undef->getName();
    if (name.starts_with("__imp_")) {
      Symbol *imp = find(name.substr(strlen("__imp_")));
      if (Defined *def = dyn_cast_or_null<Defined>(imp)) {
        def->isUsedInRegularObj = true;
        continue;
      }
    }
    if (name.contains("_PchSym_"))
      continue;
    undefs.insert(sym);
  }

  reportProblemSymbols(ctx, undefs,
                       /* localImports */ nullptr, true);
}

void SymbolTable::resolveRemainingUndefines() {
  llvm::TimeTraceScope timeScope("Resolve remaining undefined symbols");
  SmallPtrSet<Symbol *, 8> undefs;
  DenseMap<Symbol *, Symbol *> localImports;

  // Each step below changes only the symbol it visits. The visiting order is
  // observable only when a later step inspects an earlier symbol: an __imp_
  // name looks up the symbol it imports (which may have been resolved by a
  // weak alias or /force already) and appends to localImportChunks. Only then
  // visit the symbols in hash table order; otherwise resolution order gives
  // the same result without building that table.
  auto isCandidate = [](NameSlot<Symbol> *slot) {
    return isa<Undefined>(slot->symbol) && slot->symbol->isUsedInRegularObj;
  };
  std::vector<NameSlot<Symbol> *> candidates;
  for (NameSlot<Symbol> *slot : insertionOrder)
    if (isCandidate(slot))
      candidates.push_back(slot);
  if (candidates.size() > 1 &&
      (ctx.config.forceUnresolved ||
       llvm::any_of(candidates, [](NameSlot<Symbol> *slot) {
         return slot->symbol->getName().starts_with("__imp_");
       }))) {
    candidates = slotsInHashOrder();
    llvm::erase_if(candidates,
                   [&](NameSlot<Symbol> *slot) { return !isCandidate(slot); });
  }

  for (NameSlot<Symbol> *slot : candidates) {
    Symbol *sym = slot->symbol;
    auto *undef = cast<Undefined>(sym);

    StringRef name = undef->getName();

    // A weak alias may have been resolved, so check for that.
    if (Defined *d = undef->getWeakAlias()) {
      // We want to replace Sym with D. However, we can't just blindly
      // copy sizeof(SymbolUnion) bytes from D to Sym because D may be an
      // internal symbol, and internal symbols are stored as "unparented"
      // Symbols. For that reason we need to check which type of symbol we
      // are dealing with and copy the correct number of bytes.
      if (isa<DefinedRegular>(d))
        memcpy(sym, d, sizeof(DefinedRegular));
      else if (isa<DefinedAbsolute>(d))
        memcpy(sym, d, sizeof(DefinedAbsolute));
      else
        memcpy(sym, d, sizeof(SymbolUnion));
      continue;
    }

    // If we can resolve a symbol by removing __imp_ prefix, do that.
    // This odd rule is for compatibility with MSVC linker.
    if (name.starts_with("__imp_")) {
      Symbol *imp = find(name.substr(strlen("__imp_")));
      if (imp && isa<Defined>(imp)) {
        auto *d = cast<Defined>(imp);
        replaceSymbol<DefinedLocalImport>(sym, ctx, name, d);
        localImportChunks.push_back(cast<DefinedLocalImport>(sym)->getChunk());
        localImports[sym] = d;
        continue;
      }
    }

    if (name.contains("_PchSym_"))
      continue;

    // Remaining undefined symbols are not fatal if /force is specified.
    // They are replaced with dummy defined symbols.
    if (ctx.config.forceUnresolved)
      replaceSymbol<DefinedAbsolute>(sym, ctx, name, 0);
    undefs.insert(sym);
  }

  reportProblemSymbols(
      ctx, undefs,
      ctx.config.warnLocallyDefinedImported ? &localImports : nullptr, false);
}

std::pair<Symbol *, bool> SymbolTable::insert(StringRef name) {
  NameSlot<Symbol> *slot = std::exchange(insertHint, {}).match(name);
  if (!slot)
    slot = names.intern(name);
  bool inserted = false;
  if (!slot->symbol) {
    Symbol *sym = reinterpret_cast<Symbol *>(make<SymbolUnion>());
    sym->isUsedInRegularObj = false;
    sym->pendingArchiveLoad = false;
    sym->canInline = true;
    slot->symbol = sym;
    insertionOrder.push_back(slot);
    inserted = true;
  }
  return {slot->symbol, inserted};
}

std::vector<NameSlot<Symbol> *> SymbolTable::slotsInHashOrder() const {
  // A hash table that only ever grows lays its entries out as a function of
  // the order the keys arrived in, so replaying the resolution order
  // reproduces the table exactly.
  DenseMap<CachedHashStringRef, NameSlot<Symbol> *> replay;
  for (NameSlot<Symbol> *slot : insertionOrder)
    replay[CachedHashStringRef(slot->name())] = slot;
  std::vector<NameSlot<Symbol> *> slots;
  slots.reserve(replay.size());
  for (auto &entry : replay)
    slots.push_back(entry.second);
  return slots;
}

namespace {
// Interns the names initializeSymbols() inserts: those of external and weak
// external symbols. A member that does not parse is left to the ordinary
// path, which diagnoses it.
void internMemberNames(ShardedNameTable<Symbol> &names,
                       ArchiveFile::MemberNames &member) {
  Expected<std::unique_ptr<COFFObjectFile>> obj =
      COFFObjectFile::create(member.mb);
  if (!obj) {
    consumeError(obj.takeError());
    return;
  }
  const uint32_t numSymbols = (*obj)->getNumberOfSymbols();
  member.hints.assign(numSymbols, {});
  for (uint32_t i = 0; i < numSymbols; ++i) {
    Expected<COFFSymbolRef> sym = (*obj)->getSymbol(i);
    if (!sym) {
      consumeError(sym.takeError());
      member.hints.clear();
      return;
    }
    if (sym->isExternal() || sym->isWeakExternal()) {
      Expected<StringRef> name = (*obj)->getSymbolName(*sym);
      if (!name) {
        consumeError(name.takeError());
        member.hints.clear();
        return;
      }
      member.hints[i] = {names.intern(*name), name->data()};
    }
    i += sym->getNumberOfAuxSymbols();
  }
}
} // namespace

void SymbolTable::startMemberNameInterning(ArchiveFile &archive) {
  if (!interningWorkers) {
    interningWorkers = std::make_unique<LinkerTaskGroup>();
    names.setConcurrent(true);
  }
  for (ArchiveFile::MemberNames *member : archive.internedMembers)
    interningWorkers->spawn([this, member] {
      if (interningCancelled.load(std::memory_order_relaxed))
        return;
      uint8_t state = ArchiveFile::MemberNames::Pending;
      if (!member->state.compare_exchange_strong(
              state, ArchiveFile::MemberNames::Running,
              std::memory_order_acquire))
        return;
      internMemberNames(names, *member);
      member->state.store(ArchiveFile::MemberNames::Done,
                          std::memory_order_release);
    });
}

void SymbolTable::finishMemberNameInterning() {
  interningClosed = true;
  // Members no worker has started are left to the ordinary path.
  interningCancelled.store(true, std::memory_order_relaxed);
  if (!interningWorkers)
    return;
  interningWorkers->sync();
  interningWorkers.reset();
  names.setConcurrent(false);
}

std::pair<Symbol *, bool> SymbolTable::insert(StringRef name, InputFile *file) {
  std::pair<Symbol *, bool> result = insert(name);
  if (!file || !isa<BitcodeFile>(file))
    result.first->isUsedInRegularObj = true;
  return result;
}

Symbol *SymbolTable::addUndefined(StringRef name, InputFile *f,
                                  bool isWeakAlias) {
  auto [s, wasInserted] = insert(name, f);

  // LINK's first search step for a new undefined reference from an archive
  // member is that same archive. Preserve the member's origin so a lazy symbol
  // from another library does not win merely because it entered the global
  // symbol table first.
  auto loadFromParentArchive = [&]() {
    if (isWeakAlias || s->pendingArchiveLoad || !f || !f->parentArchive)
      return false;
    const Archive::Symbol *member = f->parentArchive->findSymbol(name);
    if (!member)
      return false;
    s->pendingArchiveLoad = true;
    pendingArchiveMembers.try_emplace(s, f->parentArchive, *member);
    f->parentArchive->addMember(*member);
    return true;
  };

  if (wasInserted || (s->isLazy() && isWeakAlias)) {
    replaceSymbol<Undefined>(s, name);
    if (loadFromParentArchive())
      return s;
  }
  if (s->isLazy() && loadFromParentArchive())
    return s;
  if (!isWeakAlias) {
    auto it = featureOverrideDefaults.find(name);
    if (it != featureOverrideDefaults.end() &&
        shouldSelectFeatureOverrideDefault(s)) {
      s->pendingArchiveLoad = true;
      it->second.archive->addMember(it->second.symbol);
      return s;
    }
  }
  if (s->isLazy() && !s->pendingArchiveLoad)
    forceLazy(s);
  return s;
}

bool SymbolTable::shouldSelectFeatureOverrideDefault(Symbol *symbol) const {
  if (isa<DefinedImportThunk>(symbol) || isa<DefinedImportData>(symbol))
    return true;
  if (auto *lazy = dyn_cast<LazyArchive>(symbol))
    return lazy->file->isImportLibraryMember(lazy->sym);
  auto *undefined = dyn_cast<Undefined>(symbol);
  if (!undefined || undefined->weakAlias)
    return false;
  if (!symbol->pendingArchiveLoad)
    return true;
  auto pending = pendingArchiveMembers.find(symbol);
  return pending != pendingArchiveMembers.end() &&
         pending->second.archive->isImportLibraryMember(pending->second.symbol);
}

void SymbolTable::addFeatureOverrideDefault(
    StringRef base, ArchiveFile *archive, const Archive::Symbol &defaultSymbol,
    StringRef marker) {
  auto selected =
      featureOverrideDefaults.try_emplace(base, archive, defaultSymbol).first;
  featureOverrideMarkers.insert(marker);

  Symbol *target = find(base);
  if (!target || !target->isUsedInRegularObj ||
      !shouldSelectFeatureOverrideDefault(target))
    return;
  target->pendingArchiveLoad = true;
  selected->second.archive->addMember(selected->second.symbol);
}

bool SymbolTable::isFeatureOverrideMarker(StringRef name) const {
  return featureOverrideMarkers.contains(name);
}

void SymbolTable::addLazyArchive(ArchiveFile *f, const Archive::Symbol &sym) {
  StringRef name = sym.getName();
  auto [s, wasInserted] = insert(name);
  if (wasInserted) {
    replaceSymbol<LazyArchive>(s, f, sym);
    return;
  }
  auto *u = dyn_cast<Undefined>(s);
  if (!u || u->weakAlias || s->pendingArchiveLoad)
    return;
  s->pendingArchiveLoad = true;
  pendingArchiveMembers.try_emplace(s, f, sym);
  f->addMember(sym);
}

void SymbolTable::addLazyObject(InputFile *f, StringRef n) {
  assert(f->lazy);
  auto [s, wasInserted] = insert(n, f);
  if (wasInserted) {
    replaceSymbol<LazyObject>(s, f, n);
    return;
  }
  auto *u = dyn_cast<Undefined>(s);
  if (!u || u->weakAlias || s->pendingArchiveLoad)
    return;
  s->pendingArchiveLoad = true;
  f->lazy = false;
  addFile(f);
}

namespace {
std::string getSourceLocationBitcode(BitcodeFile *file) {
  std::string res("\n>>> defined at ");
  StringRef source = file->obj->getSourceFileName();
  if (!source.empty())
    res += source.str() + "\n>>>            ";
  res += toString(file);
  return res;
}

std::string getSourceLocationObj(ObjFile *file, SectionChunk *sc,
                                 uint32_t offset, StringRef name) {
  std::optional<std::pair<StringRef, uint32_t>> fileLine;
  if (sc)
    fileLine = getFileLine(sc, offset);
  if (!fileLine)
    fileLine = file->getVariableLocation(name);

  std::string res;
  llvm::raw_string_ostream os(res);
  os << "\n>>> defined at ";
  if (fileLine)
    os << fileLine->first << ":" << fileLine->second << "\n>>>            ";
  os << toString(file);
  return os.str();
}

std::string getSourceLocation(InputFile *file, SectionChunk *sc,
                              uint32_t offset, StringRef name) {
  if (!file)
    return "";
  if (auto *o = dyn_cast<ObjFile>(file))
    return getSourceLocationObj(o, sc, offset, name);
  if (auto *b = dyn_cast<BitcodeFile>(file))
    return getSourceLocationBitcode(b);
  return "\n>>> defined at " + toString(file);
}
} // namespace

// Construct and print an error message in the form of:
//
//   neverc: error: duplicate symbol: foo
//   >>> defined at bar.c:30
//   >>>            bar.o
//   >>> defined at baz.c:563
//   >>>            baz.o
void SymbolTable::reportDuplicate(Symbol *existing, InputFile *newFile,
                                  SectionChunk *newSc,
                                  uint32_t newSectionOffset) {
  if (ctx.overrideSymbols.count(existing->getName()))
    return;

  std::string msg;
  llvm::raw_string_ostream os(msg);
  os << "duplicate symbol: " << toString(ctx, *existing);

  DefinedRegular *d = dyn_cast<DefinedRegular>(existing);
  if (d && isa<ObjFile>(d->getFile())) {
    os << getSourceLocation(d->getFile(), d->getChunk(), d->getValue(),
                            existing->getName());
  } else {
    os << getSourceLocation(existing->getFile(), nullptr, 0, "");
  }
  os << getSourceLocation(newFile, newSc, newSectionOffset,
                          existing->getName());

  if (ctx.config.forceMultiple)
    warn(os.str());
  else
    // A genuine multiply-defined regular symbol is a hard error, matching the
    // ELF/MachO backends and real link.exe (LNK2005). /force:multiple downgrades
    // it to a warning above. (Library/COMDAT/lazy collisions never reach here.)
    error(os.str());
}

Symbol *SymbolTable::addAbsolute(StringRef n, COFFSymbolRef sym) {
  auto [s, wasInserted] = insert(n, nullptr);
  s->isUsedInRegularObj = true;
  if (wasInserted || isa<Undefined>(s) || s->isLazy())
    replaceSymbol<DefinedAbsolute>(s, ctx, n, sym);
  else if (auto *da = dyn_cast<DefinedAbsolute>(s)) {
    if (da->getVA() != sym.getValue())
      reportDuplicate(s, nullptr);
  } else if (!isa<DefinedCOFF>(s))
    reportDuplicate(s, nullptr);
  return s;
}

Symbol *SymbolTable::addAbsolute(StringRef n, uint64_t va) {
  auto [s, wasInserted] = insert(n, nullptr);
  s->isUsedInRegularObj = true;
  if (wasInserted || isa<Undefined>(s) || s->isLazy())
    replaceSymbol<DefinedAbsolute>(s, ctx, n, va);
  else if (auto *da = dyn_cast<DefinedAbsolute>(s)) {
    if (da->getVA() != va)
      reportDuplicate(s, nullptr);
  } else if (!isa<DefinedCOFF>(s))
    reportDuplicate(s, nullptr);
  return s;
}

Symbol *SymbolTable::addSynthetic(StringRef n, Chunk *c) {
  auto [s, wasInserted] = insert(n, nullptr);
  s->isUsedInRegularObj = true;
  if (wasInserted || isa<Undefined>(s) || s->isLazy())
    replaceSymbol<DefinedSynthetic>(s, n, c);
  else if (!isa<DefinedCOFF>(s))
    reportDuplicate(s, nullptr);
  return s;
}

Symbol *SymbolTable::addRegular(InputFile *f, StringRef n,
                                const coff_symbol_generic *sym, SectionChunk *c,
                                uint32_t sectionOffset, bool isWeak) {
  auto [s, wasInserted] = insert(n, f);

  // __declspec(override) is orthogonal to weak/strong: an override-marked
  // definition forcibly replaces any other definition of the same name, and a
  // definition coming from an override source is itself never replaced by a
  // non-override definition. Handle this before the default duplicate logic
  // so the policy is symmetric regardless of which side is weak.
  if (!wasInserted && isa<DefinedRegular>(s)) {
    auto overIt = ctx.overrideSymbols.find(n);
    if (overIt != ctx.overrideSymbols.end()) {
      const InputFile *overrideSource = overIt->second;
      bool incomingIsOverride =
          overrideSource == nullptr || overrideSource == f;
      bool existingIsOverride =
          overrideSource == nullptr || overrideSource == s->getFile();
      if (incomingIsOverride) {
        replaceSymbol<DefinedRegular>(s, f, n, /*IsCOMDAT*/ false,
                                      /*IsExternal*/ true, sym, c, isWeak);
        return s;
      }
      if (existingIsOverride)
        return s;
    }
  }

  if (wasInserted || !isa<DefinedRegular>(s) || s->isWeak)
    replaceSymbol<DefinedRegular>(s, f, n, /*IsCOMDAT*/ false,
                                  /*IsExternal*/ true, sym, c, isWeak);
  else if (!isWeak)
    reportDuplicate(s, f, c, sectionOffset);
  return s;
}

std::pair<DefinedRegular *, bool>
SymbolTable::addComdat(InputFile *f, StringRef n,
                       const coff_symbol_generic *sym) {
  auto [s, wasInserted] = insert(n, f);
  if (wasInserted || !isa<DefinedRegular>(s)) {
    replaceSymbol<DefinedRegular>(s, f, n, /*IsCOMDAT*/ true,
                                  /*IsExternal*/ true, sym, nullptr);
    return {cast<DefinedRegular>(s), true};
  }
  auto *existingSymbol = cast<DefinedRegular>(s);
  if (!existingSymbol->isCOMDAT)
    reportDuplicate(s, f);
  return {existingSymbol, false};
}

Symbol *SymbolTable::addCommon(InputFile *f, StringRef n, uint64_t size,
                               const coff_symbol_generic *sym, CommonChunk *c) {
  auto [s, wasInserted] = insert(n, f);
  if (wasInserted || !isa<DefinedCOFF>(s))
    replaceSymbol<DefinedCommon>(s, f, n, size, sym, c);
  else if (auto *dc = dyn_cast<DefinedCommon>(s))
    if (size > dc->getSize())
      replaceSymbol<DefinedCommon>(s, f, n, size, sym, c);
  return s;
}

Symbol *SymbolTable::addImportData(StringRef n, ImportFile *f) {
  auto [s, wasInserted] = insert(n, nullptr);
  s->isUsedInRegularObj = true;
  if (wasInserted || isa<Undefined>(s) || s->isLazy()) {
    replaceSymbol<DefinedImportData>(s, n, f);
    return s;
  }

  reportDuplicate(s, f);
  return nullptr;
}

Symbol *SymbolTable::addImportThunk(StringRef name, DefinedImportData *id,
                                    uint16_t machine) {
  auto [s, wasInserted] = insert(name, nullptr);
  s->isUsedInRegularObj = true;
  if (wasInserted || isa<Undefined>(s) || s->isLazy()) {
    replaceSymbol<DefinedImportThunk>(s, ctx, name, id, machine);
    return s;
  }

  reportDuplicate(s, id->file);
  return nullptr;
}

void SymbolTable::addLibcall(StringRef name) {
  Symbol *sym = findUnderscore(name);
  if (!sym)
    return;

  if (auto *l = dyn_cast<LazyArchive>(sym)) {
    MemoryBufferRef mb = l->getMemberBuffer();
    if (isBitcode(mb))
      addUndefined(sym->getName());
  } else if (LazyObject *o = dyn_cast<LazyObject>(sym)) {
    if (isBitcode(o->file->mb))
      addUndefined(sym->getName());
  }
}

std::vector<Chunk *> SymbolTable::getChunks() const {
  std::vector<Chunk *> res;
  for (ObjFile *file : ctx.objFileInstances) {
    ArrayRef<Chunk *> v = file->getChunks();
    res.insert(res.end(), v.begin(), v.end());
  }
  return res;
}

Symbol *SymbolTable::find(StringRef name) const {
  NameSlot<Symbol> *slot = names.lookup(name);
  return slot ? slot->symbol : nullptr;
}

Symbol *SymbolTable::findUnderscore(StringRef name) const { return find(name); }

Symbol *SymbolTable::findMangle(StringRef name) {
  Symbol *result = nullptr;
  findMangles(name, result);
  return result;
}

void SymbolTable::findMangles(ArrayRef<StringRef> names,
                              MutableArrayRef<Symbol *> results) {
  assert(names.size() == results.size());
  SmallVector<std::pair<std::string, size_t>, 4> pending;
  for (size_t i = 0; i < names.size(); ++i) {
    results[i] = nullptr;
    if (Symbol *sym = find(names[i])) {
      if (auto *u = dyn_cast<Undefined>(sym)) {
        // We're specifically looking for weak aliases that ultimately resolve
        // to defined symbols, hence the call to getWeakAlias() instead of just
        // using the weakAlias member variable. This matches link.exe's
        // behavior.
        if (Symbol *weakAlias = u->getWeakAlias()) {
          results[i] = weakAlias;
          continue;
        }
      } else {
        results[i] = sym;
        continue;
      }
    }
    pending.emplace_back(("?" + names[i] + "@@Y").str(), i);
  }
  if (pending.empty())
    return;

  // Efficient fuzzy string lookup is impossible with a hash table, so scan all
  // names once, in parallel, for symbols that carry the C++ mangling of a
  // pending name. The result is the first such symbol in hash table order,
  // which only needs that order when a name has several candidates.
  struct Matches {
    SmallVector<NameSlot<Symbol> *, 4> first;
    SmallVector<uint32_t, 4> count;
  };
  const size_t numTasks =
      std::max<size_t>(1, std::min<size_t>(insertionOrder.size() / 4096,
                                           parallelThreadCount() * 4));
  std::vector<Matches> matches(numTasks);
  auto scan = [&](size_t task) {
    Matches &m = matches[task];
    m.first.assign(pending.size(), nullptr);
    m.count.assign(pending.size(), 0);
    const size_t begin = task * insertionOrder.size() / numTasks;
    const size_t end = (task + 1) * insertionOrder.size() / numTasks;
    for (size_t i = begin; i < end; ++i) {
      StringRef symName = insertionOrder[i]->name();
      if (!symName.starts_with("?"))
        continue;
      for (size_t p = 0; p < pending.size(); ++p) {
        if (!symName.starts_with(pending[p].first))
          continue;
        if (!m.count[p]++)
          m.first[p] = insertionOrder[i];
      }
    }
  };
  parallelFor(0, numTasks, scan);

  bool ambiguous = false;
  for (size_t p = 0; p < pending.size(); ++p) {
    uint32_t count = 0;
    for (const Matches &m : matches) {
      if (m.count[p] && !results[pending[p].second])
        results[pending[p].second] = m.first[p]->symbol;
      count += m.count[p];
    }
    ambiguous |= count > 1;
  }
  if (!ambiguous)
    return;

  for (auto &[prefix, index] : pending)
    results[index] = nullptr;
  size_t unresolved = pending.size();
  for (NameSlot<Symbol> *slot : slotsInHashOrder()) {
    StringRef symName = slot->name();
    if (!symName.starts_with("?"))
      continue;
    for (auto &[prefix, index] : pending) {
      if (results[index] || !symName.starts_with(prefix))
        continue;
      results[index] = slot->symbol;
      --unresolved;
    }
    if (unresolved == 0)
      return;
  }
}

Symbol *SymbolTable::addUndefined(StringRef name) {
  return addUndefined(name, nullptr, false);
}

void SymbolTable::compileBitcodeFiles() {
  ltoCompilationDone = true;
  if (ctx.bitcodeFileInstances.empty())
    return;

  llvm::TimeTraceScope timeScope("Compile bitcode");
  ScopedTimer t(ctx.ltoTimer);
  lto.reset(new BitcodeCompiler(ctx));
  for (BitcodeFile *f : ctx.bitcodeFileInstances)
    lto->add(*f);
  auto Compiled = lto->compile();
  for (InputFile *newObj : Compiled) {
    ObjFile *obj = cast<ObjFile>(newObj);
    obj->parse();
    ctx.objFileInstances.push_back(obj);
  }
}

} // namespace linker::coff
