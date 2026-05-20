#ifndef NEVERC_FOUNDATION_PARTIALDIAGNOSTIC_H
#define NEVERC_FOUNDATION_PARTIALDIAGNOSTIC_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>

namespace neverc {

class PartialDiagnostic : public StreamingDiagnostic {
private:
  // NOTE: Sema assumes that PartialDiagnostic is location-invariant
  // in the sense that its bits can be safely memcpy'ed and destructed
  // in the new location.

  mutable unsigned DiagID = 0;

public:
  struct NullDiagnostic {};

  PartialDiagnostic(NullDiagnostic) {}

  PartialDiagnostic(unsigned DiagID, DiagStorageAllocator &Allocator_)
      : StreamingDiagnostic(Allocator_), DiagID(DiagID) {}

  PartialDiagnostic(const PartialDiagnostic &Other)
      : StreamingDiagnostic(), DiagID(Other.DiagID) {
    Allocator = Other.Allocator;
    if (Other.DiagStorage) {
      DiagStorage = getStorage();
      *DiagStorage = *Other.DiagStorage;
    }
  }

  template <typename T> const PartialDiagnostic &operator<<(const T &V) const {
    const StreamingDiagnostic &DB = *this;
    DB << V;
    return *this;
  }

  // It is necessary to limit this to rvalue reference to avoid calling this
  // function with a bitfield lvalue argument since non-const reference to
  // bitfield is not allowed.
  template <typename T,
            typename = std::enable_if_t<!std::is_lvalue_reference<T>::value>>
  const PartialDiagnostic &operator<<(T &&V) const {
    const StreamingDiagnostic &DB = *this;
    DB << std::move(V);
    return *this;
  }

  PartialDiagnostic(PartialDiagnostic &&Other) : DiagID(Other.DiagID) {
    Allocator = Other.Allocator;
    DiagStorage = Other.DiagStorage;
    Other.DiagStorage = nullptr;
  }

  PartialDiagnostic(const PartialDiagnostic &Other,
                    DiagnosticStorage *DiagStorage_)
      : DiagID(Other.DiagID) {
    Allocator = reinterpret_cast<DiagStorageAllocator *>(~uintptr_t(0));
    DiagStorage = DiagStorage_;
    if (Other.DiagStorage)
      *this->DiagStorage = *Other.DiagStorage;
  }

  PartialDiagnostic(const Diagnostic &Other, DiagStorageAllocator &Allocator_)
      : DiagID(Other.getID()) {
    Allocator = &Allocator_;
    // Copy arguments.
    for (unsigned I = 0, N = Other.getNumArgs(); I != N; ++I) {
      if (Other.getArgKind(I) == DiagnosticsEngine::ak_std_string)
        AddString(Other.getArgStdStr(I));
      else
        AddTaggedVal(Other.getRawArg(I), Other.getArgKind(I));
    }

    // Copy source ranges.
    for (unsigned I = 0, N = Other.getNumRanges(); I != N; ++I)
      AddSourceRange(Other.getRange(I));

    // Copy fix-its.
    for (unsigned I = 0, N = Other.getNumFixItHints(); I != N; ++I)
      AddFixItHint(Other.getFixItHint(I));
  }

  PartialDiagnostic &operator=(const PartialDiagnostic &Other) {
    DiagID = Other.DiagID;
    if (Other.DiagStorage) {
      if (!DiagStorage)
        DiagStorage = getStorage();

      *DiagStorage = *Other.DiagStorage;
    } else {
      freeStorage();
    }

    return *this;
  }

  PartialDiagnostic &operator=(PartialDiagnostic &&Other) {
    freeStorage();

    DiagID = Other.DiagID;
    DiagStorage = Other.DiagStorage;
    Allocator = Other.Allocator;

    Other.DiagStorage = nullptr;
    return *this;
  }

  void swap(PartialDiagnostic &PD) {
    std::swap(DiagID, PD.DiagID);
    std::swap(DiagStorage, PD.DiagStorage);
    std::swap(Allocator, PD.Allocator);
  }

  unsigned getDiagID() const { return DiagID; }
  void setDiagID(unsigned ID) { DiagID = ID; }

  void Emit(const DiagnosticBuilder &DB) const {
    if (!DiagStorage)
      return;

    // Add all arguments.
    for (unsigned i = 0, e = DiagStorage->NumDiagArgs; i != e; ++i) {
      if ((DiagnosticsEngine::ArgumentKind)DiagStorage->DiagArgumentsKind[i] ==
          DiagnosticsEngine::ak_std_string)
        DB.AddString(DiagStorage->DiagArgumentsStr[i]);
      else
        DB.AddTaggedVal(
            DiagStorage->DiagArgumentsVal[i],
            (DiagnosticsEngine::ArgumentKind)DiagStorage->DiagArgumentsKind[i]);
    }

    // Add all ranges.
    for (const CharSourceRange &Range : DiagStorage->DiagRanges)
      DB.AddSourceRange(Range);

    // Add all fix-its.
    for (const FixItHint &Fix : DiagStorage->FixItHints)
      DB.AddFixItHint(Fix);
  }

  void GenToString(DiagnosticsEngine &Diags,
                   llvm::SmallVectorImpl<char> &Buf) const {
    DiagnosticBuilder DB(Diags.Report(getDiagID()));
    Emit(DB);
    Diagnostic(&Diags).FormatDiagnostic(Buf);
    DB.Clear();
    Diags.Clear();
  }

  void Reset(unsigned DiagID = 0) {
    this->DiagID = DiagID;
    freeStorage();
  }

  bool hasStorage() const { return DiagStorage != nullptr; }

  llvm::StringRef getStringArg(unsigned I) {
    assert(DiagStorage && "No diagnostic storage?");
    assert(I < DiagStorage->NumDiagArgs && "Not enough diagnostic args");
    assert(DiagStorage->DiagArgumentsKind[I] ==
               DiagnosticsEngine::ak_std_string &&
           "Not a string arg");
    return DiagStorage->DiagArgumentsStr[I];
  }
};

inline const DiagnosticBuilder &operator<<(const DiagnosticBuilder &DB,
                                           const PartialDiagnostic &PD) {
  PD.Emit(DB);
  return DB;
}

using PartialDiagnosticAt = std::pair<SourceLocation, PartialDiagnostic>;

} // namespace neverc

#endif // NEVERC_FOUNDATION_PARTIALDIAGNOSTIC_H
