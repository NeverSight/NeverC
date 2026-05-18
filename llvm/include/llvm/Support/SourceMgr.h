//===- SourceMgr.h - Manager for Source Buffers & Diagnostics ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the SMDiagnostic and SourceMgr classes.  This
// provides a simple substrate for diagnostics, #include handling, and other low
// level things for simple parsers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SOURCEMGR_H
#define LLVM_SUPPORT_SOURCEMGR_H

#include <utility>

#include "csupport/cpp_compat_stl.h"
#include "csupport/lsource_lmgr.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SMLoc.h"
#include <vector>

namespace llvm {

class raw_ostream;
class SMDiagnostic;
class SMFixIt;

struct LineColPair {
  unsigned first;
  unsigned second;
};

/// This owns the files read by a parser, handles include stacks,
/// and handles diagnostic wrangling.
class SourceMgr {
public:
  enum DiagKind {
    DK_Error,
    DK_Warning,
    DK_Remark,
    DK_Note,
  };

  /// Clients that want to handle their own diagnostics in a custom way can
  /// register a function pointer+context as a diagnostic handler.
  /// It gets called each time PrintMessage is invoked.
  using DiagHandlerTy = void (*)(const SMDiagnostic &, void *Context);

private:
  struct SrcBuffer {
    /// The memory buffer for the file.
    std::unique_ptr<MemoryBuffer> Buffer;

    /// Vector of offsets into Buffer at which there are line-endings
    /// (lazily populated). Once populated, the '\n' that marks the end of
    /// line number N from [1..] is at Buffer[OffsetCache[N-1]]. Since
    /// these offsets are in sorted (ascending) order, they can be
    /// binary-searched for the first one after any given offset (eg. an
    /// offset corresponding to a particular SMLoc).
    ///
    /// Since we're storing offsets into relatively small files (often smaller
    /// than 2^8 or 2^16 bytes), we select the offset vector element type
    /// dynamically based on the size of Buffer.
    mutable void *OffsetCache = nullptr;

    /// Look up a given \p Ptr in the buffer, determining which line it came
    /// from.
    unsigned getLineNumber(const char *Ptr) const;
    template <typename T>
    unsigned getLineNumberSpecialized(const char *Ptr) const;

    /// Return a pointer to the first character of the specified line number or
    /// null if the line number is invalid.
    const char *getPointerForLineNumber(unsigned LineNo) const;
    template <typename T>
    const char *getPointerForLineNumberSpecialized(unsigned LineNo) const;

    /// This is the location of the parent include, or null if at the top level.
    SMLoc IncludeLoc;

    SrcBuffer() = default;
    SrcBuffer(SrcBuffer &&);
    SrcBuffer(const SrcBuffer &) = delete;
    SrcBuffer &operator=(const SrcBuffer &) = delete;
    ~SrcBuffer();
  };

  /// This is all of the buffers that we are reading from.
  std::vector<SrcBuffer> Buffers;

  // This is the list of directories we should search for include files in.
  std::vector<std::string> IncludeDirectories;

  DiagHandlerTy DiagHandler = nullptr;
  void *DiagContext = nullptr;

  bool isValidBufferID(unsigned i) const { return i && i <= Buffers.size(); }

public:
  SourceMgr() = default;
  SourceMgr(const SourceMgr &) = delete;
  SourceMgr &operator=(const SourceMgr &) = delete;
  SourceMgr(SourceMgr &&) = default;
  SourceMgr &operator=(SourceMgr &&) = default;
  ~SourceMgr() = default;

  /// Return the include directories of this source manager.
  ArrayRef<std::string> getIncludeDirs() const { return IncludeDirectories; }

  void setIncludeDirs(const std::vector<std::string> &Dirs) {
    IncludeDirectories = Dirs;
  }

  /// Specify a diagnostic handler to be invoked every time PrintMessage is
  /// called. \p Ctx is passed into the handler when it is invoked.
  void setDiagHandler(DiagHandlerTy DH, void *Ctx = nullptr) {
    DiagHandler = DH;
    DiagContext = Ctx;
  }

  DiagHandlerTy getDiagHandler() const { return DiagHandler; }
  void *getDiagContext() const { return DiagContext; }

