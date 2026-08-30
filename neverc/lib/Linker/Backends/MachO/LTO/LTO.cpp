#include "Linker/MachO/LTO.h"
#include "Driver/Parallelism.h"
#include "Linker/MachO/Config.h"
#include "Linker/MachO/Driver.h"
#include "Linker/MachO/InputFiles.h"
#include "Linker/MachO/Symbols.h"

#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/LinkerParallel.h"
#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Support/Strings.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Support/Parallel.h"

using namespace linker;
using namespace linker::macho;
using namespace llvm;
using namespace llvm::MachO;

// ===----------------------------------------------------------------------===
// Internal helpers
// ===----------------------------------------------------------------------===

namespace {

// Backend inputs to createLTOConfig() beyond the driver config.  They
// shape the LTO output, so they also enter the cache key (finalize()).
constexpr char ltoCacheBackendTag[] = "macho";

// Safe ICF needs the address-significance table.
bool emitAddrsig() { return config->icfLevel == ICFLevel::safe; }

lto::Config createConfig() {
  return createLTOConfig(*config->driverCfg, diagnosticHandler, emitAddrsig());
}

} // namespace

// ===----------------------------------------------------------------------===
// BitcodeCompiler: lifecycle
// ===----------------------------------------------------------------------===

BitcodeCompiler::BitcodeCompiler() {
  ltoObj = std::make_unique<lto::LTO>(createConfig(),
                                      config->driverCfg->ltoPartitions);
  cacheUsable = ltoCacheUsable(*config->driverCfg);
}

// ===----------------------------------------------------------------------===
// Symbol resolution & input registration
// ===----------------------------------------------------------------------===

std::vector<lto::SymbolResolution>
BitcodeCompiler::prepare(BitcodeFile &f) const {
  const lto::InputFile &obj = *f.obj;
  ArrayRef<lto::InputFile::Symbol> objSyms = obj.symbols();
  std::vector<lto::SymbolResolution> resols(objSyms.size());

  const bool exportDynamic =
      config->outputType != MH_EXECUTE || config->exportDynamic;

  auto symIt = f.symbols.begin();
  for (size_t i = 0, n = objSyms.size(); i != n; ++i) {
    const lto::InputFile::Symbol &objSym = objSyms[i];
    lto::SymbolResolution &r = resols[i];
    Symbol *sym = *symIt++;

    // Override-marker globals are dropped from the linker symbol table; emit
    // a no-op resolution so the LTO slot still lines up with InputFile.
    if (!sym)
      continue;

    // Treat ASM-defined IR symbols as non-prevailing: IRObjectFile reports
    // them twice and the ASM definition wins.
    r.Prevailing = !objSym.isUndefined() && sym->getFile() == &f;

    if (!objSym.isUndefined() && sym->getFile() == &f &&
        config->overrideSymbols.count(sym->getName()))
      r.Prevailing = true;

    if (const auto *defined = dyn_cast<Defined>(sym)) {
      r.ExportDynamic =
          defined->isExternal() && !defined->privateExtern && exportDynamic;
      r.FinalDefinitionInLinkageUnit =
          !defined->isExternalWeakDef() && !defined->interposable;
    } else if (const auto *common = dyn_cast<CommonSymbol>(sym)) {
      r.ExportDynamic = !common->privateExtern && exportDynamic;
      r.FinalDefinitionInLinkageUnit = true;
    }

    r.VisibleToRegularObj =
        sym->isUsedInRegularObj || (r.Prevailing && r.ExportDynamic);
  }
  return resols;
}

void BitcodeCompiler::commit(BitcodeFile &f,
                             std::vector<lto::SymbolResolution> resols) {
  // Demote prevailing definitions to Undefined so the LTO-produced ObjFile
  // can install the native definition without a symtab collision.
  ArrayRef<lto::InputFile::Symbol> objSyms = f.obj->symbols();
  auto symIt = f.symbols.begin();
  for (size_t i = 0, n = objSyms.size(); i != n; ++i) {
    Symbol *sym = *symIt++;
    if (!sym || !resols[i].Prevailing)
      continue;
    replaceSymbol<Undefined>(sym, sym->getName(), sym->getFile(),
                             RefState::Strong, /*wasBitcodeSymbol=*/true);
  }
  if (cacheUsable)
    cacheKey.addInput(f.mb, resols);
  checkError(ltoObj->add(std::move(f.obj), resols));
  hasFiles = true;
}

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
std::vector<ObjFile *> BitcodeCompiler::compile() {
  unsigned maxTasks = ltoObj->getMaxTasks();
  buf.resize(maxTasks);

  if (hasFiles)
    runLTOWithCache(*ltoObj, cacheKey, cacheUsable, *config->driverCfg,
                    ltoCacheBackendTag, emitAddrsig(), buf);
  ltoObj.reset();

  std::vector<uint64_t> nativeObjectSizes;
  nativeObjectSizes.reserve(maxTasks);
  for (const SmallString<0> &object : buf)
    if (!object.empty())
      nativeObjectSizes.push_back(object.size());
  // Replace source-bitcode accounting even when LTO produces no object.
  configureParallelismForLTONativeObjects(nativeObjectSizes);

  const auto &D = *config->driverCfg;
  std::vector<ObjFile *> ret;
  ret.reserve(maxTasks);
  for (unsigned i = 0; i < maxTasks; ++i) {
    if (buf[i].empty())
      continue;
    if (!D.ltoNativeObjectPaths.empty() && i >= D.ltoNativeObjectPaths.size())
      fatal("driver provided too few native LTO object paths");

    StringRef ObjectPath = "lto.tmp";
    uint32_t ModTime = 0;
    if (i < D.ltoNativeObjectPaths.size()) {
      ObjectPath = D.ltoNativeObjectPaths[i];
      saveBuffer(buf[i], ObjectPath);
      ModTime = getModTime(ObjectPath);
    } else if (D.saveTemps) {
      ObjectPath = saver().save(
          config->outputFile + ((i == 0) ? "" : Twine(i)) + ".lto.o");
      saveBuffer(buf[i], ObjectPath);
      ModTime = getModTime(ObjectPath);
    }

    MemoryBufferRef NativeObject(buf[i], ObjectPath);
    ret.push_back(make<ObjFile>(NativeObject, ModTime, /*archiveName=*/"",
                                /*lazy=*/false,
                                /*forceHidden=*/false, /*compatArch=*/true,
                                /*builtFromBitcode=*/true));
  }

  return ret;
}
