#ifndef NEVERC_BUILD_BUILTINS_INTERNAL_H
#define NEVERC_BUILD_BUILTINS_INTERNAL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"

#include <memory>
#include <string>
#include <vector>

namespace neverc {
namespace build {
namespace builtins {
namespace internal {

struct Token {
  std::string Text;
  /// True when any character of this argv element was quoted. Affects glob
  /// expansion only (quoted patterns stay literal). Option parsing must ignore
  /// this flag: the shell strips quotes before argv reaches the program, so
  /// `rm "-f"` and `echo "-n"` are still options.
  bool Quoted = false;
};

bool hasGlobMeta(llvm::StringRef Path);
bool tokenizeRecipe(llvm::StringRef Cmd, llvm::SmallVectorImpl<Token> &Argv);
bool hasUnquotedGlob(llvm::ArrayRef<Token> Args);
/// True when any operand is `-` (stdin / stdout placeholder). Builtins do not
/// claim stdin, so handlers should return false and leave these to /bin/sh.
bool hasStdinDashOperand(llvm::ArrayRef<Token> Operands);
bool hasStdinDashPath(llvm::ArrayRef<std::string> Paths);

std::vector<std::string> expandOperand(const Token &Operand);
llvm::SmallVector<std::string, 8>
expandOperands(llvm::ArrayRef<Token> Operands);

bool parseNonNegLong(llvm::StringRef Text, long &Value);
bool pathsEquivalent(llvm::StringRef A, llvm::StringRef B);
bool isDirectory(llvm::StringRef Path, bool Follow = true);
bool isSameOrNestedPath(llvm::StringRef Outer, llvm::StringRef Inner);

/// Split Path into lines. Empty files yield zero lines. When the file ends
/// with a newline, that terminator does not create an extra empty line.
/// If EndsWithNewline is non-null, it is set to whether the raw buffer ended
/// with '\\n' (false for empty files).
bool readFileLines(llvm::StringRef Path,
                   llvm::SmallVectorImpl<llvm::StringRef> &Lines,
                   std::unique_ptr<llvm::MemoryBuffer> &BufKeepAlive,
                   std::string &Error, bool *EndsWithNewline = nullptr);

// --- File commands ---
bool tryExecuteRm(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteMkdir(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteTouch(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteCp(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteMv(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteLn(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteLink(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteChmod(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteChown(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteChgrp(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteInstall(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteRmdir(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteUnlink(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteTruncate(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteMktemp(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteMkfifo(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteDd(llvm::ArrayRef<Token> Argv, int &ExitCode);

// --- Text commands ---
bool tryExecuteEcho(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecutePrintf(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteCat(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteHeadTail(llvm::ArrayRef<Token> Argv, int &ExitCode, bool IsTail);
/// Uniform wrappers for the BuiltinCommands.def / StringSwitch table.
bool tryExecuteHead(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteTail(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteWc(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteSort(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteUniq(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteCut(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecutePaste(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteTac(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteNl(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteRev(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteFold(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteGrep(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteEgrep(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteFgrep(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteStrings(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteSplit(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteBase64(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteOd(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteExpand(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteUnexpand(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteComm(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteJoin(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteDos2Unix(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteUnix2Dos(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteShuf(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteSed(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteFmt(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteTsort(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteTr(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteXxd(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteHexdump(llvm::ArrayRef<Token> Argv, int &ExitCode);

// --- Path commands ---
bool tryExecutePwd(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteBasename(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteDirname(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteRealpath(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteReadlink(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteLs(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteDu(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteDf(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteStat(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteFile(llvm::ArrayRef<Token> Argv, int &ExitCode);

// --- Logic / compare / sleep ---
bool tryExecuteTrue(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteFalse(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteColon(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteTest(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteExpr(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteCmp(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteDiff(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteSleep(llvm::ArrayRef<Token> Argv, int &ExitCode);

// --- Checksums ---
enum class ChecksumKind { MD5, SHA1, SHA256, SHA512 };
bool tryExecuteChecksum(llvm::ArrayRef<Token> Argv, int &ExitCode,
                        ChecksumKind Kind);
/// Uniform wrappers for the BuiltinCommands.def / StringSwitch table.
bool tryExecuteMd5sum(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteMd5(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteSha1sum(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteSha256sum(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteSha512sum(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteCksum(llvm::ArrayRef<Token> Argv, int &ExitCode);

// --- System / identity / env ---
bool tryExecuteUname(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteArch(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteHostname(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteWhoami(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteLogname(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteGroups(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteTty(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteNproc(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteDate(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecutePrintenv(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteEnv(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteId(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteSync(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteWhich(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteCommand(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteType(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteGetconf(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteSeq(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteFactor(llvm::ArrayRef<Token> Argv, int &ExitCode);
bool tryExecuteYes(llvm::ArrayRef<Token> Argv, int &ExitCode);

} // namespace internal
} // namespace builtins
} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_BUILTINS_INTERNAL_H
