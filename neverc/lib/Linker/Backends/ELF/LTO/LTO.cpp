#include "Linker/ELF/LTO.h"
#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Runtime/Diagnostic.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/Core/Support/Strings.h"
#include "Linker/ELF/Config.h"
#include "Linker/ELF/ELFContextAccess.h"
#include "Linker/ELF/InputFiles.h"
#include "Linker/ELF/SymbolTable.h"
#include "Linker/ELF/Symbols.h"
#include "neverc/Foundation/AndroidKernelRuntimeContract.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/LTO/LTO.h"
#include <cstddef>
#include <memory>
#include <vector>

using namespace llvm;
using namespace llvm::ELF;
using namespace linker;
using namespace linker::elf;

// ===----------------------------------------------------------------------===
// Internal helpers
// ===----------------------------------------------------------------------===

namespace {

// Backend inputs to createLTOConfig() beyond the driver config.  They
// shape the LTO output, so they also enter the cache key (finalize()).
constexpr char ltoCacheBackendTag[] = "elf";
constexpr bool emitAddrsig = true;

lto::Config createConfig() {
  return createLTOConfig(*config->driverCfg, diagnosticHandler, emitAddrsig);
}

} // namespace

// ===----------------------------------------------------------------------===
// BitcodeCompiler: lifecycle
// ===----------------------------------------------------------------------===

BitcodeCompiler::BitcodeCompiler() {
  ltoObj = std::make_unique<lto::LTO>(createConfig(),
                                      config->driverCfg->ltoPartitions);
  cacheUsable = ltoCacheUsable(*config->driverCfg);

  if (elfState().bitcodeFiles.empty())
    return;
  for (Symbol *sym : symtab.getSymbols()) {
    if (sym->isPlaceholder())
      continue;
    StringRef s = sym->getName();
    for (StringRef prefix : {"__start_", "__stop_"})
      if (s.starts_with(prefix))
        usedStartStop.insert(s.substr(prefix.size()));
  }
}

BitcodeCompiler::~BitcodeCompiler() = default;

// ===----------------------------------------------------------------------===
// Symbol resolution & input registration
// ===----------------------------------------------------------------------===

std::vector<lto::SymbolResolution>
BitcodeCompiler::prepare(BitcodeFile &f) const {
  lto::InputFile &obj = *f.obj;
  bool isExec = !config->shared && !config->relocatable;

  ArrayRef<Symbol *> syms = f.getSymbols();
  ArrayRef<lto::InputFile::Symbol> objSyms = obj.symbols();
  std::vector<lto::SymbolResolution> resols(syms.size());

  for (size_t i = 0, e = syms.size(); i != e; ++i) {
    Symbol *sym = syms[i];
    const lto::InputFile::Symbol &objSym = objSyms[i];
    lto::SymbolResolution &r = resols[i];

    // Override-marker IR globals (extern_weak placeholders emitted by the
    // NeverC frontend) are deliberately not in the symbol table -- hand
    // LTO a zero-init resolution so its slot still lines up.
    if (!sym) {
      r.Prevailing = false;
      r.VisibleToRegularObj = false;
      r.ExportDynamic = false;
      r.FinalDefinitionInLinkageUnit = false;
      r.LinkerRedefined = false;
      continue;
    }

    // IRObjectFile reports module-ASM defines twice; ignore the undefined
    // shadow so a definition in ASM isn't flagged as IR-prevailing.
    r.Prevailing = !objSym.isUndefined() && sym->file == &f;

    if (!objSym.isUndefined() && sym->file == &f &&
        elfState().overrideSymbols.count(sym->getName()))
      r.Prevailing = true;

    // Preserved symbols: anything visible to non-IR consumers --
    // relocatable links, regular-obj refs, __start_/__stop_ pairs,
    // dynamic exports, and wrapped symbols.
    //
    // Embedded-runtime private definitions (the NeverC nvk kernel runtime links
    // one identical copy into every consumer TU under a reserved namespace) are
    // the sole exception at the delivered .ko boundary.  A relocatable (-r)
    // link normally forces every symbol VisibleToRegularObj, which makes LTO
    // keep and rename per-TU copies of these hidden weak/ODR privates.  That
    // would split mutable runtime state in the final module.  Intermediate .o
    // links must still preserve the symbols, however: internalizing them there
    // would prevent a later native link from coalescing that partial object
    // with runtime copies from additional translation units.
    const bool isRuntimePrivate =
        config->finalizeAndroidKernelModule &&
        neverc::AndroidKernelRuntimeContract::isLocalSymbol(sym->getName());
    r.VisibleToRegularObj = (config->relocatable && !isRuntimePrivate) ||
                            sym->isUsedInRegularObj ||
                            sym->referencedAfterWrap ||
                            (r.Prevailing && sym->includeInDynsym()) ||
                            usedStartStop.count(objSym.getSectionName());
    r.ExportDynamic =
        sym->computeBinding() != STB_LOCAL &&
        (config->exportDynamic || sym->exportDynamic || sym->inDynamicList);
    const auto *dr = dyn_cast<Defined>(sym);
    r.FinalDefinitionInLinkageUnit =
        (isExec || sym->visibility() != STV_DEFAULT) && dr &&
        // Bitcode symbols have a null section, so this skips both
        // absolute-from-ELF (avoids spurious PC-rel relocs) and
        // script-defined symbols (no File).
        !(dr->section == nullptr && (!sym->file || sym->file->isElf()));

    // Skip IPO for --wrap targets so their pre-wrap value isn't inlined.
    r.LinkerRedefined = sym->scriptDefined;
  }
  return resols;
}

