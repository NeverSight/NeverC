#include "neverc/Compiler/FrontendOptions.h"
#include "neverc/Compiler/Utils.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/MacroBuilder.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Scan/PrepOptions.h"
#include "neverc/Compiler/FrontendDiag.h"
using namespace neverc;

// Defined in InitPredefinedMacros.cpp.
namespace neverc {
void initializePredefinedMacros(const TargetInfo &TI,
                                const LangOptions &LangOpts,
                                const FrontendOptions &FEOpts,
                                const PrepOptions &PPOpts,
                                MacroBuilder &Builder);
} // namespace neverc

// ===----------------------------------------------------------------------===
// Macro definition helpers
// ===----------------------------------------------------------------------===

namespace {

LLVM_ATTRIBUTE_ALWAYS_INLINE
bool macroBodyEndsInBackslash(llvm::StringRef MacroBody) {
  size_t Len = MacroBody.size();
  if (LLVM_UNLIKELY(Len == 0))
    return false;
  char Last = MacroBody[Len - 1];
  if (LLVM_LIKELY(!isWhitespace(Last)))
    return Last == '\\';
  do {
    --Len;
  } while (Len > 0 && isWhitespace(MacroBody[Len - 1]));
  return Len > 0 && MacroBody[Len - 1] == '\\';
}

void defineBuiltinMacro(MacroBuilder &Builder, llvm::StringRef Macro,
                        DiagnosticsEngine &Diags) {
  std::pair<llvm::StringRef, llvm::StringRef> MacroPair = Macro.split('=');
  llvm::StringRef MacroName = MacroPair.first;
  llvm::StringRef MacroBody = MacroPair.second;
  if (MacroName.size() != Macro.size()) {
    llvm::StringRef::size_type End = MacroBody.find_first_of("\n\r");
    if (End != llvm::StringRef::npos)
      Diags.Report(diag::warn_fe_macro_contains_embedded_newline) << MacroName;
    MacroBody = MacroBody.substr(0, End);
    if (macroBodyEndsInBackslash(MacroBody))
      Builder.defineMacro(MacroName, llvm::Twine(MacroBody) + "\\\n");
    else
      Builder.defineMacro(MacroName, MacroBody);
  } else {
    Builder.defineMacro(Macro);
  }
}

void addImplicitInclude(MacroBuilder &Builder, llvm::StringRef File) {
  Builder.append(llvm::Twine("#include \"") + File + "\"");
}

void addImplicitIncludeMacros(MacroBuilder &Builder, llvm::StringRef File) {
  Builder.append(llvm::Twine("#__include_macros \"") + File + "\"");
  // Marker token to stop the __include_macros fetch loop.
  Builder.append("##"); // ##?
}

// ===----------------------------------------------------------------------===
// Standard predefined macros
// ===----------------------------------------------------------------------===

void initializeStandardPredefinedMacros(const TargetInfo &TI,
                                        const LangOptions &LangOpts,
                                        const FrontendOptions &FEOpts,
                                        MacroBuilder &Builder) {
  //   -- __STDC__
  if (!LangOpts.MSVCCompat && !LangOpts.TraditionalCPP)
    Builder.defineMacro("__STDC__");
  //   -- __STDC_HOSTED__
  if (LangOpts.Freestanding)
    Builder.defineMacro("__STDC_HOSTED__", "0");
  else
    Builder.defineMacro("__STDC_HOSTED__");

  //   -- __STDC_VERSION__
  if (LangOpts.C23)
    Builder.defineMacro("__STDC_VERSION__", "202311L");
  else if (LangOpts.C17)
    Builder.defineMacro("__STDC_VERSION__", "201710L");
  else if (LangOpts.C11)
    Builder.defineMacro("__STDC_VERSION__", "201112L");
  else if (LangOpts.C99)
    Builder.defineMacro("__STDC_VERSION__", "199901L");
  else if (!LangOpts.GNUMode && LangOpts.Digraphs)
    Builder.defineMacro("__STDC_VERSION__", "199409L");

  Builder.defineMacro("__STDC_UTF_16__", "1");
  Builder.defineMacro("__STDC_UTF_32__", "1");

  if (LangOpts.AsmPreprocessor)
    Builder.defineMacro("__ASSEMBLER__");
}

} // anonymous namespace

// ===----------------------------------------------------------------------===
// Entry point
// ===----------------------------------------------------------------------===

void neverc::InitializePrepEngine(PrepEngine &PP, const PrepOptions &InitOpts,
                                  const FrontendOptions &FEOpts) {
  const LangOptions &LangOpts = PP.getLangOpts();
  std::string PredefineBuffer;
  PredefineBuffer.reserve(4080);
  llvm::raw_string_ostream Predefines(PredefineBuffer);
  MacroBuilder Builder(Predefines);

  // Emit line markers for various builtin sections of the file. The 3 here
  // marks <built-in> as being a system header, which suppresses warnings when
  // the same macro is defined multiple times.
  Builder.append("# 1 \"<built-in>\" 3");

  // Install target-specific and __GNUC__ macros into the macro table.
  if (InitOpts.UsePredefines) {
    initializePredefinedMacros(PP.getTargetInfo(), LangOpts, FEOpts,
                               PP.getPrepEngineOpts(), Builder);
  }

  // Even with predefines off, some macros are still predefined.
  // These should all be defined in the preprocessor according to the
  // current language configuration.
  initializeStandardPredefinedMacros(PP.getTargetInfo(), PP.getLangOpts(),
                                     FEOpts, Builder);

  // Add on the predefines from the driver.  Wrap in a #line directive to report
  // that they come from the command line.
  Builder.append("# 1 \"<command line>\" 1");

  for (unsigned i = 0, e = InitOpts.Macros.size(); i != e; ++i) {
    if (InitOpts.Macros[i].second) // isUndef
      Builder.undefineMacro(InitOpts.Macros[i].first);
    else
      defineBuiltinMacro(Builder, InitOpts.Macros[i].first,
                         PP.getDiagnostics());
  }

  // Exit the command line and go back to <built-in> (2 is LC_LEAVE).
  Builder.append("# 1 \"<built-in>\" 2");

  // If -imacros are specified, include them now.  These are processed before
  // any -include directives.
  for (unsigned i = 0, e = InitOpts.MacroIncludes.size(); i != e; ++i)
    addImplicitIncludeMacros(Builder, InitOpts.MacroIncludes[i]);

  for (unsigned i = 0, e = InitOpts.Includes.size(); i != e; ++i) {
    const std::string &Path = InitOpts.Includes[i];
    addImplicitInclude(Builder, Path);
  }

  // Copy PredefinedBuffer into the PrepEngine.  The BuiltinString prelude
  // (when enabled) is appended later by FrontendAction::BeginSourceFile
  // once the main FileID is in SourceManager.
  PP.setPredefines(std::move(PredefineBuffer));
}
