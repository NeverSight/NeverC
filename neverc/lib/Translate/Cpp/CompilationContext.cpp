#include "CompilationContext.h"
#include "../ArtifactWriter.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace neverc::translate {
namespace {
using namespace llvm;

bool fail(Diagnostics &Errors, StringRef Code, StringRef File,
          StringRef Construct, StringRef Reason, StringRef Guidance) {
  Errors.push_back(driverDiagnostic(Code, File, Construct, Reason, Guidance));
  return false;
}

std::string digest(StringRef Bytes) {
  SHA256 Hash;
  Hash.update(Bytes);
  static constexpr char Hex[] = "0123456789abcdef";
  std::string Result;
  for (uint8_t Byte : Hash.final()) {
    Result += Hex[Byte >> 4];
    Result += Hex[Byte & 15];
  }
  return Result;
}

bool readBounded(StringRef Path, std::size_t Limit, std::string &Contents,
                 std::string &Reason) {
  auto Bytes = neverc::translate::readFile(Path, Limit);
  if (!Bytes) {
    Reason = toString(Bytes.takeError()).str().str();
    return false;
  }
  Contents = std::move(*Bytes);
  return true;
}

bool canonical(StringRef Path, StringRef Directory, std::string &Result,
               std::string &Reason) {
  if (Path.contains('\0') || !json::isUTF8(Path) || !json::isUTF8(Directory)) {
    Reason = "path must be UTF-8 text without NUL bytes";
    return false;
  }
  SmallString<256> Absolute;
  if (sys::path::is_absolute(Path))
    Absolute = Path;
  else {
    Absolute = Directory;
    sys::path::append(Absolute, Path);
  }
  // Resolve the original spelling before reducing '..': a parent can be a
  // symlink, so lexical normalization would change which file was requested.
  SmallString<256> Real;
  if (std::error_code EC = sys::fs::real_path(Absolute, Real)) {
    Reason = EC.message();
    return false;
  }
  Result = Real.str().str();
  return true;
}

bool relativeIdentity(StringRef Root, StringRef Path, std::string &Relative) {
  auto R = sys::path::begin(Root), RE = sys::path::end(Root);
  auto P = sys::path::begin(Path), PE = sys::path::end(Path);
  for (; R != RE; ++R, ++P)
    if (P == PE || *R != *P)
      return false;
  Relative.clear();
  for (; P != PE; ++P) {
    if (!Relative.empty())
      Relative += '/';
    Relative += P->str();
  }
  if (Relative.empty())
    Relative = ".";
  return true;
}

bool boundedJSONDepth(StringRef Text) {
  unsigned Depth = 0;
  bool InString = false, Escaped = false;
  for (char C : Text) {
    if (InString) {
      if (Escaped)
        Escaped = false;
      else if (C == '\\')
        Escaped = true;
      else if (C == '"')
        InString = false;
    } else if (C == '"')
      InString = true;
    else if (C == '[' || C == '{') {
      if (++Depth > 32)
        return false;
    } else if ((C == ']' || C == '}') && Depth)
      --Depth;
  }
  return true;
}

bool textField(const json::Object &O, StringRef Key, std::string &Out,
               bool Required = true) {
  const auto *V = O.get(Key);
  if (!V)
    return !Required;
  StringRef S = V->getAsString();
  if (!S.data() || S.contains('\0') || (Required && S.empty()))
    return false;
  Out = S.str();
  return true;
}

struct Entry {
  uint32_t Index = 0;
  std::string Directory, File, Output, Command;
  std::vector<std::string> Arguments;
};

bool argumentBounds(const std::vector<std::string> &Args) {
  if (Args.size() > MaxCompilationArguments)
    return false;
  std::size_t Bytes = 0;
  for (const auto &Arg : Args) {
    if (Arg.find('\0') != std::string::npos ||
        Arg.find('\r') != std::string::npos ||
        Arg.find('\n') != std::string::npos || !json::isUTF8(Arg) ||
        Arg.size() + 1 > MaxCompilationArgumentBytes - Bytes)
      return false;
    Bytes += Arg.size() + 1;
  }
  return true;
}

void tokenize(StringRef Text, CommandQuoting Quoting, bool FullCommand,
              std::vector<std::string> &Arguments) {
  BumpPtrAllocator Allocator;
  StringSaver Saver(Allocator);
  SmallVector<const char *, 32> Tokens;
  if (Quoting == CommandQuoting::GNU)
    cl::TokenizeGNUCommandLine(Text, Saver, Tokens);
  else if (FullCommand)
    cl::TokenizeWindowsCommandLineFull(Text, Saver, Tokens);
  else
    cl::TokenizeWindowsCommandLine(Text, Saver, Tokens);
  for (const char *Token : Tokens)
    Arguments.emplace_back(Token);
}

bool parseEntries(StringRef Bytes, StringRef Database,
                  std::vector<Entry> &Entries, Diagnostics &Errors) {
  auto Invalid = [&](StringRef Reason) {
    return fail(Errors, "TR0601", Database, "compilation database", Reason,
                "Supply a UTF-8 compile_commands.json array with valid "
                "directory, file, and arguments or command fields.");
  };
  if (!boundedJSONDepth(Bytes))
    return Invalid("JSON nesting limit exceeded (32)");
  auto Value = json::parse(Bytes);
  if (!Value) {
    std::string Reason = toString(Value.takeError()).str().str();
    return Invalid(Reason);
  }
  const auto *Array = Value->getAsArray();
  if (!Array)
    return Invalid("top-level value must be an array");
  if (Array->size() > MaxCompilationDatabaseEntries)
    return Invalid("compilation database entry limit exceeded (10000)");
  for (const auto &V : *Array) {
    Entry E;
    E.Index = Entries.size();
    const auto *O = V.getAsObject();
    const std::string Prefix = "entry " + std::to_string(E.Index) + ": ";
    if (!O || !textField(*O, "directory", E.Directory) ||
        !textField(*O, "file", E.File) ||
        !textField(*O, "output", E.Output, false))
      return Invalid(Prefix + "missing or invalid field");
    if (!sys::path::is_absolute(E.Directory))
      return Invalid(Prefix + "directory must be absolute");
    if (O->get("arguments")) {
      const auto *Args = O->getArray("arguments");
      if (!Args || Args->empty())
        return Invalid(Prefix + "arguments must be a nonempty string array");
      if (Args->size() > MaxCompilationArguments)
        return Invalid(Prefix + "argument count limit exceeded");
      for (const auto &Arg : *Args) {
        StringRef S = Arg.getAsString();
        if (!S.data() || S.contains('\0'))
          return Invalid(Prefix + "arguments contains an invalid string");
        E.Arguments.push_back(S.str());
      }
      if (E.Arguments.front().empty() || !argumentBounds(E.Arguments))
        return Invalid(Prefix +
                       "invalid executable or argument byte limit exceeded");
    } else if (!textField(*O, "command", E.Command) ||
               E.Command.size() > MaxCompilationArgumentBytes) {
      return Invalid(Prefix + "missing or invalid command");
    }
    Entries.push_back(std::move(E));
  }
  return true;
}

class ContextParser {
  const ProjectContextOptions &Options;
  const ProjectContext &Project;
  Diagnostics &Errors;
  TranslationUnitContext &Unit;
  std::size_t ResponseBytes = 0;
  std::size_t ArgumentBytes = 0;
  std::set<std::string> ActiveResponses;
  std::map<std::string, std::string> RecordedResponses;

