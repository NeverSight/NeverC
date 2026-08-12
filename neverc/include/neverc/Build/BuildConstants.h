#ifndef NEVERC_BUILD_BUILDCONSTANTS_H
#define NEVERC_BUILD_BUILDCONSTANTS_H

#include "llvm/ADT/StringRef.h"

namespace neverc {
namespace build {
namespace constants {

constexpr llvm::StringLiteral ToolName = "neverc make";
constexpr llvm::StringLiteral ErrorPrefix = "neverc make: *** ";

constexpr llvm::StringLiteral TargetPhony = ".PHONY";
constexpr llvm::StringLiteral TargetSuffixes = ".SUFFIXES";
constexpr llvm::StringLiteral TargetDefaultGoal = ".DEFAULT_GOAL";

constexpr llvm::StringLiteral VarCurdir = "CURDIR";
constexpr llvm::StringLiteral VarMake = "MAKE";
constexpr llvm::StringLiteral VarShell = "SHELL";
constexpr llvm::StringLiteral VarShellFlags = ".SHELLFLAGS";
constexpr llvm::StringLiteral VarMakefileList = "MAKEFILE_LIST";
constexpr llvm::StringLiteral VarMakeVersion = "MAKE_VERSION";
constexpr llvm::StringLiteral VarMakeFlags = "MAKEFLAGS";
constexpr llvm::StringLiteral VarMakeCmdGoals = "MAKECMDGOALS";
constexpr llvm::StringLiteral VarNeverCMakeExecutable =
    "NEVERC_MAKE_EXECUTABLE";

constexpr llvm::StringLiteral MakeVersionValue = "4.3";
constexpr llvm::StringLiteral ShellFlagsDefault = "-c";

#ifdef _WIN32
constexpr llvm::StringLiteral DefaultShell = "cmd.exe";
#else
constexpr llvm::StringLiteral DefaultShell = "/bin/sh";
#endif

constexpr llvm::StringLiteral EvalFilename = "<eval>";

constexpr unsigned MaxRecursionDepth = 256;
constexpr unsigned MaxPositionalArgs = 9;
constexpr unsigned ShellBufSize = 4096;

constexpr llvm::StringLiteral DefaultMakefiles[] = {
    "GNUmakefile", "makefile", "Makefile"};

} // namespace constants
} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_BUILDCONSTANTS_H
