#ifndef NEVERC_FRONTEND_FRONTENDOPTIONS_H
#define NEVERC_FRONTEND_FRONTENDOPTIONS_H

#include "neverc/Foundation/LangOpts/LangStandard.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cassert>
#include <optional>
#include <string>
#include <vector>

namespace llvm {

class MemoryBuffer;

} // namespace llvm

namespace neverc {

namespace frontend {

enum ActionKind {
  GenAssembly,

  GenBC,

  GenLLVM,

  GenObj,

  ParseSyntaxOnly,

  PrintPreprocessedInput,

  RunPreprocessorOnly
};

} // namespace frontend

class InputKind {
public:
  enum Format { Source };

private:
  Language Lang;
  LLVM_PREFERRED_TYPE(Format)
  unsigned Fmt : 2;
  LLVM_PREFERRED_TYPE(bool)
  unsigned Preprocessed : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsHeader : 1;

public:
  constexpr InputKind(Language L = Language::Unknown, Format F = Source,
                      bool PP = false, bool HD = false)
      : Lang(L), Fmt(F), Preprocessed(PP), IsHeader(HD) {}

  Language getLanguage() const { return static_cast<Language>(Lang); }
  Format getFormat() const { return static_cast<Format>(Fmt); }
  bool isPreprocessed() const { return Preprocessed; }
  bool isHeader() const { return IsHeader; }

  bool isUnknown() const { return Lang == Language::Unknown && Fmt == Source; }

  InputKind getPreprocessed() const {
    return InputKind(getLanguage(), getFormat(), true, isHeader());
  }

  InputKind getHeader() const {
    return InputKind(getLanguage(), getFormat(), isPreprocessed(), true);
  }

  InputKind withFormat(Format F) const {
    return InputKind(getLanguage(), F, isPreprocessed(), isHeader());
  }
};

class FrontendInputFile {
  std::string File;

  std::optional<llvm::MemoryBufferRef> Buffer;

  InputKind Kind;

  bool IsSystem = false;

public:
  FrontendInputFile() = default;
  FrontendInputFile(llvm::StringRef File, InputKind Kind, bool IsSystem = false)
      : File(File.str()), Kind(Kind), IsSystem(IsSystem) {}
  FrontendInputFile(llvm::MemoryBufferRef Buffer, InputKind Kind,
                    bool IsSystem = false)
      : Buffer(Buffer), Kind(Kind), IsSystem(IsSystem) {}

  InputKind getKind() const { return Kind; }
  bool isSystem() const { return IsSystem; }

  bool isEmpty() const { return File.empty() && Buffer == std::nullopt; }
  bool isFile() const { return !isBuffer(); }
  bool isBuffer() const { return Buffer != std::nullopt; }
  bool isPreprocessed() const { return Kind.isPreprocessed(); }
  bool isHeader() const { return Kind.isHeader(); }

  llvm::StringRef getFile() const {
    assert(isFile());
    return File;
  }

  llvm::MemoryBufferRef getBuffer() const {
    assert(isBuffer());
    return *Buffer;
  }
};

class FrontendOptions {
public:
  LLVM_PREFERRED_TYPE(bool)
  unsigned DisableFree : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowHelp : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowStats : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned AppendStats : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned PrintSupportedCPUs : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned ShowVersion : 1;

  LLVM_PREFERRED_TYPE(bool)
  unsigned UseTemporary : 1;

  InputKind DashX;

  llvm::SmallVector<FrontendInputFile, 0> Inputs;

  std::string OutputFile;

  frontend::ActionKind ProgramAction = frontend::ParseSyntaxOnly;

  std::vector<std::string> LLVMArgs;

  std::string StatsFile;

  unsigned TimeTraceGranularity;

  std::string TimeTracePath;

public:
  FrontendOptions()
      : DisableFree(false), ShowHelp(false), ShowStats(false),
        AppendStats(false), ShowVersion(false), UseTemporary(true),
        TimeTraceGranularity(500) {}

  static InputKind getInputKindForExtension(llvm::StringRef Extension);
};

} // namespace neverc

#endif // NEVERC_FRONTEND_FRONTENDOPTIONS_H
