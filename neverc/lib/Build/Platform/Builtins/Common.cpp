#include "Platform/Builtins/Internal.h"

#include "neverc/Build/Platform.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace neverc {
namespace build {
namespace builtins {
namespace internal {

bool hasGlobMeta(llvm::StringRef Path) {
  return Path.find_first_of("*?[") != llvm::StringRef::npos;
}


/// Split a recipe line into argv, honoring simple single/double quotes.
/// Returns false when quotes are unbalanced or the line needs a real shell.
bool tokenizeRecipe(llvm::StringRef Cmd, llvm::SmallVectorImpl<Token> &Argv) {
  Argv.clear();
  std::string Cur;
  char Quote = 0;
  bool InToken = false;
  bool Quoted = false;

  auto flush = [&]() {
    if (InToken) {
      Argv.push_back({std::move(Cur), Quoted});
      Cur.clear();
      InToken = false;
      Quoted = false;
    }
  };

  for (size_t I = 0; I < Cmd.size(); ++I) {
    const char C = Cmd[I];
    if (Quote) {
      // Double quotes still perform shell expansions; leave those to /bin/sh.
      // On Windows, `\` inside double quotes is a path character, not an escape.
      if (Quote == '"' && (C == '$' || C == '`'))
        return false;
#ifndef _WIN32
      if (Quote == '"' && C == '\\')
        return false;
#endif
      if (C == Quote) {
        Quote = 0;
        continue;
      }
      Cur.push_back(C);
      InToken = true;
      continue;
    }
    if (C == '\'' || C == '"') {
      Quote = C;
      Quoted = true;
      InToken = true;
      continue;
    }
    if (C == ' ' || C == '\t') {
      flush();
      continue;
    }
    // POSIX sh: unquoted '#' that begins a word starts a comment.
    if (C == '#' && !InToken)
      break;
    // Unquoted shell control / grouping characters are not claimed by builtins.
    // Also reject `{` so brace expansion like `file{1,2}.o` stays on /bin/sh
    // (we do not implement brace expansion ourselves).
#ifdef _WIN32
    // On Windows, `\` is a path separator, not a shell escape — allow it.
    if (llvm::StringRef("|<>&;`$(){").contains(C))
      return false;
#else
    if (llvm::StringRef("|<>&;`$\\(){").contains(C))
      return false;
#endif
    // Tilde expansion only applies at the start of a word; leave it to /bin/sh.
    if (C == '~' && !InToken)
      return false;
    Cur.push_back(C);
    InToken = true;
  }

  if (Quote)
    return false;
  flush();
  return true;
}


bool hasUnquotedGlob(llvm::ArrayRef<Token> Args) {
  for (const Token &T : Args)
    if (!T.Quoted && hasGlobMeta(T.Text))
      return true;
  return false;
}


bool hasStdinDashOperand(llvm::ArrayRef<Token> Operands) {
  for (const Token &T : Operands)
    if (T.Text == "-")
      return true;
  return false;
}


bool hasStdinDashPath(llvm::ArrayRef<std::string> Paths) {
  for (const std::string &P : Paths)
    if (P == "-")
      return true;
  return false;
}


bool pathsEquivalent(llvm::StringRef A, llvm::StringRef B) {
  if (A == B)
    return true;
  if (platform::absolutePath(A.str()) == platform::absolutePath(B.str()))
    return true;
  bool Equivalent = false;
  if (!llvm::sys::fs::equivalent(A, B, Equivalent) && Equivalent)
    return true;
  return false;
}


std::vector<std::string> expandOperand(const Token &Operand) {
  if (Operand.Quoted || !hasGlobMeta(Operand.Text))
    return {Operand.Text};

  std::vector<std::string> Matches = platform::globFiles(Operand.Text);
  // Without nullglob, a non-matching pattern is left as a literal operand.
  if (Matches.empty())
    return {Operand.Text};
  return Matches;
}


llvm::SmallVector<std::string, 8>
expandOperands(llvm::ArrayRef<Token> Operands) {
  llvm::SmallVector<std::string, 8> Out;
  for (const Token &Operand : Operands) {
    for (std::string &Path : expandOperand(Operand))
      Out.push_back(std::move(Path));
  }
  return Out;
}


bool parseNonNegLong(llvm::StringRef Text, long &Value) {
  if (Text.empty())
    return false;
  const std::string S = Text.str();
  char *End = nullptr;
  errno = 0;
  const long Parsed = std::strtol(S.c_str(), &End, 10);
  if (End == S.c_str() || (End && *End != '\0') || errno == ERANGE ||
      Parsed < 0)
    return false;
  Value = Parsed;
  return true;
}


bool isDirectory(llvm::StringRef Path, bool Follow) {
  llvm::sys::fs::file_status Status;
  if (llvm::sys::fs::status(Path, Status, Follow))
    return false;
  return Status.type() == llvm::sys::fs::file_type::directory_file;
}


bool isSameOrNestedPath(llvm::StringRef Outer, llvm::StringRef Inner) {
  // Strip trailing separators so `cp -r dir/ dir/nested` is still rejected.
  // Keep a lone root (`/` or `C:\`) intact.
  auto stripTrailingSeparators = [](std::string Path) {
    while (Path.size() > 1 && (Path.back() == '/' || Path.back() == '\\')) {
#ifdef _WIN32
      // Do not strip the separator from a drive root such as `C:\`.
      const llvm::StringRef Root = llvm::sys::path::root_path(Path);
      if (!Root.empty() && Root == Path)
        break;
#endif
      Path.pop_back();
    }
    return Path;
  };
  const std::string A = stripTrailingSeparators(platform::absolutePath(Outer.str()));
  const std::string B = stripTrailingSeparators(platform::absolutePath(Inner.str()));
  if (A == B)
    return true;
  if (B.size() <= A.size() || !llvm::StringRef(B).starts_with(A))
    return false;
  const char Sep = B[A.size()];
  return Sep == '/' || Sep == '\\';
}


bool readFileLines(llvm::StringRef Path,
                   llvm::SmallVectorImpl<llvm::StringRef> &Lines,
                   std::unique_ptr<llvm::MemoryBuffer> &BufKeepAlive,
                   std::string &Error, bool *EndsWithNewline) {
  auto Buf = llvm::MemoryBuffer::getFile(Path);
  if (!Buf) {
    Error = Buf.getError().message();
    return false;
  }
  BufKeepAlive = std::move(*Buf);
  const llvm::StringRef Text = BufKeepAlive->getBuffer();
  // StringRef::split on "" yields a single empty element; treat empty files as
  // zero lines so `head`/`wc`-style consumers match coreutils.
  if (Text.empty()) {
    Lines.clear();
    if (EndsWithNewline)
      *EndsWithNewline = false;
    return true;
  }
  Text.split(Lines, '\n');
  const bool TrailingNl = Text.back() == '\n';
  // Preserve POSIX text-file behavior: a trailing newline yields an empty
  // final split element that is not a real line for head/tail counting.
  if (!Lines.empty() && Lines.back().empty() && TrailingNl)
    Lines.pop_back();
  if (EndsWithNewline)
    *EndsWithNewline = TrailingNl;
  return true;
}


} // namespace internal
} // namespace builtins
} // namespace build
} // namespace neverc