void BitcodeCompiler::commit(BitcodeFile &f,
                             std::vector<lto::SymbolResolution> resols) {
  ArrayRef<Symbol *> syms = f.getSymbols();
  for (size_t i = 0, e = syms.size(); i != e; ++i) {
    Symbol *sym = syms[i];
    if (!sym || !resols[i].Prevailing)
      continue;
    Undefined(nullptr, StringRef(), STB_GLOBAL, STV_DEFAULT, sym->type)
        .overwrite(*sym);
  }
  if (cacheUsable)
    cacheKey.addInput(f.mb, resols);
  checkError(ltoObj->add(std::move(f.obj), resols));
}

void BitcodeCompiler::add(BitcodeFile &f) { commit(f, prepare(f)); }

void BitcodeCompiler::addBatch(ArrayRef<BitcodeFile *> files) {
  if (files.empty())
    return;
  std::vector<std::vector<lto::SymbolResolution>> precomputed(files.size());
  parallelFor(0, files.size(),
              [&](size_t i) { precomputed[i] = prepare(*files[i]); });
  for (size_t i = 0, n = files.size(); i != n; ++i)
    commit(*files[i], std::move(precomputed[i]));
}

// ===----------------------------------------------------------------------===
// Bitcode merge & code generation
// ===----------------------------------------------------------------------===
std::vector<InputFile *> BitcodeCompiler::compile() {
  unsigned maxTasks = ltoObj->getMaxTasks();
  buf.resize(maxTasks);

  if (!elfState().bitcodeFiles.empty())
    runLTOWithCache(*ltoObj, cacheKey, cacheUsable, *config->driverCfg,
                    ltoCacheBackendTag, emitAddrsig, buf);
  ltoObj.reset();

  std::vector<InputFile *> ret;
  for (unsigned i = 0; i != maxTasks; ++i) {
    if (buf[i].empty())
      continue;
    if (config->driverCfg->saveTemps)
      saveBuffer(buf[i], config->outputFile +
                             (i == 0 ? Twine(".lto.o") : Twine(i) + ".lto.o"));
    auto *f = createObjFile(MemoryBufferRef(buf[i], "lto.tmp"));
    f->builtFromBitcode = true;
    ret.push_back(f);
  }
  return ret;
}