  const SrcBuffer &getBufferInfo(unsigned i) const {
    assert(isValidBufferID(i));
    return Buffers[i - 1];
  }

  const MemoryBuffer *getMemoryBuffer(unsigned i) const {
    assert(isValidBufferID(i));
    return Buffers[i - 1].Buffer.get();
  }

  unsigned getNumBuffers() const { return Buffers.size(); }

  unsigned getMainFileID() const {
    assert(getNumBuffers());
    return 1;
  }

  SMLoc getParentIncludeLoc(unsigned i) const {
    assert(isValidBufferID(i));
    return Buffers[i - 1].IncludeLoc;
  }

  /// Add a new source buffer to this source manager. This takes ownership of
  /// the memory buffer.
  unsigned AddNewSourceBuffer(std::unique_ptr<MemoryBuffer> F,
                              SMLoc IncludeLoc) {
    SrcBuffer NB;
    NB.Buffer = std::move(F);
    NB.IncludeLoc = IncludeLoc;
    Buffers.push_back(std::move(NB));
    return Buffers.size();
  }

  /// Takes the source buffers from the given source manager and append them to
  /// the current manager. `MainBufferIncludeLoc` is an optional include
  /// location to attach to the main buffer of `SrcMgr` after it gets moved to
  /// the current manager.
  void takeSourceBuffersFrom(SourceMgr &SrcMgr,
                             SMLoc MainBufferIncludeLoc = SMLoc()) {
    if (SrcMgr.Buffers.empty())
      return;

    size_t OldNumBuffers = getNumBuffers();
    std::move(SrcMgr.Buffers.begin(), SrcMgr.Buffers.end(),
              std::back_inserter(Buffers));
    SrcMgr.Buffers.clear();
    Buffers[OldNumBuffers].IncludeLoc = MainBufferIncludeLoc;
  }

  /// Search for a file with the specified name in the current directory or in
  /// one of the IncludeDirs.
  ///
  /// If no file is found, this returns 0, otherwise it returns the buffer ID
  /// of the stacked file. The full path to the included file can be found in
  /// \p IncludedFile.
  unsigned AddIncludeFile(StringRef Filename, SMLoc IncludeLoc,
                          SmallVectorImpl<char> &IncludedFile);

  /// Search for a file with the specified name in the current directory or in
  /// one of the IncludeDirs, and try to open it **without** adding to the
  /// SourceMgr. If the opened file is intended to be added to the source
  /// manager, prefer `AddIncludeFile` instead.
  ///
  /// If no file is found, this returns an Error, otherwise it returns the
  /// buffer of the stacked file. The full path to the included file can be
  /// found in \p IncludedFile.
  ErrorOr<std::unique_ptr<MemoryBuffer>>
  OpenIncludeFile(StringRef Filename, SmallVectorImpl<char> &IncludedFile);

  /// Return the ID of the buffer containing the specified location.
  ///
  /// 0 is returned if the buffer is not found.
  unsigned FindBufferContainingLoc(SMLoc Loc) const;

  /// Find the line number for the specified location in the specified file.
  /// This is not a fast method.
  unsigned FindLineNumber(SMLoc Loc, unsigned BufferID = 0) const {
    return getLineAndColumn(Loc, BufferID).first;
  }

  /// Find the line and column number for the specified location in the
  /// specified file. This is not a fast method.
  LineColPair getLineAndColumn(SMLoc Loc, unsigned BufferID = 0) const;

  /// Get a string with the \p SMLoc filename and line number
  /// formatted in the standard style.
  SmallString<256> getFormattedLocationNoOffset(SMLoc Loc,
                                                bool IncludePath = false) const;

  /// Given a line and column number in a mapped buffer, turn it into an SMLoc.
  /// This will return a null SMLoc if the line/column location is invalid.
  SMLoc FindLocForLineAndColumn(unsigned BufferID, unsigned LineNo,
                                unsigned ColNo);

