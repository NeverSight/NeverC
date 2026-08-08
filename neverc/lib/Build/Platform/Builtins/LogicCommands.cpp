#include "Platform/Builtins/Internal.h"

#include "neverc/Build/Platform.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#else
#include <unistd.h>
#endif

namespace neverc {
namespace build {
namespace builtins {
namespace internal {


bool tryExecuteSleep(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  if (Argv.size() != 2) {
    llvm::errs() << "neverc make: sleep: expected one operand\n";
    ExitCode = 1;
    return true;
  }

  char *End = nullptr;
  const double Seconds = std::strtod(Argv[1].Text.c_str(), &End);
  if (End == Argv[1].Text.c_str() || (End && *End != '\0') ||
      !std::isfinite(Seconds) || Seconds < 0 ||
      Seconds > static_cast<double>(std::numeric_limits<int64_t>::max() /
                                    1000000000LL)) {
    llvm::errs() << "neverc make: sleep: invalid time interval '"
                 << Argv[1].Text << "'\n";
    ExitCode = 1;
    return true;
  }

  const auto Nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::duration<double>(Seconds));
  std::this_thread::sleep_for(Nanos);
  ExitCode = 0;
  return true;
}


bool tryExecuteTrue(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  (void)Argv;
  ExitCode = 0;
  return true;
}


bool tryExecuteFalse(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  (void)Argv;
  ExitCode = 1;
  return true;
}


bool tryExecuteColon(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  (void)Argv;
  ExitCode = 0;
  return true;
}


bool parseTestInteger(llvm::StringRef Text, long &Value) {
  if (Text.empty())
    return false;
  char *End = nullptr;
  const std::string S = Text.str();
  errno = 0;
  const long Parsed = std::strtol(S.c_str(), &End, 10);
  if (End == S.c_str() || (End && *End != '\0') || errno == ERANGE)
    return false;
  Value = Parsed;
  return true;
}


/// Evaluate a small POSIX `test`/`[` expression subset used by recipes.
/// Returns false when the expression needs a real shell/test implementation.
bool evalTestExpr(llvm::ArrayRef<Token> Expr, bool &Result) {
  if (Expr.empty()) {
    Result = false;
    return true;
  }

  // Resolve connectors before stripping leading `!`. POSIX gives `!` higher
  // precedence than `-a`/`-o`, so `! A -a B` must mean `(! A) -a B`, not
  // `!(A -a B)`. Recursing into each side preserves that binding.
  // Nested connectors stay on the host tool (single-connector subset only).
  for (size_t K = 1; K + 1 < Expr.size(); ++K) {
    if (Expr[K].Text != "-a" && Expr[K].Text != "-o")
      continue;
    auto sideOk = [](llvm::ArrayRef<Token> Side) {
      for (const Token &T : Side)
        if (T.Text == "-a" || T.Text == "-o")
          return false;
      return !Side.empty();
    };
    const llvm::ArrayRef<Token> Left = Expr.take_front(K);
    const llvm::ArrayRef<Token> Right = Expr.drop_front(K + 1);
    if (!sideOk(Left) || !sideOk(Right))
      return false;
    bool L = false;
    bool R = false;
    if (!evalTestExpr(Left, L) || !evalTestExpr(Right, R))
      return false;
    Result = Expr[K].Text == "-a" ? (L && R) : (L || R);
    return true;
  }

  size_t I = 0;
  bool Negate = false;
  while (I < Expr.size() && Expr[I].Text == "!") {
    Negate = !Negate;
    ++I;
  }
  if (I >= Expr.size())
    return false;

  auto finish = [&](bool Value) {
    Result = Negate ? !Value : Value;
    return true;
  };

  llvm::StringRef A = Expr[I].Text;
  const size_t Remain = Expr.size() - I;

  if (Remain == 1)
    return finish(!A.empty());

  if (Remain == 2) {
    llvm::StringRef Op = A;
    llvm::StringRef Operand = Expr[I + 1].Text;
    if (Op == "-z")
      return finish(Operand.empty());
    if (Op == "-n")
      return finish(!Operand.empty());
    if (Op == "-e")
      return finish(llvm::sys::fs::exists(Operand));
    if (Op == "-f") {
      llvm::sys::fs::file_status Status;
      if (llvm::sys::fs::status(Operand, Status))
        return finish(false);
      return finish(Status.type() == llvm::sys::fs::file_type::regular_file);
    }
    if (Op == "-d")
      return finish(isDirectory(Operand));
    if (Op == "-h" || Op == "-L") {
      llvm::sys::fs::file_status Status;
      if (llvm::sys::fs::status(Operand, Status, /*follow=*/false))
        return finish(false);
      return finish(Status.type() == llvm::sys::fs::file_type::symlink_file);
    }
    if (Op == "-s") {
      llvm::sys::fs::file_status Status;
      if (llvm::sys::fs::status(Operand, Status))
        return finish(false);
      return finish(Status.getSize() > 0);
    }
    if (Op == "-r") {
#ifndef _WIN32
      return finish(::access(Operand.str().c_str(), R_OK) == 0);
#else
      // Approximate readable as "exists" when the CRT has no R_OK probe.
      return finish(llvm::sys::fs::exists(Operand));
#endif
    }
    if (Op == "-w")
      return finish(llvm::sys::fs::can_write(Operand));
    if (Op == "-x")
      return finish(llvm::sys::fs::can_execute(Operand));
    return false;
  }

  if (Remain == 3) {
    llvm::StringRef LHS = A;
    llvm::StringRef Op = Expr[I + 1].Text;
    llvm::StringRef RHS = Expr[I + 2].Text;
    if (Op == "=" || Op == "==")
      return finish(LHS == RHS);
    if (Op == "!=")
      return finish(LHS != RHS);
    if (Op == "-nt" || Op == "-ot") {
      // Newer-than / older-than: missing files make the primary false.
      const int64_t LT = platform::getFileTimestamp(LHS.str());
      const int64_t RT = platform::getFileTimestamp(RHS.str());
      if (LT < 0 || RT < 0)
        return finish(false);
      if (Op == "-nt")
        return finish(LT > RT);
      return finish(LT < RT);
    }
    if (Op == "-ef") {
      // Same device+inode (follows symlinks), matching POSIX test -ef.
      bool Equivalent = false;
      if (!llvm::sys::fs::equivalent(LHS, RHS, Equivalent) && Equivalent)
        return finish(true);
      return finish(false);
    }

    long L = 0;
    long R = 0;
    if (!parseTestInteger(LHS, L) || !parseTestInteger(RHS, R))
      return false;
    if (Op == "-eq")
      return finish(L == R);
    if (Op == "-ne")
      return finish(L != R);
    if (Op == "-gt")
      return finish(L > R);
    if (Op == "-ge")
      return finish(L >= R);
    if (Op == "-lt")
      return finish(L < R);
    if (Op == "-le")
      return finish(L <= R);
    return false;
  }

  return false;
}


bool tryExecuteTest(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Glob expansion changes `test` arity; leave unquoted globs to /bin/sh.
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  llvm::ArrayRef<Token> Expr = Argv.drop_front();
  if (Argv[0].Text == "[") {
    if (Expr.empty() || Expr.back().Text != "]") {
      llvm::errs() << "neverc make: [: missing ']'\n";
      ExitCode = 2;
      return true;
    }
    Expr = Expr.drop_back();
  }

  bool Result = false;
  if (!evalTestExpr(Expr, Result))
    return false;
  ExitCode = Result ? 0 : 1;
  return true;
}


bool tryExecuteCmp(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  bool Silent = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg == "--") {
      for (size_t J = I + 1; J < Argv.size(); ++J)
        Files.push_back(Argv[J]);
      break;
    }
    if (Arg == "-s" || Arg == "--quiet" || Arg == "--silent") {
      Silent = true;
      continue;
    }
    if (Arg.starts_with("-") && Arg != "-")
      return false;
    Files.push_back(Argv[I]);
  }
  if (hasStdinDashOperand(Files))
    return false;
  if (Files.size() != 2) {
    llvm::errs() << "neverc make: cmp: expected two file operands\n";
    ExitCode = 1;
    return true;
  }