  bool error(StringRef Code, StringRef Construct, StringRef Reason,
             StringRef Guidance = "Use a supported explicit C++17 compilation "
                                  "context within --project-root.") {
    return fail(Errors, Code, Unit.SourceAbsolute,
                "compilation database entry " +
                    std::to_string(Unit.EntryIndex) + ": " + Construct.str(),
                Reason, Guidance);
  }

  bool expand(const std::vector<std::string> &Input,
              std::vector<std::string> &Output, unsigned Depth = 0) {
    for (const std::string &Arg : Input) {
      if (Arg.empty() || Arg[0] != '@') {
        if (Output.size() >= MaxCompilationArguments ||
            Arg.size() + 1 > MaxCompilationArgumentBytes - ArgumentBytes)
          return error("TR0603", "response expansion",
                       "expanded argument limit exceeded");
        ArgumentBytes += Arg.size() + 1;
        Output.push_back(Arg);
        continue;
      }
      if (Depth >= MaxResponseFileDepth)
        return error("TR0603", Arg, "response nesting limit exceeded (16)");
      std::string Path, Reason, Relative, Bytes;
      if (Arg.size() == 1 ||
          !canonical(StringRef(Arg).drop_front(), Unit.WorkingDirectoryAbsolute,
                     Path, Reason))
        return error("TR0603", Arg, "cannot resolve response file: " + Reason);
      if (!relativeIdentity(Project.ProjectRootAbsolute, Path, Relative))
        return error("TR0603", Arg, "response file is outside --project-root");
      if (!ActiveResponses.insert(Path).second)
        return error("TR0603", Arg, "response file cycle at " + Relative);
      if (!readBounded(Path, MaxResponseFileBytes - ResponseBytes, Bytes,
                       Reason))
        return error("TR0603", Arg, "cannot read response file: " + Reason);
      ResponseBytes += Bytes.size();
      if (!json::isUTF8(Bytes) || StringRef(Bytes).contains('\0'))
        return error("TR0603", Arg,
                     "response file must be UTF-8 text without NUL bytes");
      const std::string Hash = digest(Bytes);
      auto Existing = RecordedResponses.emplace(Path, Hash);
      if (!Existing.second && Existing.first->second != Hash)
        return error("TR0603", Arg,
                     "response file changed between repeated expansions");
      if (Existing.second)
        Unit.ResponseFiles.push_back({Path, Relative, Hash});
      std::vector<std::string> Nested;
      tokenize(Bytes, Options.Quoting, false, Nested);
      // Clang resolves nested response references against the compilation cwd.
      if (!expand(Nested, Output, Depth + 1))
        return false;
      ActiveResponses.erase(Path);
    }
    return true;
  }

