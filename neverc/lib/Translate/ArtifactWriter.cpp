#include "ArtifactWriter.h"
#include "ArtifactPlatform.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"
#include <set>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace llvm;
namespace neverc::translate {
namespace {
Error ioError(StringRef Path, std::error_code EC) {
  return createStringError(EC, "TR0502: %s: %s", Path.str().c_str(),
                           EC.message().c_str());
}
Error collision(StringRef Path) {
  return createStringError(
      inconvertibleErrorCode(),
      "TR0501: output already exists or aliases another path: %s",
      Path.str().c_str());
}
Error requireAbsent(StringRef Path) {
  sys::fs::file_status S;
  auto EC = sys::fs::status(Path, S, /*follow=*/false);
  if (!EC && S.type() != sys::fs::file_type::file_not_found)
    return collision(Path);
  if (EC && EC != std::errc::no_such_file_or_directory)
    return ioError(Path, EC);
  return Error::success();
}
Expected<std::string> canonicalDestination(StringRef Path) {
  SmallString<256> Absolute(Path);
  if (auto EC = sys::fs::make_absolute(Absolute))
    return ioError(Path, EC);
  if (sys::path::filename(Absolute).empty() ||
      sys::path::filename(Absolute) == "." ||
      sys::path::filename(Absolute) == "..")
    return collision(Path);
  SmallString<256> Parent;
  if (auto EC = resolveExistingPath(sys::path::parent_path(Absolute), Parent))
    return ioError(Path, EC);
  sys::path::append(Parent, sys::path::filename(Absolute));
  return Parent.str().str();
}
std::string appendPath(StringRef Parent, StringRef Child) {
  SmallString<256> P(Parent);
  sys::path::append(P, Child);
  return P.str().str();
}
} // namespace

Expected<std::string> readFile(StringRef Path, uint64_t Limit) {
#ifdef _WIN32
  auto Opened = sys::fs::openNativeFileForRead(Path);
  if (!Opened)
    return ioError(Path, errorToErrorCode(Opened.takeError()));
  sys::fs::file_t FD = *Opened;
#else
  // Open without waiting for a FIFO writer, then inspect that exact descriptor.
  // A separate path stat followed by a blocking open can race a replacement.
  sys::fs::file_t FD =
      ::open(Path.str().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (FD == -1)
    return ioError(Path, {errno, std::generic_category()});
#endif
  auto Close = make_scope_exit([&]() { (void)sys::fs::closeFile(FD); });
  sys::fs::file_status Status;
  if (auto EC = sys::fs::status(FD, Status))
    return ioError(Path, EC);
  if (!sys::fs::is_regular_file(Status))
    return createStringError(inconvertibleErrorCode(),
                             "TR0502: expected a regular file: %s",
                             Path.str().c_str());
  if (Status.getSize() > Limit)
    return createStringError(inconvertibleErrorCode(),
                             "TR0502: file exceeds size limit: %s",
                             Path.str().c_str());
  // Do not mmap mutable inputs or allocate from a second path lookup. Bound
  // every read, including a file which grows after the descriptor's size check.
  std::string Contents;
  char Buffer[64 * 1024];
  for (;;) {
    const uint64_t Remaining = Limit - Contents.size();
    const size_t Count = Remaining >= sizeof(Buffer)
                             ? sizeof(Buffer)
                             : static_cast<size_t>(Remaining) + 1;
    auto Read =
        sys::fs::readNativeFile(FD, MutableArrayRef<char>(Buffer, Count));
    if (!Read)
      return ioError(Path, errorToErrorCode(Read.takeError()));
    if (*Read > Remaining)
      return createStringError(inconvertibleErrorCode(),
                               "TR0502: file exceeds size limit: %s",
                               Path.str().c_str());
    if (*Read == 0)
      return Contents;
    Contents.append(Buffer, *Read);
  }
}

std::string sha256(StringRef Contents) {
  auto Digest = SHA256::hash(ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(Contents.data()), Contents.size()));
  return toHex(ArrayRef<uint8_t>(Digest), /*LowerCase=*/true);
}

Error writeExclusive(StringRef Path, StringRef Contents) {
  int FD = -1;
  if (auto EC = sys::fs::openFileForWrite(Path, FD, sys::fs::CD_CreateNew)) {
    if (EC == std::errc::file_exists)
      return collision(Path);
    return ioError(Path, EC);
  }
  raw_fd_ostream OS(FD, /*shouldClose=*/true);
  OS << Contents;
  OS.close();
  if (OS.has_error()) {
    auto EC = OS.error();
    OS.clear_error();
    (void)sys::fs::remove(Path);
    return ioError(Path, EC);
  }
  return Error::success();
}

Expected<std::unique_ptr<ArtifactWriter>>
ArtifactWriter::create(const ArtifactOptions &Options, StringRef Source) {
  auto Writer = std::unique_ptr<ArtifactWriter>(new ArtifactWriter);
  if (Options.Project && !Options.Output.empty())
    return createStringError(inconvertibleErrorCode(),
                             "TR0502: project artifacts require a new output "
                             "directory or check mode");
  std::set<std::string> Destinations;
  auto reserve = [&](StringRef P, std::string &Destination) -> Error {
    auto Canonical = canonicalDestination(P);
    if (!Canonical)
      return Canonical.takeError();
    Destination = *Canonical;
    if (!Destinations.insert(Destination).second)
      return collision(P);
    return requireAbsent(Destination);
  };

  SmallString<256> Parent;
  if (!Options.Output.empty()) {
    if (Error E = reserve(Options.Output, Writer->Output))
      return std::move(E);
    Writer->SourceName = sys::path::filename(Writer->Output).str();
    Writer->MapName = Writer->SourceName + ".map.json";
    Writer->ManifestName = Writer->SourceName + ".manifest.json";
    Parent = sys::path::parent_path(Writer->Output);
    std::string Ignored;
    if (Error E = reserve(Writer->Output + ".map.json", Ignored))
      return std::move(E);
    if (Error E = reserve(Writer->Output + ".manifest.json", Ignored))
      return std::move(E);
  } else if (!Options.OutputDirectory.empty()) {
    if (Error E = reserve(Options.OutputDirectory, Writer->OutputDirectory))
      return std::move(E);
    Parent = sys::path::parent_path(Writer->OutputDirectory);
    Writer->SourceName = sys::path::stem(Source).str() + ".nc";
    Writer->MapName = "translate.map.json";
    Writer->ManifestName = "translate-manifest.json";
  } else {
    sys::path::system_temp_directory(true, Parent);
    Writer->SourceName = sys::path::stem(Source).str() + ".nc";
    Writer->MapName = "translate.map.json";
    Writer->ManifestName = "translate-manifest.json";
  }
  if (Options.Project) {
    Writer->SourceName = "translated.nc";
    Writer->HeaderName = "translated.h";
  }

  if (!Options.Report.empty()) {
    SmallString<256> Report(Options.Report);
    if (auto EC = sys::fs::make_absolute(Report))
      return ioError(Report, EC);
    bool Inside = false;
    if (!Writer->OutputDirectory.empty()) {
      auto ParentDestination =
          canonicalDestination(sys::path::parent_path(Report));
      if (ParentDestination)
        Inside = *ParentDestination == Writer->OutputDirectory;
      else
        consumeError(ParentDestination.takeError());
    }
    if (Inside) {
      Writer->ReportInsideDirectory = true;
      Writer->ReportName = sys::path::filename(Report).str();
      if (Writer->ReportName.empty() || Writer->ReportName == "." ||
          Writer->ReportName == ".." ||
          Writer->ReportName == Writer->SourceName ||
          Writer->ReportName == Writer->MapName ||
          Writer->ReportName == Writer->HeaderName ||
          Writer->ReportName == Writer->ManifestName)
        return collision(Report);
      Writer->ReportPath =
          appendPath(Writer->OutputDirectory, Writer->ReportName);
    } else if (Error E = reserve(Report, Writer->ReportPath)) {
      return std::move(E);
    }
  }
  auto Prefix = appendPath(Parent, ".neverc-translate");
  SmallString<256> Temp;
  if (auto EC = sys::fs::createUniqueDirectory(Prefix, Temp))
    return ioError(Prefix, EC);
  Writer->Staging = Temp.str().str();
  return std::move(Writer);
}

ArtifactWriter::~ArtifactWriter() {
  if (!Committed)
    consumeError(rollback());
  // A committed writer keeps its outputs, but must release identity handles
  // before deleting the staging links (including external report staging).
  Published.clear();
  cleanupReportStaging();
  if (!Staging.empty())
    (void)sys::fs::remove_directories(Staging);
}

std::string ArtifactWriter::stagePath(StringRef Name) const {
  return appendPath(Staging, Name);
}

Error ArtifactWriter::writeStage(StringRef Name, StringRef Contents) {
  if (sys::path::filename(Name) != Name || Name == "." || Name == "..")
    return createStringError(inconvertibleErrorCode(),
                             "TR0502: invalid staging filename");
  return writeExclusive(stagePath(Name), Contents);
}

Error ArtifactWriter::publishFile(StringRef From, StringRef To) {
  // Hard-link publication is atomic and fails even if a destination appeared
  // after preflight. Staging lives on the same filesystem as the destination.
  ArtifactFileIdentity Identity;
  if (auto EC = ArtifactFileIdentity::capture(From, Identity))
    return ioError(From, EC);
  if (auto EC = sys::fs::create_hard_link(From, To)) {
    if (EC == std::errc::file_exists)
      return collision(To);
    return ioError(To, EC);
  }
  Published.push_back({To.str(), std::move(Identity)});
  return Error::success();
}

Error ArtifactWriter::rollback() {
  Error Result = Error::success();
  for (auto I = Published.rbegin(); I != Published.rend(); ++I) {
    bool Same = false;
    if (auto EC = I->Identity.matches(I->Path, Same)) {
      if (EC != std::errc::no_such_file_or_directory)
        Result = joinErrors(std::move(Result), ioError(I->Path, EC));
      continue;
    }
    // Preserve replacements already present when identity is checked. The
    // comparison and path-based removal are not atomic against a later replace.
    if (Same)
      if (auto EC = sys::fs::remove(I->Path))
        Result = joinErrors(std::move(Result), ioError(I->Path, EC));
  }
  // Close retained Windows handles before reusing a removed report pathname.
  Published.clear();
  cleanupReportStaging();
  return Result;
}

void ArtifactWriter::cleanupReportStaging() {
  for (const auto &Directory : ReportStaging)
    (void)sys::fs::remove_directories(Directory);
  ReportStaging.clear();
}

Error ArtifactWriter::publishReport(StringRef Contents) {
  if (ReportPath.empty())
    return Error::success();
  SmallString<256> Temporary;
  auto Prefix =
      appendPath(sys::path::parent_path(ReportPath), ".neverc-report");
  if (auto EC = sys::fs::createUniqueDirectory(Prefix, Temporary))
    return ioError(ReportPath, EC);
  // Keep this original link until rollback/commit releases its identity token.
  // On Windows an open identity handle can defer the link's final deletion.
  ReportStaging.push_back(Temporary.str().str());
  auto Path = appendPath(Temporary, "report.json");
  Error E = writeExclusive(Path, Contents);
  if (!E)
    E = publishFile(Path, ReportPath);
  return E;
}

Error ArtifactWriter::publish(const std::vector<Artifact> &Artifacts,
                              StringRef Report,
                              std::function<bool()> IsCancelled) {
  auto checkpoint = [&]() -> Error {
    if (IsCancelled && IsCancelled())
      return createStringError(
          inconvertibleErrorCode(),
          "TR0005: translation cancelled before publication completed");
    return Error::success();
  };
  if (Error E = checkpoint())
    return E;
  if (Output.empty() && OutputDirectory.empty()) {
    if (Error E = publishReport(Report))
      return E;
    Committed = true;
    return Error::success();
  }
  // Use a separate final directory so requests, helper diagnostics, and
  // validation object files can never accidentally become published output.
  const std::string Final = stagePath("artifacts");
  if (auto EC = sys::fs::create_directory(Final, false))
    return ioError(Final, EC);
  for (const auto &A : Artifacts) {
    if (Error E = checkpoint())
      return E;
    if (sys::path::filename(A.Name) != A.Name)
      return createStringError(inconvertibleErrorCode(),
                               "TR0502: invalid artifact filename");
    if (Error E = writeExclusive(appendPath(Final, A.Name), A.Contents))
      return E;
  }
  if (ReportInsideDirectory) {
    if (Error E = writeExclusive(appendPath(Final, ReportName), Report))
      return E;
  } else if (Error E = publishReport(Report)) {
    return E;
  }
  if (!OutputDirectory.empty()) {
    if (Error E = checkpoint())
      return E;
    if (auto EC = renameDirectoryExclusive(Final, OutputDirectory)) {
      if (EC == std::errc::file_exists || EC == std::errc::directory_not_empty)
        return collision(OutputDirectory);
      return ioError(OutputDirectory, EC);
    }
  } else {
    // Manifest is the completion marker, and is always published last.
    for (const auto &A : Artifacts) {
      if (Error E = checkpoint())
        return E;
      if (A.Name != ManifestName)
        if (Error E =
                publishFile(appendPath(Final, A.Name),
                            appendPath(sys::path::parent_path(Output), A.Name)))
          return E;
    }
    if (Error E = checkpoint())
      return E;
    if (Error E = publishFile(appendPath(Final, ManifestName),
                              Output + ".manifest.json"))
      return E;
  }
  Committed = true;
  return Error::success();
}

Error ArtifactWriter::publishFailureReport(StringRef Report) {
  // Publication can fail after a source/map or an optimistic success report
  // was created. Roll those back before retaining a failure report.
  if (Error E = rollback())
    return E;
  // A failure must not create the directory reserved for generated artifacts.
  // Reports within that directory therefore cannot be published on failure.
  if (ReportInsideDirectory)
    return createStringError(inconvertibleErrorCode(),
                             "TR0502: failure report must be outside the "
                             "unpublished output directory");
  if (Error E = publishReport(Report))
    return E;
  Committed = true;
  return Error::success();
}
} // namespace neverc::translate