  auto A = llvm::MemoryBuffer::getFile(Files[0].Text);
  auto B = llvm::MemoryBuffer::getFile(Files[1].Text);
  if (!A) {
    if (!Silent)
      llvm::errs() << "neverc make: cmp: " << Files[0].Text << ": "
                   << A.getError().message() << "\n";
    ExitCode = 2;
    return true;
  }
  if (!B) {
    if (!Silent)
      llvm::errs() << "neverc make: cmp: " << Files[1].Text << ": "
                   << B.getError().message() << "\n";
    ExitCode = 2;
    return true;
  }
  if ((*A)->getBuffer() == (*B)->getBuffer()) {
    ExitCode = 0;
    return true;
  }
  if (!Silent)
    llvm::errs() << "neverc make: cmp: " << Files[0].Text << " "
                 << Files[1].Text << " differ\n";
  ExitCode = 1;
  return true;
}


bool tryExecuteExpr(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset used by Makefiles:
  //   expr INTEGER OP INTEGER   with OP in + - * / %
  //   expr length STRING
  //   expr substr STRING POS LEN   (1-based POS; LEN may extend past end)
  // Unquoted `*` is a glob in sh; only claim when the operator is quoted or is
  // a non-glob token such as '+' / '-' / '/' / '%'.
  if (Argv.size() < 2 || hasUnquotedGlob(Argv.drop_front()))
    return false;

  auto parseLong = [](llvm::StringRef Text, long &Value) {
    if (Text.empty())
      return false;
    const std::string S = Text.str();
    char *End = nullptr;
    errno = 0;
    const long Parsed = std::strtol(S.c_str(), &End, 10);
    if (End == S.c_str() || (End && *End != '\0') || errno == ERANGE)
      return false;
    Value = Parsed;
    return true;
  };
  auto finishInt = [&](long Result) {
    llvm::outs() << Result << '\n';
    llvm::outs().flush();
    ExitCode = Result == 0 ? 1 : 0; // expr exits 1 when the result is zero
    return true;
  };
  auto finishStr = [&](llvm::StringRef Result) {
    llvm::outs() << Result << '\n';
    llvm::outs().flush();
    ExitCode = Result.empty() ? 1 : 0;
    return true;
  };

  if (Argv.size() == 3 && Argv[1].Text == "length") {
    // POSIX: length is character count of the string operand.
    return finishInt(static_cast<long>(Argv[2].Text.size()));
  }

  if (Argv.size() == 5 && Argv[1].Text == "substr") {
    long Pos = 0;
    long Len = 0;
    if (!parseLong(Argv[3].Text, Pos) || !parseLong(Argv[4].Text, Len))
      return false;
    // POSIX: POS < 1 or LEN < 1 yields an empty string (exit 1).
    if (Pos < 1 || Len < 1)
      return finishStr({});
    const llvm::StringRef S = Argv[2].Text;
    if (static_cast<size_t>(Pos) > S.size())
      return finishStr({});
    const size_t Start = static_cast<size_t>(Pos - 1);
    const size_t Take =
        std::min(static_cast<size_t>(Len), S.size() - Start);
    return finishStr(S.substr(Start, Take));
  }

  if (Argv.size() != 4)
    return false;

  long LHS = 0;
  long RHS = 0;
  if (!parseLong(Argv[1].Text, LHS) || !parseLong(Argv[3].Text, RHS))
    return false;

  const llvm::StringRef Op = Argv[2].Text;
  long long Wide = 0;
  if (Op == "+") {
    Wide = static_cast<long long>(LHS) + static_cast<long long>(RHS);
  } else if (Op == "-") {
    Wide = static_cast<long long>(LHS) - static_cast<long long>(RHS);
  } else if (Op == "*" || Op == "\\*") {
    Wide = static_cast<long long>(LHS) * static_cast<long long>(RHS);
  } else if (Op == "/") {
    if (RHS == 0) {
      llvm::errs() << "neverc make: expr: division by zero\n";
      ExitCode = 2;
      return true;
    }
    Wide = static_cast<long long>(LHS) / static_cast<long long>(RHS);
  } else if (Op == "%") {
    if (RHS == 0) {
      llvm::errs() << "neverc make: expr: division by zero\n";
      ExitCode = 2;
      return true;
    }
    Wide = static_cast<long long>(LHS) % static_cast<long long>(RHS);
  } else {
    return false;
  }
  if (Wide > static_cast<long long>(std::numeric_limits<long>::max()) ||
      Wide < static_cast<long long>(std::numeric_limits<long>::min())) {
    llvm::errs() << "neverc make: expr: overflow\n";
    ExitCode = 2;
    return true;
  }
  return finishInt(static_cast<long>(Wide));
}


bool tryExecuteDiff(llvm::ArrayRef<Token> Argv, int &ExitCode) {
  // Portable subset: `diff -q FILE1 FILE2` (and clustered `-q`).
  if (hasUnquotedGlob(Argv.drop_front()))
    return false;
  bool Brief = false;
  llvm::SmallVector<Token, 4> Files;
  for (size_t I = 1; I < Argv.size(); ++I) {
    llvm::StringRef Arg = Argv[I].Text;
    if (Arg.starts_with("-") && Arg.size() > 1 &&
        !Arg.starts_with("--")) {
      for (size_t J = 1; J < Arg.size(); ++J) {
        if (Arg[J] != 'q')
          return false;
        Brief = true;
      }
      continue;
    }
    if (Arg.starts_with("-") && Arg.size() > 1)
      return false;
    Files.push_back(Argv[I]);
  }
  if (!Brief || Files.size() != 2 || hasStdinDashOperand(Files))
    return false;

  auto A = llvm::MemoryBuffer::getFile(Files[0].Text);
  auto B = llvm::MemoryBuffer::getFile(Files[1].Text);
  if (!A) {
    llvm::errs() << "neverc make: diff: " << Files[0].Text << ": "
                 << A.getError().message() << "\n";
    ExitCode = 2;
    return true;
  }
  if (!B) {
    llvm::errs() << "neverc make: diff: " << Files[1].Text << ": "
                 << B.getError().message() << "\n";
    ExitCode = 2;
    return true;
  }
  if ((*A)->getBuffer() == (*B)->getBuffer()) {
    ExitCode = 0;
    return true;
  }
  llvm::outs() << "Files " << Files[0].Text << " and " << Files[1].Text
               << " differ\n";
  llvm::outs().flush();
  ExitCode = 1;
  return true;
}


} // namespace internal
} // namespace builtins
} // namespace build
} // namespace neverc
