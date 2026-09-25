#include "Linker/ELF/SymbolTable.h"
#include "Linker/Core/Runtime/Allocator.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/Core/Support/Strings.h"
#include "Linker/ELF/Config.h"
#include "Linker/ELF/ELFContextAccess.h"
#include "Linker/ELF/InputFiles.h"
#include "Linker/ELF/Symbols.h"
#include "llvm/ADT/STLExtras.h"
using namespace llvm;
using namespace llvm::object;
using namespace llvm::ELF;
using namespace linker;
using namespace linker::elf;

// ===----------------------------------------------------------------------===
// Wrap & redirect (--wrap)
// ===----------------------------------------------------------------------===

void SymbolTable::wrap(Symbol *sym, Symbol *real, Symbol *wrap) {
  // Redirect __real_foo to the original foo and foo to the original __wrap_foo.
  // All three names were inserted before wrapping, so their slots exist.
  Symbol *&symTarget = intern(CachedHashStringRef(sym->getName()))->symbol;
  Symbol *&realTarget = intern(CachedHashStringRef(real->getName()))->symbol;
  Symbol *&wrapTarget = intern(CachedHashStringRef(wrap->getName()))->symbol;

  realTarget = symTarget;
  symTarget = wrapTarget;

  // Propagate symbol usage information to the redirected symbols.
  if (sym->isUsedInRegularObj)
    wrap->isUsedInRegularObj = true;
  if (real->isUsedInRegularObj)
    sym->isUsedInRegularObj = true;
  else if (!sym->isDefined())
    // Now that all references to sym have been redirected to wrap, if there are
    // no references to real (which has been redirected to sym), we only need to
    // keep sym if it was defined, otherwise it's unused and can be dropped.
    sym->isUsedInRegularObj = false;

  // Now renaming is complete, and no one refers to real. We drop real from
  // .symtab and .dynsym. If real is undefined, it is important that we don't
  // leave it in .dynsym, because otherwise it might lead to an undefined symbol
  // error in a subsequent link. If real is defined, we could emit real as an
  // alias for sym, but that could degrade the user experience of some tools
  // that can print out only one symbol for each location: sym is a preferred
  // name than real, but they might print out real instead.
  memcpy(real, sym, sizeof(SymbolUnion));
  real->isUsedInRegularObj = false;
}

// ===----------------------------------------------------------------------===
// Symbol lookup & insertion
// ===----------------------------------------------------------------------===

namespace {
constexpr size_t fastIndexProbes = 16;
}

SymbolNameSlot *SymbolTable::findFast(CachedHashStringRef name) const {
  if (!fastIndex)
    return nullptr;
  const uint32_t hash = name.hash();
  const size_t size = name.size();
  for (size_t i = hash, n = 0; n != fastIndexProbes; ++i, ++n) {
    SymbolNameSlot *slot =
        fastIndex[i & fastIndexMask].load(std::memory_order_acquire);
    if (!slot)
      return nullptr;
    if (slot->hash == hash && slot->size == size &&
        memcmp(slot->name, name.val().data(), size) == 0)
      return slot;
  }
  return nullptr;
}

void SymbolTable::publishFast(SymbolNameSlot *slot) {
  if (!fastIndex)
    return;
  for (size_t i = slot->hash, n = 0; n != fastIndexProbes; ++i, ++n) {
    std::atomic<SymbolNameSlot *> &entry = fastIndex[i & fastIndexMask];
    SymbolNameSlot *expected = nullptr;
    if (entry.compare_exchange_strong(expected, slot,
                                      std::memory_order_release,
                                      std::memory_order_acquire) ||
        expected == slot)
      return;
  }
}

SymbolNameSlot *SymbolTable::intern(CachedHashStringRef name) {
  if (SymbolNameSlot *slot = findFast(name))
    return slot;
  SymbolNameSlot *result;
  {
    NameShard &shard = shardFor(name);
    NameShardGuard lock(shard.mutex, concurrentInterning);
    SymbolNameSlot *&slot = shard.map[name];
    if (!slot) {
      slot = new (shard.slots.Allocate()) SymbolNameSlot;
      slot->symbol = nullptr;
      slot->size = static_cast<uint32_t>(name.size());
      slot->hash = name.hash();
      slot->name = name.val().data();
      slot->comdatOwner = nullptr;
    }
    result = slot;
  }
  publishFast(result);
  return result;
}

SymbolNameSlot *SymbolTable::lookup(CachedHashStringRef name) {
  NameShard &shard = shardFor(name);
  NameShardGuard lock(shard.mutex, concurrentInterning);
  auto it = shard.map.find(name);
  return it == shard.map.end() ? nullptr : it->second;
}

