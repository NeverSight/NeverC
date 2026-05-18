#ifndef LINKER_COFF_DRIVER_H
#define LINKER_COFF_DRIVER_H

#include "Linker/COFF/Config.h"
#include "Linker/COFF/SymbolTable.h"
#include "Linker/Core/Driver/Dispatcher.h"
#include "Linker/Core/Support/LlvmAliases.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace linker::coff {

using llvm::COFF::MachineTypes;
using llvm::COFF::WindowsSubsystem;
using std::optional;

class COFFOptTable : public llvm::opt::GenericOptTable {
public:
  COFFOptTable();
};

// The result of parsing the .drective section. --export= and --include= are
// handled separately because they reference symbols, and the number of symbols
// can be quite large. The LLVM Option library will perform at least one memory
// allocation per argument, and that is prohibitively slow for parsing
// directives.
struct ParsedDirectives {
  std::vector<StringRef> exports;
  std::vector<StringRef> includes;
  llvm::opt::InputArgList args;
};

class ArgParser {
public:
  ArgParser(COFFLinkerContext &ctx);

  // Parses command line options.
  llvm::opt::InputArgList parse(llvm::ArrayRef<const char *> args);

  // Tokenizes a given string and then parses as command line options in
  // .drectve section. /EXPORT options are returned in second element
  // to be processed in fastpath.
  ParsedDirectives parseDirectives(StringRef s);

private:
  COFFLinkerContext &ctx;
};

class LinkerDriver {
public:
  LinkerDriver(COFFLinkerContext &ctx) : ctx(ctx) {}

  void run(llvm::ArrayRef<const char *> args,
           const LinkerDriverConfig &driverCfg);

  // Used by the resolver to parse .drectve section contents.
  void parseDirectives(InputFile *file);

  // Used by ArchiveFile to enqueue members.
  void enqueueArchiveMember(const Archive::Child &c, const Archive::Symbol &sym,
                            StringRef parentName);

  MemoryBufferRef takeBuffer(std::unique_ptr<MemoryBuffer> mb);

  void enqueuePath(StringRef path, bool wholeArchive, bool lazy);
  void enqueuePathInternal(StringRef path, bool wholeArchive, bool lazy);

public:
  // Searches a file from search paths.
  std::optional<StringRef> findFileIfNew(StringRef filename);
  std::optional<StringRef> findLibIfNew(StringRef filename);
  StringRef findFile(StringRef filename);
  StringRef findLib(StringRef filename);

  bool findUnderscoreMangle(StringRef sym);

  // Symbol names are mangled by prepending "_" on x86.
  StringRef mangle(StringRef sym);

  llvm::Triple::ArchType getArch();

  uint64_t getDefaultImageBase();

  bool isDecorated(StringRef sym);

  std::string getImplibPath();

  // The import name is calculated as follows:
  //
  //        | LIBRARY w/ ext |   LIBRARY w/o ext   | no LIBRARY
  //   -----+----------------+---------------------+------------------
  //   LINK | {value}        | {value}.{.dll/.exe} | {output name}
  //    LIB | {value}        | {value}.dll         | {output name}.dll
  //
  std::string getImportName(bool asLib);

  void createImportLibrary(bool asLib);

  void parseModuleDefs(StringRef path);

  // Parse an /order file. If an option is given, the linker places COMDAT
  // sections int he same order as their names appear in the given file.
  void parseOrderFile(StringRef arg);

  void parseCallGraphFile(StringRef path);

  // Library search path. The first element is always "" (current directory).
  std::vector<StringRef> searchPaths;

  // Convert resource files and potentially merge input resource object
  // trees into one resource tree.
  void convertResources();

  // We don't want to add the same file more than once.
  // Files are uniquified by their filesystem and file number.
  std::set<llvm::sys::fs::UniqueID> visitedFiles;

  std::set<std::string> visitedLibs;

  Symbol *addUndefined(StringRef sym);

  StringRef mangleMaybe(Symbol *s);

  // Windows specific -- "main" is not the only main function in Windows.
  // You can choose one from these four -- {w,}{WinMain,main}.
  // There are four different entry point functions for them,
  // {w,}{WinMain,main}CRTStartup, respectively. The linker needs to
  // choose the right one depending on which "main" function is defined.
  // This function looks up the symbol table and resolve corresponding
  // entry point name.
  StringRef findDefaultEntry();
  WindowsSubsystem inferSubsystem();

  void addBuffer(std::unique_ptr<MemoryBuffer> mb, bool wholeArchive,
                 bool lazy);
  void addArchiveBuffer(MemoryBufferRef mbref, StringRef symName,
                        StringRef parentName, uint64_t offsetInArchive);

  void enqueueTask(std::function<void()> task);
  bool run();

  std::list<std::function<void()>> taskQueue;
  std::vector<StringRef> filePaths;
  std::vector<MemoryBufferRef> resources;

  llvm::DenseSet<StringRef> directivesExports;

  COFFLinkerContext &ctx;

  // Functions below this line are defined in CoffCommandLine.cpp.

  // Parses a string in the form of "<integer>[,<integer>]".
  void parseNumbers(StringRef arg, uint64_t *addr, uint64_t *size = nullptr);

  void parseGuard(StringRef arg);

  // Parses a string in the form of "<integer>[.<integer>]".
  // Minor's default value is 0.
  void parseVersion(StringRef arg, uint32_t *major, uint32_t *minor);

  // Parses a string in the form of "<subsystem>[,<integer>[.<integer>]]".
  void parseSubsystem(StringRef arg, WindowsSubsystem *sys, uint32_t *major,
                      uint32_t *minor, bool *gotVersion = nullptr);

  void parseAlternateName(StringRef);
  void parseMerge(StringRef);
  void parseSection(StringRef);
  void parseAligncomm(StringRef);

  // Parses a string in the form of "[:<integer>]"
  void parseFunctionPadMin(llvm::opt::Arg *a);

  // Parses a string in the form of "[:<integer>]"
  void parseDependentLoadFlags(llvm::opt::Arg *a);

  // Parses a string in the form of "EMBED[,=<integer>]|NO".
  void parseManifest(StringRef arg);

  // Parses a string in the form of "level=<string>|uiAccess=<string>"
  void parseManifestUAC(StringRef arg);

  // Parses a string in the form of "cd|net[,(cd|net)]*"
  void parseSwaprun(StringRef arg);

  // Create a resource file containing a manifest XML.
  std::unique_ptr<MemoryBuffer> createManifestRes();
  void createSideBySideManifest();
  std::string createDefaultXml();
  std::string createManifestXmlWithExternalMt(StringRef defaultXml);
  std::string createManifestXml();

  std::unique_ptr<llvm::WritableMemoryBuffer>
  createMemoryBufferForManifestRes(size_t manifestRes);

  // Used for dllexported symbols.
  Export parseExport(StringRef arg);
  void fixupExports();
  void assignExportOrdinals();

  // Parses a string in the form of "key=value" and check
  // if value matches previous values for the key.
  // This feature used in the directive section to reject
  // incompatible objects.
  void checkFailIfMismatch(StringRef arg, InputFile *source);

  // Convert Windows resource files (.res files) to a .obj file.
  MemoryBufferRef convertResToCOFF(ArrayRef<MemoryBufferRef> mbs,
                                   ArrayRef<ObjFile *> objs);
};

// Create enum with OPT_xxx values for each option in Options.td
enum {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Options.td.h"
#undef OPTION
};

} // namespace linker::coff

#endif
