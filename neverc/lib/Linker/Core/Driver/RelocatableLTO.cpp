//===----------------------------------------------------------------------===//
//
//  runPluginRelocatableLTO -- compiles the bitcode inputs of a plugin-mediated
//  relocatable (-r) link into native relocatable object images.
//
//  A plugin that intercepts the relocatable-link route receives typed
//  ObjectGraphs, which only exist for native objects.  When the link inputs are
//  LLVM bitcode (e.g. `-fandroid-kernel-driver-mode`, which implies
//  `-flto=full`), the bitcode must first be lowered to native objects -- the
//  same step the native `-r` path performs (`compileBitcodeFiles`) before the
//  byte merge.  This helper does exactly that, reusing the shared LTO config
//  (which wires in the loaded plugin's IR/MIR/optimization hooks) and the LTO
//  runner, so the plugin's object-merge / object-phase providers can then run on
//  real native objects.
//
//  Symbol resolution here is the relocatable-link subset: symbols stay visible
//  to regular objects (a `-r` output keeps them for later links) and each name
//  gets exactly one prevailing definition (first non-weak definition wins, else
//  the first definition).  Embedded nvk runtime privates are the exception:
//  they must coalesce to one instance, matching the native ELF BitcodeCompiler
//  resolution.  This is otherwise simpler than the full-link resolution the
//  native backend derives from its symbol table.  The exception applies only
//  when this link finalizes the delivered .ko; partial objects keep the symbols
//  visible so later links can still coalesce them.
//
//===----------------------------------------------------------------------===//

#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Driver/LTOCache.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/Core/Runtime/Session.h"
#include "neverc/Foundation/AndroidKernelRuntimeContract.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <utility>
#include <vector>

using namespace llvm;

namespace linker {

namespace {
// The winning definition of one symbol name.  Kept at namespace scope: MSVC's
// ARM64 front end hits an internal compiler error (symtable.cpp) when a
// function-local class is used as a DenseMap value type.
struct PrevailingDefinition {
  size_t File = 0;
  size_t Symbol = 0;
  bool Weak = true;
};
} // namespace

Expected<std::vector<SmallString<0>>>
runPluginRelocatableLTO(const LinkerDriverConfig &Config,
                        ArrayRef<MemoryBufferRef> BitcodeBuffers,
                        StringRef BackendTag, bool EmitAddrsig) {
  if (BitcodeBuffers.empty())
    return std::vector<SmallString<0>>{};

  // The shared LTO config and runner reach for the active CommonLinkerContext
  // (diagnostics, bump allocator, parallel codegen pool).  The plugin link
  // bridge runs before any backend driver has installed one, so establish a
  // self-contained context for the duration of this LTO run.
  CommonLinkerContext Context;
  LinkerContextGuard ContextGuard(Context);
  Context.configureParallel(Config.threadCount, 16);
  Context.e.initialize(llvm::outs(), llvm::errs(), /*exitEarly=*/false,
                       /*disableOutput=*/false);
  Context.e.errorLimit = Config.errorLimit;
  Context.e.logName = "neverc";

  lto::Config LtoConfig = createLTOConfig(Config, diagnosticHandler, EmitAddrsig);
  auto Lto = std::make_unique<lto::LTO>(std::move(LtoConfig),
                                        Config.ltoPartitions);

  // Parse every bitcode input up front so prevailing selection can see all
  // definitions before any InputFile is handed (moved) to lto::LTO::add.
  std::vector<std::unique_ptr<lto::InputFile>> Files;
  Files.reserve(BitcodeBuffers.size());
  for (MemoryBufferRef Buffer : BitcodeBuffers) {
    auto File = lto::InputFile::create(Buffer);
    if (!File)
      return File.takeError();
    Files.push_back(std::move(*File));
  }

  // Choose one prevailing definition per symbol name across all modules:
  // a non-weak definition beats a weak one; otherwise the first seen wins.
  //
  // The names are copied rather than referenced. Handing an input to
  // lto::LTO::add below transfers ownership of it, and the call destroys it
  // before returning -- so a name recorded here would outlive the object that
  // spells it, and the lookups for every later input would read freed memory.
  // Whether it actually does depends on where the reader put the string table:
  // a bitcode file carrying a symbol table of the current version is read in
  // place, leaving the names pointing into the caller's buffer, while one that
  // has to have its symbol table rebuilt -- an older version, or a file the
  // reader could not use as-is -- gets a table the input owns. That is a
  // property of the input, not something this can rely on.
  StringMap<PrevailingDefinition> Winners;
  for (size_t FileIndex = 0; FileIndex != Files.size(); ++FileIndex) {
    size_t SymbolIndex = 0;
    for (const lto::InputFile::Symbol &Symbol : Files[FileIndex]->symbols()) {
      if (!Symbol.isUndefined()) {
        const StringRef Name = Symbol.getName();
        const bool Weak = Symbol.isWeak();
        auto It = Winners.find(Name);
        if (It == Winners.end())
          Winners[Name] = PrevailingDefinition{FileIndex, SymbolIndex, Weak};
        else if (It->second.Weak && !Weak)
          It->second = PrevailingDefinition{FileIndex, SymbolIndex, Weak};
      }
      ++SymbolIndex;
    }
  }

  for (size_t FileIndex = 0; FileIndex != Files.size(); ++FileIndex) {
    std::vector<lto::SymbolResolution> Resolutions;
    size_t SymbolIndex = 0;
    for (const lto::InputFile::Symbol &Symbol : Files[FileIndex]->symbols()) {
      lto::SymbolResolution Resolution;
      // Relocatable outputs preserve symbols for later links.  At the delivered
      // .ko boundary only, let embedded-runtime ODR privates coalesce to one
      // instance; doing that in an intermediate .o would internalize its copy
      // before later native inputs have a chance to coalesce with it.  This
      // mirrors the native ELF BitcodeCompiler resolution.
      const bool IsRuntimePrivate =
          Config.finalizeAndroidKernelModule &&
          neverc::AndroidKernelRuntimeContract::isLocalSymbol(Symbol.getName());
      Resolution.VisibleToRegularObj = !IsRuntimePrivate;
      if (!Symbol.isUndefined()) {
        auto It = Winners.find(Symbol.getName());
        Resolution.Prevailing = It != Winners.end() &&
                                It->second.File == FileIndex &&
                                It->second.Symbol == SymbolIndex;
      }
      Resolutions.push_back(Resolution);
      ++SymbolIndex;
    }
    if (Error E = Lto->add(std::move(Files[FileIndex]), Resolutions))
      return std::move(E);
  }

  const unsigned MaxTasks = Lto->getMaxTasks();
  std::vector<SmallString<0>> Buffers(MaxTasks);
  // A relocatable plugin link always runs with plugins active, which disables
  // the LTO output cache; pass usable=false so the empty key is never used.
  LTOCacheKey CacheKey;
  runLTOWithCache(*Lto, CacheKey, /*usable=*/false, Config, BackendTag,
                  EmitAddrsig, Buffers);
  Lto.reset();

  std::vector<SmallString<0>> Objects;
  Objects.reserve(Buffers.size());
  for (SmallString<0> &Buffer : Buffers)
    if (!Buffer.empty())
      Objects.push_back(std::move(Buffer));

  if (Objects.empty())
    return createStringError(inconvertibleErrorCode(),
                             "relocatable LTO produced no native objects");
  return Objects;
}

} // namespace linker
