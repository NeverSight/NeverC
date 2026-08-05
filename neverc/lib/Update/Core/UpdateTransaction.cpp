//===--- UpdateTransaction.cpp - Exact-path updater transaction ----------===//

#include "neverc/Update/UpdateTransaction.h"

#include "neverc/Release/ReleaseClient.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#ifdef _WIN32
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Windows/WindowsSupport.h"
#include <windows.h>
#endif

#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace llvm;

namespace neverc {
namespace update {
namespace {

constexpr StringLiteral StagePrefix(".neverc-update-");
constexpr StringLiteral PlanFileName("update-plan.json");
constexpr int64_t PlanSchema = 1;

Expected<std::string> canonicalExistingPath(StringRef Path,
                                            StringRef Description) {
  SmallString<256> Canonical;
  if (std::error_code EC = sys::fs::real_path(Path, Canonical))
    return createStringError(EC, "cannot resolve %s '%s'",
                             Description.str().c_str(), Path.str().c_str());
  return Canonical.str().str();
}

Expected<std::string> validatedRelativePath(StringRef Path,
                                            StringRef Description) {
  if (Path.empty() || Path.contains('\0') || sys::path::is_absolute(Path) ||
      !sys::path::root_name(Path).empty() ||
      !sys::path::root_directory(Path).empty())
    return createStringError(inconvertibleErrorCode(),
                             "%s must be a non-empty relative path",
                             Description.str().c_str());

  SmallString<256> Native(Path);
  sys::path::native(Native);
  for (auto I = sys::path::begin(Native), E = sys::path::end(Native); I != E;
       ++I) {
    if (*I == "." || *I == "..")
      return createStringError(inconvertibleErrorCode(),
                               "%s contains path traversal: '%s'",
                               Description.str().c_str(), Path.str().c_str());
  }
  SmallString<256> Slashes = sys::path::convert_to_slash(Native);
  return Slashes.str().str();
}

SmallString<256> appendRelative(StringRef Base, StringRef Relative) {
  SmallString<256> Result(Base);
  sys::path::append(Result, Relative);
  return Result;
}

Expected<bool> existsWithoutFollowing(StringRef Path) {
  sys::fs::file_status Status;
  std::error_code EC = sys::fs::status(Path, Status, /*follow=*/false);
  if (!EC)
    return sys::fs::exists(Status);
  if (EC == std::errc::no_such_file_or_directory)
    return false;
  return createStringError(EC, "cannot inspect '%s'", Path.str().c_str());
}

Error validateParentChain(StringRef Base, StringRef Relative,
                          StringRef Description) {
  SmallString<256> Native(Relative);
  sys::path::native(Native);
  StringRef Parent = sys::path::parent_path(Native);
  SmallString<256> Current(Base);
  for (auto I = sys::path::begin(Parent), E = sys::path::end(Parent); I != E;
       ++I) {
    sys::path::append(Current, *I);
    sys::fs::file_status Status;
    std::error_code EC = sys::fs::status(Current, Status, /*follow=*/false);
    if (EC == std::errc::no_such_file_or_directory)
      return Error::success();
    if (EC)
      return createStringError(EC, "cannot inspect %s parent '%s'",
                               Description.str().c_str(), Current.c_str());
    if (Status.type() != sys::fs::file_type::directory_file)
      return createStringError(
          inconvertibleErrorCode(),
          "%s parent is not a real directory (symlinks are refused): '%s'",
          Description.str().c_str(), Current.c_str());
  }
  return Error::success();
}

Error renameError(std::error_code EC, StringRef From, StringRef To) {
  return createStringError(EC, "cannot rename '%s' to '%s'", From.str().c_str(),
                           To.str().c_str());
}

std::error_code moveExactItem(StringRef From, StringRef To) {
  sys::fs::file_status Status;
  std::error_code EC = sys::fs::status(From, Status, /*follow=*/false);
  if (EC)
    return EC;

#ifdef _WIN32
  if (sys::fs::is_directory(Status)) {
    SmallVector<wchar_t, 260> WideFrom;
    SmallVector<wchar_t, 260> WideTo;
    if (std::error_code WEC = sys::windows::widenPath(From, WideFrom))
      return WEC;
    if (std::error_code WEC = sys::windows::widenPath(To, WideTo))
      return WEC;
    SmallString<256> Parent(sys::path::parent_path(To));
    if (!Parent.empty()) {
      if (std::error_code DEC = sys::fs::create_directories(Parent))
        return DEC;
    }
    if (!::MoveFileExW(WideFrom.data(), WideTo.data(), MOVEFILE_WRITE_THROUGH))
      return mapWindowsError(::GetLastError());
    return {};
  }
#endif

  return sys::fs::rename(From, To);
}

Error removeExactItem(StringRef Path) {
  sys::fs::file_status Status;
  std::error_code EC = sys::fs::status(Path, Status, /*follow=*/false);
  if (EC == std::errc::no_such_file_or_directory)
    return Error::success();
  if (EC)
    return createStringError(EC, "cannot inspect rollback item '%s'",
                             Path.str().c_str());
  if (sys::fs::is_directory(Status))
    EC = sys::fs::remove_directories(Path, /*IgnoreErrors=*/false);
  else
    EC = sys::fs::remove(Path);
  if (EC)
    return createStringError(EC, "cannot remove rollback item '%s'",
                             Path.str().c_str());
  return Error::success();
}

} // namespace

UpdateTransaction::UpdateTransaction(std::string Root, std::string Stage,
                                     std::string TargetTag,
                                     RenameFunction Rename)
    : Root(std::move(Root)), Stage(std::move(Stage)),
      TargetTag(std::move(TargetTag)), Rename(std::move(Rename)) {
  if (!this->Rename)
    this->Rename = moveExactItem;
}

Expected<UpdateTransaction> UpdateTransaction::create(StringRef Root,
                                                      StringRef Stage,
                                                      StringRef TargetTag,
                                                      RenameFunction Rename) {
  Expected<std::string> CanonicalRoot =
      canonicalExistingPath(Root, "update root");
  if (!CanonicalRoot)
    return CanonicalRoot.takeError();
  Expected<std::string> CanonicalStage =
      canonicalExistingPath(Stage, "update stage");
  if (!CanonicalStage)
    return CanonicalStage.takeError();

  if (sys::path::root_path(*CanonicalRoot) == *CanonicalRoot)
    return createStringError(inconvertibleErrorCode(),
                             "refusing a filesystem root update transaction");
  if (sys::path::parent_path(*CanonicalStage) != *CanonicalRoot ||
      !sys::path::filename(*CanonicalStage).starts_with(StagePrefix))
    return createStringError(inconvertibleErrorCode(),
                             "update stage must be a direct child of the root "
                             "named %s*",
                             StagePrefix.str().c_str());

  std::string Tag = release::normalizeReleaseTag(TargetTag);
  if (Tag.empty() || Tag == "latest")
    return createStringError(
        inconvertibleErrorCode(),
        "update transaction requires a concrete release tag");
  return UpdateTransaction(std::move(*CanonicalRoot),
                           std::move(*CanonicalStage), std::move(Tag),
                           std::move(Rename));
}

Error UpdateTransaction::addEntry(StringRef StagedRelativePath,
                                  StringRef LiveRelativePath) {
  Expected<std::string> Staged =
      validatedRelativePath(StagedRelativePath, "staged path");
  if (!Staged)
    return Staged.takeError();
  Expected<std::string> Live =
      validatedRelativePath(LiveRelativePath, "live path");
  if (!Live)
    return Live.takeError();

  for (const Entry &Existing : Entries) {
    if (Existing.StagedRelativePath == *Staged)
      return createStringError(inconvertibleErrorCode(),
                               "duplicate staged update path '%s'",
                               Staged->c_str());
    if (Existing.LiveRelativePath == *Live)
      return createStringError(inconvertibleErrorCode(),
                               "duplicate live update path '%s'",
                               Live->c_str());
  }
  Entries.push_back({std::move(*Staged), std::move(*Live)});
  return Error::success();
}

Error UpdateTransaction::writePlan() const {
  json::Array SerializedEntries;
  for (const Entry &Item : Entries) {
    json::Object Serialized;
    Serialized["staged"] = Item.StagedRelativePath;
    Serialized["live"] = Item.LiveRelativePath;
    SerializedEntries.push_back(std::move(Serialized));
  }

  json::Object Plan;
  Plan["schema"] = PlanSchema;
  Plan["root"] = Root;
  Plan["stage"] = Stage;
  Plan["target_tag"] = TargetTag;
  Plan["entries"] = std::move(SerializedEntries);

  SmallString<256> Path(Stage);
  sys::path::append(Path, PlanFileName);
  std::error_code EC;
  raw_fd_ostream Output(Path, EC, sys::fs::OF_Text);
  if (EC)
    return createStringError(EC, "cannot write update plan '%s'", Path.c_str());
  Output << json::Value(std::move(Plan));
  Output.close();
  if (Output.has_error())
    return createStringError(Output.error(), "cannot finish update plan '%s'",
                             Path.c_str());
  return Error::success();
}

Expected<UpdateTransaction> UpdateTransaction::readPlan(StringRef Stage,
                                                        RenameFunction Rename) {
  SmallString<256> Path(Stage);
  sys::path::append(Path, PlanFileName);
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
      MemoryBuffer::getFile(Path, /*IsText=*/true);
  if (!Buffer)
    return createStringError(Buffer.getError(), "cannot read update plan '%s'",
                             Path.c_str());
  Expected<json::Value> Parsed = json::parse(Buffer.get()->getBuffer());
  if (!Parsed)
    return Parsed.takeError();
  const json::Object *Plan = Parsed->getAsObject();
  if (!Plan)
    return createStringError(inconvertibleErrorCode(),
                             "update plan is not a JSON object");

  int64_t Schema = 0;
  if (!Plan->getInteger("schema", Schema) || Schema != PlanSchema)
    return createStringError(inconvertibleErrorCode(),
                             "unsupported update plan schema");
  StringRef Root = Plan->getString("root");
  StringRef StoredStage = Plan->getString("stage");
  StringRef TargetTag = Plan->getString("target_tag");
  const json::Array *SerializedEntries = Plan->getArray("entries");
  if (!Root.data() || !StoredStage.data() || !TargetTag.data() ||
      !SerializedEntries)
    return createStringError(inconvertibleErrorCode(),
                             "update plan is missing required fields");

  Expected<std::string> CanonicalArgumentStage =
      canonicalExistingPath(Stage, "helper stage");
  if (!CanonicalArgumentStage)
    return CanonicalArgumentStage.takeError();
  Expected<std::string> CanonicalStoredStage =
      canonicalExistingPath(StoredStage, "stored helper stage");
  if (!CanonicalStoredStage)
    return CanonicalStoredStage.takeError();
  if (*CanonicalArgumentStage != *CanonicalStoredStage)
    return createStringError(
        inconvertibleErrorCode(),
        "update plan stage does not match helper argument");

  Expected<UpdateTransaction> Result =
      create(Root, StoredStage, TargetTag, std::move(Rename));
  if (!Result)
    return Result.takeError();
  for (const json::Value &Value : *SerializedEntries) {
    const json::Object *Serialized = Value.getAsObject();
    if (!Serialized)
      return createStringError(inconvertibleErrorCode(),
                               "update plan entry is not an object");
    StringRef Staged = Serialized->getString("staged");
    StringRef Live = Serialized->getString("live");
    if (!Staged.data() || !Live.data())
      return createStringError(inconvertibleErrorCode(),
                               "update plan entry is missing a path");
    if (Error E = Result->addEntry(Staged, Live))
      return std::move(E);
  }
  return Result;
}

Error UpdateTransaction::apply(ValidationFunction ValidateInstalledState) {
  struct ResolvedEntry {
    const Entry *Definition;
    SmallString<256> Staged;
    SmallString<256> Live;
    SmallString<256> Backup;
    bool HadLiveItem = false;
    bool BackedUp = false;
    bool Installed = false;
  };

  SmallString<256> BackupDirectory(Stage);
  sys::path::append(BackupDirectory, "backup");
  Expected<bool> BackupExists = existsWithoutFollowing(BackupDirectory);
  if (!BackupExists)
    return BackupExists.takeError();
  if (*BackupExists)
    return createStringError(inconvertibleErrorCode(),
                             "update backup directory already exists; refusing "
                             "to overwrite possible recovery data");

  std::vector<ResolvedEntry> Resolved;
  Resolved.reserve(Entries.size());
  for (size_t Index = 0; Index < Entries.size(); ++Index) {
    const Entry &Definition = Entries[Index];
    ResolvedEntry Item;
    Item.Definition = &Definition;
    Item.Staged = appendRelative(Stage, Definition.StagedRelativePath);
    Item.Live = appendRelative(Root, Definition.LiveRelativePath);
    Item.Backup = BackupDirectory;
    sys::path::append(Item.Backup, Twine(Index).str());

    if (Error E = validateParentChain(Stage, Definition.StagedRelativePath,
                                      "staged update"))
      return std::move(E);
    if (Error E = validateParentChain(Root, Definition.LiveRelativePath,
                                      "live update"))
      return std::move(E);
    Expected<bool> StagedExists = existsWithoutFollowing(Item.Staged);
    if (!StagedExists)
      return StagedExists.takeError();
    if (!*StagedExists)
      return createStringError(inconvertibleErrorCode(),
                               "staged update item is missing: '%s'",
                               Item.Staged.c_str());
    Expected<bool> LiveExists = existsWithoutFollowing(Item.Live);
    if (!LiveExists)
      return LiveExists.takeError();
    Item.HadLiveItem = *LiveExists;

    SmallString<256> Parent(sys::path::parent_path(Item.Live));
    if (std::error_code EC = sys::fs::create_directories(Parent))
      return createStringError(EC, "cannot create live parent '%s'",
                               Parent.c_str());
    Resolved.push_back(std::move(Item));
  }

  if (std::error_code EC = sys::fs::create_directory(BackupDirectory))
    return createStringError(EC, "cannot create update backup directory '%s'",
                             BackupDirectory.c_str());

  auto rollback = [&]() -> Error {
    Error RollbackError = Error::success();
    for (auto I = Resolved.rbegin(), E = Resolved.rend(); I != E; ++I) {
      if (!I->Installed)
        continue;
      if (std::error_code EC = Rename(I->Live, I->Staged)) {
        RollbackError = joinErrors(std::move(RollbackError),
                                   renameError(EC, I->Live, I->Staged));
        RollbackError =
            joinErrors(std::move(RollbackError), removeExactItem(I->Live));
      }
      I->Installed = false;
    }
    for (auto I = Resolved.rbegin(), E = Resolved.rend(); I != E; ++I) {
      if (!I->BackedUp)
        continue;
      if (std::error_code EC = Rename(I->Backup, I->Live))
        RollbackError = joinErrors(std::move(RollbackError),
                                   renameError(EC, I->Backup, I->Live));
      else
        I->BackedUp = false;
    }
    return RollbackError;
  };

  for (ResolvedEntry &Item : Resolved) {
    if (!Item.HadLiveItem)
      continue;
    if (std::error_code EC = Rename(Item.Live, Item.Backup)) {
      Error Primary = renameError(EC, Item.Live, Item.Backup);
      return joinErrors(std::move(Primary), rollback());
    }
    Item.BackedUp = true;
  }

  for (ResolvedEntry &Item : Resolved) {
    if (std::error_code EC = Rename(Item.Staged, Item.Live)) {
      Error Primary = renameError(EC, Item.Staged, Item.Live);
      return joinErrors(std::move(Primary), rollback());
    }
    Item.Installed = true;
  }
  if (ValidateInstalledState) {
    if (Error E = ValidateInstalledState())
      return joinErrors(std::move(E), rollback());
  }
  return Error::success();
}

} // namespace update
} // namespace neverc