void SymbolTable::reserveNames(size_t expectedNames) {
  // Room for eight times the expected names keeps probe sequences short; the
  // index is small enough to stay in the caches.
  if (!fastIndex) {
    const size_t capacity =
        llvm::PowerOf2Ceil(std::max<size_t>(expectedNames * 8, 1 << 16));
    fastIndex.reset(new std::atomic<SymbolNameSlot *>[capacity]);
    fastIndexMask = capacity - 1;
    parallelFor(0, capacity / 4096, [&](size_t i) {
      for (size_t k = i * 4096, e = k + 4096; k != e; ++k)
        fastIndex[k].store(nullptr, std::memory_order_relaxed);
    });
  }
  const size_t perShard = expectedNames / numNameShards + 1;
  // Sizing all shards touches tens of megabytes; spread it over the workers.
  parallelFor(0, numNameShards,
              [&](size_t i) { nameShards[i].map.reserve(perShard); });
}

Symbol *SymbolTable::createSymbol(SymbolNameSlot *slot, StringRef name,
                                  bool hasVersionSuffix) {
  if (!symbolArena)
    symbolArena = &getSpecificAllocSingleton<SymbolUnion>();
  Symbol *sym =
      reinterpret_cast<Symbol *>(new (symbolArena->Allocate()) SymbolUnion());
  slot->symbol = sym;
  symVector.push_back(sym);

  // *sym was not initialized by a constructor. Initialize all Symbol fields.
  memset(sym, 0, sizeof(Symbol));
  sym->setName(name);
  sym->partition = 1;
  sym->versionId = VER_NDX_GLOBAL;
  if (hasVersionSuffix)
    sym->hasVersionSuffix = true;
  return sym;
}

// Find an existing symbol or create a new one.
Symbol *SymbolTable::insert(StringRef name) {
  // <name>@@<version> means the symbol is the default version. In that
  // case <name>@@<version> will be used to resolve references to <name>.
  //
  // Since this is a hot path, the following string search code is
  // optimized for speed. StringRef::find(char) is much faster than
  // StringRef::find(StringRef).
  StringRef stem = name;
  size_t pos = name.find('@');
  if (pos != StringRef::npos && pos + 1 < name.size() && name[pos + 1] == '@')
    stem = name.take_front(pos);

  SymbolNameSlot *slot = intern(CachedHashStringRef(stem));
  if (Symbol *sym = slot->symbol) {
    if (stem.size() != name.size()) {
      sym->setName(name);
      sym->hasVersionSuffix = true;
    }
    return sym;
  }
  return createSymbol(slot, name, pos != StringRef::npos);
}

// This variant of addSymbol is used by BinaryFile::parse to check duplicate
// symbol errors.
Symbol *SymbolTable::addAndCheckDuplicate(const Defined &newSym) {
  Symbol *sym = insert(newSym.getName());
  if (sym->isDefined())
    sym->checkDuplicate(newSym);
  sym->resolve(newSym);
  sym->isUsedInRegularObj = true;
  return sym;
}

Symbol *SymbolTable::find(StringRef name) {
  SymbolNameSlot *slot = lookup(CachedHashStringRef(name));
  return slot ? slot->symbol : nullptr;
}

// ===----------------------------------------------------------------------===
// Version-script lookup
// ===----------------------------------------------------------------------===

namespace {
// A version script/dynamic list is only meaningful for a Defined symbol.
// A CommonSymbol will be converted to a Defined in replaceCommonSymbols().
// A lazy symbol may be made Defined if an LTO libcall extracts it.
bool canBeVersioned(const Symbol &sym) {
  return sym.isDefined() || sym.isCommon() || sym.isLazy();
}
} // namespace

SmallVector<Symbol *, 0> SymbolTable::findByVersion(SymbolVersion ver) {
  if (Symbol *sym = find(ver.name))
    if (canBeVersioned(*sym))
      return {sym};
  return {};
}

SmallVector<Symbol *, 0> SymbolTable::findAllByVersion(SymbolVersion ver,
                                                       bool includeNonDefault) {
  SmallVector<Symbol *, 0> res;
  SingleStringMatcher m(ver.name);
  auto check = [&](const Symbol &sym) -> bool {
    if (!includeNonDefault)
      return !sym.hasVersionSuffix;
    StringRef name = sym.getName();
    size_t pos = name.find('@');
    return !(pos + 1 < name.size() && name[pos + 1] == '@');
  };

  for (Symbol *sym : symVector)
    if (canBeVersioned(*sym) && check(*sym) && m.match(sym->getName()))
      res.push_back(sym);
  return res;
}

// ===----------------------------------------------------------------------===
// Version assignment
// ===----------------------------------------------------------------------===

void SymbolTable::handleDynamicList() {
  SmallVector<Symbol *, 0> syms;
  for (SymbolVersion &ver : config->dynamicList) {
    if (ver.hasWildcard)
      syms = findAllByVersion(ver, /*includeNonDefault=*/true);
    else
      syms = findByVersion(ver);

    for (Symbol *sym : syms)
      sym->inDynamicList = true;
  }
}

