#ifndef NEVERC_TRANSLATE_CPP_COMPILATIONCONTEXT_H
#define NEVERC_TRANSLATE_CPP_COMPILATIONCONTEXT_H

#include "../Diagnostics.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace neverc::translate {

inline constexpr std::size_t MaxCompilationDatabaseBytes = 32 * 1024 * 1024;
inline constexpr std::size_t MaxCompilationDatabaseEntries = 10000;
inline constexpr std::size_t MaxResponseFileDepth = 16;
inline constexpr std::size_t MaxResponseFileBytes = 4 * 1024 * 1024;
inline constexpr std::size_t MaxCompilationArguments = 65536;
inline constexpr std::size_t MaxCompilationArgumentBytes = 4 * 1024 * 1024;

enum class CommandQuoting { GNU, Windows };

struct CompilationSelection {
  std::string Source; // Resolve CLI spelling against the invocation directory.
  std::optional<uint32_t> EntryIndex;
};

struct ContextInput {
  std::string AbsolutePath; // Process/snapshot context, never a semantic ID.
  std::string
      RelativePath;   // Real project-root-relative identity, '/' separated.
  std::string SHA256; // Exact bytes, not a normalized semantic digest.
};

struct TranslationUnitContext {
  uint32_t EntryIndex = 0;
  std::string
      ConfigurationID; // Hash of normalized semantic context, not index.
  std::string SourceAbsolute;
  std::string SourceRelative;
  std::string WorkingDirectoryAbsolute;
  std::string WorkingDirectoryRelative;
  std::string CompilerExecutable; // Declared provenance; never executed.
  std::string OriginalOutput;     // Declared provenance; never a link recipe.
  std::vector<std::string>
      FrontendArguments; // Expanded absolute path operands.
  std::vector<std::string> NormalizedArguments; // Root-relative path operands.
  std::vector<ContextInput> ResponseFiles;
};

struct ProjectContextOptions {
  std::string Database;
  std::string ProjectRoot;
  std::string
      TargetTriple; // Empty selects the normalized NeverC native target.
#ifdef _WIN32
  CommandQuoting Quoting = CommandQuoting::Windows;
#else
  CommandQuoting Quoting = CommandQuoting::GNU;
#endif
  std::vector<CompilationSelection> Selections;
  // Appended to each entry and resolved against that entry's working directory.
  std::vector<std::string> ExtraSourceArguments;
};

struct ProjectContext {
  ContextInput Database;
  std::string ProjectRootAbsolute;
  std::string TargetTriple;
  std::vector<TranslationUnitContext> Units;
};

/// Validate every selected compilation before returning any executable jobs.
/// Result is empty on failure. Stored commands and response files are data
/// only.
bool parseProjectContext(const ProjectContextOptions &Options,
                         ProjectContext &Result, Diagnostics &Errors);

/// Recheck database/response snapshots before committing translation artifacts.
bool verifyProjectContextInputs(const ProjectContext &Context,
                                Diagnostics &Errors);

} // namespace neverc::translate
#endif
