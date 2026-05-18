#include "Linker/COFF/LTO.h"
#include "Linker/COFF/COFFLinkerContext.h"
#include "Linker/COFF/Config.h"
#include "Linker/COFF/InputFiles.h"
#include "Linker/COFF/Symbols.h"
#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Support/Strings.h"
#include "llvm/LTO/LTO.h"
#include <memory>
#include <vector>

using namespace llvm;
using namespace linker;
using namespace linker::coff;

// ===----------------------------------------------------------------------===
// LTO configuration
// ===----------------------------------------------------------------------===

lto::Config BitcodeCompiler::createConfig() {
  return createLTOConfig(*ctx.config.driverCfg, diagnosticHandler);
}

// ===----------------------------------------------------------------------===
// BitcodeCompiler: lifecycle
// ===----------------------------------------------------------------------===

BitcodeCompiler::BitcodeCompiler(COFFLinkerContext &c) : ctx(c) {
  ltoObj = std::make_unique<lto::LTO>(createConfig(),
                                      ctx.config.driverCfg->ltoPartitions);
}

BitcodeCompiler::~BitcodeCompiler() = default;

// ===----------------------------------------------------------------------===
// Symbol resolution & input registration
// ===----------------------------------------------------------------------===

namespace {
void undefine(Symbol *s) { replaceSymbol<Undefined>(s, s->getName()); }
} // namespace

void BitcodeCompiler::add(BitcodeFile &f) {
  lto::InputFile &obj = *f.obj;
  unsigned symNum = 0;
  std::vector<Symbol *> symBodies = f.getSymbols();
  std::vector<lto::SymbolResolution> resols(symBodies.size());

  // Provide a resolution to the LTO API for each symbol.
  for (const lto::InputFile::Symbol &objSym : obj.symbols()) {
    Symbol *sym = symBodies[symNum];
    lto::SymbolResolution &r = resols[symNum];
    ++symNum;

    // Override marker IR globals are intentionally absent from the linker
    // symbol table (see BitcodeFile::parse). Hand LTO a no-op resolution
    // for their slots so the markers don't participate in LTO symbol
    // selection. Markers are extern_weak with no section and no uses, so they
    // are not emitted into native objects.
    if (!sym) {
      r.Prevailing = false;
      r.VisibleToRegularObj = false;
      r.LinkerRedefined = false;
      continue;
    }

    // Ideally we shouldn't check for SF_Undefined but currently IRObjectFile
    // reports two symbols for module ASM defined. Without this check, the
    // linker flags an undefined in IR with a definition in ASM as prevailing.
    // Once IRObjectFile is fixed to report only one symbol this hack can
    // be removed.
    r.Prevailing = !objSym.isUndefined() && sym->getFile() == &f;

    if (!objSym.isUndefined() && sym->getFile() == &f &&
        ctx.overrideSymbols.count(sym->getName()))
      r.Prevailing = true;

    r.VisibleToRegularObj = sym->isUsedInRegularObj;
    if (r.Prevailing)
      undefine(sym);

    // We tell LTO to not apply interprocedural optimization for wrapped
    // (with -wrap) symbols because otherwise LTO would inline them while
    // their values are still not final.
    r.LinkerRedefined = !sym->canInline;
  }
  checkError(ltoObj->add(std::move(f.obj), resols));
}

// Merge all the bitcode files we have seen, codegen the result
// and return the resulting objects.
// ===----------------------------------------------------------------------===
// Bitcode merge & code generation
// ===----------------------------------------------------------------------===

std::vector<InputFile *> BitcodeCompiler::compile() {
  unsigned maxTasks = ltoObj->getMaxTasks();
  buf.resize(maxTasks);

  checkError(ltoObj->run([&](size_t task, const Twine &moduleName) {
    return std::make_unique<CachedFileStream>(
        std::make_unique<raw_svector_ostream>(buf[task]));
  }));

  const auto &D = *ctx.config.driverCfg;
  std::vector<InputFile *> ret;
  for (unsigned i = 0; i != maxTasks; ++i) {
    if (buf[i].empty())
      continue;
    if (D.saveTemps)
      saveBuffer(buf[i],
                 saver().save(Twine(ctx.config.outputFile) + ".lto" +
                              (i == 0 ? Twine("") : Twine('.') + Twine(i)) +
                              ".obj"));
    auto *f = make<ObjFile>(ctx, MemoryBufferRef(buf[i], "lto.tmp"));
    f->builtFromBitcode = true;
    ret.push_back(f);
  }
  return ret;
}
