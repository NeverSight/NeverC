#ifndef NEVERC_INVOKE_PLUGIN_DRIVERARTIFACTS_H
#define NEVERC_INVOKE_PLUGIN_DRIVERARTIFACTS_H

#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/PluginDriver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace neverc::driver {

struct DriverArgumentToken {
  std::string Value;
  NevercArgumentOrigin Origin = NEVERC_ARGUMENT_ORIGIN_COMMAND_LINE;
  std::string Source;
  uint64_t Position = 0;
  bool Protected = false;
  bool EndOfOptions = false;
};

class DriverRawArgumentsArtifact {
public:
  explicit DriverRawArgumentsArtifact(
      std::vector<DriverArgumentToken> TokensValue);
  DriverRawArgumentsArtifact(const DriverRawArgumentsArtifact &Other);
  DriverRawArgumentsArtifact &
  operator=(const DriverRawArgumentsArtifact &) = delete;

  llvm::ArrayRef<DriverArgumentToken> tokens() const { return Tokens; }
  llvm::Error verify() const;

private:
  llvm::Expected<std::vector<DriverArgumentToken>> beginMutation();
  llvm::Error commitMutation(std::vector<DriverArgumentToken> NewTokens);
  void abortMutation();

  mutable std::mutex Mutex;
  std::vector<DriverArgumentToken> Tokens;
  bool MutationActive = false;

  friend class DriverArgumentMutation;
};

class DriverArgumentMutation {
public:
  static llvm::Expected<std::unique_ptr<DriverArgumentMutation>>
  create(DriverRawArgumentsArtifact &Target);
  ~DriverArgumentMutation();

  DriverArgumentMutation(const DriverArgumentMutation &) = delete;
  DriverArgumentMutation &operator=(const DriverArgumentMutation &) = delete;

  llvm::Error insert(uint64_t Index, llvm::StringRef Value);
  llvm::Error replace(uint64_t Index, llvm::StringRef Value);
  llvm::Error erase(uint64_t Index);
  llvm::Error commit();
  void abort();
  bool isFinished() const { return Finished; }

private:
  DriverArgumentMutation(DriverRawArgumentsArtifact &TargetValue,
                         std::vector<DriverArgumentToken> TokensValue);

  DriverRawArgumentsArtifact &Target;
  std::vector<DriverArgumentToken> Tokens;
  bool Finished = false;
};

struct DriverParsedOptionOccurrence {
  DriverParsedOptionOccurrence() = default;
  DriverParsedOptionOccurrence(const DriverParsedOptionOccurrence &Other);
  DriverParsedOptionOccurrence &
  operator=(const DriverParsedOptionOccurrence &Other);
  DriverParsedOptionOccurrence(DriverParsedOptionOccurrence &&Other) noexcept;
  DriverParsedOptionOccurrence &
  operator=(DriverParsedOptionOccurrence &&Other) noexcept;

  void rebuildValueViews();

  uint64_t ID = 0;
  std::string Spelling;
  std::vector<std::string> Values;
  std::vector<NevercStringView> ValueViews;
  NevercArgumentOrigin Origin = NEVERC_ARGUMENT_ORIGIN_COMMAND_LINE;
  size_t Start = 0;
  size_t End = 0;
};

class DriverParsedArgumentsArtifact {
public:
  using RenderOption =
      std::function<llvm::Expected<std::vector<DriverArgumentToken>>(
          llvm::StringRef, llvm::ArrayRef<llvm::StringRef>)>;

  DriverParsedArgumentsArtifact(
      std::vector<DriverArgumentToken> TokensValue,
      std::vector<DriverParsedOptionOccurrence> OccurrencesValue,
      RenderOption RenderValue);
  DriverParsedArgumentsArtifact(const DriverParsedArgumentsArtifact &Other);
  DriverParsedArgumentsArtifact &
  operator=(const DriverParsedArgumentsArtifact &) = delete;

  llvm::ArrayRef<DriverArgumentToken> tokens() const { return Tokens; }
  llvm::ArrayRef<DriverParsedOptionOccurrence> occurrences() const {
    return Occurrences;
  }
  llvm::Error verify() const;

private:
  struct MutationSnapshot {
    std::vector<DriverArgumentToken> Tokens;
    std::vector<DriverParsedOptionOccurrence> Occurrences;
    RenderOption Render;
  };

  llvm::Expected<MutationSnapshot> beginMutation();
  llvm::Error commitMutation(MutationSnapshot Snapshot);
  void abortMutation();

  mutable std::mutex Mutex;
  std::vector<DriverArgumentToken> Tokens;
  std::vector<DriverParsedOptionOccurrence> Occurrences;
  RenderOption Render;
  bool MutationActive = false;

  friend class DriverParsedArgumentMutation;
};

class DriverParsedArgumentMutation {
public:
  static llvm::Expected<std::unique_ptr<DriverParsedArgumentMutation>>
  create(DriverParsedArgumentsArtifact &Target);
  ~DriverParsedArgumentMutation();

  DriverParsedArgumentMutation(const DriverParsedArgumentMutation &) = delete;
  DriverParsedArgumentMutation &
  operator=(const DriverParsedArgumentMutation &) = delete;

  llvm::Error add(llvm::StringRef Spelling,
                  llvm::ArrayRef<llvm::StringRef> Values);
  llvm::Error remove(uint64_t Occurrence);
  llvm::Error replace(uint64_t Occurrence, llvm::StringRef Spelling,
                      llvm::ArrayRef<llvm::StringRef> Values);
  llvm::Error commit();
  void abort();

private:
  DriverParsedArgumentMutation(
      DriverParsedArgumentsArtifact &TargetValue,
      DriverParsedArgumentsArtifact::MutationSnapshot SnapshotValue);

  DriverParsedOptionOccurrence *find(uint64_t Occurrence);
  llvm::Error replaceRange(size_t Start, size_t End,
                           std::vector<DriverArgumentToken> Replacement,
                           DriverParsedOptionOccurrence *EditedOccurrence);
  void adjustRanges(size_t Start, size_t End, size_t NewSize,
                    DriverParsedOptionOccurrence *EditedOccurrence);

  DriverParsedArgumentsArtifact &Target;
  DriverParsedArgumentsArtifact::MutationSnapshot Snapshot;
  bool Finished = false;
};

NevercInterfaceID driverRawArgumentsArtifactID();
NevercInterfaceID driverRawArgumentsPhaseID();
NevercInterfaceID driverParsedArgumentsArtifactID();
NevercInterfaceID driverParsedArgumentsPhaseID();

llvm::Expected<std::shared_ptr<const plugin::PluginArtifactType>>
registerDriverRawArgumentsArtifact(plugin::PluginArtifactRegistry &Registry);
llvm::Expected<std::shared_ptr<const plugin::PluginArtifactType>>
registerDriverParsedArgumentsArtifact(plugin::PluginArtifactRegistry &Registry);

bool isProtectedDriverBootstrapArgument(llvm::StringRef Value);
llvm::Error
verifyDriverArgumentTokens(llvm::ArrayRef<DriverArgumentToken> Tokens);

} // namespace neverc::driver

#endif
