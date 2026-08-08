#include "Platform/Builtins/Internal.h"

#include "neverc/Build/Platform.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Base64.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace neverc {
namespace build {
namespace builtins {
namespace internal {


bool tryExecuteEcho(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  size_t Start = 1;
  bool Newline = true;
  if (Argv.size() >= 2 && Argv[1].Text == "-n") {
    Newline = false;
    Start = 2;
  } else if (Argv.size() >= 2 && llvm::StringRef(Argv[1].Text).starts_with("-") &&
             Argv[1].Text != "-") {
    // Other echo flags are implementation-defined; defer to the host shell.
    return false;
  }

  // Match /bin/sh: unquoted globs are expanded before echo sees them.
  const llvm::SmallVector<std::string, 8> Words =
      expandOperands(llvm::ArrayRef<Token>(Argv).drop_front(Start));
  for (size_t I = 0; I < Words.size(); ++I) {
    if (I)
      llvm::outs() << ' ';
    llvm::outs() << Words[I];
  }
  if (Newline)
    llvm::outs() << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteCat(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // No file operands (or "-") means stdin — leave that to the host shell.
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Files.push_back(Argv[J]);
      break;
    }
    if (Arg.starts_with("-"))
      return false; // no flag forms / stdin ("-")
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: cat: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::outs() << (*Buf)->getBuffer();
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool appendPrintfEscapes(llvm::StringRef Format, size_t &I, std::string &Out) {
  if (I + 1 >= Format.size())
    return false;
  switch (Format[I + 1]) {
  case '\\':
    Out.push_back('\\');
    break;
  case 'n':
    Out.push_back('\n');
    break;
  case 't':
    Out.push_back('\t');
    break;
  case '%':
    Out.push_back('%');
    break;
  default:
    return false;
  }
  I += 1;
  return true;
}


bool tryExecutePrintf(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  if (Argv.size() < 2) {
    llvm::errs() << "neverc make: printf: missing operand\n";
    ExitCode = 1;
    return true;
  }

  // GNU/BSD printf accept a leading `--` end-of-options marker. Without this,
  // `printf -- '%s\n' x` would treat `--` as the format string.
  size_t FmtIdx = 1;
  if (Argv[1].Text == "--") {
    if (Argv.size() < 3) {
      llvm::errs() << "neverc make: printf: missing operand\n";
      ExitCode = 1;
      return true;
    }
    FmtIdx = 2;
  }

  // Match /bin/sh: unquoted globs in printf arguments are expanded first.
  // The format string itself is never glob-expanded (quoted or not).
  const llvm::SmallVector<std::string, 8> Args =
      expandOperands(llvm::ArrayRef<Token>(Argv).drop_front(FmtIdx + 1));

  llvm::StringRef Format = Argv[FmtIdx].Text;
  size_t ArgI = 0;
  std::string Out;
  bool HardError = false;

  // Count conversion specs so we can reuse the format (POSIX printf).
  unsigned SpecCount = 0;
  for (size_t I = 0; I < Format.size(); ++I) {
    if (Format[I] == '\\') {
      if (I + 1 < Format.size())
        ++I;
      continue;
    }
    if (Format[I] != '%' || I + 1 >= Format.size())
      continue;
    const char Spec = Format[I + 1];
    if (Spec == '%') {
      ++I;
      continue;
    }
    if (Spec != 's' && Spec != 'd' && Spec != 'i' && Spec != 'c')
      return false;
    ++SpecCount;
    ++I;
  }

  auto appendOnePass = [&](bool ConsumeArgs) -> bool {
    for (size_t I = 0; I < Format.size(); ++I) {
      const char C = Format[I];
      if (C == '\\') {
        if (!appendPrintfEscapes(Format, I, Out))
          return false;
        continue;
      }
      if (C != '%') {
        Out.push_back(C);
        continue;
      }
      if (I + 1 >= Format.size())
        return false;
      const char Spec = Format[I + 1];
      if (Spec == '%') {
        Out.push_back('%');
        I += 1;
        continue;
      }
      llvm::StringRef Arg;
      if (ConsumeArgs && ArgI < Args.size())
        Arg = Args[ArgI++];
      // POSIX: missing args are treated as empty / zero for our subset.
      switch (Spec) {
      case 's':
        Out += Arg.str();
        break;
      case 'd':
      case 'i': {
        if (Arg.empty()) {
          Out += '0';
          break;
        }
        const std::string ArgStr = Arg.str();
        char *End = nullptr;
        long Value = std::strtol(ArgStr.c_str(), &End, 10);
        if (End == ArgStr.c_str() || (End && *End != '\0')) {
          llvm::errs() << "neverc make: printf: expected integer, got '" << Arg
                       << "'\n";
          HardError = true;
          return false;
        }
        Out += std::to_string(Value);
        break;
      }
      case 'c':
        if (!Arg.empty())
          Out.push_back(Arg[0]);
        break;
      default:
        return false;
      }
      I += 1;
    }
    return true;
  };

  // At least one pass (even with no args / no specs), then reuse while args
  // remain — matching POSIX/GNU printf format-reuse semantics.
  if (SpecCount == 0) {
    if (!appendOnePass(/*ConsumeArgs=*/false))
      return false;
  } else {
    do {
      if (!appendOnePass(/*ConsumeArgs=*/true)) {
        if (HardError) {
          ExitCode = 1;
          return true;
        }
        return false;
      }
    } while (ArgI < Args.size());
  }

  llvm::outs() << Out;
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteHeadTail(llvm::ArrayRef<Token> Argv, int &ExitCode, bool IsTail) {
  const char *Name = IsTail ? "tail" : "head";
  long Count = 10;
  bool ByteMode = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-n" || Arg == "-c") {
      const bool WantBytes = Arg == "-c";
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: " << Name
                     << ": option requires an argument -- "
                     << (WantBytes ? "c" : "n") << "\n";
        ExitCode = 1;
        return true;
      }
      llvm::StringRef CountText = Argv[++I].Text;
      // POSIX/GNU `tail -n +N` / `tail -c +N` mean "start at", not "last N".
      // Leave those forms (and signed counts) on the host tool.
      if (IsTail && CountText.starts_with("+"))
        return false;
      if (!parseNonNegLong(CountText, Count))
        return false;
      ByteMode = WantBytes;
      continue;
    }
    if (Arg.starts_with("-n") && Arg.size() > 2) {
      llvm::StringRef CountText = Arg.drop_front(2);
      if (IsTail && CountText.starts_with("+"))
        return false;
      if (!parseNonNegLong(CountText, Count))
        return false;
      ByteMode = false;
      continue;
    }
    if (Arg.starts_with("-c") && Arg.size() > 2) {
      llvm::StringRef CountText = Arg.drop_front(2);
      if (IsTail && CountText.starts_with("+"))
        return false;
      if (!parseNonNegLong(CountText, Count))
        return false;
      ByteMode = true;
      continue;
    }
    // Obsolescent `tail +N` (start at line N) is not the same as last-N.
    if (IsTail && Arg.starts_with("+") && Arg.size() > 1 &&
        llvm::all_of(Arg.drop_front(),
                     [](char C) { return C >= '0' && C <= '9'; }))
      return false;
    if (Arg.starts_with("-") && Arg.size() > 1) {
      // Historic `head -5` / `tail -5` (lines only).
      if (llvm::all_of(Arg.drop_front(),
                       [](char C) { return C >= '0' && C <= '9'; })) {
        if (!parseNonNegLong(Arg.drop_front(), Count))
          return false;
        ByteMode = false;
        continue;
      }
      return false;
    }
    Files.push_back(Argv[I]);
  }
  // No file operands / stdin ("-") means leave that to the host shell.
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  const auto Expanded = expandOperands(Files);
  if (hasStdinDashPath(Expanded))
    return false;
  for (size_t Fi = 0; Fi < Expanded.size(); ++Fi) {
    const std::string &Path = Expanded[Fi];
    if (Expanded.size() > 1) {
      if (Fi > 0)
        llvm::outs() << '\n';
      llvm::outs() << "==> " << Path << " <==\n";
    }
    if (ByteMode) {
      auto Buf = llvm::MemoryBuffer::getFile(Path);
      if (!Buf) {
        llvm::errs() << "neverc make: " << Name << ": " << Path << ": "
                     << Buf.getError().message() << "\n";
        Failed = true;
        continue;
      }
      llvm::StringRef Data = (*Buf)->getBuffer();
      const size_t N = std::min(static_cast<size_t>(Count), Data.size());
      if (!IsTail)
        llvm::outs() << Data.take_front(N);
      else
        llvm::outs() << Data.take_back(N);
      continue;
    }

    std::unique_ptr<llvm::MemoryBuffer> Buf;
    llvm::SmallVector<llvm::StringRef, 32> Lines;
    std::string Error;
    bool EndsWithNewline = false;
    if (!readFileLines(Path, Lines, Buf, Error, &EndsWithNewline)) {
      llvm::errs() << "neverc make: " << Name << ": " << Path << ": " << Error
                   << "\n";
      Failed = true;
      continue;
    }
    auto emitLine = [&](size_t I) {
      llvm::outs() << Lines[I];
      // Match coreutils: only the file's final line may omit a trailing
      // newline; every earlier line is terminated.
      if (I + 1 < Lines.size() || EndsWithNewline)
        llvm::outs() << '\n';
    };
    if (!IsTail) {
      const size_t N = std::min(static_cast<size_t>(Count), Lines.size());
      for (size_t I = 0; I < N; ++I)
        emitLine(I);
    } else {
      const size_t N = std::min(static_cast<size_t>(Count), Lines.size());
      const size_t Start = Lines.size() - N;
      for (size_t I = Start; I < Lines.size(); ++I)
        emitLine(I);
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}

bool tryExecuteHead(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  return tryExecuteHeadTail(Argv, ExitCode, /*IsTail=*/false);
}

bool tryExecuteTail(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  return tryExecuteHeadTail(Argv, ExitCode, /*IsTail=*/true);
}

bool tryExecuteWc(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool ShowLines = false;
  bool ShowWords = false;
  bool ShowBytes = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1 &&
        !Arg.starts_with("--")) {
      for (size_t J = 1; J < Arg.size(); ++J) {
        switch (Arg[J]) {
        case 'l':
          ShowLines = true;
          break;
        case 'w':
          ShowWords = true;
          break;
        case 'c':
          ShowBytes = true;
          break;
        default:
          return false;
        }
      }
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  if (!ShowLines && !ShowWords && !ShowBytes) {
    ShowLines = ShowWords = ShowBytes = true;
  }
  // No file operands / stdin ("-") means leave that to the host shell.
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: wc: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::StringRef Text = (*Buf)->getBuffer();
    size_t Lines = 0;
    size_t Words = 0;
    bool InWord = false;
    for (char C : Text) {
      if (C == '\n')
        ++Lines;
      const bool Space = C == ' ' || C == '\t' || C == '\n' || C == '\r' ||
                         C == '\f' || C == '\v';
      if (Space) {
        InWord = false;
      } else if (!InWord) {
        InWord = true;
        ++Words;
      }
    }
    const size_t Bytes = Text.size();
    if (ShowLines)
      llvm::outs() << Lines << ' ';
    if (ShowWords)
      llvm::outs() << Words << ' ';
    if (ShowBytes)
      llvm::outs() << Bytes << ' ';
    llvm::outs() << Path << '\n';
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteCut(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  char Delim = '\t';
  llvm::SmallVector<unsigned, 4> Fields;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-d") {
      if (I + 1 >= Argv.size() || Argv[I + 1].Text.empty()) {
        llvm::errs() << "neverc make: cut: option requires an argument -- d\n";
        ExitCode = 1;
        return true;
      }
      Delim = Argv[++I].Text[0];
      continue;
    }
    if (Arg.starts_with("-d") && Arg.size() > 2) {
      Delim = Arg[2];
      continue;
    }
    if (Arg == "-f") {
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: cut: option requires an argument -- f\n";
        ExitCode = 1;
        return true;
      }
      Arg = Argv[++I].Text;
      // Fall through to parse field list.
    } else if (Arg.starts_with("-f") && Arg.size() > 2) {
      Arg = Arg.drop_front(2);
    } else if (Arg.starts_with("-") && Arg.size() > 1) {
      return false;
    } else {
      Files.push_back(Argv[I]);
      continue;
    }

    llvm::SmallVector<llvm::StringRef, 4> Parts;
    Arg.split(Parts, ',', -1, false);
    for (llvm::StringRef Part : Parts) {
      long Field = 0;
      if (!parseNonNegLong(Part, Field) || Field < 1)
        return false;
      Fields.push_back(static_cast<unsigned>(Field));
    }
  }
  if (Fields.empty() || Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: cut: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::SmallVector<llvm::StringRef, 32> Lines;
    (*Buf)->getBuffer().split(Lines, '\n');
    if (!Lines.empty() && Lines.back().empty() &&
        !(*Buf)->getBuffer().empty() && (*Buf)->getBuffer().back() == '\n')
      Lines.pop_back();
    for (llvm::StringRef Line : Lines) {
      llvm::SmallVector<llvm::StringRef, 16> Cols;
      Line.split(Cols, Delim, -1, true);
      bool Any = false;
      for (unsigned Field : Fields) {
        if (Field == 0 || Field > Cols.size())
          continue;
        if (Any)
          llvm::outs() << Delim;
        llvm::outs() << Cols[Field - 1];
        Any = true;
      }
      llvm::outs() << '\n';
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteSort(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  bool Unique = false;
  bool Reverse = false;
  bool Numeric = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1 &&
        !Arg.starts_with("--")) {
      for (size_t J = 1; J < Arg.size(); ++J) {
        switch (Arg[J]) {
        case 'u':
          Unique = true;
          break;
        case 'r':
          Reverse = true;
          break;
        case 'n':
          Numeric = true;
          break;
        default:
          return false; // -k/-h/etc. stay on the host tool
        }
      }
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false; // stdin

  std::vector<std::string> Lines;
  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: sort: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::SmallVector<llvm::StringRef, 64> Parts;
    (*Buf)->getBuffer().split(Parts, '\n');
    if (!Parts.empty() && Parts.back().empty() &&
        !(*Buf)->getBuffer().empty() && (*Buf)->getBuffer().back() == '\n')
      Parts.pop_back();
    for (llvm::StringRef Part : Parts)
      Lines.push_back(Part.str());
  }
  auto numericKey = [](llvm::StringRef Text) -> long long {
    // GNU sort -n: leading blanks, optional sign, then digits. Non-numeric
    // lines compare as zero (with a later lexicographic tie-break).
    Text = Text.ltrim(" \t");
    if (Text.empty())
      return 0;
    const std::string S = Text.str();
    char *End = nullptr;
    errno = 0;
    const long long Parsed = std::strtoll(S.c_str(), &End, 10);
    if (End == S.c_str() || errno == ERANGE)
      return 0;
    return Parsed;
  };
  auto less = [&](const std::string &A, const std::string &B) {
    if (Numeric) {
      const long long NA = numericKey(A);
      const long long NB = numericKey(B);
      if (NA != NB)
        return Reverse ? NA > NB : NA < NB;
    }
    return Reverse ? A > B : A < B;
  };
  llvm::sort(Lines, less);
  if (Unique) {
    // GNU `sort -u` uniques by sort key. With `-n`, "1" and "01" collapse.
    if (Numeric) {
      Lines.erase(std::unique(Lines.begin(), Lines.end(),
                              [&](const std::string &A, const std::string &B) {
                                return numericKey(A) == numericKey(B);
                              }),
                  Lines.end());
    } else {
      Lines.erase(std::unique(Lines.begin(), Lines.end()), Lines.end());
    }
  }
  for (const std::string &Line : Lines)
    llvm::outs() << Line << '\n';
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteUniq(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  bool Count = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1 &&
        !Arg.starts_with("--")) {
      for (size_t J = 1; J < Arg.size(); ++J) {
        if (Arg[J] != 'c')
          return false; // -d/-u stay on the host tool
        Count = true;
      }
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  // Portable subset: one input file (no OUTPUT operand / no stdin).
  if (Files.size() != 1 || hasStdinDashOperand(Files))
    return false;

  auto Buf = llvm::MemoryBuffer::getFile(Files[0].Text);
  if (!Buf) {
    llvm::errs() << "neverc make: uniq: " << Files[0].Text << ": "
                 << Buf.getError().message() << "\n";
    ExitCode = 1;
    return true;
  }
  llvm::SmallVector<llvm::StringRef, 64> Parts;
  (*Buf)->getBuffer().split(Parts, '\n');
  if (!Parts.empty() && Parts.back().empty() &&
      !(*Buf)->getBuffer().empty() && (*Buf)->getBuffer().back() == '\n')
    Parts.pop_back();

  llvm::StringRef Prev;
  bool HavePrev = false;
  size_t Run = 0;
  auto flush = [&](llvm::StringRef Line, size_t N) {
    if (Count) {
      char Num[32];
      std::snprintf(Num, sizeof(Num), "%7zu ", N);
      llvm::outs() << Num;
    }
    llvm::outs() << Line << '\n';
  };
  for (llvm::StringRef Part : Parts) {
    if (HavePrev && Part == Prev) {
      ++Run;
      continue;
    }
    if (HavePrev)
      flush(Prev, Run);
    Prev = Part;
    HavePrev = true;
    Run = 1;
  }
  if (HavePrev)
    flush(Prev, Run);
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteGrepWithDialect(llvm::ArrayRef<Token> Argv, int &ExitCode,
                               llvm::StringRef DialectFlag) {
  llvm::SmallVector<Token, 8> Injected;
  Injected.reserve(Argv.size() + 1);
  Injected.push_back(Argv[0]);
  Injected.push_back(Token{DialectFlag.str(), /*Quoted=*/false});
  for (size_t I = 1; I < Argv.size(); ++I)
    Injected.push_back(Argv[I]);
  return tryExecuteGrep(Injected, ExitCode);
}


bool tryExecuteEgrep(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Historical alias: egrep ≈ grep -E
  return tryExecuteGrepWithDialect(Argv, ExitCode, "-E");
}


bool tryExecuteFgrep(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Historical alias: fgrep ≈ grep -F
  return tryExecuteGrepWithDialect(Argv, ExitCode, "-F");
}


bool tryExecuteGrep(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset used by recipes/configure checks:
  //   grep [-q|-c|-l|-n|-v] [-i] [-F|-E] PATTERN FILE...
  // Basic BRE without -F/-E stays on the host tool. Stdin is not claimed.
  bool Quiet = false;
  bool CountOnly = false;
  bool FilesWithMatches = false;
  bool LineNumber = false;
  bool IgnoreCase = false;
  bool Invert = false;
  bool Fixed = false;
  bool Extended = false;
  llvm::SmallVector<Token, 4> Operands;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1 &&
        !Arg.starts_with("--")) {
      for (size_t J = 1; J < Arg.size(); ++J) {
        switch (Arg[J]) {
        case 'q':
          Quiet = true;
          break;
        case 'c':
          CountOnly = true;
          break;
        case 'l':
          FilesWithMatches = true;
          break;
        case 'n':
          LineNumber = true;
          break;
        case 'i':
          IgnoreCase = true;
          break;
        case 'v':
          Invert = true;
          break;
        case 'F':
          Fixed = true;
          break;
        case 'E':
          Extended = true;
          break;
        default:
          return false;
        }
      }
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Operands.push_back(Argv[I]);
  }
  // Require an explicit regex dialect so we do not pretend to implement BRE.
  if ((!Fixed && !Extended) || (Fixed && Extended))
    return false;
  if (Operands.size() < 2)
    return false; // PATTERN + at least one file
  if ((Quiet && CountOnly) || (Quiet && FilesWithMatches) ||
      (CountOnly && FilesWithMatches) || (Quiet && LineNumber) ||
      (CountOnly && LineNumber) || (FilesWithMatches && LineNumber))
    return false;

  const std::string Pattern = Operands[0].Text;
  const llvm::SmallVector<std::string, 8> Files =
      expandOperands(llvm::ArrayRef<Token>(Operands).drop_front());
  if (Files.empty() || hasStdinDashPath(Files))
    return false;

  std::unique_ptr<llvm::Regex> Re;
  if (Extended) {
    unsigned Flags = llvm::Regex::Newline;
    if (IgnoreCase)
      Flags |= llvm::Regex::IgnoreCase;
    Re = std::make_unique<llvm::Regex>(Pattern, Flags);
    llvm::SmallVector<char, 64> Err;
    if (!Re->isValid(Err)) {
      llvm::errs() << "neverc make: grep: " << Err << "\n";
      ExitCode = 2;
      return true;
    }
  }

  std::string PatternLower;
  if (IgnoreCase && Fixed) {
    const auto Lowered = llvm::StringRef(Pattern).lower();
    PatternLower.assign(Lowered.begin(), Lowered.end());
  }
  auto matches = [&](llvm::StringRef Line) -> bool {
    if (Fixed) {
      if (!IgnoreCase)
        return Line.contains(Pattern);
      const auto LoweredLine = Line.lower();
      return llvm::StringRef(LoweredLine.begin(), LoweredLine.size())
          .contains(PatternLower);
    }
    return Re->match(Line);
  };

  bool Failed = false;
  bool AnyMatch = false;
  for (const std::string &Path : Files) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: grep: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::SmallVector<llvm::StringRef, 64> Lines;
    (*Buf)->getBuffer().split(Lines, '\n');
    if (!Lines.empty() && Lines.back().empty() &&
        !(*Buf)->getBuffer().empty() && (*Buf)->getBuffer().back() == '\n')
      Lines.pop_back();

    size_t MatchCount = 0;
    for (size_t LineNo = 0; LineNo < Lines.size(); ++LineNo) {
      llvm::StringRef Line = Lines[LineNo];
      if (matches(Line) == Invert)
        continue;
      ++MatchCount;
      AnyMatch = true;
      if (Quiet) {
        ExitCode = 0;
        return true;
      }
      if (FilesWithMatches) {
        llvm::outs() << Path << '\n';
        break;
      }
      if (!CountOnly) {
        if (Files.size() > 1)
          llvm::outs() << Path << ':';
        if (LineNumber)
          llvm::outs() << (LineNo + 1) << ':';
        llvm::outs() << Line << '\n';
      }
    }
    if (CountOnly)
      llvm::outs() << (Files.size() > 1 ? Path + ":" : std::string())
                   << MatchCount << '\n';
  }
  llvm::outs().flush();
  if (Failed && !AnyMatch) {
    ExitCode = 2;
    return true;
  }
  ExitCode = AnyMatch ? 0 : 1;
  return true;
}


bool tryExecuteTac(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Reverse-cat of file operands (no flags / no stdin).
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    std::unique_ptr<llvm::MemoryBuffer> Buf;
    llvm::SmallVector<llvm::StringRef, 32> Lines;
    std::string Error;
    if (!readFileLines(Path, Lines, Buf, Error)) {
      llvm::errs() << "neverc make: tac: " << Path << ": " << Error << "\n";
      Failed = true;
      continue;
    }
    for (size_t I = Lines.size(); I > 0; --I)
      llvm::outs() << Lines[I - 1] << '\n';
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteNl(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `nl FILE...` (number non-empty lines, default GNU-ish).
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false; // -ba/-bt/-nr... stay on the host tool
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  long LineNo = 1;
  for (const std::string &Path : expandOperands(Files)) {
    std::unique_ptr<llvm::MemoryBuffer> Buf;
    llvm::SmallVector<llvm::StringRef, 32> Lines;
    std::string Error;
    if (!readFileLines(Path, Lines, Buf, Error)) {
      llvm::errs() << "neverc make: nl: " << Path << ": " << Error << "\n";
      Failed = true;
      continue;
    }
    for (llvm::StringRef Line : Lines) {
      if (Line.empty()) {
        llvm::outs() << '\n';
        continue;
      }
      // Match common `nl` width-6 right-aligned number + tab.
      char Num[32];
      std::snprintf(Num, sizeof(Num), "%6ld\t", LineNo++);
      llvm::outs() << Num << Line << '\n';
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool decodeBase64Text(llvm::StringRef In, std::string &Out) {
  auto value = [](char C) -> int {
    if (C >= 'A' && C <= 'Z')
      return C - 'A';
    if (C >= 'a' && C <= 'z')
      return C - 'a' + 26;
    if (C >= '0' && C <= '9')
      return C - '0' + 52;
    if (C == '+')
      return 62;
    if (C == '/')
      return 63;
    return -1;
  };

  std::string Filtered;
  Filtered.reserve(In.size());
  for (char C : In) {
    if (C == ' ' || C == '\t' || C == '\n' || C == '\r')
      continue;
    Filtered.push_back(C);
  }
  if (Filtered.size() % 4 != 0)
    return false;

  Out.clear();
  Out.reserve(Filtered.size() / 4 * 3);
  for (size_t I = 0; I < Filtered.size(); I += 4) {
    const int A = value(Filtered[I]);
    const int B = value(Filtered[I + 1]);
    const int C = Filtered[I + 2] == '=' ? -2 : value(Filtered[I + 2]);
    const int D = Filtered[I + 3] == '=' ? -2 : value(Filtered[I + 3]);
    if (A < 0 || B < 0 || C == -1 || D == -1)
      return false;
    Out.push_back(static_cast<char>((A << 2) | (B >> 4)));
    if (C == -2) {
      if (D != -2 || I + 4 != Filtered.size())
        return false;
      break;
    }
    Out.push_back(static_cast<char>(((B & 15) << 4) | (C >> 2)));
    if (D == -2) {
      if (I + 4 != Filtered.size())
        return false;
      break;
    }
    Out.push_back(static_cast<char>(((C & 3) << 6) | D));
  }
  return true;
}


bool tryExecuteBase64(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `base64 FILE...` and `base64 -d FILE...` (no stdin/wrap).
  bool Decode = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if ((Arg == "-d" || Arg == "--decode")) {
      Decode = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -w / -i stay on the host tool
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: base64: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    if (Decode) {
      std::string Out;
      if (!decodeBase64Text((*Buf)->getBuffer(), Out)) {
        llvm::errs() << "neverc make: base64: " << Path << ": invalid input\n";
        Failed = true;
        continue;
      }
      llvm::outs() << Out;
    } else {
      llvm::outs() << llvm::encodeBase64((*Buf)->getBuffer()) << '\n';
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteRev(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Reverse characters of each line for file operands (no flags / no stdin).
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    std::unique_ptr<llvm::MemoryBuffer> Buf;
    llvm::SmallVector<llvm::StringRef, 32> Lines;
    std::string Error;
    if (!readFileLines(Path, Lines, Buf, Error)) {
      llvm::errs() << "neverc make: rev: " << Path << ": " << Error << "\n";
      Failed = true;
      continue;
    }
    for (llvm::StringRef Line : Lines) {
      for (size_t I = Line.size(); I > 0; --I)
        llvm::outs() << Line[I - 1];
      llvm::outs() << '\n';
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteFold(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `fold [-w WIDTH] FILE...` (default width 80; no -s/-b).
  long Width = 80;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-w") {
      if (I + 1 >= Argv.size()) {
        llvm::errs()
            << "neverc make: fold: option requires an argument -- w\n";
        ExitCode = 1;
        return true;
      }
      if (!parseNonNegLong(Argv[++I].Text, Width) || Width == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("-w") && Arg.size() > 2) {
      if (!parseNonNegLong(Arg.drop_front(2), Width) || Width == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    std::unique_ptr<llvm::MemoryBuffer> Buf;
    llvm::SmallVector<llvm::StringRef, 32> Lines;
    std::string Error;
    if (!readFileLines(Path, Lines, Buf, Error)) {
      llvm::errs() << "neverc make: fold: " << Path << ": " << Error << "\n";
      Failed = true;
      continue;
    }
    const size_t W = static_cast<size_t>(Width);
    for (llvm::StringRef Line : Lines) {
      if (Line.empty()) {
        llvm::outs() << '\n';
        continue;
      }
      for (size_t I = 0; I < Line.size(); I += W) {
        llvm::outs() << Line.substr(I, W) << '\n';
      }
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecutePaste(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `paste [-d DELIM] FILE FILE...` (serial merge by line).
  // DELIM may be empty (`paste -d '' a b` concatenates columns).
  std::string Delim = "\t";
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-d") {
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: paste: option requires an argument -- d\n";
        ExitCode = 1;
        return true;
      }
      // Use only the first character of the delimiter list (POSIX paste).
      // An empty list means no separator between columns.
      Delim = Argv[++I].Text.empty() ? std::string()
                                     : std::string(1, Argv[I].Text[0]);
      continue;
    }
    if (Arg.starts_with("-d") && Arg.size() > 2) {
      Delim.assign(1, Arg[2]);
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.size() < 2 || hasStdinDashOperand(Files))
    return false;

  std::vector<llvm::SmallVector<llvm::StringRef, 32>> Columns;
  std::vector<std::unique_ptr<llvm::MemoryBuffer>> Bufs;
  size_t MaxLines = 0;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: paste: " << Path << ": "
                   << Buf.getError().message() << "\n";
      ExitCode = 1;
      return true;
    }
    Bufs.push_back(std::move(*Buf));
    llvm::SmallVector<llvm::StringRef, 32> Lines;
    Bufs.back()->getBuffer().split(Lines, '\n');
    if (!Lines.empty() && Lines.back().empty() &&
        !Bufs.back()->getBuffer().empty() &&
        Bufs.back()->getBuffer().back() == '\n')
      Lines.pop_back();
    MaxLines = std::max(MaxLines, Lines.size());
    Columns.push_back(std::move(Lines));
  }

  for (size_t Row = 0; Row < MaxLines; ++Row) {
    for (size_t Col = 0; Col < Columns.size(); ++Col) {
      if (Col)
        llvm::outs() << Delim;
      if (Row < Columns[Col].size())
        llvm::outs() << Columns[Col][Row];
    }
    llvm::outs() << '\n';
  }
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteSplit(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `split -l LINES FILE [PREFIX]` (default prefix "x").
  long LinesPer = 0;
  bool HaveLines = false;
  llvm::SmallVector<Token, 4> Operands;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-l") {
      if (I + 1 >= Argv.size()) {
        llvm::errs()
            << "neverc make: split: option requires an argument -- l\n";
        ExitCode = 1;
        return true;
      }
      if (!parseNonNegLong(Argv[++I].Text, LinesPer) || LinesPer == 0)
        return false;
      HaveLines = true;
      continue;
    }
    if (Arg.starts_with("-l") && Arg.size() > 2) {
      if (!parseNonNegLong(Arg.drop_front(2), LinesPer) || LinesPer == 0)
        return false;
      HaveLines = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -b/-a/-d stay on the host tool
    Operands.push_back(Argv[I]);
  }
  if (!HaveLines || Operands.empty() || Operands.size() > 2)
    return false;
  if (hasUnquotedGlob(Operands) || hasStdinDashOperand(Operands))
    return false;

  const std::string Input = Operands[0].Text;
  const std::string Prefix =
      Operands.size() == 2 ? Operands[1].Text : std::string("x");
  if (Input.empty() || Prefix.empty()) {
    llvm::errs() << "neverc make: split: empty operand\n";
    ExitCode = 1;
    return true;
  }

  auto BufOrErr = llvm::MemoryBuffer::getFile(Input);
  if (!BufOrErr) {
    llvm::errs() << "neverc make: split: " << Input << ": "
                 << BufOrErr.getError().message() << "\n";
    ExitCode = 1;
    return true;
  }
  std::unique_ptr<llvm::MemoryBuffer> Buf = std::move(*BufOrErr);
  const llvm::StringRef Data = Buf->getBuffer();
  // Empty input: classic split creates no output files. Important: do this
  // before split() — StringRef("").split('\n') yields a single empty element,
  // which would otherwise create a spurious zero-byte `PREFIX`+`aa` file.
  if (Data.empty()) {
    ExitCode = 0;
    return true;
  }
  // Preserve whether the final line lacked a terminating newline. Blindly
  // rewriting every line with '\n' would corrupt `printf 'a\nb\nc' | split`.
  const bool EndsWithNewline = Data.back() == '\n';
  llvm::SmallVector<llvm::StringRef, 64> Lines;
  Data.split(Lines, '\n');
  if (EndsWithNewline && !Lines.empty() && Lines.back().empty())
    Lines.pop_back();
  if (Lines.empty()) {
    ExitCode = 0;
    return true;
  }

  // Two-letter suffixes: xaa, xab, ... xzz (676 parts), matching classic split.
  auto suffixAt = [](size_t Index, std::string &Out) -> bool {
    if (Index >= 26 * 26)
      return false;
    Out.push_back(static_cast<char>('a' + Index / 26));
    Out.push_back(static_cast<char>('a' + Index % 26));
    return true;
  };

  const size_t Per = static_cast<size_t>(LinesPer);
  size_t Part = 0;
  for (size_t Start = 0; Start < Lines.size(); Start += Per, ++Part) {
    std::string Suffix;
    if (!suffixAt(Part, Suffix)) {
      llvm::errs() << "neverc make: split: too many files\n";
      ExitCode = 1;
      return true;
    }
    const std::string OutPath = Prefix + Suffix;
    std::error_code EC;
    // OF_None: do not rewrite `\n` as CRLF on Windows.
    llvm::raw_fd_ostream Out(OutPath, EC, llvm::sys::fs::OF_None);
    if (EC) {
      llvm::errs() << "neverc make: split: " << OutPath << ": " << EC.message()
                   << "\n";
      ExitCode = 1;
      return true;
    }
    const size_t End = std::min(Start + Per, Lines.size());
    for (size_t I = Start; I < End; ++I) {
      Out << Lines[I];
      const bool IsLastLine = (I + 1 == Lines.size());
      if (!IsLastLine || EndsWithNewline)
        Out << '\n';
    }
  }
  ExitCode = 0;
  return true;
}


bool tryExecuteStrings(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `strings FILE...` (min length 4, ASCII printable).
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    // Flags and stdin ("-") stay on the host tool.
    if (Arg.starts_with("-"))
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  constexpr size_t MinLen = 4;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: strings: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::StringRef Data = (*Buf)->getBuffer();
    size_t Run = 0;
    for (size_t I = 0; I <= Data.size(); ++I) {
      const bool Printable =
          I < Data.size() && Data[I] >= 32 && Data[I] < 127;
      if (Printable) {
        ++Run;
        continue;
      }
      if (Run >= MinLen)
        llvm::outs() << Data.substr(I - Run, Run) << '\n';
      Run = 0;
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteOd(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset used by recipes/tests:
  //   od -An -tx1 FILE...
  //   od -t x1 FILE...
  //   od -c FILE...
  // Address columns and other type strings stay on the host tool.
  bool NoAddress = false;
  bool HexBytes = false;
  bool CChars = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-An") {
      NoAddress = true;
      continue;
    }
    if (Arg == "-c") {
      CChars = true;
      continue;
    }
    if (Arg == "-tx1") {
      HexBytes = true;
      continue;
    }
    if (Arg == "-t") {
      if (I + 1 >= Argv.size() || Argv[I + 1].Text != "x1")
        return false;
      ++I;
      HexBytes = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  // Require an explicit type and no-address mode so we do not invent GNU's
  // default octal dump / address columns.
  if (!NoAddress || Files.empty() || hasStdinDashOperand(Files))
    return false;
  if (HexBytes == CChars)
    return false; // need exactly one of -tx1 / -c

  auto emitCChar = [](unsigned char C) {
    switch (C) {
    case '\0':
      llvm::outs() << "  \\0";
      return;
    case '\a':
      llvm::outs() << "  \\a";
      return;
    case '\b':
      llvm::outs() << "  \\b";
      return;
    case '\t':
      llvm::outs() << "  \\t";
      return;
    case '\n':
      llvm::outs() << "  \\n";
      return;
    case '\v':
      llvm::outs() << "  \\v";
      return;
    case '\f':
      llvm::outs() << "  \\f";
      return;
    case '\r':
      llvm::outs() << "  \\r";
      return;
    default:
      break;
    }
    if (C >= 32 && C < 127) {
      llvm::outs() << "   " << static_cast<char>(C);
      return;
    }
    char Buf[8];
    std::snprintf(Buf, sizeof(Buf), " %03o", C);
    llvm::outs() << Buf;
  };

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: od: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::StringRef Data = (*Buf)->getBuffer();
    constexpr size_t Width = 16;
    for (size_t Off = 0; Off < Data.size(); Off += Width) {
      const size_t N = std::min(Width, Data.size() - Off);
      for (size_t I = 0; I < N; ++I) {
        const unsigned char C =
            static_cast<unsigned char>(Data[Off + I]);
        if (HexBytes) {
          if (I)
            llvm::outs() << ' ';
          char Hex[4];
          std::snprintf(Hex, sizeof(Hex), "%02x", C);
          llvm::outs() << Hex;
        } else {
          emitCChar(C);
        }
      }
      llvm::outs() << '\n';
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteExpand(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `expand [-t N] FILE...` (tab stops every N, default 8).
  long Tab = 8;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-t") {
      if (I + 1 >= Argv.size()) {
        llvm::errs()
            << "neverc make: expand: option requires an argument -- t\n";
        ExitCode = 1;
        return true;
      }
      if (!parseNonNegLong(Argv[++I].Text, Tab) || Tab == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("-t") && Arg.size() > 2) {
      if (!parseNonNegLong(Arg.drop_front(2), Tab) || Tab == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -i / comma lists stay on the host tool
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false; // stdin

  bool Failed = false;
  const size_t TabStop = static_cast<size_t>(Tab);
  for (const std::string &Path : expandOperands(Files)) {
    std::unique_ptr<llvm::MemoryBuffer> Buf;
    llvm::SmallVector<llvm::StringRef, 32> Lines;
    std::string Error;
    if (!readFileLines(Path, Lines, Buf, Error)) {
      llvm::errs() << "neverc make: expand: " << Path << ": " << Error << "\n";
      Failed = true;
      continue;
    }
    for (llvm::StringRef Line : Lines) {
      size_t Col = 0;
      for (char C : Line) {
        if (C == '\t') {
          const size_t Spaces = TabStop - (Col % TabStop);
          for (size_t S = 0; S < Spaces; ++S)
            llvm::outs() << ' ';
          Col += Spaces;
          continue;
        }
        llvm::outs() << C;
        ++Col;
      }
      llvm::outs() << '\n';
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteComm(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `comm [-123] FILE1 FILE2` on already-sorted inputs.
  bool Suppress1 = false;
  bool Suppress2 = false;
  bool Suppress3 = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1 &&
        !Arg.starts_with("--")) {
      for (size_t J = 1; J < Arg.size(); ++J) {
        switch (Arg[J]) {
        case '1':
          Suppress1 = true;
          break;
        case '2':
          Suppress2 = true;
          break;
        case '3':
          Suppress3 = true;
          break;
        default:
          return false; // --check-order / -z stay on the host tool
        }
      }
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  // Unquoted globs change arity; leave them to /bin/sh.
  if (Files.size() != 2 || hasUnquotedGlob(Files) || hasStdinDashOperand(Files))
    return false;

  auto loadLines = [](llvm::StringRef Path,
                      llvm::SmallVectorImpl<llvm::StringRef> &Lines,
                      std::unique_ptr<llvm::MemoryBuffer> &Buf,
                      std::string &Error) -> bool {
    return readFileLines(Path, Lines, Buf, Error);
  };

  std::unique_ptr<llvm::MemoryBuffer> BufA;
  std::unique_ptr<llvm::MemoryBuffer> BufB;
  llvm::SmallVector<llvm::StringRef, 64> A;
  llvm::SmallVector<llvm::StringRef, 64> B;
  std::string Error;
  if (!loadLines(Files[0].Text, A, BufA, Error)) {
    llvm::errs() << "neverc make: comm: " << Files[0].Text << ": " << Error
                 << "\n";
    ExitCode = 1;
    return true;
  }
  if (!loadLines(Files[1].Text, B, BufB, Error)) {
    llvm::errs() << "neverc make: comm: " << Files[1].Text << ": " << Error
                 << "\n";
    ExitCode = 1;
    return true;
  }

  auto emit = [&](int Col, llvm::StringRef Line) {
    // Col is 1/2/3. Leading tabs separate columns that were not suppressed,
    // matching POSIX/GNU comm.
    if (Col == 1 && Suppress1)
      return;
    if (Col == 2 && Suppress2)
      return;
    if (Col == 3 && Suppress3)
      return;
    int Tabs = 0;
    if (Col >= 2 && !Suppress1)
      ++Tabs;
    if (Col >= 3 && !Suppress2)
      ++Tabs;
    for (int T = 0; T < Tabs; ++T)
      llvm::outs() << '\t';
    llvm::outs() << Line << '\n';
  };

  size_t I = 0;
  size_t J = 0;
  while (I < A.size() && J < B.size()) {
    if (A[I] == B[J]) {
      emit(3, A[I]);
      ++I;
      ++J;
    } else if (A[I] < B[J]) {
      emit(1, A[I]);
      ++I;
    } else {
      emit(2, B[J]);
      ++J;
    }
  }
  while (I < A.size()) {
    emit(1, A[I]);
    ++I;
  }
  while (J < B.size()) {
    emit(2, B[J]);
    ++J;
  }
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteUnexpand(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `unexpand [-t N] FILE...` (leading blanks → tabs).
  long Tab = 8;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-t") {
      if (I + 1 >= Argv.size()) {
        llvm::errs()
            << "neverc make: unexpand: option requires an argument -- t\n";
        ExitCode = 1;
        return true;
      }
      if (!parseNonNegLong(Argv[++I].Text, Tab) || Tab == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("-t") && Arg.size() > 2) {
      if (!parseNonNegLong(Arg.drop_front(2), Tab) || Tab == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -a / comma lists stay on the host tool
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false; // stdin

  bool Failed = false;
  const size_t TabStop = static_cast<size_t>(Tab);
  for (const std::string &Path : expandOperands(Files)) {
    std::unique_ptr<llvm::MemoryBuffer> Buf;
    llvm::SmallVector<llvm::StringRef, 32> Lines;
    std::string Error;
    if (!readFileLines(Path, Lines, Buf, Error)) {
      llvm::errs() << "neverc make: unexpand: " << Path << ": " << Error
                   << "\n";
      Failed = true;
      continue;
    }
    for (llvm::StringRef Line : Lines) {
      // POSIX default unexpand: only convert leading blanks to tabs.
      size_t Col = 0;
      size_t I = 0;
      while (I < Line.size() && (Line[I] == ' ' || Line[I] == '\t')) {
        if (Line[I] == '\t')
          Col += TabStop - (Col % TabStop);
        else
          ++Col;
        ++I;
      }
      for (size_t T = 0; T < Col / TabStop; ++T)
        llvm::outs() << '\t';
      for (size_t S = 0; S < Col % TabStop; ++S)
        llvm::outs() << ' ';
      llvm::outs() << Line.drop_front(I) << '\n';
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool rewriteLineEndings(llvm::StringRef Path, bool ToUnix, std::string &Error) {
  auto Buf = llvm::MemoryBuffer::getFile(Path);
  if (!Buf) {
    Error = Buf.getError().message();
    return false;
  }
  llvm::StringRef In = (*Buf)->getBuffer();
  std::string Out;
  Out.reserve(In.size() + (ToUnix ? 0 : In.size() / 16));
  if (ToUnix) {
    for (size_t I = 0; I < In.size(); ++I) {
      if (In[I] == '\r' && I + 1 < In.size() && In[I + 1] == '\n')
        continue; // drop CR from CRLF
      if (In[I] == '\r') {
        Out.push_back('\n'); // lone CR → LF
        continue;
      }
      Out.push_back(In[I]);
    }
  } else {
    for (size_t I = 0; I < In.size(); ++I) {
      if (In[I] == '\n' && (I == 0 || In[I - 1] != '\r'))
        Out.push_back('\r');
      Out.push_back(In[I]);
    }
  }
  std::error_code EC;
  llvm::raw_fd_ostream OS(Path, EC, llvm::sys::fs::OF_None);
  if (EC) {
    Error = EC.message();
    return false;
  }
  OS << Out;
  OS.flush();
  if (OS.has_error()) {
    Error = "write failed";
    OS.clear_error();
    return false;
  }
  return true;
}


bool tryExecuteDos2Unix(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: in-place CRLF/CR → LF for file operands (no flags/stdin).
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    std::string Error;
    if (!rewriteLineEndings(Path, /*ToUnix=*/true, Error)) {
      llvm::errs() << "neverc make: dos2unix: " << Path << ": " << Error
                   << "\n";
      Failed = true;
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteUnix2Dos(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: in-place LF → CRLF for file operands (no flags/stdin).
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    std::string Error;
    if (!rewriteLineEndings(Path, /*ToUnix=*/false, Error)) {
      llvm::errs() << "neverc make: unix2dos: " << Path << ": " << Error
                   << "\n";
      Failed = true;
    }
  }
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteShuf(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `shuf FILE` (one file, no flags / no stdin).
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  llvm::SmallVector<Token, 2> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    if (llvm::StringRef(Argv[I].Text).starts_with("-") &&
        Argv[I].Text != "-")
      return false; // -n/-i/-e/-r stay on the host tool
    Files.push_back(Argv[I]);
  }
  if (Files.size() != 1 || hasStdinDashOperand(Files))
    return false;

  std::unique_ptr<llvm::MemoryBuffer> Buf;
  llvm::SmallVector<llvm::StringRef, 64> Lines;
  std::string Error;
  if (!readFileLines(Files[0].Text, Lines, Buf, Error)) {
    llvm::errs() << "neverc make: shuf: " << Files[0].Text << ": " << Error
                 << "\n";
    ExitCode = 1;
    return true;
  }
  std::vector<llvm::StringRef> Shuffled(Lines.begin(), Lines.end());
  // Deterministic Fisher–Yates with a fixed seed so recipe output is stable
  // across runs (Makefiles rarely want true entropy from shuf).
  uint64_t State = 0x9e3779b97f4a7c15ULL ^ static_cast<uint64_t>(Shuffled.size());
  auto next = [&]() {
    State ^= State << 7;
    State ^= State >> 9;
    State ^= State << 8;
    return State;
  };
  for (size_t I = Shuffled.size(); I > 1; --I) {
    const size_t J = static_cast<size_t>(next() % I);
    std::swap(Shuffled[I - 1], Shuffled[J]);
  }
  for (llvm::StringRef Line : Shuffled)
    llvm::outs() << Line << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteJoin(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `join FILE1 FILE2` on whitespace field-1 sorted inputs.
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -t/-1/-2/-a/-v stay on the host tool
    Files.push_back(Argv[I]);
  }
  if (Files.size() != 2 || hasStdinDashOperand(Files))
    return false;

  auto load = [](llvm::StringRef Path,
                 llvm::SmallVectorImpl<llvm::StringRef> &Lines,
                 std::unique_ptr<llvm::MemoryBuffer> &Buf,
                 std::string &Error) -> bool {
    return readFileLines(Path, Lines, Buf, Error);
  };

  std::unique_ptr<llvm::MemoryBuffer> BufA;
  std::unique_ptr<llvm::MemoryBuffer> BufB;
  llvm::SmallVector<llvm::StringRef, 64> A;
  llvm::SmallVector<llvm::StringRef, 64> B;
  std::string Error;
  if (!load(Files[0].Text, A, BufA, Error)) {
    llvm::errs() << "neverc make: join: " << Files[0].Text << ": " << Error
                 << "\n";
    ExitCode = 1;
    return true;
  }
  if (!load(Files[1].Text, B, BufB, Error)) {
    llvm::errs() << "neverc make: join: " << Files[1].Text << ": " << Error
                 << "\n";
    ExitCode = 1;
    return true;
  }

  auto keyAndRest = [](llvm::StringRef Line, llvm::StringRef &Key,
                       llvm::StringRef &Rest) {
    Line = Line.ltrim(" \t");
    const size_t Sp = Line.find_first_of(" \t");
    if (Sp == llvm::StringRef::npos) {
      Key = Line;
      Rest = {};
      return;
    }
    Key = Line.take_front(Sp);
    Rest = Line.drop_front(Sp).ltrim(" \t");
  };

  size_t I = 0;
  size_t J = 0;
  while (I < A.size() && J < B.size()) {
    llvm::StringRef KeyA;
    llvm::StringRef RestA;
    llvm::StringRef KeyB;
    llvm::StringRef RestB;
    keyAndRest(A[I], KeyA, RestA);
    keyAndRest(B[J], KeyB, RestB);
    if (KeyA == KeyB) {
      // POSIX join: equal keys form a Cartesian product of the two runs.
      size_t IEnd = I + 1;
      size_t JEnd = J + 1;
      while (IEnd < A.size()) {
        llvm::StringRef K;
        llvm::StringRef R;
        keyAndRest(A[IEnd], K, R);
        if (K != KeyA)
          break;
        ++IEnd;
      }
      while (JEnd < B.size()) {
        llvm::StringRef K;
        llvm::StringRef R;
        keyAndRest(B[JEnd], K, R);
        if (K != KeyB)
          break;
        ++JEnd;
      }
      for (size_t AI = I; AI < IEnd; ++AI) {
        llvm::StringRef KA;
        llvm::StringRef RA;
        keyAndRest(A[AI], KA, RA);
        for (size_t BJ = J; BJ < JEnd; ++BJ) {
          llvm::StringRef KB;
          llvm::StringRef RB;
          keyAndRest(B[BJ], KB, RB);
          llvm::outs() << KA;
          if (!RA.empty())
            llvm::outs() << ' ' << RA;
          if (!RB.empty())
            llvm::outs() << ' ' << RB;
          llvm::outs() << '\n';
        }
      }
      I = IEnd;
      J = JEnd;
    } else if (KeyA < KeyB) {
      ++I;
    } else {
      ++J;
    }
  }
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool tryExecuteXxd(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `xxd -p [-c N] FILE...` (plain hex dump; no stdin).
  bool Plain = false;
  long Cols = 32; // GNU xxd -p default
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-p" || Arg == "-ps" || Arg == "-postscript" ||
        Arg == "-plain") {
      Plain = true;
      continue;
    }
    if (Arg == "-c") {
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: xxd: option requires an argument -- c\n";
        ExitCode = 1;
        return true;
      }
      if (!parseNonNegLong(Argv[++I].Text, Cols) || Cols == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("-c") && Arg.size() > 2) {
      if (!parseNonNegLong(Arg.drop_front(2), Cols) || Cols == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  if (!Plain || Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  const size_t Width = static_cast<size_t>(Cols);
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: xxd: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::StringRef Data = (*Buf)->getBuffer();
    for (size_t Off = 0; Off < Data.size(); Off += Width) {
      const size_t N = std::min(Width, Data.size() - Off);
      for (size_t I = 0; I < N; ++I) {
        char Hex[3];
        std::snprintf(Hex, sizeof(Hex), "%02x",
                      static_cast<unsigned char>(Data[Off + I]));
        llvm::outs() << Hex;
      }
      llvm::outs() << '\n';
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteHexdump(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `hexdump -C FILE...` (canonical hex+ASCII; no stdin).
  bool Canonical = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "-C") {
      Canonical = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -n/-s/-v/format strings stay on the host tool
    Files.push_back(Argv[I]);
  }
  if (!Canonical || Files.empty() || hasStdinDashOperand(Files))
    return false;

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: hexdump: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    llvm::StringRef Data = (*Buf)->getBuffer();
    for (size_t Off = 0; Off < Data.size(); Off += 16) {
      const size_t N = std::min<size_t>(16, Data.size() - Off);
      char Addr[16];
      std::snprintf(Addr, sizeof(Addr), "%08llx",
                    static_cast<unsigned long long>(Off));
      llvm::outs() << Addr << "  ";
      for (size_t I = 0; I < 16; ++I) {
        if (I == 8)
          llvm::outs() << ' ';
        if (I < N) {
          char Hex[3];
          std::snprintf(Hex, sizeof(Hex), "%02x",
                        static_cast<unsigned char>(Data[Off + I]));
          llvm::outs() << Hex;
        } else {
          llvm::outs() << "  ";
        }
        llvm::outs() << ' ';
      }
      llvm::outs() << " |";
      for (size_t I = 0; I < N; ++I) {
        const unsigned char C = static_cast<unsigned char>(Data[Off + I]);
        llvm::outs() << ((C >= 32 && C < 127) ? static_cast<char>(C) : '.');
      }
      llvm::outs() << "|\n";
    }
    if (!Data.empty()) {
      char Addr[16];
      std::snprintf(Addr, sizeof(Addr), "%08llx",
                    static_cast<unsigned long long>(Data.size()));
      llvm::outs() << Addr << '\n';
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


/// Parse a single `s<delim><pat><delim><repl><delim>[g]` expression.
/// Returns false when the form is outside the portable subset we claim.
bool parseSedSubstitute(llvm::StringRef Expr, std::string &Pattern,
                        std::string &Replacement, bool &Global) {
  if (Expr.size() < 4 || Expr[0] != 's')
    return false;
  const char Delim = Expr[1];
  if (Delim == '\0' || Delim == '\\' || Delim == '\n')
    return false;
  size_t I = 2;
  auto takeUntilDelim = [&](std::string &Out) -> bool {
    Out.clear();
    while (I < Expr.size()) {
      const char C = Expr[I];
      if (C == Delim) {
        ++I;
        return true;
      }
      if (C == '\\') {
        // Keep escapes so llvm::Regex / replacement processing can see them.
        if (I + 1 >= Expr.size())
          return false;
        Out.push_back(C);
        Out.push_back(Expr[I + 1]);
        I += 2;
        continue;
      }
      Out.push_back(C);
      ++I;
    }
    return false;
  };
  if (!takeUntilDelim(Pattern) || !takeUntilDelim(Replacement))
    return false;
  Global = false;
  while (I < Expr.size()) {
    switch (Expr[I]) {
    case 'g':
      Global = true;
      break;
    case 'p':
    case 'i':
    case 'I':
    case 'm':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      // Numbered/print/ignore-case forms stay on the host sed.
      return false;
    default:
      return false;
    }
    ++I;
  }
  // Empty patterns are legal in GNU sed but match every position; leave them
  // to the host tool so we do not invent surprising expansions.
  if (Pattern.empty())
    return false;
  // llvm::Regex is ERE, while sed(1) defaults to BRE. Reject patterns where
  // the two dialects disagree so we never claim a command and "succeed" with
  // host-incompatible substitutions (e.g. unescaped `+` / `|` / `()`).
  for (size_t Pi = 0; Pi < Pattern.size(); ++Pi) {
    const char C = Pattern[Pi];
    if (C == '\\') {
      if (Pi + 1 >= Pattern.size())
        return false;
      const char N = Pattern[Pi + 1];
      // Allowed escapes: \\ \. \* \[ \] \^ \$ \n \t
      // BRE grouping/counts/backrefs and other escapes stay on host sed.
      const bool Allowed = N == '\\' || N == '.' || N == '*' || N == '[' ||
                           N == ']' || N == '^' || N == '$' || N == 'n' ||
                           N == 't';
      if (!Allowed)
        return false;
      ++Pi;
      continue;
    }
    if (C == '+' || C == '?' || C == '|' || C == '(' || C == ')' || C == '{' ||
        C == '}')
      return false;
  }
  return true;
}


bool applySedReplacement(llvm::StringRef Repl, llvm::StringRef Match,
                         std::string &Out) {
  Out.clear();
  for (size_t I = 0; I < Repl.size(); ++I) {
    const char C = Repl[I];
    if (C == '&') {
      Out += Match;
      continue;
    }
    if (C == '\\' && I + 1 < Repl.size()) {
      const char N = Repl[I + 1];
      if (N == '&' || N == '\\') {
        Out.push_back(N);
        ++I;
        continue;
      }
      // Backrefs (`\1`) and other escapes stay on the host tool.
      return false;
    }
    Out.push_back(C);
  }
  return true;
}


bool sedSubstituteLine(llvm::StringRef Line, llvm::Regex &Re,
                       llvm::StringRef Replacement, bool Global,
                       std::string &Out, std::string &Error) {
  Out.clear();
  auto replaceOnce = [&](llvm::StringRef Match, std::string &ReplOut) -> bool {
    if (!applySedReplacement(Replacement, Match, ReplOut)) {
      Error = "unsupported replacement escape";
      return false;
    }
    return true;
  };

  if (!Global) {
    llvm::SmallVector<llvm::StringRef, 2> Matches;
    if (!Re.match(Line, &Matches) || Matches.empty()) {
      Out = Line.str();
      return true;
    }
    const llvm::StringRef Match = Matches[0];
    const size_t Pos = static_cast<size_t>(Match.data() - Line.data());
    std::string Repl;
    if (!replaceOnce(Match, Repl))
      return false;
    Out = Line.take_front(Pos).str();
    Out += Repl;
    Out += Line.drop_front(Pos + Match.size());
    return true;
  }

  // Global: walk non-overlapping matches left-to-right.
  llvm::StringRef Rest = Line;
  while (!Rest.empty()) {
    llvm::SmallVector<llvm::StringRef, 2> Matches;
    if (!Re.match(Rest, &Matches) || Matches.empty()) {
      Out += Rest;
      break;
    }
    const llvm::StringRef Match = Matches[0];
    if (Match.data() < Rest.data() ||
        Match.data() > Rest.data() + Rest.size()) {
      Error = "invalid match";
      return false;
    }
    const size_t Pos = static_cast<size_t>(Match.data() - Rest.data());
    Out += Rest.take_front(Pos);
    std::string Repl;
    if (!replaceOnce(Match, Repl))
      return false;
    Out += Repl;
    Rest = Rest.drop_front(Pos + Match.size());
    if (Match.empty()) {
      // Zero-length matches would loop forever; leave to the host tool.
      Error = "zero-length match";
      return false;
    }
  }
  return true;
}


bool tryExecuteSed(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable Makefile subset:
  //   sed [-i[SUFFIX]] s/PAT/REPL/[g] FILE...
  //   sed [-i[SUFFIX]] -e s/PAT/REPL/[g] FILE...
  // BRE quirks, addresses, branching, and stdin stay on the host tool.
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;

  bool InPlace = false;
  std::string BackupSuffix;
  llvm::SmallVector<std::string, 2> Scripts;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Files.push_back(Argv[J]);
      break;
    }
    if (Arg == "-i" || Arg == "--in-place") {
      InPlace = true;
      BackupSuffix.clear();
      continue;
    }
    if (Arg.starts_with("-i") && Arg.size() > 2) {
      InPlace = true;
      BackupSuffix = Arg.drop_front(2).str();
      continue;
    }
    if (Arg == "-e") {
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: sed: option requires an argument -- e\n";
        ExitCode = 1;
        return true;
      }
      Scripts.push_back(Argv[++I].Text);
      continue;
    }
    if (Arg.starts_with("-e") && Arg.size() > 2) {
      Scripts.push_back(Arg.drop_front(2).str());
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -n/-r/-E/-f stay on the host tool
    if (Scripts.empty()) {
      Scripts.push_back(Arg.str());
      continue;
    }
    Files.push_back(Argv[I]);
  }
  if (Scripts.empty() || Files.empty() || hasStdinDashOperand(Files))
    return false;

  struct SubProg {
    std::unique_ptr<llvm::Regex> Re;
    std::string Replacement;
    bool Global = false;
  };
  llvm::SmallVector<SubProg, 2> Progs;
  for (const std::string &Script : Scripts) {
    std::string Pattern;
    std::string Replacement;
    bool Global = false;
    if (!parseSedSubstitute(Script, Pattern, Replacement, Global))
      return false;
    auto Re = std::make_unique<llvm::Regex>(Pattern, llvm::Regex::Newline);
    llvm::SmallVector<char, 64> Err;
    if (!Re->isValid(Err)) {
      llvm::errs() << "neverc make: sed: " << Err << "\n";
      ExitCode = 1;
      return true;
    }
    // Reject replacement escapes we do not implement up front.
    if (llvm::StringRef(Replacement).contains('\\')) {
      for (size_t I = 0; I < Replacement.size(); ++I) {
        if (Replacement[I] != '\\' || I + 1 >= Replacement.size())
          continue;
        const char N = Replacement[I + 1];
        if (N != '&' && N != '\\')
          return false;
        ++I;
      }
    }
    Progs.push_back(
        SubProg{std::move(Re), std::move(Replacement), Global});
  }

  struct FileResult {
    std::string Path;
    std::string Transformed;
    bool ReadFailed = false;
    std::string ReadError;
  };
  llvm::SmallVector<FileResult, 4> Results;
  for (const std::string &Path : expandOperands(Files)) {
    FileResult FR;
    FR.Path = Path;
    std::unique_ptr<llvm::MemoryBuffer> Buf;
    llvm::SmallVector<llvm::StringRef, 64> Lines;
    std::string Error;
    bool EndsWithNewline = false;
    if (!readFileLines(Path, Lines, Buf, Error, &EndsWithNewline)) {
      FR.ReadFailed = true;
      FR.ReadError = Error;
      Results.push_back(std::move(FR));
      continue;
    }

    for (size_t Li = 0; Li < Lines.size(); ++Li) {
      std::string Cur = Lines[Li].str();
      for (SubProg &Prog : Progs) {
        std::string Next;
        std::string SubErr;
        if (!sedSubstituteLine(Cur, *Prog.Re, Prog.Replacement, Prog.Global,
                               Next, SubErr)) {
          // Unsupported construct discovered at match time — fall back before
          // any stdout/in-place write so the host sed remains authoritative.
          return false;
        }
        Cur = std::move(Next);
      }
      FR.Transformed += Cur;
      if (Li + 1 < Lines.size() || EndsWithNewline)
        FR.Transformed.push_back('\n');
    }
    Results.push_back(std::move(FR));
  }

  bool Failed = false;
  for (const FileResult &FR : Results) {
    if (FR.ReadFailed) {
      llvm::errs() << "neverc make: sed: " << FR.Path << ": " << FR.ReadError
                   << "\n";
      Failed = true;
      continue;
    }
    if (InPlace) {
      if (!BackupSuffix.empty()) {
        const std::string Backup = FR.Path + BackupSuffix;
        if (std::error_code EC = llvm::sys::fs::copy_file(FR.Path, Backup)) {
          llvm::errs() << "neverc make: sed: " << Backup << ": "
                       << EC.message() << "\n";
          Failed = true;
          continue;
        }
      }
      // Write via a temp file so we never truncate a MemoryBuffer-mapped path.
      llvm::SmallString<256> Tmp(FR.Path);
      Tmp += ".neverc-sed-%%%%%%";
      int TmpFD = -1;
      if (std::error_code EC =
              llvm::sys::fs::createUniqueFile(Tmp, TmpFD, Tmp)) {
        llvm::errs() << "neverc make: sed: " << FR.Path << ": " << EC.message()
                     << "\n";
        Failed = true;
        continue;
      }
      {
        llvm::raw_fd_ostream Out(TmpFD, /*shouldClose=*/true);
        Out << FR.Transformed;
        if (Out.has_error()) {
          llvm::errs() << "neverc make: sed: " << Tmp << ": write failed\n";
          llvm::sys::fs::remove(Tmp);
          Failed = true;
          continue;
        }
      }
      if (std::error_code EC = llvm::sys::fs::rename(Tmp, FR.Path)) {
        // Windows cannot always rename over an existing file; remove+retry.
        if (std::error_code RmEC = llvm::sys::fs::remove(FR.Path)) {
          llvm::errs() << "neverc make: sed: " << FR.Path << ": "
                       << RmEC.message() << "\n";
          llvm::sys::fs::remove(Tmp);
          Failed = true;
          continue;
        }
        if (std::error_code RetryEC = llvm::sys::fs::rename(Tmp, FR.Path)) {
          llvm::errs() << "neverc make: sed: " << FR.Path << ": "
                       << RetryEC.message() << "\n";
          llvm::sys::fs::remove(Tmp);
          Failed = true;
          continue;
        }
      }
    } else {
      llvm::outs() << FR.Transformed;
    }
  }
  if (!InPlace)
    llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteFmt(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `fmt [-w WIDTH] FILE...` (simple greedy word wrap).
  // stdin / complex options (`-c`/`-s`/`-p`) stay on the host tool.
  long Width = 75;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Files.push_back(Argv[J]);
      break;
    }
    if (Arg == "-w") {
      if (I + 1 >= Argv.size()) {
        llvm::errs() << "neverc make: fmt: option requires an argument -- w\n";
        ExitCode = 1;
        return true;
      }
      if (!parseNonNegLong(Argv[++I].Text, Width) || Width == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("-w") && Arg.size() > 2) {
      if (!parseNonNegLong(Arg.drop_front(2), Width) || Width == 0)
        return false;
      continue;
    }
    // Historic `fmt -WIDTH`.
    if (Arg.starts_with("-") && Arg.size() > 1 &&
        llvm::all_of(Arg.drop_front(),
                     [](char C) { return C >= '0' && C <= '9'; })) {
      if (!parseNonNegLong(Arg.drop_front(), Width) || Width == 0)
        return false;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  const size_t MaxWidth = static_cast<size_t>(Width);
  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    std::unique_ptr<llvm::MemoryBuffer> Buf;
    llvm::SmallVector<llvm::StringRef, 32> Lines;
    std::string Error;
    if (!readFileLines(Path, Lines, Buf, Error)) {
      llvm::errs() << "neverc make: fmt: " << Path << ": " << Error << "\n";
      Failed = true;
      continue;
    }
    std::string OutLine;
    auto flush = [&]() {
      if (!OutLine.empty()) {
        llvm::outs() << OutLine << '\n';
        OutLine.clear();
      }
    };
    for (llvm::StringRef Line : Lines) {
      if (Line.empty()) {
        flush();
        llvm::outs() << '\n';
        continue;
      }
      size_t I = 0;
      while (I < Line.size()) {
        while (I < Line.size() && (Line[I] == ' ' || Line[I] == '\t'))
          ++I;
        if (I >= Line.size())
          break;
        size_t J = I;
        while (J < Line.size() && Line[J] != ' ' && Line[J] != '\t')
          ++J;
        const llvm::StringRef Word = Line.slice(I, J);
        if (OutLine.empty()) {
          OutLine = Word.str();
        } else if (OutLine.size() + 1 + Word.size() <= MaxWidth) {
          OutLine.push_back(' ');
          OutLine += Word;
        } else {
          flush();
          OutLine = Word.str();
        }
        I = J;
      }
    }
    flush();
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


bool tryExecuteTsort(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `tsort FILE` (one file; whitespace-separated pairs).
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  llvm::SmallVector<Token, 2> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Files.push_back(Argv[J]);
      break;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  if (Files.size() != 1 || hasStdinDashOperand(Files))
    return false;

  std::unique_ptr<llvm::MemoryBuffer> Buf;
  llvm::SmallVector<llvm::StringRef, 64> Lines;
  std::string Error;
  if (!readFileLines(Files[0].Text, Lines, Buf, Error)) {
    llvm::errs() << "neverc make: tsort: " << Files[0].Text << ": " << Error
                 << "\n";
    ExitCode = 1;
    return true;
  }

  // Preserve first-seen order for deterministic output.
  std::vector<std::string> Nodes;
  llvm::StringMap<unsigned> Index;
  std::vector<std::vector<unsigned>> Adj;
  std::vector<unsigned> InDegree;

  auto getId = [&](llvm::StringRef Name) -> unsigned {
    auto It = Index.find(Name);
    if (It != Index.end())
      return It->second;
    const unsigned Id = static_cast<unsigned>(Nodes.size());
    Nodes.push_back(Name.str());
    Index[Name] = Id;
    Adj.emplace_back();
    InDegree.push_back(0);
    return Id;
  };

  for (llvm::StringRef Line : Lines) {
    llvm::SmallVector<llvm::StringRef, 8> Tok;
    Line.split(Tok, ' ', -1, /*KeepEmpty=*/false);
    // Also split on tabs by rescanning whitespace-separated tokens.
    llvm::SmallVector<llvm::StringRef, 8> Words;
    for (llvm::StringRef T : Tok) {
      llvm::SmallVector<llvm::StringRef, 4> Parts;
      T.split(Parts, '\t', -1, /*KeepEmpty=*/false);
      for (llvm::StringRef P : Parts)
        if (!P.empty())
          Words.push_back(P);
    }
    if (Words.empty())
      continue;
    if (Words.size() % 2 != 0) {
      llvm::errs() << "neverc make: tsort: odd number of tokens\n";
      ExitCode = 1;
      return true;
    }
    for (size_t I = 0; I + 1 < Words.size(); I += 2) {
      const unsigned A = getId(Words[I]);
      const unsigned B = getId(Words[I + 1]);
      if (A == B)
        continue; // self-edge is a no-op for ordering
      Adj[A].push_back(B);
      ++InDegree[B];
    }
  }

  // Kahn's algorithm with a stable queue ordered by first appearance.
  std::vector<unsigned> Queue;
  for (unsigned I = 0; I < Nodes.size(); ++I)
    if (InDegree[I] == 0)
      Queue.push_back(I);
  std::vector<std::string> Ordered;
  size_t QHead = 0;
  while (QHead < Queue.size()) {
    const unsigned U = Queue[QHead++];
    Ordered.push_back(Nodes[U]);
    for (unsigned V : Adj[U]) {
      if (--InDegree[V] == 0)
        Queue.push_back(V);
    }
  }
  if (Ordered.size() != Nodes.size()) {
    llvm::errs() << "neverc make: tsort: input contains a loop\n";
    ExitCode = 1;
    return true;
  }
  for (const std::string &N : Ordered)
    llvm::outs() << N << '\n';
  llvm::outs().flush();
  ExitCode = 0;
  return true;
}


bool expandTrSet(llvm::StringRef Spec, std::string &Out) {
  // Small portable SET subset: literals, \\ \n \t \r, and c-c ranges.
  // Complement (`-c`), classes (`[:space:]`), and equivalence classes stay on
  // the host tool.
  Out.clear();
  for (size_t I = 0; I < Spec.size(); ++I) {
    char C = Spec[I];
    if (C == '\\') {
      if (I + 1 >= Spec.size())
        return false;
      switch (Spec[I + 1]) {
      case '\\':
        C = '\\';
        break;
      case 'n':
        C = '\n';
        break;
      case 't':
        C = '\t';
        break;
      case 'r':
        C = '\r';
        break;
      default:
        return false;
      }
      ++I;
    } else if (I + 2 < Spec.size() && Spec[I + 1] == '-') {
      const char Lo = C;
      const char Hi = Spec[I + 2];
      if (static_cast<unsigned char>(Lo) > static_cast<unsigned char>(Hi))
        return false;
      for (unsigned Ch = static_cast<unsigned char>(Lo);
           Ch <= static_cast<unsigned char>(Hi); ++Ch)
        Out.push_back(static_cast<char>(Ch));
      I += 2;
      continue;
    }
    Out.push_back(C);
  }
  return true;
}


bool tryExecuteTr(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable file-operand form (stdin/pipes still fall back via tokenize):
  //   tr -d SET FILE...
  //   tr SET1 SET2 FILE...
  // GNU tr has no file operands; this claims an explicit NeverC recipe form so
  // common CRLF / case transforms can run in-process without a shell redirect.
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;

  bool Delete = false;
  llvm::SmallVector<Token, 4> Operands;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Operands.push_back(Argv[J]);
      break;
    }
    if (Arg == "-d") {
      Delete = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false; // -c/-s/-C stay on the host tool
    Operands.push_back(Argv[I]);
  }

  if (Delete) {
    if (Operands.size() < 2)
      return false;
  } else if (Operands.size() < 3) {
    return false;
  }

  std::string Set1;
  std::string Set2;
  llvm::ArrayRef<Token> Files;
  if (Delete) {
    if (!expandTrSet(Operands[0].Text, Set1) || Set1.empty())
      return false;
    Files = llvm::ArrayRef<Token>(Operands).drop_front();
  } else {
    if (!expandTrSet(Operands[0].Text, Set1) || Set1.empty() ||
        !expandTrSet(Operands[1].Text, Set2) || Set2.empty())
      return false;
    Files = llvm::ArrayRef<Token>(Operands).drop_front(2);
  }
  if (Files.empty() || hasStdinDashOperand(Files))
    return false;

  std::array<bool, 256> DeleteMap{};
  std::array<unsigned char, 256> Map{};
  for (unsigned I = 0; I < 256; ++I)
    Map[I] = static_cast<unsigned char>(I);
  if (Delete) {
    for (unsigned char C : Set1)
      DeleteMap[C] = true;
  } else {
    // GNU: when SET2 is shorter, repeat its last character; when longer, ignore
    // the excess. Empty SET2 is rejected above.
    for (size_t I = 0; I < Set1.size(); ++I) {
      const size_t J = I < Set2.size() ? I : Set2.size() - 1;
      Map[static_cast<unsigned char>(Set1[I])] =
          static_cast<unsigned char>(Set2[J]);
    }
  }

  bool Failed = false;
  for (const std::string &Path : expandOperands(Files)) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      llvm::errs() << "neverc make: tr: " << Path << ": "
                   << Buf.getError().message() << "\n";
      Failed = true;
      continue;
    }
    for (unsigned char C : (*Buf)->getBuffer()) {
      if (Delete) {
        if (!DeleteMap[C])
          llvm::outs() << static_cast<char>(C);
      } else {
        llvm::outs() << static_cast<char>(Map[C]);
      }
    }
  }
  llvm::outs().flush();
  ExitCode = Failed ? 1 : 0;
  return true;
}


} // namespace internal
} // namespace builtins
} // namespace build
} // namespace neverc
