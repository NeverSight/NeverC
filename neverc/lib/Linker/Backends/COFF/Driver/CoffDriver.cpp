#include "Linker/COFF/COFFLinkerContext.h"
#include "Linker/COFF/Config.h"
#include "Linker/COFF/Driver.h"
#include "Linker/COFF/Emit.h"
#include "Linker/COFF/ICF.h"
#include "Linker/COFF/InputFiles.h"
#include "Linker/COFF/MarkLive.h"
#include "Linker/COFF/SymbolTable.h"
#include "Linker/COFF/Symbols.h"
#include "Linker/Core/Driver/ArgList.h"
#include "Linker/Core/Driver/CommonLTOConfig.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Runtime/Session.h"
#include "Linker/Core/Runtime/Stopwatch.h"
#include "Linker/Core/Support/FileIO.h"
#include "neverc/Invoke/InMemoryFileStore.h"
#include "neverc/Plugin/PluginLoader.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/LTO/LTO.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Object/COFFModuleDefinition.h"
#include "llvm/Object/WindowsMachineFlag.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <regex>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::COFF;
using namespace llvm::sys;

namespace linker::coff {

// ===----------------------------------------------------------------------===
// Entry point
// ===----------------------------------------------------------------------===

bool link(ArrayRef<const char *> args, llvm::raw_ostream &stdoutOS,
          llvm::raw_ostream &stderrOS, bool exitEarly, bool disableOutput,
          const LinkerDriverConfig &driverCfg) {
  auto *ctx = new COFFLinkerContext;

  ctx->e.initialize(stdoutOS, stderrOS, exitEarly, disableOutput);
  ctx->e.errorLimit = driverCfg.errorLimit;
  ctx->e.logName = args::getFilenameWithoutExe(args[0]);
  ctx->e.errorLimitExceededMsg = "too many errors emitted, stopping now"
                                 " (use -ferror-limit=0 to see all errors)";

  // Load neverc out-of-tree C-ABI plugins so their linker passes can fire at
  // the LINK_* hook points during image emission.  Idempotent with the LTO
  // path (loadPlugin dedups by path); also covers pure object-file links.
  for (const std::string &Path : driverCfg.nevercPluginPaths) {
    std::string Err;
    if (!neverc::plugin::getGlobalPluginLoader().loadPlugin(Path, Err))
      error(Err);
  }

  ctx->driver.run(args, driverCfg);

  return errorCount() == 0;
}

// ===----------------------------------------------------------------------===
// Path & file utilities
// ===----------------------------------------------------------------------===

namespace {
std::string getOutputPath(StringRef path, bool isDll, bool isDriver) {
  StringRef ext = ".exe";
  if (isDll)
    ext = ".dll";
  else if (isDriver)
    ext = ".sys";

  return (sys::path::stem(path) + ext).str();
}
} // namespace

using MBErrPair = std::pair<std::unique_ptr<MemoryBuffer>, std::error_code>;

namespace {
MBErrPair readFileSync(StringRef path) {
  auto mbOrErr = MemoryBuffer::getFile(path, /*IsText=*/false,
                                       /*RequiresNullTerminator=*/false);
  if (!mbOrErr)
    return {nullptr, mbOrErr.getError()};
  return {std::move(*mbOrErr), std::error_code()};
}
} // namespace

// ===----------------------------------------------------------------------===
// Symbol mangling & buffer management
// ===----------------------------------------------------------------------===

StringRef LinkerDriver::mangle(StringRef sym) {
  assert(ctx.config.machine != IMAGE_FILE_MACHINE_UNKNOWN);
  return sym;
}

llvm::Triple::ArchType LinkerDriver::getArch() {
  switch (ctx.config.machine) {
  case AMD64:
    return llvm::Triple::ArchType::x86_64;
  case ARM64:
    return llvm::Triple::ArchType::aarch64;
  default:
    return llvm::Triple::ArchType::UnknownArch;
  }
}

bool LinkerDriver::findUnderscoreMangle(StringRef sym) {
  Symbol *s = ctx.symtab.findMangle(mangle(sym));
  return s && !isa<Undefined>(s);
}

MemoryBufferRef LinkerDriver::takeBuffer(std::unique_ptr<MemoryBuffer> mb) {
  MemoryBufferRef mbref = *mb;
  make<std::unique_ptr<MemoryBuffer>>(std::move(mb)); // take ownership

  return mbref;
}

void LinkerDriver::addBuffer(std::unique_ptr<MemoryBuffer> mb,
                             bool wholeArchive, bool lazy) {
  StringRef filename = mb->getBufferIdentifier();

  MemoryBufferRef mbref = takeBuffer(std::move(mb));
  filePaths.push_back(filename);

  switch (identify_magic(mbref.getBuffer())) {
  case file_magic::windows_resource:
    resources.push_back(mbref);
    break;
  case file_magic::archive:
    if (wholeArchive) {
      std::unique_ptr<Archive> file =
          CHECK(Archive::create(mbref), filename + ": failed to parse archive");
      Archive *archive = file.get();
      make<std::unique_ptr<Archive>>(std::move(file)); // take ownership

      int memberIndex = 0;
      for (MemoryBufferRef m : getArchiveMembers(archive))
        addArchiveBuffer(m, "<whole-archive>", filename, memberIndex++);
      return;
    }
    ctx.symtab.addFile(make<ArchiveFile>(ctx, mbref));
    break;
  case file_magic::bitcode:
    ctx.symtab.addFile(make<BitcodeFile>(ctx, mbref, "", 0, lazy));
    break;
  case file_magic::coff_object:
  case file_magic::coff_import_library:
    ctx.symtab.addFile(make<ObjFile>(ctx, mbref, lazy));
    break;
  case file_magic::pdb:
    error(filename + ": PDB files are not supported; NeverC uses DWARF");
    break;
  case file_magic::coff_cl_gl_object:
    error(filename + ": is not a native COFF file. Recompile without /GL");
    break;
  case file_magic::pecoff_executable:
    if (filename.ends_with_insensitive(".dll")) {
      error(filename + ": bad file type. Did you specify a DLL instead of an "
                       "import library?");
      break;
    }
    [[fallthrough]];
  default:
    error(mbref.getBufferIdentifier() + ": unknown file type");
    break;
  }
}

void LinkerDriver::enqueuePathInternal(StringRef path, bool wholeArchive,
                                       bool lazy) {
  // Fast path: in-memory bitcode from the integrated compiler pipeline.
  // Skip the future/promise/shared_ptr machinery — the buffer is already
  // available and no async I/O is needed.
  if (auto mbref = neverc::InMemoryFileStore::instance().tryGet(path)) {
    std::string pathStr(path);
    enqueueTask([this, buf = *mbref, pathStr, wholeArchive, lazy]() {
      llvm::TimeTraceScope timeScope("File: ", pathStr);
      ctx.driver.addBuffer(
          MemoryBuffer::getMemBuffer(buf.getBuffer(), buf.getBufferIdentifier(),
                                     /*RequiresNullTerminator=*/false),
          wholeArchive, lazy);
    });
    return;
  }

  std::string pathStr = std::string(path);
  enqueueTask([this, pathStr, wholeArchive, lazy]() {
    llvm::TimeTraceScope timeScope("File: ", pathStr);
    auto [mb, ec] = readFileSync(pathStr);
    if (ec) {
      // Retry reading the file synchronously -- search paths may have been
      // extended by .drective sections processed since the async read was
      // enqueued.  Synchronous retry preserves input ordering.
      if (std::optional<StringRef> retryPath = findFileIfNew(pathStr)) {
        auto retryMb = MemoryBuffer::getFile(*retryPath, /*IsText=*/false,
                                             /*RequiresNullTerminator=*/false);
        ec = retryMb.getError();
        if (!ec)
          mb = std::move(*retryMb);
      } else {
        // We've already handled this file.
        return;
      }
    }
    if (ec) {
      std::string msg = "could not open '" + pathStr + "': " + ec.message();
      // Check if the filename is a typo for an option flag. OptTable thinks
      // that all args that are not known options and that start with / are
      // filenames, but e.g. `/nodefaultlibs` is more likely a typo for
      // the option `/nodefaultlib` than a reference to a file in the root
      // directory.
      std::string nearest;
      if (ctx.optTable.findNearest(pathStr, nearest) > 1)
        message(msg); //[MSVC Compatibility]
      else
        error(msg + "; did you mean '" + nearest + "'");
    } else
      ctx.driver.addBuffer(std::move(mb), wholeArchive, lazy);
  });
}

void LinkerDriver::enqueuePath(StringRef path, bool wholeArchive, bool lazy) {
  bool hasWildcard = sys::path::filename(path).contains("*");
  if (!hasWildcard) {
    enqueuePathInternal(path, wholeArchive, lazy);
    return;
  }

  std::string regexStr =
      std::regex_replace("^" + path.str() + "$", std::regex("\\\\"), "\\\\");
  regexStr = std::regex_replace(regexStr, std::regex("/"), "\\\\");
  regexStr = std::regex_replace(regexStr, std::regex("\\."), "\\.");
  regexStr = std::regex_replace(regexStr, std::regex("\\*"), ".*");
  std::regex regex(regexStr);

  std::error_code ec;
  for (sys::fs::recursive_directory_iterator
           i(sys::path::parent_path(path), ec),
       e;
       i != e; i.increment(ec)) {
    if (ec) {
      std::string msg = "could not open '" + path.str() + "': " + ec.message() +
                        " in LinkerDriver::enqueuePath";
      error(msg);
      break;
    }

    if (std::regex_match(i->path(), regex)) {
      enqueuePathInternal(i->path(), wholeArchive, lazy);
    }
  }
}

void LinkerDriver::addArchiveBuffer(MemoryBufferRef mb, StringRef symName,
                                    StringRef parentName,
                                    uint64_t offsetInArchive) {
  file_magic magic = identify_magic(mb.getBuffer());
  if (magic == file_magic::coff_import_library) {
    InputFile *imp = make<ImportFile>(ctx, mb);
    imp->parentName = parentName;
    ctx.symtab.addFile(imp);
    return;
  }

  InputFile *obj;
  if (magic == file_magic::coff_object) {
    obj = make<ObjFile>(ctx, mb);
  } else if (magic == file_magic::bitcode) {
    obj =
        make<BitcodeFile>(ctx, mb, parentName, offsetInArchive, /*lazy=*/false);
  } else if (magic == file_magic::coff_cl_gl_object) {
    error(mb.getBufferIdentifier() +
          ": is not a native COFF file. Recompile without /GL?");
    return;
  } else {
    error("unknown file type: " + mb.getBufferIdentifier());
    return;
  }

  obj->parentName = parentName;
  ctx.symtab.addFile(obj);
  log("Loaded " + toString(obj) + " for " + symName);
}

void LinkerDriver::enqueueArchiveMember(const Archive::Child &c,
                                        const Archive::Symbol &sym,
                                        StringRef parentName) {

  auto reportBufferError = [=](Error &&e, StringRef childName) {
    fatal("could not get the buffer for the member defining symbol " +
          toCOFFString(ctx, sym) + ": " + parentName + "(" + childName +
          "): " + toString(std::move(e)));
  };

  if (!c.getParent()->isThin()) {
    uint64_t offsetInArchive = c.getChildOffset();
    Expected<MemoryBufferRef> mbOrErr = c.getMemoryBufferRef();
    if (!mbOrErr)
      reportBufferError(mbOrErr.takeError(), check(c.getFullName()));
    MemoryBufferRef mb = mbOrErr.get();
    enqueueTask([=]() {
      llvm::TimeTraceScope timeScope("Archive: ", mb.getBufferIdentifier());
      ctx.driver.addArchiveBuffer(mb, toCOFFString(ctx, sym), parentName,
                                  offsetInArchive);
    });
    return;
  }

  std::string symStr = toCOFFString(ctx, sym);
  std::string childName = CHECK(
      c.getFullName(),
      "could not get the filename for the member defining symbol " + symStr);
  enqueueTask([this, childName, symStr, reportBufferError]() {
    auto [mb, ec] = readFileSync(childName);
    if (ec)
      reportBufferError(errorCodeToError(ec), childName);
    llvm::TimeTraceScope timeScope("Archive: ", mb->getBufferIdentifier());
    ctx.driver.addArchiveBuffer(takeBuffer(std::move(mb)), symStr, "",
                                /*OffsetInArchive=*/0);
  });
}

// ===----------------------------------------------------------------------===
// Directive & library resolution
// ===----------------------------------------------------------------------===

bool LinkerDriver::isDecorated(StringRef sym) {
  return sym.starts_with("@") || sym.contains("@@") || sym.starts_with("?") ||
         sym.contains('@');
}

void LinkerDriver::parseDirectives(InputFile *file) {
  StringRef s = file->getDirectives();
  if (s.empty())
    return;

  ArgParser parser(ctx);
  ParsedDirectives directives = parser.parseDirectives(s);

  for (StringRef e : directives.exports) {
    if (!directivesExports.insert(e).second)
      continue;

    Export exp = parseExport(e);
    exp.source = ExportSource::Directives;
    ctx.config.exports.push_back(exp);
  }

  for (StringRef inc : directives.includes)
    addUndefined(inc);

  // https://docs.microsoft.com/en-us/cpp/preprocessor/comment-c-cpp?view=msvc-160
  for (auto *arg : directives.args) {
    switch (arg->getOption().getID()) {
    case OPT_aligncomm:
      parseAligncomm(arg->getValue());
      break;
    case OPT_alternatename:
      parseAlternateName(arg->getValue());
      break;
    case OPT_defaultlib:
      if (std::optional<StringRef> path = findLibIfNew(arg->getValue()))
        enqueuePath(*path, false, false);
      break;
    case OPT_disallowlib:
      ctx.config.noDefaultLibs.insert(
          std::string(findLib(arg->getValue()).lower().str()));
      break;
    case OPT_entry:
      ctx.config.entry = addUndefined(mangle(arg->getValue()));
      break;
    case OPT_failifmismatch:
      checkFailIfMismatch(arg->getValue(), file);
      break;
    case OPT_incl:
      addUndefined(arg->getValue());
      break;
    case OPT_manifestdependency:
      ctx.config.manifestDependencies.insert(arg->getValue());
      break;
    case OPT_merge:
      parseMerge(arg->getValue());
      break;
    case OPT_nodefaultlib:
      ctx.config.noDefaultLibs.insert(
          std::string(findLib(arg->getValue()).lower().str()));
      break;
    case OPT_release:
      ctx.config.writeCheckSum = true;
      break;
    case OPT_section:
      parseSection(arg->getValue());
      break;
    case OPT_stack:
      parseNumbers(arg->getValue(), &ctx.config.stackReserve,
                   &ctx.config.stackCommit);
      break;
    case OPT_subsystem: {
      bool gotVersion = false;
      parseSubsystem(arg->getValue(), &ctx.config.subsystem,
                     &ctx.config.majorSubsystemVersion,
                     &ctx.config.minorSubsystemVersion, &gotVersion);
      if (gotVersion) {
        ctx.config.majorOSVersion = ctx.config.majorSubsystemVersion;
        ctx.config.minorOSVersion = ctx.config.minorSubsystemVersion;
      }
      break;
    }
    case OPT_guardsym:
    case OPT_inferasanlibs:
    case OPT_inferasanlibs_no:
      break;
    case OPT_align:
      parseNumbers(arg->getValue(), &ctx.config.align);
      break;
    case OPT_incremental:
      ctx.config.incremental = true;
      break;
    case OPT_incremental_no:
      ctx.config.incremental = false;
      break;
    default:
      error(arg->getSpelling() + " is not allowed in .drectve (" +
            toString(file) + ")");
    }
  }
}

StringRef LinkerDriver::findFile(StringRef filename) {
  auto getFilename = [this](StringRef filename) -> StringRef {
    if (ctx.config.vfs)
      if (auto statOrErr = ctx.config.vfs->status(filename))
        return saver().save(statOrErr->getName());
    return filename;
  };

  if (sys::path::is_absolute(filename))
    return getFilename(filename);

  bool hasPathSep = (filename.find_first_of("/\\") != StringRef::npos);
  if (hasPathSep) {
    if (sys::fs::exists(filename.str())) {
      // [MSVC Compatibility]
      // If the file is not found, the search should continue
      return getFilename(filename);
    }
  }

  bool hasExt = filename.contains('.');
  for (StringRef dir : searchPaths) {
    SmallString<128> path = dir;
    sys::path::append(path, filename);
    path = SmallString<128>{getFilename(path.str())};
    if (sys::fs::exists(path.str()))
      return saver().save(path.str());
    if (!hasExt) {
      path.append(".obj");
      path = SmallString<128>{getFilename(path.str())};
      if (sys::fs::exists(path.str()))
        return saver().save(path.str());
    }
  }
  return filename;
}

namespace {
std::optional<sys::fs::UniqueID> getUniqueID(StringRef path) {
  sys::fs::UniqueID ret;
  if (sys::fs::getUniqueID(path, ret))
    return std::nullopt;
  return ret;
}
} // namespace

std::optional<StringRef> LinkerDriver::findFileIfNew(StringRef filename) {
  StringRef path = findFile(filename);

  if (std::optional<sys::fs::UniqueID> id = getUniqueID(path)) {
    bool seen = !visitedFiles.insert(*id).second;
    if (seen)
      return std::nullopt;
  }

  if (path.ends_with_insensitive(".lib"))
    visitedLibs.insert(std::string(sys::path::filename(path).lower().str()));
  return path;
}

StringRef LinkerDriver::findLib(StringRef filename) {
  bool hasExt = filename.contains('.');
  if (!hasExt)
    filename = saver().save(filename + ".lib");
  return findFile(filename);
}

std::optional<StringRef> LinkerDriver::findLibIfNew(StringRef filename) {
  if (ctx.config.noDefaultLibAll)
    return std::nullopt;
  if (!visitedLibs.insert(std::string(filename.lower().str())).second)
    return std::nullopt;

  StringRef path = findLib(filename);
  if (ctx.config.noDefaultLibs.count(std::string(path.lower().str())))
    return std::nullopt;

  if (std::optional<sys::fs::UniqueID> id = getUniqueID(path))
    if (!visitedFiles.insert(*id).second)
      return std::nullopt;
  return path;
}

// detectWinSysRoot, addNeverCLibSearchPaths, addWinSysRootLibSearchPaths,
// addLibSearchPaths removed: the neverc driver performs all SDK / toolchain
// discovery and passes resolved --libpath= arguments to the linker backend.
// See MSVC.cpp::ConstructJob() and MSVCToolChain constructor.

Symbol *LinkerDriver::addUndefined(StringRef name) {
  Symbol *b = ctx.symtab.addUndefined(name);
  if (!b->isGCRoot) {
    b->isGCRoot = true;
    ctx.config.gcroot.push_back(b);
  }
  return b;
}

// ===----------------------------------------------------------------------===
// Entry point & subsystem inference
// ===----------------------------------------------------------------------===

StringRef LinkerDriver::mangleMaybe(Symbol *s) {
  Undefined *unmangled = dyn_cast<Undefined>(s);
  if (!unmangled)
    return "";

  Symbol *mangled = ctx.symtab.findMangle(unmangled->getName());
  if (!mangled)
    return "";

  log(unmangled->getName() + " aliased to " + mangled->getName());
  unmangled->weakAlias = ctx.symtab.addUndefined(mangled->getName());
  return mangled->getName();
}

// Infer CRT entry point from user-defined main/WinMain/wmain/wWinMain.
StringRef LinkerDriver::findDefaultEntry() {
  assert(ctx.config.subsystem != IMAGE_SUBSYSTEM_UNKNOWN &&
         "must handle /subsystem before calling this");

  if (ctx.config.subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI) {
    if (findUnderscoreMangle("wWinMain")) {
      if (!findUnderscoreMangle("WinMain"))
        return mangle("wWinMainCRTStartup");
      warn("found both wWinMain and WinMain; using latter");
    }
    return mangle("WinMainCRTStartup");
  }
  if (findUnderscoreMangle("wmain")) {
    if (!findUnderscoreMangle("main"))
      return mangle("wmainCRTStartup");
    warn("found both wmain and main; using latter");
  }
  return mangle("mainCRTStartup");
}

WindowsSubsystem LinkerDriver::inferSubsystem() {
  if (ctx.config.dll)
    return IMAGE_SUBSYSTEM_WINDOWS_GUI;
  // Note that link.exe infers the subsystem from the presence of these
  // functions even if /entry: or /nodefaultlib are passed which causes them
  // to not be called.
  bool haveMain = findUnderscoreMangle("main");
  bool haveWMain = findUnderscoreMangle("wmain");
  bool haveWinMain = findUnderscoreMangle("WinMain");
  bool haveWWinMain = findUnderscoreMangle("wWinMain");
  if (haveMain || haveWMain) {
    if (haveWinMain || haveWWinMain) {
      warn(std::string("found ") + (haveMain ? "main" : "wmain") + " and " +
           (haveWinMain ? "WinMain" : "wWinMain") +
           "; defaulting to /subsystem:console");
    }
    return IMAGE_SUBSYSTEM_WINDOWS_CUI;
  }
  if (haveWinMain || haveWWinMain)
    return IMAGE_SUBSYSTEM_WINDOWS_GUI;
  return IMAGE_SUBSYSTEM_UNKNOWN;
}

uint64_t LinkerDriver::getDefaultImageBase() {
  return ctx.config.dll ? 0x180000000 : 0x140000000;
}

// ===----------------------------------------------------------------------===
// Debug, import library & module definition
// ===----------------------------------------------------------------------===

std::string LinkerDriver::getImplibPath() {
  if (!ctx.config.implib.empty())
    return std::string(ctx.config.implib);
  SmallString<128> out = StringRef(ctx.config.outputFile);
  sys::path::replace_extension(out, ".lib");
  return std::string(out.str());
}

std::string LinkerDriver::getImportName(bool asLib) {
  SmallString<128> out;

  if (ctx.config.importName.empty()) {
    out.assign(sys::path::filename(ctx.config.outputFile));
    if (asLib)
      sys::path::replace_extension(out, ".dll");
  } else {
    out.assign(ctx.config.importName);
    if (!sys::path::has_extension(out))
      sys::path::replace_extension(out,
                                   (ctx.config.dll || asLib) ? ".dll" : ".exe");
  }

  return std::string(out.str());
}

void LinkerDriver::createImportLibrary(bool asLib) {
  llvm::TimeTraceScope timeScope("Create import library");
  std::vector<COFFShortExport> exports;
  for (Export &e1 : ctx.config.exports) {
    COFFShortExport e2;
    e2.Name = std::string(e1.name);
    e2.SymbolName = std::string(e1.symbolName);
    e2.ExtName = std::string(e1.extName);
    e2.AliasTarget = std::string(e1.aliasTarget);
    e2.Ordinal = e1.ordinal;
    e2.Noname = e1.noname;
    e2.Data = e1.data;
    e2.Private = e1.isPrivate;
    e2.Constant = e1.constant;
    exports.push_back(e2);
  }

  std::string libName = getImportName(asLib);
  std::string path = getImplibPath();

  if (!ctx.config.incremental) {
    checkError(
        writeImportLibrary(libName, path, exports, ctx.config.machine, false));
    return;
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> oldBuf = MemoryBuffer::getFile(
      path, /*IsText=*/false, /*RequiresNullTerminator=*/false);
  if (!oldBuf) {
    checkError(
        writeImportLibrary(libName, path, exports, ctx.config.machine, false));
    return;
  }

  SmallString<128> tmpName;
  if (std::error_code ec =
          sys::fs::createUniqueFile(path + ".tmp-%%%%%%%%.lib", tmpName))
    fatal("cannot create temporary file for import library " + path + ": " +
          ec.message());

  if (Error e = writeImportLibrary(libName, tmpName, exports,
                                   ctx.config.machine, false)) {
    checkError(std::move(e));
    return;
  }

  std::unique_ptr<MemoryBuffer> newBuf = check(MemoryBuffer::getFile(
      tmpName, /*IsText=*/false, /*RequiresNullTerminator=*/false));
  if ((*oldBuf)->getBuffer() != newBuf->getBuffer()) {
    oldBuf->reset();
    checkError(errorCodeToError(sys::fs::rename(tmpName, path)));
  } else {
    sys::fs::remove(tmpName);
  }
}

void LinkerDriver::parseModuleDefs(StringRef path) {
  llvm::TimeTraceScope timeScope("Parse def file");
  std::unique_ptr<MemoryBuffer> mb =
      CHECK(MemoryBuffer::getFile(path, /*IsText=*/false,
                                  /*RequiresNullTerminator=*/false,
                                  /*IsVolatile=*/true),
            "could not open " + path);
  COFFModuleDefinition m =
      check(parseCOFFModuleDefinition(mb->getMemBufferRef()));

  ctx.driver.takeBuffer(std::move(mb));

  if (ctx.config.outputFile.empty())
    ctx.config.outputFile = std::string(saver().save(m.OutputFile));
  ctx.config.importName = std::string(saver().save(m.ImportName));
  if (m.ImageBase)
    ctx.config.imageBase = m.ImageBase;
  if (m.StackReserve)
    ctx.config.stackReserve = m.StackReserve;
  if (m.StackCommit)
    ctx.config.stackCommit = m.StackCommit;
  if (m.HeapReserve)
    ctx.config.heapReserve = m.HeapReserve;
  if (m.HeapCommit)
    ctx.config.heapCommit = m.HeapCommit;
  if (m.MajorImageVersion)
    ctx.config.majorImageVersion = m.MajorImageVersion;
  if (m.MinorImageVersion)
    ctx.config.minorImageVersion = m.MinorImageVersion;
  if (m.MajorOSVersion)
    ctx.config.majorOSVersion = m.MajorOSVersion;
  if (m.MinorOSVersion)
    ctx.config.minorOSVersion = m.MinorOSVersion;

  for (COFFShortExport e1 : m.Exports) {
    Export e2;
    // In simple cases, only Name is set. Renamed exports are parsed
    // and set as "ExtName = Name". If Name has the form "OtherDll.Func",
    // it shouldn't be a normal exported function but a forward to another
    // DLL instead. This is supported by both MS and GNU linkers.
    if (!e1.ExtName.empty() && e1.ExtName != e1.Name &&
        StringRef(e1.Name).contains('.')) {
      e2.name = saver().save(e1.ExtName);
      e2.forwardTo = saver().save(e1.Name);
      ctx.config.exports.push_back(e2);
      continue;
    }
    e2.name = saver().save(e1.Name);
    e2.extName = saver().save(e1.ExtName);
    e2.aliasTarget = saver().save(e1.AliasTarget);
    e2.ordinal = e1.Ordinal;
    e2.noname = e1.Noname;
    e2.data = e1.Data;
    e2.isPrivate = e1.Private;
    e2.constant = e1.Constant;
    e2.source = ExportSource::ModuleDefinition;
    ctx.config.exports.push_back(e2);
  }
}

// ===----------------------------------------------------------------------===
// Task queue
// ===----------------------------------------------------------------------===

void LinkerDriver::enqueueTask(std::function<void()> task) {
  taskQueue.push_back(std::move(task));
}

bool LinkerDriver::run() {
  llvm::TimeTraceScope timeScope("Read input files");
  ScopedTimer t(ctx.inputFileTimer);

  bool didWork = !taskQueue.empty();
  while (!taskQueue.empty()) {
    taskQueue.front()();
    taskQueue.pop_front();
  }
  return didWork;
}

// ===----------------------------------------------------------------------===
// Order file & call graph
// ===----------------------------------------------------------------------===

void LinkerDriver::parseOrderFile(StringRef arg) {
  // For some reason, the MSVC linker requires a filename to be
  // preceded by "@".
  if (!arg.starts_with("@")) {
    error("malformed /order option: '@' missing");
    return;
  }

  DenseSet<StringRef> set;
  for (Chunk *c : ctx.symtab.getChunks())
    if (auto *sec = dyn_cast<SectionChunk>(c))
      if (sec->sym)
        set.insert(sec->sym->getName());

  StringRef path = arg.substr(1);
  std::unique_ptr<MemoryBuffer> mb =
      CHECK(MemoryBuffer::getFile(path, /*IsText=*/false,
                                  /*RequiresNullTerminator=*/false,
                                  /*IsVolatile=*/true),
            "could not open " + path);

  // Symbols not present in the order file get priority 0 (placed last).
  for (StringRef arg : args::getLines(mb->getMemBufferRef())) {
    std::string s(arg);
    if (set.count(s) == 0) {
      if (ctx.config.warnMissingOrderSymbol)
        warn("--order:" + arg + ": missing symbol: " + s + " [LNK4037]");
    } else
      ctx.config.order[s] = INT_MIN + ctx.config.order.size();
  }

  ctx.driver.takeBuffer(std::move(mb));
}

void LinkerDriver::parseCallGraphFile(StringRef path) {
  std::unique_ptr<MemoryBuffer> mb =
      CHECK(MemoryBuffer::getFile(path, /*IsText=*/false,
                                  /*RequiresNullTerminator=*/false,
                                  /*IsVolatile=*/true),
            "could not open " + path);

  DenseMap<StringRef, Symbol *> map;
  for (ObjFile *file : ctx.objFileInstances)
    for (Symbol *sym : file->getSymbols())
      if (sym)
        map[sym->getName()] = sym;

  auto findSection = [&](StringRef name) -> SectionChunk * {
    Symbol *sym = map.lookup(name);
    if (!sym) {
      if (ctx.config.warnMissingOrderSymbol)
        warn(path + ": no such symbol: " + name);
      return nullptr;
    }

    if (DefinedCOFF *dr = dyn_cast_or_null<DefinedCOFF>(sym))
      return dyn_cast_or_null<SectionChunk>(dr->getChunk());
    return nullptr;
  };

  for (StringRef line : args::getLines(*mb)) {
    SmallVector<StringRef, 3> fields;
    line.split(fields, ' ');
    uint64_t count;

    if (fields.size() != 3 || !to_integer(fields[2], count)) {
      error(path + ": parse error");
      return;
    }

    if (SectionChunk *from = findSection(fields[0]))
      if (SectionChunk *to = findSection(fields[1]))
        ctx.config.callGraphProfile[{from, to}] += count;
  }

  ctx.driver.takeBuffer(std::move(mb));
}

namespace {
void readCallGraphsFromObjectFiles(COFFLinkerContext &ctx) {
  for (ObjFile *obj : ctx.objFileInstances) {
    if (obj->callgraphSec) {
      ArrayRef<uint8_t> contents;
      cantFail(
          obj->getCOFFObj()->getSectionContents(obj->callgraphSec, contents));
      BinaryStreamReader reader(contents, llvm::endianness::little);
      while (!reader.empty()) {
        uint32_t fromIndex, toIndex;
        uint64_t count;
        if (Error err = reader.readInteger(fromIndex))
          fatal(toString(obj) + ": Expected 32-bit integer");
        if (Error err = reader.readInteger(toIndex))
          fatal(toString(obj) + ": Expected 32-bit integer");
        if (Error err = reader.readInteger(count))
          fatal(toString(obj) + ": Expected 64-bit integer");
        auto *fromSym = dyn_cast_or_null<Defined>(obj->getSymbol(fromIndex));
        auto *toSym = dyn_cast_or_null<Defined>(obj->getSymbol(toIndex));
        if (!fromSym || !toSym)
          continue;
        auto *from = dyn_cast_or_null<SectionChunk>(fromSym->getChunk());
        auto *to = dyn_cast_or_null<SectionChunk>(toSym->getChunk());
        if (from && to)
          ctx.config.callGraphProfile[{from, to}] += count;
      }
    }
  }
}

void markAddrsig(Symbol *s) {
  if (auto *d = dyn_cast_or_null<Defined>(s))
    if (SectionChunk *c = dyn_cast_or_null<SectionChunk>(d->getChunk()))
      c->keepUnique = true;
}

void findKeepUniqueSections(COFFLinkerContext &ctx) {
  llvm::TimeTraceScope timeScope("Find keep unique sections");

  // Exported symbols could be address-significant in other executables or DSOs.
  for (Export &r : ctx.config.exports)
    markAddrsig(r.sym);

  for (ObjFile *obj : ctx.objFileInstances) {
    ArrayRef<Symbol *> syms = obj->getSymbols();
    if (obj->addrsigSec) {
      ArrayRef<uint8_t> contents;
      cantFail(
          obj->getCOFFObj()->getSectionContents(obj->addrsigSec, contents));
      const uint8_t *cur = contents.begin();
      while (cur != contents.end()) {
        unsigned size;
        const char *err = nullptr;
        uint64_t symIndex = decodeULEB128(cur, &size, contents.end(), &err);
        if (err)
          fatal(toString(obj) + ": could not decode addrsig section: " + err);
        if (symIndex >= syms.size())
          fatal(toString(obj) + ": invalid symbol index in addrsig section");
        markAddrsig(syms[symIndex]);
        cur += size;
      }
    } else {
      // If an object file does not have an address-significance table,
      // conservatively mark all of its symbols as address-significant.
      for (Symbol *s : syms)
        markAddrsig(s);
    }
  }
}
} // namespace

// ===----------------------------------------------------------------------===
// Resources & VFS
// ===----------------------------------------------------------------------===

void LinkerDriver::convertResources() {
  llvm::TimeTraceScope timeScope("Convert resources");
  std::vector<ObjFile *> resourceObjFiles;

  for (ObjFile *f : ctx.objFileInstances) {
    if (f->isResourceObjFile())
      resourceObjFiles.push_back(f);
  }

  if (resourceObjFiles.size() > 1 ||
      (resourceObjFiles.size() == 1 && !resources.empty())) {
    error((!resources.empty() ? "internal .obj file created from .res files"
                              : toString(resourceObjFiles[1])) +
          ": more than one resource obj file not allowed, already got " +
          toString(resourceObjFiles.front()));
    return;
  }

  if (resources.empty() && resourceObjFiles.size() <= 1) {
    // No resources to convert, and max one resource object file in
    // the input. Keep that preconverted resource section as is.
    for (ObjFile *f : resourceObjFiles)
      f->includeResourceChunks();
    return;
  }
  ObjFile *f =
      make<ObjFile>(ctx, convertResToCOFF(resources, resourceObjFiles));
  ctx.symtab.addFile(f);
  f->includeResourceChunks();
}

namespace {
std::unique_ptr<llvm::vfs::FileSystem> getVFS(const opt::InputArgList &args) {
  using namespace llvm::vfs;

  const opt::Arg *arg = args.getLastArg(OPT_vfsoverlay);
  if (!arg)
    return nullptr;

  auto bufOrErr = llvm::MemoryBuffer::getFile(arg->getValue());
  if (!bufOrErr) {
    checkError(errorCodeToError(bufOrErr.getError()));
    return nullptr;
  }

  if (auto ret = vfs::getVFSFromYAML(std::move(*bufOrErr),
                                     /*DiagHandler*/ nullptr, arg->getValue()))
    return ret;

  error("Invalid vfs overlay");
  return nullptr;
}
} // namespace

// ===----------------------------------------------------------------------===
// Main driver
// ===----------------------------------------------------------------------===

void LinkerDriver::run(ArrayRef<const char *> argsArr,
                       const LinkerDriverConfig &driverCfg) {
  ScopedTimer rootTimer(ctx.rootTimer);
  Configuration *config = &ctx.config;

  // --- Initialization ---

  InitializeAllTargetInfos();
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  errorHandler().fatalWarnings = driverCfg.fatalWarnings;
  errorHandler().suppressWarnings = driverCfg.suppressWarnings;

  ArgParser parser(ctx);
  opt::InputArgList args = parser.parse(argsArr);

  bool hasPrintArgs = false;
  for (auto arg : args) {
    if (StringRef(arg->getAsString(args)).compare("-fprint-arguments") == 0) {
      hasPrintArgs = true;
      break;
    }
  }

  if (hasPrintArgs) {
    llvm::outs() << "neverc linker arguments: \n";
    for (auto arg : args) {
      llvm::outs() << "\"" << arg->getAsString(args) << "\",\n";
    }
  }

  config->driverCfg = &driverCfg;

  if (driverCfg.timeTraceEnabled)
    timeTraceProfilerInitialize(driverCfg.timeTraceGranularity, argsArr[0]);

  llvm::TimeTraceScope timeScope("COFF link");

  parseMllvmOptions(driverCfg);

  config->vfs = getVFS(args);

  if (driverCfg.threadCount) {
    parallel::strategy = hardware_concurrency(driverCfg.threadCount);
  }

  config->showSummary = args.hasArg(OPT_summary);

  if (!args.hasArg(OPT_INPUT, OPT_wholearchive_file)) {
    if (args.hasArg(OPT_deffile))
      config->noEntry = true;
    else
      fatal("no input files");
  }

  {
    llvm::TimeTraceScope timeScope2("Search paths");
    searchPaths.emplace_back("");
    for (auto *arg : args.filtered(OPT_libpath))
      searchPaths.push_back(arg->getValue());
  }

  // --- Diagnostics & force ---

  for (auto *arg : args.filtered(OPT_ignore)) {
    SmallVector<StringRef, 8> vec;
    StringRef(arg->getValue()).split(vec, ',');
    for (StringRef s : vec) {
      if (s == "4037")
        config->warnMissingOrderSymbol = false;
      else if (s == "4217")
        config->warnLocallyDefinedImported = false;
      else if (s == "longsections")
        config->warnLongSectionNames = false;
      // Other warning numbers are ignored.
    }
  }

  if (!driverCfg.outputFile.empty())
    config->outputFile = driverCfg.outputFile;

  config->verbose = driverCfg.verbose;
  errorHandler().verbose = config->verbose;

  if (args.hasArg(OPT_force, OPT_force_unresolved))
    config->forceUnresolved = true;
  if (args.hasArg(OPT_force, OPT_force_multiple))
    config->forceMultiple = true;
  if (args.hasArg(OPT_force, OPT_force_multipleres))
    config->forceMultipleRes = true;
  for (auto *arg : args.filtered(OPT_override))
    ctx.overrideSymbols.try_emplace(arg->getValue(), nullptr);

  // --- Debug (DWARF) ---

  bool doGC = true;
  if (driverCfg.debugInfo) {
    config->debug = true;
    config->incremental = true;
    config->includeDwarfChunks = true;
    config->writeSymtab = true;
    config->warnLongSectionNames = false;
    doGC = false;
  }
  for (auto *arg : args.filtered(OPT_debug_opt)) {
    std::string str = std::string(StringRef(arg->getValue()).lower().str());
    SmallVector<StringRef, 1> vec;
    StringRef(str).split(vec, ',');
    for (StringRef s : vec) {
      if (s == "none") {
        config->debug = false;
        config->incremental = false;
        config->includeDwarfChunks = false;
        config->writeSymtab = false;
        doGC = true;
      } else if (s == "dwarf") {
        config->debug = true;
        config->incremental = true;
        config->includeDwarfChunks = true;
        config->writeSymtab = true;
        config->warnLongSectionNames = false;
        doGC = false;
      } else if (s == "nodwarf") {
        config->includeDwarfChunks = false;
      } else if (s == "symtab") {
        config->writeSymtab = true;
        doGC = false;
      } else if (s == "nosymtab") {
        config->writeSymtab = false;
      } else if (s == "full" || s == "ghash" || s == "noghash" ||
                 s == "fastlink") {
        error("NeverC no longer generates PDB; use /debug:dwarf instead of "
              "--debug=" +
              s);
      } else {
        error("--debug: unknown option: " + s);
      }
    }
  }

  config->demangle = driverCfg.demangle;

  config->driverUponly = args.hasArg(OPT_driver_uponly) ||
                         args.hasArg(OPT_driver_uponly_wdm) ||
                         args.hasArg(OPT_driver_wdm_uponly);
  config->driverWdm = args.hasArg(OPT_driver_wdm) ||
                      args.hasArg(OPT_driver_uponly_wdm) ||
                      args.hasArg(OPT_driver_wdm_uponly);
  config->driver |=
      config->driverUponly || config->driverWdm || args.hasArg(OPT_driver);

  // --- Image format & entry ---

  if (args.hasArg(OPT_noentry)) {
    if (driverCfg.shared)
      config->noEntry = true;
    else
      error("--noentry must be specified with --dll");
  }

  if (driverCfg.shared) {
    config->dll = true;
    config->manifestID = 2;
  }

  // Can't use hasFlag for /dynamicbase — need to check if it or its inverse
  // was explicitly present in order to handle /fixed correctly.
  auto *dynamicBaseArg = args.getLastArg(OPT_dynamicbase, OPT_dynamicbase_no);
  if (dynamicBaseArg &&
      dynamicBaseArg->getOption().getID() == OPT_dynamicbase_no)
    config->dynamicBase = false;

  // MSDN claims "/FIXED:NO is the default setting for a DLL, and /FIXED is the
  // default setting for any other project type.", but link.exe defaults to
  // /FIXED:NO for exe outputs as well. Match behavior, not docs.
  bool fixed = args.hasFlag(OPT_fixed, OPT_fixed_no, false);
  if (fixed) {
    if (dynamicBaseArg &&
        dynamicBaseArg->getOption().getID() == OPT_dynamicbase) {
      error("--fixed must not be specified with --dynamicbase");
    } else {
      config->relocatable = false;
      config->dynamicBase = false;
    }
  }

  config->appContainer =
      args.hasFlag(OPT_appcontainer, OPT_appcontainer_no, false);

  {
    llvm::TimeTraceScope timeScope2("Machine arg");
    if (auto *arg = args.getLastArg(OPT_machine)) {
      config->machine = getMachineType(arg->getValue());
      if (config->machine == IMAGE_FILE_MACHINE_UNKNOWN)
        fatal(Twine("unknown /machine argument: ") + arg->getValue());
    }
  }

  {
    llvm::TimeTraceScope timeScope2("Nodefaultlib");
    for (auto *arg : args.filtered(OPT_nodefaultlib))
      config->noDefaultLibs.insert(
          std::string(findLib(arg->getValue()).lower().str()));
  }
  if (args.hasArg(OPT_nodefaultlib_all))
    config->noDefaultLibAll = true;

  // --- Memory layout & alignment ---

  if (auto *arg = args.getLastArg(OPT_base))
    parseNumbers(arg->getValue(), &config->imageBase);

  if (auto *arg = args.getLastArg(OPT_filealign)) {
    parseNumbers(arg->getValue(), &config->fileAlign);
    if (!isPowerOf2_64(config->fileAlign))
      error("--filealign: not a power of two: " + Twine(config->fileAlign));
  }

  if (auto *arg = args.getLastArg(OPT_stack))
    parseNumbers(arg->getValue(), &config->stackReserve, &config->stackCommit);

  // /guard — driverCfg conveys the default; -Wl, overrides still accepted.
  if (!driverCfg.guardSpec.empty())
    parseGuard(driverCfg.guardSpec);
  if (auto *arg = args.getLastArg(OPT_guard))
    parseGuard(arg->getValue());

  if (auto *arg = args.getLastArg(OPT_heap))
    parseNumbers(arg->getValue(), &config->heapReserve, &config->heapCommit);

  // --- Version & subsystem ---

  if (auto *arg = args.getLastArg(OPT_version))
    parseVersion(arg->getValue(), &config->majorImageVersion,
                 &config->minorImageVersion);

  if (auto *arg = args.getLastArg(OPT_subsystem))
    parseSubsystem(arg->getValue(), &config->subsystem,
                   &config->majorSubsystemVersion,
                   &config->minorSubsystemVersion);

  if (auto *arg = args.getLastArg(OPT_osversion)) {
    parseVersion(arg->getValue(), &config->majorOSVersion,
                 &config->minorOSVersion);
  } else {
    config->majorOSVersion = config->majorSubsystemVersion;
    config->minorOSVersion = config->minorSubsystemVersion;
  }

  // /Brepro now conveyed via driverCfg.repro.
  if (driverCfg.repro) {
    config->timestamp = 0;
    config->repro = true;
  } else if (auto *arg = args.getLastArg(OPT_timestamp)) {
    config->repro = false;
    StringRef value(arg->getValue());
    if (value.getAsInteger(0, config->timestamp))
      fatal(Twine("invalid timestamp: ") + value +
            ".  Expected 32-bit integer");
  } else {
    config->repro = false;
    config->timestamp = time(nullptr);
  }

  // --- Symbol & linking control ---

  for (auto *arg : args.filtered(OPT_alternatename))
    parseAlternateName(arg->getValue());

  for (auto *arg : args.filtered(OPT_incl))
    addUndefined(arg->getValue());

  if (auto *arg = args.getLastArg(OPT_implib))
    config->implib = arg->getValue();
  config->noimplib = args.hasArg(OPT_noimplib);

  if (args.hasArg(OPT_profile))
    doGC = true;
  std::optional<ICFLevel> icfLevel;
  if (args.hasArg(OPT_profile))
    icfLevel = ICFLevel::None;
  unsigned tailMerge = 1;
  for (auto *arg : args.filtered(OPT_opt)) {
    auto str = StringRef(arg->getValue()).lower();
    SmallVector<StringRef, 1> vec;
    StringRef(str).split(vec, ',');
    for (StringRef s : vec) {
      if (s == "ref") {
        doGC = true;
      } else if (s == "noref") {
        doGC = false;
      } else if (s == "icf" || s.starts_with("icf=")) {
        icfLevel = ICFLevel::All;
      } else if (s == "safeicf") {
        icfLevel = ICFLevel::Safe;
      } else if (s == "noicf") {
        icfLevel = ICFLevel::None;
      } else if (s == "neverctailmerge") {
        tailMerge = 2;
      } else if (s == "noneverctailmerge") {
        tailMerge = 0;
      } else if (s == "ltodebugpassmanager" || s == "noltodebugpassmanager") {
        // Controlled by compiler -O flag via driverCfg; ignored here.
      } else if (s.consume_front("neverclto=") ||
                 s.consume_front("nevercltocgo=") ||
                 s.consume_front("nevercltopartitions=")) {
        // Ignored: LTO levels are controlled by compiler -O flag via driverCfg.
      } else if (s != "lbr" && s != "nolbr")
        error("--opt: unknown option: " + s);
    }
  }

  if (!icfLevel) {
    if (driverCfg.icfLevel >= 2)
      icfLevel = ICFLevel::All;
    else if (driverCfg.icfLevel == 1)
      icfLevel = ICFLevel::Safe;
    else
      icfLevel = doGC ? ICFLevel::All : ICFLevel::None;
  }
  config->doGC = doGC;
  config->doICF = *icfLevel;
  config->tailMerge =
      (tailMerge == 1 && config->doICF != ICFLevel::None) || tailMerge == 2;

  if (args.hasArg(OPT_kill_at))
    config->killAt = true;

  // --- Section merging & layout ---

  for (auto *arg : args.filtered(OPT_failifmismatch))
    checkFailIfMismatch(arg->getValue(), nullptr);

  for (auto *arg : args.filtered(OPT_merge))
    parseMerge(arg->getValue());

  if (args.hasArg(OPT_dont_merge_sections)) {
    config->dontMergeSections = true;
    warn("Each data directory will have its own section!");
  }

  if (!config->dontMergeSections) {
    // Add default section merging rules after user rules. User rules take
    // precedence, but we will emit a warning if there is a conflict.
    parseMerge(".idata=.rdata");
    parseMerge(".didat=.rdata");
    if (!config->driver)
      parseMerge(".edata=.rdata");
    parseMerge(".xdata=.rdata");
    parseMerge(".00cfg=.rdata");

    parseMerge(".voltbl=.rdata");

    if (config->driver)
      parseMerge("INIT2=INIT");
  }

  for (auto *arg : args.filtered(OPT_section))
    parseSection(arg->getValue());

  if (auto *arg = args.getLastArg(OPT_align)) {
    parseNumbers(arg->getValue(), &config->align);
    if (!isPowerOf2_64(config->align))
      error("--align: not a power of two: " + StringRef(arg->getValue()));
    if (!args.hasArg(OPT_driver))
      warn("--align specified without --driver; image may not run");
  }

  for (auto *arg : args.filtered(OPT_aligncomm))
    parseAligncomm(arg->getValue());

  // --- Manifest ---

  for (auto *arg : args.filtered(OPT_manifestdependency))
    config->manifestDependencies.insert(arg->getValue());

  if (auto *arg = args.getLastArg(OPT_manifest, OPT_manifest_colon)) {
    if (arg->getOption().getID() == OPT_manifest)
      config->manifest = Configuration::SideBySide;
    else
      parseManifest(arg->getValue());
  }

  if (auto *arg = args.getLastArg(OPT_manifestuac))
    parseManifestUAC(arg->getValue());

  if (auto *arg = args.getLastArg(OPT_manifestfile))
    config->manifestFile = arg->getValue();

  for (auto *arg : args.filtered(OPT_manifestinput))
    config->manifestInput.push_back(arg->getValue());

  if (!config->manifestInput.empty() &&
      config->manifest != Configuration::Embed) {
    fatal("--manifestinput: requires --manifest=embed");
  }

  // --- LTO ---

  config->allowBind = args.hasFlag(OPT_allowbind, OPT_allowbind_no, true);
  config->allowIsolation =
      args.hasFlag(OPT_allowisolation, OPT_allowisolation_no, true);
  config->incremental =
      args.hasFlag(OPT_incremental, OPT_incremental_no,
                   !config->doGC && config->doICF == ICFLevel::None &&
                       !args.hasArg(OPT_order) && !args.hasArg(OPT_profile));
  config->integrityCheck =
      args.hasFlag(OPT_integritycheck, OPT_integritycheck_no, false);
  config->cetCompat = args.hasFlag(OPT_cetcompat, OPT_cetcompat_no, false);
  config->nxCompat = args.hasFlag(OPT_nxcompat, OPT_nxcompat_no, true);
  for (auto *arg : args.filtered(OPT_swaprun))
    parseSwaprun(arg->getValue());
  config->terminalServerAware =
      !config->dll && args.hasFlag(OPT_tsaware, OPT_tsaware_no, true);
  config->callGraphProfileSort = (driverCfg.callGraphProfileSort != "none");

  if (args.hasFlag(OPT_inferasanlibs, OPT_inferasanlibs_no, false))
    warn("ignoring '--inferasanlibs', this flag is not supported");

  if (config->incremental && args.hasArg(OPT_profile)) {
    warn("ignoring '--incremental' due to '--profile' specification");
    config->incremental = false;
  }

  if (config->incremental && args.hasArg(OPT_order)) {
    warn("ignoring '--incremental' due to '--order' specification");
    config->incremental = false;
  }

  if (config->incremental && config->doGC) {
    warn(
        "ignoring '--incremental' because REF is enabled; use '--opt=noref' to "
        "disable");
    config->incremental = false;
  }

  if (config->incremental && config->doICF != ICFLevel::None) {
    warn(
        "ignoring '--incremental' because ICF is enabled; use '--opt=noicf' to "
        "disable");
    config->incremental = false;
  }

  if (errorCount())
    return;

  std::set<sys::fs::UniqueID> wholeArchives;
  for (auto *arg : args.filtered(OPT_wholearchive_file))
    if (std::optional<StringRef> path = findFile(arg->getValue()))
      if (std::optional<sys::fs::UniqueID> id = getUniqueID(*path))
        wholeArchives.insert(*id);

  auto isWholeArchive = [&](StringRef path) -> bool {
    if (args.hasArg(OPT_wholearchive_flag))
      return true;
    if (std::optional<sys::fs::UniqueID> id = getUniqueID(path))
      return wholeArchives.count(*id);
    return false;
  };

  {
    llvm::TimeTraceScope timeScope2("Parse & queue inputs");
    bool inLib = false;
    for (auto *arg : args) {
      switch (arg->getOption().getID()) {
      case OPT_end_lib:
        if (!inLib)
          error("stray " + arg->getSpelling());
        inLib = false;
        break;
      case OPT_start_lib:
        if (inLib)
          error("nested " + arg->getSpelling());
        inLib = true;
        break;
      case OPT_wholearchive_file:
        if (std::optional<StringRef> path = findFileIfNew(arg->getValue()))
          enqueuePath(*path, true, inLib);
        break;
      case OPT_INPUT:
        if (std::optional<StringRef> path = findFileIfNew(arg->getValue()))
          enqueuePath(*path, isWholeArchive(*path), inLib);
        break;
      default:
        // Ignore other options.
        break;
      }
    }
  }

  run();
  if (errorCount())
    return;

  if (config->machine == IMAGE_FILE_MACHINE_UNKNOWN) {
    warn("--machine is not specified. x64 is assumed");
    config->machine = AMD64;
  }
  if (config->verbose) {
    SmallString<256> buffer;
    raw_svector_ostream stream(buffer);
    stream << "Library search paths:\n";

    for (StringRef path : searchPaths) {
      if (path == "")
        path = "(cwd)";
      stream << "  " << path << "\n";
    }

    message(buffer);
  }

  for (auto *arg : args.filtered(OPT_defaultlib))
    if (std::optional<StringRef> path = findLibIfNew(arg->getValue()))
      enqueuePath(*path, false, false);

  if (config->driver) {
  } else {
    // Add --defaultlib= "Psapi.lib"
    if (std::optional<StringRef> path = findLibIfNew("Psapi.lib"))
      enqueuePath(*path, false, false);
  }

  if (args.hasArg(OPT_release))
    config->writeCheckSum = true;

  run();
  if (errorCount())
    return;

  // --- Post-input adjustments ---

  // /functionpadmin default via driverCfg; /functionpadmin:N via -Wl,.
  if (driverCfg.functionPadMin && config->functionPadMin == 0) {
    if (config->machine == AMD64)
      config->functionPadMin = 6;
  }
  for (auto *arg : args.filtered(OPT_functionpadmin_opt))
    parseFunctionPadMin(arg);

  for (auto *arg :
       args.filtered(OPT_dependentloadflag, OPT_dependentloadflag_opt))
    parseDependentLoadFlags(arg);

  config->largeAddressAware =
      args.hasFlag(OPT_largeaddressaware, OPT_largeaddressaware_no, true);
  config->highEntropyVA =
      args.hasFlag(OPT_highentropyva, OPT_highentropyva_no, true);

  if (!config->dynamicBase && config->machine == ARM64)
    config->dynamicBase = true;

  // --- Exports & module definitions ---

  {
    llvm::TimeTraceScope timeScope("Parse --export");
    for (auto *arg : args.filtered(OPT_export)) {
      Export e = parseExport(arg->getValue());
      config->exports.push_back(e);
    }
  }

  if (auto *arg = args.getLastArg(OPT_deffile))
    parseModuleDefs(arg->getValue());

  // Def-only invocation: write import lib and exit early.
  if (!args.hasArg(OPT_INPUT, OPT_wholearchive_file)) {
    fixupExports();
    if (!config->noimplib)
      createImportLibrary(/*asLib=*/true);
    return;
  }

  // --- Entry point & subsystem resolution ---

  // Must happen before /entry and after the def-only early return.
  if (config->subsystem == IMAGE_SUBSYSTEM_UNKNOWN) {
    llvm::TimeTraceScope timeScope("Infer subsystem");
    config->subsystem = inferSubsystem();
    if (config->subsystem == IMAGE_SUBSYSTEM_UNKNOWN)
      fatal("subsystem must be defined");
  }

  {
    llvm::TimeTraceScope timeScope("Entry point");
    if (auto *arg = args.getLastArg(OPT_entry)) {
      config->entry = addUndefined(mangle(arg->getValue()));
    } else if (!config->entry && !config->noEntry) {
      if (config->dll) {
        config->entry = addUndefined("_DllMainCRTStartup");
      } else if (config->driverWdm) {
        // --driver=wdm implies --entry=_NtProcessStartup
        config->entry = addUndefined(mangle("_NtProcessStartup"));
      } else {
        StringRef s = findDefaultEntry();
        if (s.empty())
          fatal("entry point must be defined");
        config->entry = addUndefined(s);
        log("Entry name inferred: " + s);
      }
    }
  }

  {
    llvm::TimeTraceScope timeScope("Delay load");
    for (auto *arg : args.filtered(OPT_delayload)) {
      config->delayLoads.insert(
          std::string(StringRef(arg->getValue()).lower().str()));
      config->delayLoadHelper = addUndefined("__delayLoadHelper2");
    }
  }

  // --- Output configuration ---

  if (config->outputFile.empty()) {
    config->outputFile = getOutputPath(
        (*args.filtered(OPT_INPUT, OPT_wholearchive_file).begin())->getValue(),
        config->dll, config->driver);
  }

  if (auto e = tryCreateFile(config->outputFile)) {
    error("cannot open output file " + config->outputFile + ": " + e.message());
    return;
  }

  config->mapFile = driverCfg.mapFile;

  if (!config->mapFile.empty() && args.hasArg(OPT_map_info)) {
    for (auto *arg : args.filtered(OPT_map_info)) {
      auto s = StringRef(arg->getValue()).lower();
      if (s == "exports")
        config->mapInfo = true;
      else
        error("unknown option: /mapinfo:" + s);
    }
  }

  if (!driverCfg.buildId.empty())
    config->buildIDHash = FormIDHash::Binary;

  if (config->imageBase == uint64_t(-1))
    config->imageBase = getDefaultImageBase();

  // --- Symbol resolution ---

  ctx.symtab.addSynthetic(mangle("__ImageBase"), nullptr);

  ctx.symtab.addAbsolute(mangle("__guard_fids_count"), 0);
  ctx.symtab.addAbsolute(mangle("__guard_fids_table"), 0);
  ctx.symtab.addAbsolute(mangle("__guard_flags"), 0);
  ctx.symtab.addAbsolute(mangle("__guard_iat_count"), 0);
  ctx.symtab.addAbsolute(mangle("__guard_iat_table"), 0);
  ctx.symtab.addAbsolute(mangle("__guard_longjmp_count"), 0);
  ctx.symtab.addAbsolute(mangle("__guard_longjmp_table"), 0);
  // Needed for MSVC 2017 15.5 CRT.
  ctx.symtab.addAbsolute(mangle("__enclave_config"), 0);
  // Needed for MSVC 2019 16.8 CRT.
  ctx.symtab.addAbsolute(mangle("__guard_eh_cont_count"), 0);
  ctx.symtab.addAbsolute(mangle("__guard_eh_cont_table"), 0);

  if (config->debug || config->buildIDHash != FormIDHash::None)
    if (ctx.symtab.findUnderscore("__buildid"))
      ctx.symtab.addUndefined(mangle("__buildid"));

  // Keep running until no new symbols are discovered.
  {
    llvm::TimeTraceScope timeScope("Add unresolved symbols");
    do {
      if (config->entry)
        mangleMaybe(config->entry);

      for (Export &e : config->exports) {
        if (!e.forwardTo.empty())
          continue;
        e.sym = addUndefined(e.name);
        if (e.source != ExportSource::Directives)
          e.symbolName = mangleMaybe(e.sym);
      }

      // Weak aliases give remaining undefined symbols a final resolution
      // chance.
      for (auto &pair : config->alternateNames) {
        StringRef from = pair.first;
        StringRef to = pair.second;
        Symbol *sym = ctx.symtab.find(from);
        if (!sym)
          continue;
        if (auto *u = dyn_cast<Undefined>(sym))
          if (!u->weakAlias)
            u->weakAlias = ctx.symtab.addUndefined(to);
      }

      // LTO may reference runtime libcalls not in the bitcode symbol table.
      if (!ctx.bitcodeFileInstances.empty())
        for (auto *s : lto::LTO::getRuntimeLibcallSymbols())
          ctx.symtab.addLibcall(s);

      if (ctx.symtab.findUnderscore("_load_config_used"))
        addUndefined(mangle("_load_config_used"));

      if (args.hasArg(OPT_include_optional)) {
        for (auto *arg : args.filtered(OPT_include_optional))
          if (isa_and_nonnull<LazyArchive>(ctx.symtab.find(arg->getValue())))
            addUndefined(arg->getValue());
      }
    } while (run());
  }

  // Check for unresolvable symbols before LTO to avoid wasting codegen time.
  if (!ctx.bitcodeFileInstances.empty() && !config->forceUnresolved)
    ctx.symtab.reportUnresolvable();
  if (errorCount())
    return;

  config->hadExplicitExports = !config->exports.empty();

  ctx.symtab.compileBitcodeFiles();

  if (Defined *d =
          dyn_cast_or_null<Defined>(ctx.symtab.findUnderscore("_tls_used")))
    config->gcroot.push_back(d);

  run();
  ctx.symtab.resolveRemainingUndefines();
  if (errorCount())
    return;

  // --- Finalization ---

  if (!config->exports.empty() || config->dll) {
    llvm::TimeTraceScope timeScope("Create .lib exports");
    fixupExports();
    if (!config->noimplib)
      createImportLibrary(/*asLib=*/false);
    assignExportOrdinals();
  }

  for (auto &pair : config->alignComm) {
    StringRef name = pair.first;
    uint32_t alignment = pair.second;

    Symbol *sym = ctx.symtab.find(name);
    if (!sym) {
      warn("--aligncomm symbol " + name + " not found");
      continue;
    }

    auto *dc = dyn_cast<DefinedCommon>(sym);
    if (!dc)
      continue;

    CommonChunk *c = dc->getChunk();
    c->setAlignment(std::max(c->getAlignment(), alignment));
  }

  if (config->manifest == Configuration::Embed)
    addBuffer(createManifestRes(), false, false);
  else if (config->manifest == Configuration::SideBySide ||
           (config->manifest == Configuration::Default &&
            !config->manifestDependencies.empty()))
    createSideBySideManifest();

  if (auto *arg = args.getLastArg(OPT_order)) {
    if (!driverCfg.callGraphOrderingFile.empty())
      error("--order and --call-graph-ordering-file may not be used together");
    parseOrderFile(arg->getValue());
    config->callGraphProfileSort = false;
  }

  if (config->callGraphProfileSort) {
    llvm::TimeTraceScope timeScope("Call graph");
    if (!driverCfg.callGraphOrderingFile.empty())
      parseCallGraphFile(driverCfg.callGraphOrderingFile);
    readCallGraphsFromObjectFiles(ctx);
  }

  if (!driverCfg.printSymbolOrder.empty())
    config->printSymbolOrder = saver().save(driverCfg.printSymbolOrder);

  if (config->doGC)
    markLive(ctx);

  convertResources();

  if (config->doICF != ICFLevel::None) {
    findKeepUniqueSections(ctx);
    doICF(ctx);
  }

  writeOutput(ctx);

  rootTimer.stop();
  if (config->driverCfg->timeTraceEnabled) {
    ctx.rootTimer.print();
    timeTraceProfilerEnd();

    checkError(timeTraceProfilerWrite(std::string(), config->outputFile));
    timeTraceProfilerCleanup();
  }
}

} // namespace linker::coff
