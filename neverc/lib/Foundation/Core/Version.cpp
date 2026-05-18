#include "neverc/Foundation/Core/Version.h"
#include "neverc/Config/config.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdlib>

#include "VCSVersion.inc"

namespace neverc {

static constexpr llvm::StringLiteral RepoPath =
#if defined(NEVERC_REPOSITORY_STRING)
    NEVERC_REPOSITORY_STRING;
#elif defined(NEVERC_REPOSITORY)
    NEVERC_REPOSITORY;
#else
    "";
#endif

static constexpr llvm::StringLiteral LLVMRepo =
#ifdef LLVM_REPOSITORY
    LLVM_REPOSITORY;
#else
    "";
#endif

static constexpr llvm::StringLiteral NevercRev =
#ifdef NEVERC_REVISION
    NEVERC_REVISION;
#else
    "";
#endif

static constexpr llvm::StringLiteral LLVMRev =
#ifdef LLVM_REVISION
    LLVM_REVISION;
#else
    "";
#endif

static constexpr llvm::StringLiteral Vendor =
#ifdef NEVERC_VENDOR
    NEVERC_VENDOR;
#else
    "";
#endif

std::string getNeverCFullRepositoryVersion() {
  llvm::SmallString<128> Buf;
  llvm::raw_svector_ostream OS(Buf);
  if (!RepoPath.empty() || !NevercRev.empty()) {
    OS << '(';
    if (!RepoPath.empty())
      OS << RepoPath;
    if (!NevercRev.empty()) {
      if (!RepoPath.empty())
        OS << ' ';
      OS << NevercRev;
    }
    OS << ')';
  }
  if (!LLVMRev.empty() && LLVMRev != NevercRev) {
    OS << " (";
    if (!LLVMRepo.empty())
      OS << LLVMRepo << ' ';
    OS << LLVMRev << ')';
  }
  return std::string(Buf);
}

std::string getNeverCFullVersion() {
  return getNeverCToolFullVersion("neverc");
}

std::string getNeverCToolFullVersion(llvm::StringRef ToolName) {
  llvm::SmallString<128> Buf;
  llvm::raw_svector_ostream OS(Buf);
  OS << Vendor << ToolName << " version " NEVERC_VERSION_STRING;
  auto Repo = getNeverCFullRepositoryVersion();
  if (!Repo.empty())
    OS << ' ' << Repo;
  return std::string(Buf);
}

std::string getNeverCFullVersionForMacro() {
  llvm::SmallString<128> Buf;
  llvm::raw_svector_ostream OS(Buf);
  OS << Vendor << "NeverC " NEVERC_VERSION_STRING;
  auto Repo = getNeverCFullRepositoryVersion();
  if (!Repo.empty())
    OS << ' ' << Repo;
  return std::string(Buf);
}

} // end namespace neverc
