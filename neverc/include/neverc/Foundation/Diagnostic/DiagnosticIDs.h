#ifndef NEVERC_BASIC_DIAGNOSTICIDS_H
#define NEVERC_BASIC_DIAGNOSTICIDS_H

#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <optional>
#include <vector>

namespace neverc {
class DiagnosticsEngine;
class SourceLocation;

// Import the diagnostic enums themselves.
namespace diag {
enum class Group;

// Size of each of the diagnostic categories.
enum {
  DIAG_SIZE_COMMON = 300,
  DIAG_SIZE_DRIVER = 400,
  DIAG_SIZE_FRONTEND = 150,
  DIAG_SIZE_LEX = 400,
  DIAG_SIZE_PARSE = 700,
  DIAG_SIZE_AST = 300,
  DIAG_SIZE_SEMA = 2000,
};
// Start position for diagnostics.
enum {
  DIAG_START_COMMON = 0,
  DIAG_START_DRIVER = DIAG_START_COMMON + static_cast<int>(DIAG_SIZE_COMMON),
  DIAG_START_FRONTEND = DIAG_START_DRIVER + static_cast<int>(DIAG_SIZE_DRIVER),
  DIAG_START_LEX = DIAG_START_FRONTEND + static_cast<int>(DIAG_SIZE_FRONTEND),
  DIAG_START_PARSE = DIAG_START_LEX + static_cast<int>(DIAG_SIZE_LEX),
  DIAG_START_AST = DIAG_START_PARSE + static_cast<int>(DIAG_SIZE_PARSE),
  DIAG_START_SEMA = DIAG_START_AST + static_cast<int>(DIAG_SIZE_AST),
  DIAG_UPPER_LIMIT = DIAG_START_SEMA + static_cast<int>(DIAG_SIZE_SEMA)
};

class CustomDiagInfo;

typedef unsigned kind;

// Get typedefs for common diagnostics.
enum {
#define DIAG(ENUM, FLAGS, DEFAULT_MAPPING, DESC, GROUP, CATEGORY, NOWERROR,    \
             SHOWINSYSHEADER, SHOWINSYSMACRO, DEFFERABLE)                      \
  ENUM,
#define COMMONSTART
#include "neverc/Foundation/DiagnosticCommonKinds.td.h"
  NUM_BUILTIN_COMMON_DIAGNOSTICS
#undef DIAG
};

enum class Severity {
  // NOTE: 0 means "uncomputed".
  Ignored = 1, ///< Do not present this diagnostic, ignore it.
  Remark = 2,  ///< Present this diagnostic as a remark.
  Warning = 3, ///< Present this diagnostic as a warning.
  Error = 4,   ///< Present this diagnostic as an error.
  Fatal = 5    ///< Present this diagnostic as a fatal error.
};

enum class Flavor {
  WarningOrError, ///< A diagnostic that indicates a problem or potential
                  ///< problem. Can be made fatal by -Werror.
  Remark          ///< A diagnostic that indicates normal progress through
                  ///< compilation.
};
} // namespace diag

class DiagnosticMapping {
  LLVM_PREFERRED_TYPE(diag::Severity)
  unsigned Severity : 3;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsUser : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsPragma : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasNoWarningAsError : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasNoErrorAsFatal : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned WasUpgradedFromWarning : 1;

public:
  static DiagnosticMapping Make(diag::Severity Severity, bool IsUser,
                                bool IsPragma) {
    DiagnosticMapping Result;
    Result.Severity = (unsigned)Severity;
    Result.IsUser = IsUser;
    Result.IsPragma = IsPragma;
    Result.HasNoWarningAsError = 0;
    Result.HasNoErrorAsFatal = 0;
    Result.WasUpgradedFromWarning = 0;
    return Result;
  }

  diag::Severity getSeverity() const { return (diag::Severity)Severity; }
  void setSeverity(diag::Severity Value) { Severity = (unsigned)Value; }

  bool isUser() const { return IsUser; }
  bool isPragma() const { return IsPragma; }

  bool isErrorOrFatal() const {
    return getSeverity() == diag::Severity::Error ||
           getSeverity() == diag::Severity::Fatal;
  }

