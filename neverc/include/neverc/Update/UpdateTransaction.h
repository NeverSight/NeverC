#ifndef NEVERC_UPDATE_UPDATETRANSACTION_H
#define NEVERC_UPDATE_UPDATETRANSACTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <functional>
#include <string>
#include <system_error>
#include <vector>

namespace neverc {
namespace update {

/// A persisted exact-path rename transaction. Live paths are always relative
/// to Root and staged paths are always relative to Stage.
class UpdateTransaction {
public:
  struct Entry {
    std::string StagedRelativePath;
    std::string LiveRelativePath;
  };

  using RenameFunction =
      std::function<std::error_code(llvm::StringRef, llvm::StringRef)>;
  using ValidationFunction = std::function<llvm::Error()>;

  static llvm::Expected<UpdateTransaction> create(llvm::StringRef Root,
                                                  llvm::StringRef Stage,
                                                  llvm::StringRef TargetTag,
                                                  RenameFunction Rename = {});

  static llvm::Expected<UpdateTransaction> readPlan(llvm::StringRef Stage,
                                                    RenameFunction Rename = {});

  llvm::Error addEntry(llvm::StringRef StagedRelativePath,
                       llvm::StringRef LiveRelativePath);

  llvm::Error writePlan() const;

  /// Apply all entries or restore every prior live item on failure.
  llvm::Error apply(ValidationFunction ValidateInstalledState = {});

  llvm::StringRef root() const { return Root; }
  llvm::StringRef stage() const { return Stage; }
  llvm::StringRef targetTag() const { return TargetTag; }
  llvm::ArrayRef<Entry> entries() const { return Entries; }

private:
  UpdateTransaction(std::string Root, std::string Stage, std::string TargetTag,
                    RenameFunction Rename);

  std::string Root;
  std::string Stage;
  std::string TargetTag;
  std::vector<Entry> Entries;
  RenameFunction Rename;
};

} // namespace update
} // namespace neverc

#endif // NEVERC_UPDATE_UPDATETRANSACTION_H