  /// Emit a message about the specified location with the specified string.
  ///
  /// \param ShowColors Display colored messages if output is a terminal and
  /// the default error handler is used.
  void PrintMessage(raw_ostream &OS, SMLoc Loc, DiagKind Kind, const Twine &Msg,
                    ArrayRef<SMRange> Ranges = {},
                    ArrayRef<SMFixIt> FixIts = {},
                    bool ShowColors = true) const;

  /// Emits a diagnostic to llvm::errs().
  void PrintMessage(SMLoc Loc, DiagKind Kind, const Twine &Msg,
                    ArrayRef<SMRange> Ranges = {},
                    ArrayRef<SMFixIt> FixIts = {},
                    bool ShowColors = true) const;

  /// Emits a manually-constructed diagnostic to the given output stream.
  ///
  /// \param ShowColors Display colored messages if output is a terminal and
  /// the default error handler is used.
  void PrintMessage(raw_ostream &OS, const SMDiagnostic &Diagnostic,
                    bool ShowColors = true) const;

  /// Return an SMDiagnostic at the specified location with the specified
  /// string.
  ///
  /// \param Msg If non-null, the kind of message (e.g., "error") which is
  /// prefixed to the message.
  SMDiagnostic GetMessage(SMLoc Loc, DiagKind Kind, const Twine &Msg,
                          ArrayRef<SMRange> Ranges = {},
                          ArrayRef<SMFixIt> FixIts = {}) const;

  /// Prints the names of included files and the line of the file they were
  /// included from. A diagnostic handler can use this before printing its
  /// custom formatted message.
  ///
  /// \param IncludeLoc The location of the include.
  /// \param OS the raw_ostream to print on.
  void PrintIncludeStack(SMLoc IncludeLoc, raw_ostream &OS) const;
};

/// Represents a single fixit, a replacement of one range of text with another.
class SMFixIt {
  SMRange Range;

  std::string Text;

public:
  SMFixIt(SMRange R, const Twine &Replacement);

  SMFixIt(SMLoc Loc, const Twine &Replacement)
      : SMFixIt(SMRange(Loc, Loc), Replacement) {}

  StringRef getText() const { return Text; }
  SMRange getRange() const { return Range; }

  bool operator<(const SMFixIt &Other) const {
    if (Range.Start.getPointer() != Other.Range.Start.getPointer())
      return Range.Start.getPointer() < Other.Range.Start.getPointer();
    if (Range.End.getPointer() != Other.Range.End.getPointer())
      return Range.End.getPointer() < Other.Range.End.getPointer();
    return Text < Other.Text;
  }
};

/// Instances of this class encapsulate one diagnostic report, allowing
/// printing to a raw_ostream as a caret diagnostic.
class SMDiagnostic {
  const SourceMgr *SM = nullptr;
  SMLoc Loc;
  SmallString<256> Filename;
  int LineNo = 0;
  int ColumnNo = 0;
  SourceMgr::DiagKind Kind = SourceMgr::DK_Error;
  SmallString<256> Message;
  SmallString<256> LineContents;
  SmallVector<LineColPair, 4> Ranges;
  SmallVector<SMFixIt, 4> FixIts;

public:
  // Null diagnostic.
  SMDiagnostic() = default;
  // Diagnostic with no location (e.g. file not found, command line arg error).
  SMDiagnostic(StringRef filename, SourceMgr::DiagKind Knd, StringRef Msg)
      : Filename(filename), LineNo(-1), ColumnNo(-1), Kind(Knd), Message(Msg) {}

  // Diagnostic with a location.
  SMDiagnostic(const SourceMgr &sm, SMLoc L, StringRef FN, int Line, int Col,
               SourceMgr::DiagKind Kind, StringRef Msg, StringRef LineStr,
               ArrayRef<LineColPair> Ranges, ArrayRef<SMFixIt> FixIts = {});

  const SourceMgr *getSourceMgr() const { return SM; }
  SMLoc getLoc() const { return Loc; }
  StringRef getFilename() const { return Filename; }
  int getLineNo() const { return LineNo; }
  int getColumnNo() const { return ColumnNo; }
  SourceMgr::DiagKind getKind() const { return Kind; }
  StringRef getMessage() const { return Message; }
  StringRef getLineContents() const { return LineContents; }
  ArrayRef<LineColPair> getRanges() const { return Ranges; }