  bool normalize(const std::vector<std::string> &Args) {
    std::size_t Sources = 0;
    auto Add = [&](const std::string &Arg) {
      Unit.FrontendArguments.push_back(Arg);
      Unit.NormalizedArguments.push_back(Arg);
    };
    Add("-std=c++17");
    Triple Target(Project.TargetTriple);
    for (std::size_t I = 0; I < Args.size(); ++I) {
      StringRef Arg = Args[I];
      auto Operand = [&](StringRef Flag, std::string &Value) {
        if (++I >= Args.size() || Args[I].empty())
          return error("TR0004", Flag, "missing option operand");
        Value = Args[I];
        return true;
      };
      if (Arg == "-c" || Arg == "-MD" || Arg == "-MMD" || Arg == "-MP")
        continue;
      if (Arg == "-o" || Arg == "-MF" || Arg == "-MT" || Arg == "-MQ") {
        std::string Value;
        if (!Operand(Arg, Value))
          return false;
        if (Arg == "-o" && Unit.OriginalOutput.empty())
          Unit.OriginalOutput = Value;
        continue;
      }
      if ((Arg.starts_with("-MF") || Arg.starts_with("-MT") ||
           Arg.starts_with("-MQ")) &&
          Arg.size() > 3) {
        continue;
      }
      if (Arg == "-std=c++17")
        continue;
      if (Arg == "-x" || Arg.starts_with("-x")) {
        std::string Value = Arg.drop_front(2).str();
        if (Arg == "-x" && !Operand(Arg, Value))
          return false;
        if (Value != "c++")
          return error("TR0004", Arg,
                       "only the C++ source language is supported");
        continue;
      }
      if (Arg == "-D" || Arg == "-U" || Arg.starts_with("-D") ||
          Arg.starts_with("-U")) {
        std::string Value = Arg.drop_front(2).str();
        if (Arg.size() == 2 && !Operand(Arg, Value))
          return false;
        if (Value.empty())
          return error("TR0004", Arg, "empty macro operand");
        Add(Arg.take_front(2).str() + Value);
        continue;
      }
      StringRef IncludeKind;
      if (Arg == "-I-" || Arg.starts_with("-isystem-after"))
        return error("TR0004", Arg, "unsupported include search option");
      if (Arg.starts_with("-isystem"))
        IncludeKind = "-isystem";
      else if (Arg.starts_with("-iquote"))
        IncludeKind = "-iquote";
      else if (Arg.starts_with("-I"))
        IncludeKind = "-I";
      if (!IncludeKind.empty()) {
        std::string Value = Arg.drop_front(IncludeKind.size()).str();
        if (Value.empty() && !Operand(Arg, Value))
          return false;
        if (StringRef(Value).starts_with("=") ||
            StringRef(Value).starts_with("$SYSROOT"))
          return error("TR0004", Arg,
                       "sysroot-relative include paths are unsupported");
        std::string Path, Relative, Reason;
        if (!canonical(Value, Unit.WorkingDirectoryAbsolute, Path, Reason) ||
            !sys::fs::is_directory(Path))
          return error("TR0004", Arg,
                       "include directory cannot be resolved: " + Reason);
        if (!relativeIdentity(Project.ProjectRootAbsolute, Path, Relative))
          return error("TR0004", Arg,
                       "include directory is outside --project-root");
        Unit.FrontendArguments.push_back(IncludeKind.str());
        Unit.FrontendArguments.push_back(Path);
        Unit.NormalizedArguments.push_back(IncludeKind.str());
        Unit.NormalizedArguments.push_back("$PROJECT/" + Relative);
        continue;
      }
      if (Arg == "-target" || Arg == "--target" ||
          Arg.starts_with("--target=") || Arg.starts_with("-target=")) {
        std::string Value;
        if (Arg.contains('='))
          Value = Arg.split('=').second.str();
        else if (!Operand(Arg, Value))
          return false;
        if (Triple::normalize(Value) != Project.TargetTriple)
          return error("TR0204", Arg,
                       "source target disagrees with the requested target " +
                           Project.TargetTriple);
        continue;
      }
      if (Arg == "-arch") {
        std::string Value;
        if (!Operand(Arg, Value))
          return false;
        Triple Requested(Value + "-unknown-unknown");
        if (Requested.getArch() == Triple::UnknownArch ||
            Requested.getArch() != Target.getArch())
          return error(
              "TR0204", Arg,
              "source architecture disagrees with the requested target");
        continue;
      }
      if (Arg == "-m32" || Arg == "-m64") {
        if ((Arg == "-m32" && Target.getArch() != Triple::x86) ||
            (Arg == "-m64" && !Target.isArch64Bit()))
          return error(
              "TR0204", Arg,
              "source pointer width disagrees with the requested target");
        continue;
      }
      if (Arg == "-O0" || Arg == "-O2" || Arg == "-Wall" || Arg == "-Wextra" ||
          Arg == "-Wpedantic" || Arg == "-Werror" || Arg == "-Wno-error" ||
          Arg == "-Wno-unused-parameter") {
        Add(Arg.str());
        continue;
      }
      if (Arg.empty() || Arg.starts_with("-"))
        return error("TR0004", Arg, "unsupported source compiler option");
      std::string Path, Reason;
      if (!canonical(Arg, Unit.WorkingDirectoryAbsolute, Path, Reason) ||
          Path != Unit.SourceAbsolute)
        return error("TR0602", Arg,
                     "input operand does not identify the selected source");
      if (++Sources != 1)
        return error("TR0602", Arg,
                     "selected source appears more than once in the command");
    }
    if (Sources != 1)
      return error("TR0602", "source operand",
                   "command must contain exactly the selected source");
    return true;
  }

public:
  ContextParser(const ProjectContextOptions &Options,
                const ProjectContext &Project, TranslationUnitContext &Unit,
                Diagnostics &Errors)
      : Options(Options), Project(Project), Errors(Errors), Unit(Unit) {}