// Set symbol versions to symbols. This function handles patterns containing no
// wildcard characters. Return false if no symbol definition matches ver.
bool SymbolTable::assignExactVersion(SymbolVersion ver, uint16_t versionId) {
  SmallVector<Symbol *, 0> syms = findByVersion(ver);

  auto getName = [](uint16_t ver) -> std::string {
    if (ver == VER_NDX_LOCAL)
      return "VER_NDX_LOCAL";
    if (ver == VER_NDX_GLOBAL)
      return "VER_NDX_GLOBAL";
    return ("version '" + config->versionDefinitions[ver].name + "'").str();
  };

  // Assign the version.
  for (Symbol *sym : syms) {
    // Symbol versions specified by symbol names are governed by their own
    // version node in parseSymbolVersion(), not by this exact-match pass.
    if (sym->hasVersionSuffix)
      continue;

    // If the version has not been assigned, assign versionId to the symbol.
    if (!sym->versionScriptAssigned) {
      sym->versionScriptAssigned = true;
      sym->versionId = versionId;
    }
    if (sym->versionId == versionId)
      continue;

    warn("attempt to reassign symbol '" + ver.name + "' of " +
         getName(sym->versionId) + " to " + getName(versionId));
  }
  return !syms.empty();
}

void SymbolTable::assignWildcardVersion(SymbolVersion ver, uint16_t versionId) {
  // Exact matching takes precedence over fuzzy matching,
  // so we set a version to a symbol only if no version has been assigned
  // to the symbol. This behavior is compatible with GNU.
  for (Symbol *sym : findAllByVersion(ver, /*includeNonDefault=*/false))
    if (!sym->versionScriptAssigned) {
      sym->versionScriptAssigned = true;
      sym->versionId = versionId;
    }
}

// This function processes version scripts by updating the versionId
// member of symbols.
// If there's only one anonymous version definition in a version
// script file, the script does not actually define any symbol version,
// but just specifies symbols visibilities.
void SymbolTable::scanVersionScript() {
  SmallString<128> buf;
  // First, we assign versions to exact matching symbols,
  // i.e. version definitions not containing any glob meta-characters.
  for (VersionDefinition &v : config->versionDefinitions) {
    auto assignExact = [&](SymbolVersion pat, uint16_t id, StringRef ver) {
      bool found = assignExactVersion(pat, id);
      // A foo@v definition is keyed by its suffixed name. Finding it suppresses
      // the undefined-pattern diagnostic, but parseSymbolVersion() owns its
      // version and visibility assignment.
      buf.clear();
      found |= !findByVersion({(pat.name + "@" + v.name).toStringRef(buf),
                               /*hasWildCard=*/false})
                    .empty();
      if (!found && !config->undefinedVersion)
        errorOrWarn("version script assignment of '" + ver + "' to symbol '" +
                    pat.name + "' failed: symbol not defined");
    };
    for (SymbolVersion &pat : v.nonLocalPatterns)
      if (!pat.hasWildcard)
        assignExact(pat, v.id, v.name);
    for (SymbolVersion pat : v.localPatterns)
      if (!pat.hasWildcard)
        assignExact(pat, VER_NDX_LOCAL, "local");
  }

  // Next, assign versions to wildcards that are not "*". Note that because the
  // last match takes precedence over previous matches, we iterate over the
  // definitions in the reverse order.
  for (VersionDefinition &v : llvm::reverse(config->versionDefinitions)) {
    for (SymbolVersion &pat : v.nonLocalPatterns)
      if (pat.hasWildcard && pat.name != "*")
        assignWildcardVersion(pat, v.id);
    for (SymbolVersion &pat : v.localPatterns)
      if (pat.hasWildcard && pat.name != "*")
        assignWildcardVersion(pat, VER_NDX_LOCAL);
  }

  // Then, assign versions to "*". In GNU linkers they have lower priority than
  // other wildcards.
  for (VersionDefinition &v : llvm::reverse(config->versionDefinitions)) {
    for (SymbolVersion &pat : v.nonLocalPatterns)
      if (pat.hasWildcard && pat.name == "*")
        assignWildcardVersion(pat, v.id);
    for (SymbolVersion &pat : v.localPatterns)
      if (pat.hasWildcard && pat.name == "*")
        assignWildcardVersion(pat, VER_NDX_LOCAL);
  }

  // Symbol themselves might know their versions because symbols
  // can contain versions in the form of <name>@<version>.
  // Let them parse and update their names to exclude version suffix.
  for (Symbol *sym : symVector)
    if (sym->hasVersionSuffix)
      sym->parseSymbolVersion();

  // isPreemptible is false at this point. To correctly compute the binding of a
  // Defined (which is used by includeInDynsym()), we need to know if it is
  // VER_NDX_LOCAL or not. Compute symbol versions before handling
  // --dynamic-list.
  handleDynamicList();
}