  void addFixIt(const SMFixIt &Hint) { FixIts.push_back(Hint); }

  ArrayRef<SMFixIt> getFixIts() const { return FixIts; }

  void print(const char *ProgName, raw_ostream &S, bool ShowColors = true,
             bool ShowKindLabel = true) const;
};

} // end namespace llvm

// === Inline implementations (moved from cpp_bridge.cpp) ===

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#ifndef BRIDGE_MIN
#define BRIDGE_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

namespace llvm {

inline static const size_t TabStop = 8;

inline unsigned SourceMgr::AddIncludeFile(StringRef Filename, SMLoc IncludeLoc,
                                          SmallVectorImpl<char> &IncludedFile) {
  ErrorOr<uptr_t<MemoryBuffer>> NewBufOrErr =
      OpenIncludeFile(Filename, IncludedFile);
  if (!NewBufOrErr)
    return 0;

  return AddNewSourceBuffer(std::move(*NewBufOrErr), IncludeLoc);
}

inline ErrorOr<uptr_t<MemoryBuffer>>
SourceMgr::OpenIncludeFile(StringRef Filename,
                           SmallVectorImpl<char> &IncludedFile) {
  ErrorOr<uptr_t<MemoryBuffer>> NewBufOrErr = MemoryBuffer::getFile(Filename);

  SmallString<64> Buffer(Filename);
  for (unsigned i = 0, e = IncludeDirectories.size(); i != e && !NewBufOrErr;
       ++i) {
    Buffer = IncludeDirectories[i];
    sys::path::append(Buffer, Filename);
    NewBufOrErr = MemoryBuffer::getFile(Buffer);
  }

  if (NewBufOrErr)
    IncludedFile.assign(Buffer.begin(), Buffer.end());

  return NewBufOrErr;
}

inline unsigned SourceMgr::FindBufferContainingLoc(SMLoc Loc) const {
  for (unsigned i = 0, e = Buffers.size(); i != e; ++i)
    if (Loc.getPointer() >= Buffers[i].Buffer->getBufferStart() &&
        // Use <= here so that a pointer to the null at the end of the buffer
        // is included as part of the buffer.
        Loc.getPointer() <= Buffers[i].Buffer->getBufferEnd())
      return i + 1;
  return 0;
}

typedef csupport_offset_cache_t COffsetArray;
#define coffset_create csupport_offset_cache_create
#define coffset_destroy csupport_offset_cache_destroy
#define coffset_get csupport_offset_cache_get
#define coffset_lower_bound csupport_offset_cache_lower_bound

inline static COffsetArray *GetOrCreateOffsetCacheC(void *&OffsetCache,
                                                    MemoryBuffer *Buffer,
                                                    unsigned elem_size) {
  if (OffsetCache)
    return (COffsetArray *)OffsetCache;

  COffsetArray *Offsets = csupport_offset_cache_build(
      Buffer->getBufferStart(), Buffer->getBufferSize(), elem_size);
  OffsetCache = Offsets;
  return Offsets;
}

/* getElemSize eliminated -- csupport_offset_cache_elem_size called directly */
#define getElemSize(Sz) csupport_offset_cache_elem_size(Sz)

inline unsigned SourceMgr::SrcBuffer::getLineNumber(const char *Ptr) const {
  size_t Sz = Buffer->getBufferSize();
  COffsetArray *Offsets =
      GetOrCreateOffsetCacheC(OffsetCache, Buffer.get(), getElemSize(Sz));
  const char *BufStart = Buffer->getBufferStart();
  assert(Ptr >= BufStart && Ptr <= Buffer->getBufferEnd());
  ptrdiff_t PtrDiff = Ptr - BufStart;
  assert(PtrDiff >= 0);
  return (unsigned)coffset_lower_bound(Offsets, (uint64_t)PtrDiff) + 1;
}

inline const char *
SourceMgr::SrcBuffer::getPointerForLineNumber(unsigned LineNo) const {
  size_t Sz = Buffer->getBufferSize();
  COffsetArray *Offsets =
      GetOrCreateOffsetCacheC(OffsetCache, Buffer.get(), getElemSize(Sz));
  if (LineNo != 0)
    --LineNo;
  const char *BufStart = Buffer->getBufferStart();
  if (LineNo == 0)
    return BufStart;
  if (LineNo > csupport_offset_cache_count(Offsets))
    return 0;
  return BufStart + coffset_get(Offsets, LineNo - 1) + 1;
}

inline SourceMgr::SrcBuffer::SrcBuffer(SourceMgr::SrcBuffer &&Other)
    : Buffer(std::move(Other.Buffer)), OffsetCache(Other.OffsetCache),
      IncludeLoc(Other.IncludeLoc) {
  Other.OffsetCache = 0;
}

inline SourceMgr::SrcBuffer::~SrcBuffer() {
  if (OffsetCache) {
    coffset_destroy((COffsetArray *)OffsetCache);
    OffsetCache = 0;
  }
}

inline llvm::LineColPair SourceMgr::getLineAndColumn(SMLoc Loc,
                                                     unsigned BufferID) const {
  if (!BufferID)
    BufferID = FindBufferContainingLoc(Loc);
  assert(BufferID && "Invalid location!");

  auto &SB = getBufferInfo(BufferID);
  const char *Ptr = Loc.getPointer();

  unsigned LineNo = SB.getLineNumber(Ptr);
  const char *BufStart = SB.Buffer->getBufferStart();
  size_t NewlineOffs = StringRef(BufStart, Ptr - BufStart).find_last_of("\n\r");
  if (NewlineOffs == StringRef::npos)
    NewlineOffs = ~(size_t)0;
  return {LineNo, (unsigned)(Ptr - BufStart - NewlineOffs)};
}

// FIXME: Note that the formatting of source locations is spread between
// multiple functions, some in SourceMgr and some in SMDiagnostic. A better
// solution would be a general-purpose source location formatter
// in one of those two classes, or possibly in SMLoc.

/// Get a string with the source location formatted in the standard
/// style, but without the line offset. If \p IncludePath is true, the path
/// is included. If false, only the file name and extension are included.
inline SmallString<256>
SourceMgr::getFormattedLocationNoOffset(SMLoc Loc, bool IncludePath) const {
  auto BufferID = FindBufferContainingLoc(Loc);
  assert(BufferID && "Invalid location!");
  auto FileSpec = getBufferInfo(BufferID).Buffer->getBufferIdentifier();

  SmallString<256> Result;
  if (IncludePath) {
    Result += FileSpec;
  } else {
    auto I = FileSpec.find_last_of("/\\");
    I = (I == FileSpec.size()) ? 0 : (I + 1);
    Result += FileSpec.substr(I);
  }
  Result += ":";
  raw_svector_ostream(Result) << FindLineNumber(Loc, BufferID);
  return SmallString<256>(Result);
}

/// Given a line and column number in a mapped buffer, turn it into an SMLoc.
/// This will return a null SMLoc if the line/column location is invalid.
inline SMLoc SourceMgr::FindLocForLineAndColumn(unsigned BufferID,
                                                unsigned LineNo,
                                                unsigned ColNo) {
  auto &SB = getBufferInfo(BufferID);
  const char *Ptr = SB.getPointerForLineNumber(LineNo);
  if (!Ptr)
    return SMLoc();

  // We start counting line and column numbers from 1.
  if (ColNo != 0)
    --ColNo;

  // If we have a column number, validate it.
  if (ColNo) {
    // Make sure the location is within the current line.
    if (Ptr + ColNo > SB.Buffer->getBufferEnd())
      return SMLoc();

    // Make sure there is no newline in the way.
    if (StringRef(Ptr, ColNo).find_first_of("\n\r") != StringRef::npos)
      return SMLoc();

    Ptr += ColNo;
  }

  return SMLoc::getFromPointer(Ptr);
}

inline void SourceMgr::PrintIncludeStack(SMLoc IncludeLoc,
                                         raw_ostream &OS) const {
  if (IncludeLoc == SMLoc())
    return; // Top of stack.

  unsigned CurBuf = FindBufferContainingLoc(IncludeLoc);
  assert(CurBuf && "Invalid or unspecified location!");

  PrintIncludeStack(getBufferInfo(CurBuf).IncludeLoc, OS);

  OS << "Included from " << getBufferInfo(CurBuf).Buffer->getBufferIdentifier()
     << ":" << FindLineNumber(IncludeLoc, CurBuf) << ":\n";
}

inline SMDiagnostic SourceMgr::GetMessage(SMLoc Loc, SourceMgr::DiagKind Kind,
                                          const Twine &Msg,
                                          ArrayRef<SMRange> Ranges,
                                          ArrayRef<SMFixIt> FixIts) const {
  // First thing to do: find the current buffer containing the specified
  // location to pull out the source line.
  SmallVector<LineColPair, 4> ColRanges;
  LineColPair LineAndCol = {0, 0};
  StringRef BufferID = "<unknown>";
  StringRef LineStr;

  if (Loc.isValid()) {
    unsigned CurBuf = FindBufferContainingLoc(Loc);
    assert(CurBuf && "Invalid or unspecified location!");

    const MemoryBuffer *CurMB = getMemoryBuffer(CurBuf);
    BufferID = CurMB->getBufferIdentifier();

    const char *LineStart = Loc.getPointer();
    const char *BufStart = CurMB->getBufferStart();
    while (LineStart != BufStart && LineStart[-1] != '\n' &&
           LineStart[-1] != '\r')
      --LineStart;

    const char *LineEnd = Loc.getPointer();
    const char *BufEnd = CurMB->getBufferEnd();
    while (LineEnd != BufEnd && LineEnd[0] != '\n' && LineEnd[0] != '\r')
      ++LineEnd;
    LineStr = StringRef(LineStart, LineEnd - LineStart);

    for (SMRange R : Ranges) {
      if (!R.isValid())
        continue;
      if (R.Start.getPointer() > LineEnd || R.End.getPointer() < LineStart)
        continue;
      if (R.Start.getPointer() < LineStart)
        R.Start = SMLoc::getFromPointer(LineStart);
      if (R.End.getPointer() > LineEnd)
        R.End = SMLoc::getFromPointer(LineEnd);
      ColRanges.push_back({(unsigned)(R.Start.getPointer() - LineStart),
                           (unsigned)(R.End.getPointer() - LineStart)});
    }

    LineAndCol = getLineAndColumn(Loc, CurBuf);
  }

  return SMDiagnostic(*this, Loc, BufferID, LineAndCol.first,
                      LineAndCol.second - 1, Kind, Msg.str(), LineStr,
                      ColRanges, FixIts);
}

inline void SourceMgr::PrintMessage(raw_ostream &OS,
                                    const SMDiagnostic &Diagnostic,
                                    bool ShowColors) const {
  // Report the message with the diagnostic handler if present.
  if (DiagHandler) {
    DiagHandler(Diagnostic, DiagContext);
    return;
  }

  if (Diagnostic.getLoc().isValid()) {
    unsigned CurBuf = FindBufferContainingLoc(Diagnostic.getLoc());
    assert(CurBuf && "Invalid or unspecified location!");
    PrintIncludeStack(getBufferInfo(CurBuf).IncludeLoc, OS);
  }

  Diagnostic.print(0, OS, ShowColors);
}

inline void SourceMgr::PrintMessage(raw_ostream &OS, SMLoc Loc,
                                    SourceMgr::DiagKind Kind, const Twine &Msg,
                                    ArrayRef<SMRange> Ranges,
                                    ArrayRef<SMFixIt> FixIts,
                                    bool ShowColors) const {
  PrintMessage(OS, GetMessage(Loc, Kind, Msg, Ranges, FixIts), ShowColors);
}

inline void SourceMgr::PrintMessage(SMLoc Loc, SourceMgr::DiagKind Kind,
                                    const Twine &Msg, ArrayRef<SMRange> Ranges,
                                    ArrayRef<SMFixIt> FixIts,
                                    bool ShowColors) const {
  PrintMessage(errs(), Loc, Kind, Msg, Ranges, FixIts, ShowColors);
}

//===----------------------------------------------------------------------===//
// SMFixIt Implementation
//===----------------------------------------------------------------------===//

inline SMFixIt::SMFixIt(SMRange R, const Twine &Replacement)
    : Range(R), Text(Replacement.str()) {
  assert(R.isValid());
}

//===----------------------------------------------------------------------===//
// SMDiagnostic Implementation
//===----------------------------------------------------------------------===//

inline SMDiagnostic::SMDiagnostic(const SourceMgr &sm, SMLoc L, StringRef FN,
                                  int Line, int Col, SourceMgr::DiagKind Kind,
                                  StringRef Msg, StringRef LineStr,
                                  ArrayRef<LineColPair> Ranges,
                                  ArrayRef<SMFixIt> Hints)
    : SM(&sm), Loc(L), Filename(FN), LineNo(Line), ColumnNo(Col), Kind(Kind),
      Message(Msg), LineContents(LineStr), Ranges(Ranges.begin(), Ranges.end()),
      FixIts(Hints.begin(), Hints.end()) {
  llvm::sort(FixIts);
}

inline static void buildFixItLine(SmallVectorImpl<char> &CaretLine,
                                  SmallVectorImpl<char> &FixItLine,
                                  ArrayRef<SMFixIt> FixIts,
                                  ArrayRef<char> SourceLine) {
  if (FixIts.empty())
    return;

  const char *LineStart = SourceLine.begin();
  const char *LineEnd = SourceLine.end();

  SmallVector<const char *, 8> texts;
  SmallVector<size_t, 8> text_lens;
  SmallVector<size_t, 8> start_cols;
  SmallVector<size_t, 8> end_cols;

  for (const llvm::SMFixIt &Fixit : FixIts) {
    if (Fixit.getText().find_first_of("\n\r\t") != StringRef::npos)
      continue;
    SMRange R = Fixit.getRange();
    if (R.Start.getPointer() > LineEnd || R.End.getPointer() < LineStart)
      continue;
    texts.push_back(Fixit.getText().data());
    text_lens.push_back(Fixit.getText().size());
    start_cols.push_back(R.Start.getPointer() < LineStart
                             ? 0
                             : R.Start.getPointer() - LineStart);
    end_cols.push_back(R.End.getPointer() >= LineEnd
                           ? (size_t)(LineEnd - LineStart)
                           : (size_t)(R.End.getPointer() - LineStart));
  }

  if (texts.empty())
    return;

  size_t max_needed = CaretLine.size();
  for (size_t i = 0; i < texts.size(); i++)
    max_needed = max_needed > start_cols[i] + text_lens[i] + 2
                     ? max_needed
                     : start_cols[i] + text_lens[i] + 2;
  if (max_needed > FixItLine.size())
    FixItLine.resize(max_needed, ' ');

  size_t fixit_len = FixItLine.size();
  csupport_build_fixit_line(CaretLine.data(), CaretLine.size(),
                            FixItLine.data(), &fixit_len, FixItLine.size(),
                            texts.data(), text_lens.data(), start_cols.data(),
                            end_cols.data(), texts.size(), LineStart, LineEnd);
  if (fixit_len > FixItLine.size())
    FixItLine.resize(fixit_len, ' ');
}

inline static void printSourceLine(raw_ostream &S, StringRef LineContents) {
  char buf[4096];
  size_t len = csupport_expand_tabs_to_string(
      LineContents.data(), LineContents.size(), buf, sizeof(buf), TabStop);
  S.write(buf, len);
  S << '\n';
}

static bool isNonASCII(char c) { return csupport_is_non_ascii(c) != 0; }

inline void SMDiagnostic::print(const char *ProgName, raw_ostream &OS,
                                bool ShowColors, bool ShowKindLabel) const {
  ColorMode Mode = ShowColors ? ColorMode::Auto : ColorMode::Disable;

  {
    WithColor S(OS, raw_ostream::SAVEDCOLOR, true, false, Mode);

    if (ProgName && ProgName[0])
      S << ProgName << ": ";

    if (!Filename.empty()) {
      if (Filename == "-")
        S << "<stdin>";
      else
        S << Filename;

      if (LineNo != -1) {
        S << ':' << LineNo;
        if (ColumnNo != -1)
          S << ':' << (ColumnNo + 1);
      }
      S << ": ";
    }
  }

  if (ShowKindLabel) {
    switch (Kind) {
    case SourceMgr::DK_Error:
      WithColor::error(OS, "", !ShowColors);
      break;
    case SourceMgr::DK_Warning:
      WithColor::warning(OS, "", !ShowColors);
      break;
    case SourceMgr::DK_Note:
      WithColor::note(OS, "", !ShowColors);
      break;
    case SourceMgr::DK_Remark:
      WithColor::remark(OS, "", !ShowColors);
      break;
    }
  }

  WithColor(OS, raw_ostream::SAVEDCOLOR, true, false, Mode) << Message << '\n';

  if (LineNo == -1 || ColumnNo == -1)
    return;

  // FIXME: If there are multibyte or multi-column characters in the source, all
  // our ranges will be wrong. To do this properly, we'll need a byte-to-column
  // map like NeverC's TextDiagnostic. For now, we'll just handle tabs by
  // expanding them later, and bail out rather than show incorrect ranges and
  // misaligned fixits for any other odd characters.
  if (any_of(LineContents, isNonASCII)) {
    printSourceLine(OS, LineContents);
    return;
  }
  size_t NumColumns = LineContents.size();

  // Build the line with the caret and ranges.
  SmallString<256> CaretLine;
  CaretLine.assign(NumColumns + 1, ' ');

  // Expand any ranges.
  for (const auto &R : Ranges)
    memset(&CaretLine[R.first], '~',
           BRIDGE_MIN((size_t)R.second, CaretLine.size()) - R.first);

  // Add any fix-its.
  SmallString<128> FixItInsertionLine;
  buildFixItLine(CaretLine, FixItInsertionLine, FixIts,
                 ArrayRef(Loc.getPointer() - ColumnNo, LineContents.size()));

  // Finally, plop on the caret.
  if (unsigned(ColumnNo) <= NumColumns)
    CaretLine[ColumnNo] = '^';
  else
    CaretLine[NumColumns] = '^';

  // ... and remove trailing whitespace so the output doesn't wrap for it.  We
  // know that the line isn't completely empty because it has the caret in it at
  // least.
  CaretLine.resize(StringRef(CaretLine).find_last_not_of(' ') + 1);

  printSourceLine(OS, LineContents);

  {
    ColorMode Mode = ShowColors ? ColorMode::Auto : ColorMode::Disable;
    WithColor S(OS, raw_ostream::GREEN, true, false, Mode);

    // Print out the caret line, matching tabs in the source line.
    for (unsigned i = 0, e = CaretLine.size(), OutCol = 0; i != e; ++i) {
      if (i >= LineContents.size() || LineContents[i] != '\t') {
        S << CaretLine[i];
        ++OutCol;
        continue;
      }

      // Okay, we have a tab.  Insert the appropriate number of characters.
      do {
        S << CaretLine[i];
        ++OutCol;
      } while ((OutCol % TabStop) != 0);
    }
    S << '\n';
  }

  // Print out the replacement line, matching tabs in the source line.
  if (FixItInsertionLine.empty())
    return;

  for (size_t i = 0, e = FixItInsertionLine.size(), OutCol = 0; i < e; ++i) {
    if (i >= LineContents.size() || LineContents[i] != '\t') {
      OS << FixItInsertionLine[i];
      ++OutCol;
      continue;
    }

    // Okay, we have a tab.  Insert the appropriate number of characters.
    do {
      OS << FixItInsertionLine[i];
      // FIXME: This is trying not to break up replacements, but then to re-sync
      // with the tabs between replacements. This will fail, though, if two
      // fix-it replacements are exactly adjacent, or if a fix-it contains a
      // space. Really we should be precomputing column widths, which we'll
      // need anyway for multibyte chars.
      if (FixItInsertionLine[i] != ' ')
        ++i;
      ++OutCol;
    } while (((OutCol % TabStop) != 0) && i != e);
  }
  OS << '\n';
}

} // namespace llvm

#endif // LLVM_SUPPORT_SOURCEMGR_H