  bool parse(const Entry &E) {
    std::vector<std::string> Args = E.Arguments;
    if (Args.empty())
      tokenize(E.Command, Options.Quoting, true, Args);
    if (Args.empty() || !argumentBounds(Args))
      return error("TR0601", "command",
                   "empty command or argument limit exceeded");
    Unit.CompilerExecutable = Args.front();
    StringRef Driver = sys::path::filename(
        Unit.CompilerExecutable, Options.Quoting == CommandQuoting::Windows
                                     ? sys::path::Style::windows
                                     : sys::path::Style::native);
    if (Driver.ends_with(".exe"))
      Driver = Driver.drop_back(4);
    // The helper pins Clang semantics. The declared executable is provenance,
    // and wrappers or another driver's option language must not be guessed.
    bool ClangDriver = Driver == "clang" || Driver == "clang++";
    for (StringRef Prefix : {StringRef("clang-"), StringRef("clang++-")}) {
      StringRef Suffix = Driver;
      if (Suffix.consume_front(Prefix) && !Suffix.empty() &&
          Suffix.find_first_not_of("0123456789.") == StringRef::npos)
        ClangDriver = true;
    }
    if (!ClangDriver)
      return error("TR0004", "compiler executable",
                   "unsupported driver or wrapper: " + Unit.CompilerExecutable,
                   "Record direct GNU-style clang/clang++ arguments; stored "
                   "commands are never executed.");
    Args.erase(Args.begin());
    Args.insert(Args.end(), Options.ExtraSourceArguments.begin(),
                Options.ExtraSourceArguments.end());
    if (!argumentBounds(Args))
      return error("TR0004", "source arguments",
                   "argument byte/count limit exceeded or NUL operand");
    std::vector<std::string> Expanded;
    if (!expand(Args, Expanded))
      return false;
    if (!argumentBounds(Expanded))
      return error("TR0004", "expanded source arguments",
                   "argument limits or UTF-8/control-byte rules were violated");
    if (!normalize(Expanded))
      return false;
    std::sort(Unit.ResponseFiles.begin(), Unit.ResponseFiles.end(),
              [](const ContextInput &A, const ContextInput &B) {
                return A.RelativePath < B.RelativePath;
              });
    // Length framing prevents delimiter characters in paths/macro values from
    // aliasing a different context. Raw control-file bytes have separate
    // hashes.
    std::string Semantic;
    auto Frame = [&](StringRef Value) {
      Semantic += std::to_string(Value.size()) + ":" + Value.str();
    };
    Frame("neverc.cpp-context.v1");
    Frame(Unit.SourceRelative);
    Frame(Unit.WorkingDirectoryRelative);
    Frame(Project.TargetTriple);
    for (const auto &Arg : Unit.NormalizedArguments)
      Frame(Arg);
    Unit.ConfigurationID = digest(Semantic);
    return true;
  }
};

} // namespace