  bool hasNoWarningAsError() const { return HasNoWarningAsError; }
  void setNoWarningAsError(bool Value) { HasNoWarningAsError = Value; }

  bool hasNoErrorAsFatal() const { return HasNoErrorAsFatal; }
  void setNoErrorAsFatal(bool Value) { HasNoErrorAsFatal = Value; }

  bool wasUpgradedFromWarning() const { return WasUpgradedFromWarning; }
  void setUpgradedFromWarning(bool Value) { WasUpgradedFromWarning = Value; }

  unsigned serialize() const {
    return (IsUser << 7) | (IsPragma << 6) | (HasNoWarningAsError << 5) |
           (HasNoErrorAsFatal << 4) | (WasUpgradedFromWarning << 3) | Severity;
  }
  static DiagnosticMapping deserialize(unsigned Bits) {
    DiagnosticMapping Result;
    Result.IsUser = (Bits >> 7) & 1;
    Result.IsPragma = (Bits >> 6) & 1;
    Result.HasNoWarningAsError = (Bits >> 5) & 1;
    Result.HasNoErrorAsFatal = (Bits >> 4) & 1;
    Result.WasUpgradedFromWarning = (Bits >> 3) & 1;
    Result.Severity = Bits & 0x7;
    return Result;
  }

  bool operator==(DiagnosticMapping Other) const {
    return serialize() == Other.serialize();
  }
};

class DiagnosticIDs : public llvm::RefCountedBase<DiagnosticIDs> {
public:
  enum Level { Ignored, Note, Remark, Warning, Error, Fatal };

private:
  std::unique_ptr<diag::CustomDiagInfo> CustomDiagInfo;

public:
  DiagnosticIDs();
  ~DiagnosticIDs();

  unsigned getCustomDiagID(Level L, llvm::StringRef FormatString);

  //===--------------------------------------------------------------------===//
  // Diagnostic classification and reporting interfaces.
  //

  llvm::StringRef getDescription(unsigned DiagID) const;

  static bool isBuiltinWarningOrExtension(unsigned DiagID);

  static bool isDefaultMappingAsError(unsigned DiagID);

  static DiagnosticMapping getDefaultMapping(unsigned DiagID);

  static bool isBuiltinNote(unsigned DiagID);

  static bool isBuiltinExtensionDiag(unsigned DiagID) {
    bool ignored;
    return isBuiltinExtensionDiag(DiagID, ignored);
  }

  static bool isBuiltinExtensionDiag(unsigned DiagID, bool &EnabledByDefault);

  static llvm::StringRef getWarningOptionForGroup(diag::Group);

  static llvm::StringRef getWarningOptionDocumentation(diag::Group GroupID);

  static std::optional<diag::Group> getGroupForWarningOption(llvm::StringRef);

  static std::optional<diag::Group> getGroupForDiag(unsigned DiagID);

  static llvm::StringRef getWarningOptionForDiag(unsigned DiagID);

  static unsigned getCategoryNumberForDiag(unsigned DiagID);

  static unsigned getNumberOfCategories();

  static llvm::StringRef getCategoryNameFromID(unsigned CategoryID);

  static bool isDeferrable(unsigned DiagID);

  static std::vector<std::string> getDiagnosticFlags();

  bool getDiagnosticsInGroup(diag::Flavor Flavor, llvm::StringRef Group,
                             llvm::SmallVectorImpl<diag::kind> &Diags) const;

  static void getAllDiagnostics(diag::Flavor Flavor,
                                std::vector<diag::kind> &Diags);

  static llvm::StringRef getNearestOption(diag::Flavor Flavor,
                                          llvm::StringRef Group);

private:
  DiagnosticIDs::Level
  getDiagnosticLevel(unsigned DiagID, SourceLocation Loc,
                     const DiagnosticsEngine &Diag) const LLVM_READONLY;

  diag::Severity
  getDiagnosticSeverity(unsigned DiagID, SourceLocation Loc,
                        const DiagnosticsEngine &Diag) const LLVM_READONLY;

  bool ProcessDiag(DiagnosticsEngine &Diag) const;

  void GenDiag(DiagnosticsEngine &Diag, Level DiagLevel) const;

  bool isUnrecoverable(unsigned DiagID) const;

  friend class DiagnosticsEngine;
};

} // end namespace neverc

#endif