bool parseProjectContext(const ProjectContextOptions &Options,
                         ProjectContext &Result, Diagnostics &Errors) {
  Result = {};
  const auto InitialErrors = Errors.size();
  ProjectContext Parsed;
  std::string Reason, CWD, Bytes;
  SmallString<256> Current;
  if (std::error_code EC = sys::fs::current_path(Current))
    return fail(Errors, "TR0601", Options.Database, "invocation directory",
                EC.message(),
                "Run translation from an accessible working directory.");
  CWD = Current.str().str();
  if (Options.ProjectRoot.empty() ||
      !canonical(Options.ProjectRoot, CWD, Parsed.ProjectRootAbsolute,
                 Reason) ||
      !sys::fs::is_directory(Parsed.ProjectRootAbsolute))
    return fail(Errors, "TR0601", Options.ProjectRoot, "project root",
                "missing or inaccessible --project-root: " + Reason,
                "Choose an existing explicit directory containing the selected "
                "source/build inputs.");
  if (Options.Database.empty() ||
      !canonical(Options.Database, CWD, Parsed.Database.AbsolutePath, Reason) ||
      !relativeIdentity(Parsed.ProjectRootAbsolute,
                        Parsed.Database.AbsolutePath,
                        Parsed.Database.RelativePath))
    return fail(Errors, "TR0601", Options.Database, "compilation database",
                "database must resolve within --project-root: " + Reason,
                "Supply a compilation database contained by the explicit "
                "project root.");
  if (!readBounded(Parsed.Database.AbsolutePath, MaxCompilationDatabaseBytes,
                   Bytes, Reason))
    return fail(Errors, "TR0601", Options.Database, "compilation database",
                Reason,
                "Supply a readable bounded compile_commands.json file.");
  Parsed.Database.SHA256 = digest(Bytes);
  Parsed.TargetTriple = Triple::normalize(Options.TargetTriple.empty()
                                              ? sys::getDefaultTargetTriple()
                                              : Options.TargetTriple);
  if (Triple(Parsed.TargetTriple).getArch() == Triple::UnknownArch)
    return fail(Errors, "TR0204", Options.Database, "target",
                "unknown requested target architecture",
                "Select a supported target triple.");
  std::vector<Entry> Entries;
  if (!parseEntries(Bytes, Parsed.Database.AbsolutePath, Entries, Errors))
    return false;
  if (Options.Selections.empty())
    return fail(Errors, "TR0602", Options.Database, "source selection",
                "no translation units were explicitly selected",
                "List the source files to translate; a database does not "
                "select them implicitly.");
  if (Options.Selections.size() > MaxCompilationDatabaseEntries)
    return fail(Errors, "TR0602", Options.Database, "source selection",
                "selected source count limit exceeded (10000)",
                "Select a bounded subset of the compilation database.");
  std::map<std::string, std::vector<const Entry *>> EntriesBySource;
  for (const auto &E : Entries) {
    std::string Path;
    if (canonical(E.File, E.Directory, Path, Reason))
      EntriesBySource[Path].push_back(&E);
  }
  std::set<std::string> Selected;
  for (const auto &Selection : Options.Selections) {
    TranslationUnitContext Unit;
    if (Selection.Source.empty() ||
        !canonical(Selection.Source, CWD, Unit.SourceAbsolute, Reason) ||
        !sys::fs::is_regular_file(Unit.SourceAbsolute)) {
      fail(Errors, "TR0203", Selection.Source, "selected source",
           "source cannot be resolved: " + Reason,
           "Create the selected source before translation.");
      continue;
    }
    if (!relativeIdentity(Parsed.ProjectRootAbsolute, Unit.SourceAbsolute,
                          Unit.SourceRelative) ||
        !Selected.insert(Unit.SourceAbsolute).second) {
      fail(Errors, "TR0602", Selection.Source, "source selection",
           "source is outside the project root or was selected more than once",
           "Select each owned source exactly once using one unambiguous "
           "spelling.");
      continue;
    }
    const auto &Candidates = EntriesBySource[Unit.SourceAbsolute];
    const Entry *Chosen = nullptr;
    if (Selection.EntryIndex) {
      for (const Entry *E : Candidates)
        if (E->Index == *Selection.EntryIndex)
          Chosen = E;
    } else if (Candidates.size() == 1)
      Chosen = Candidates.front();
    if (!Chosen) {
      std::string Choices;
      for (const Entry *E : Candidates) {
        if (!Choices.empty())
          Choices += "; ";
        Choices +=
            "index " + std::to_string(E->Index) + " (directory=" + E->Directory;
        if (!E->Output.empty())
          Choices += ", output=" + E->Output;
        Choices += ")";
      }
      fail(Errors, "TR0602", Selection.Source, "configuration selection",
           Candidates.empty()
               ? "no matching compilation database entry"
               : "entry selection is missing or mismatched; candidates: " +
                     Choices,
           "Use --compdb-entry FILE=INDEX to select exactly one matching "
           "zero-based database entry.");
      continue;
    }
    Unit.EntryIndex = Chosen->Index;
    Unit.OriginalOutput = Chosen->Output;
    if (!canonical(Chosen->Directory, CWD, Unit.WorkingDirectoryAbsolute,
                   Reason) ||
        !sys::fs::is_directory(Unit.WorkingDirectoryAbsolute) ||
        !relativeIdentity(Parsed.ProjectRootAbsolute,
                          Unit.WorkingDirectoryAbsolute,
                          Unit.WorkingDirectoryRelative)) {
      fail(Errors, "TR0602", Selection.Source, "compilation directory",
           "selected compilation directory must resolve inside --project-root",
           "Choose an encompassing project root for the selected source and "
           "build directory.");
      continue;
    }
    ContextParser Parser(Options, Parsed, Unit, Errors);
    if (Parser.parse(*Chosen))
      Parsed.Units.push_back(std::move(Unit));
  }
  if (Errors.size() != InitialErrors)
    return false;
  Result = std::move(Parsed);
  return true;
}

bool verifyProjectContextInputs(const ProjectContext &Context,
                                Diagnostics &Errors) {
  bool Valid = true;
  auto Check = [&](const ContextInput &Input, std::size_t Limit,
                   StringRef Code) {
    std::string Bytes, Reason;
    if (!readBounded(Input.AbsolutePath, Limit, Bytes, Reason) ||
        digest(Bytes) != Input.SHA256) {
      Valid = false;
      fail(Errors, Code, Input.RelativePath, "compilation context snapshot",
           "declared input changed or became unreadable during translation",
           "Retry translation while the database and response files remain "
           "unchanged.");
    }
  };
  Check(Context.Database, MaxCompilationDatabaseBytes, "TR0601");
  std::set<std::pair<std::string, std::string>> Seen;
  for (const auto &Unit : Context.Units)
    for (const auto &Input : Unit.ResponseFiles)
      if (Seen.insert({Input.AbsolutePath, Input.SHA256}).second)
        Check(Input, MaxResponseFileBytes, "TR0603");
  return Valid;
}

} // namespace neverc::translate
