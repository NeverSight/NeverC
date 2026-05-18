#include "neverc/Compiler/TextDiagnostic.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.h"
#include "neverc/Scan/SourceScanner.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Locale.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <optional>

#ifdef __AVX2__
#include <immintrin.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

using namespace neverc;

// ===----------------------------------------------------------------------===
// Internal helpers
// ===----------------------------------------------------------------------===

namespace {

const enum llvm::raw_ostream::Colors noteColor = llvm::raw_ostream::CYAN;
const enum llvm::raw_ostream::Colors remarkColor = llvm::raw_ostream::BLUE;
const enum llvm::raw_ostream::Colors fixitColor = llvm::raw_ostream::GREEN;
const enum llvm::raw_ostream::Colors caretColor = llvm::raw_ostream::GREEN;
const enum llvm::raw_ostream::Colors warningColor = llvm::raw_ostream::MAGENTA;
const enum llvm::raw_ostream::Colors templateColor = llvm::raw_ostream::CYAN;
const enum llvm::raw_ostream::Colors errorColor = llvm::raw_ostream::RED;
const enum llvm::raw_ostream::Colors fatalColor = llvm::raw_ostream::RED;
// Used for changing only the bold attribute.
const enum llvm::raw_ostream::Colors savedColor = llvm::raw_ostream::SAVEDCOLOR;

void applyTemplateHighlighting(llvm::raw_ostream &OS, llvm::StringRef Str,
                               bool &Normal, bool Bold) {
  while (true) {
    size_t Pos = Str.find(ToggleHighlight);
    OS << Str.slice(0, Pos);
    if (Pos == llvm::StringRef::npos)
      break;

    Str = Str.substr(Pos + 1);
    if (Normal)
      OS.changeColor(templateColor, true);
    else {
      OS.resetColor();
      if (Bold)
        OS.changeColor(savedColor, true);
    }
    Normal = !Normal;
  }
}

const unsigned WordWrapIndentation = 6;

int bytesSincePreviousTabOrLineBegin(llvm::StringRef SourceLine, size_t i) {
  const char *Data = SourceLine.data();
#if defined(__aarch64__) && defined(__ARM_NEON)
  {
    const uint8x16_t VTab = vdupq_n_u8('\t');
    while (i >= 16) {
      uint8x16_t V = vld1q_u8(reinterpret_cast<const uint8_t *>(Data + i - 16));
      uint8x16_t Hits = vceqq_u8(V, VTab);
      if (vmaxvq_u8(Hits) != 0) {
        uint64x2_t As64 = vreinterpretq_u64_u8(Hits);
        uint64_t Hi = vgetq_lane_u64(As64, 1);
        if (Hi)
          return static_cast<int>(i) - 8 - (63 - __builtin_clzll(Hi)) / 8 - 1;
        uint64_t Lo = vgetq_lane_u64(As64, 0);
        return static_cast<int>(i) - 16 + 8 - (63 - __builtin_clzll(Lo)) / 8 -
               1;
      }
      i -= 16;
    }
  }
#elif defined(__AVX2__)
  {
    const __m256i VTab = _mm256_set1_epi8('\t');
    while (i >= 32) {
      __m256i V = _mm256_loadu_si256((const __m256i *)(Data + i - 32));
      unsigned Mask = static_cast<unsigned>(
          _mm256_movemask_epi8(_mm256_cmpeq_epi8(V, VTab)));
      if (Mask != 0) {
        unsigned LastBit = 31 - __builtin_clz(Mask);
        return static_cast<int>(i) - 32 + static_cast<int>(32 - LastBit - 1);
      }
      i -= 32;
    }
  }
#elif defined(__SSE2__)
  {
    const __m128i VTab = _mm_set1_epi8('\t');
    while (i >= 16) {
      __m128i V = _mm_loadu_si128((const __m128i *)(Data + i - 16));
      unsigned Mask =
          static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(V, VTab)));
      if (Mask != 0) {
        unsigned LastBit = 31 - __builtin_clz(Mask);
        return static_cast<int>(i) - 16 + static_cast<int>(16 - LastBit - 1);
      }
      i -= 16;
    }
  }
#endif
  int bytes = 0;
  while (0 < i) {
    if (Data[--i] == '\t')
      break;
    ++bytes;
  }
  return bytes;
}

std::pair<llvm::SmallString<16>, bool>
printableTextForNextCharacter(llvm::StringRef SourceLine, size_t *I,
                              unsigned TabStop) {
  assert(I && "I must not be null");
  assert(*I < SourceLine.size() && "must point to a valid index");

  if (SourceLine[*I] == '\t') {
    assert(0 < TabStop && TabStop <= DiagnosticOptions::MaxTabStop &&
           "Invalid -ftabstop value");
    unsigned Col = bytesSincePreviousTabOrLineBegin(SourceLine, *I);
    unsigned NumSpaces = TabStop - (Col % TabStop);
    assert(0 < NumSpaces && NumSpaces <= TabStop &&
           "Invalid computation of space amt");
    ++(*I);

    llvm::SmallString<16> ExpandedTab;
    ExpandedTab.assign(NumSpaces, ' ');
    return std::make_pair(ExpandedTab, true);
  }

  const unsigned char *Begin = SourceLine.bytes_begin() + *I;

  // Fast path for the common ASCII case.
  if (*Begin < 0x80 && llvm::sys::locale::isPrint(*Begin)) {
    ++(*I);
    return std::make_pair(llvm::SmallString<16>(Begin, Begin + 1), true);
  }
  unsigned CharSize = llvm::getNumBytesForUTF8(*Begin);
  const unsigned char *End = Begin + CharSize;

  // Convert it to UTF32 and check if it's printable.
  if (End <= SourceLine.bytes_end() && llvm::isLegalUTF8Sequence(Begin, End)) {
    llvm::UTF32 C;
    llvm::UTF32 *CPtr = &C;

    // Begin and end before conversion.
    unsigned char const *OriginalBegin = Begin;
    llvm::ConversionResult Res = llvm::ConvertUTF8toUTF32(
        &Begin, End, &CPtr, CPtr + 1, llvm::strictConversion);
    (void)Res;
    assert(Res == llvm::conversionOK);
    assert(OriginalBegin < Begin);
    assert((Begin - OriginalBegin) == CharSize);

    (*I) += (Begin - OriginalBegin);

    // Valid, multi-byte, printable UTF8 character.
    if (llvm::sys::locale::isPrint(C))
      return std::make_pair(llvm::SmallString<16>(OriginalBegin, End), true);

    // Valid but not printable.
    llvm::SmallString<16> Str("<U+>");
    while (C) {
      Str.insert(Str.begin() + 3, llvm::hexdigit(C % 16));
      C /= 16;
    }
    while (Str.size() < 8)
      Str.insert(Str.begin() + 3, llvm::hexdigit(0));
    return std::make_pair(Str, false);
  }

  // Otherwise, not printable since it's not valid UTF8.
  llvm::SmallString<16> ExpandedByte("<XX>");
  unsigned char Byte = SourceLine[*I];
  ExpandedByte[1] = llvm::hexdigit(Byte / 16);
  ExpandedByte[2] = llvm::hexdigit(Byte % 16);
  ++(*I);
  return std::make_pair(ExpandedByte, false);
}

void expandTabs(std::string &SourceLine, unsigned TabStop) {
  if (LLVM_LIKELY(SourceLine.find('\t') == std::string::npos))
    return;

  size_t I = SourceLine.size();
  while (I > 0) {
    I--;
    if (LLVM_LIKELY(SourceLine[I] != '\t'))
      continue;
    size_t TmpI = I;
    auto [Str, Printable] =
        printableTextForNextCharacter(SourceLine, &TmpI, TabStop);
    SourceLine.replace(I, 1, Str.c_str());
  }
}

void genColumnByteMapping(llvm::StringRef SourceLine, unsigned TabStop,
                          llvm::SmallVectorImpl<int> &BytesOut,
                          llvm::SmallVectorImpl<int> &ColumnsOut) {
  assert(BytesOut.empty());
  assert(ColumnsOut.empty());

  if (SourceLine.empty()) {
    BytesOut.resize(1u, 0);
    ColumnsOut.resize(1u, 0);
    return;
  }

  ColumnsOut.resize(SourceLine.size() + 1, -1);

  int Columns = 0;
  size_t I = 0;
  while (I < SourceLine.size()) {
    ColumnsOut[I] = Columns;
    BytesOut.resize(Columns + 1, -1);
    BytesOut.back() = I;
    auto [Str, Printable] =
        printableTextForNextCharacter(SourceLine, &I, TabStop);
    Columns += llvm::sys::locale::columnWidth(Str);
  }

  ColumnsOut.back() = Columns;
  BytesOut.resize(Columns + 1, -1);
  BytesOut.back() = I;
}

struct SourceColumnMap {
  SourceColumnMap(llvm::StringRef SourceLine, unsigned TabStop)
      : m_SourceLine(SourceLine) {

    genColumnByteMapping(SourceLine, TabStop, m_columnToByte, m_byteToColumn);

    assert(m_byteToColumn.size() == SourceLine.size() + 1);
    assert(0 < m_byteToColumn.size() && 0 < m_columnToByte.size());
    assert(m_byteToColumn.size() ==
           static_cast<unsigned>(m_columnToByte.back() + 1));
    assert(static_cast<unsigned>(m_byteToColumn.back() + 1) ==
           m_columnToByte.size());
  }
  int columns() const { return m_byteToColumn.back(); }
  int bytes() const { return m_columnToByte.back(); }

  int byteToColumn(int n) const {
    assert(0 <= n && n < static_cast<int>(m_byteToColumn.size()));
    return m_byteToColumn[n];
  }

  int byteToContainingColumn(int N) const {
    assert(0 <= N && N < static_cast<int>(m_byteToColumn.size()));
    while (m_byteToColumn[N] == -1)
      --N;
    return m_byteToColumn[N];
  }

  int columnToByte(int n) const {
    assert(0 <= n && n < static_cast<int>(m_columnToByte.size()));
    return m_columnToByte[n];
  }

  int startOfNextColumn(int N) const {
    assert(0 <= N && N < static_cast<int>(m_byteToColumn.size() - 1));
    while (byteToColumn(++N) == -1) {
    }
    return N;
  }

  int startOfPreviousColumn(int N) const {
    assert(0 < N && N < static_cast<int>(m_byteToColumn.size()));
    while (byteToColumn(--N) == -1) {
    }
    return N;
  }

  llvm::StringRef getSourceLine() const { return m_SourceLine; }

private:
  const std::string m_SourceLine;
  llvm::SmallVector<int, 200> m_byteToColumn;
  llvm::SmallVector<int, 200> m_columnToByte;
};

// ===----------------------------------------------------------------------===
// Source region selection & word wrapping
// ===----------------------------------------------------------------------===

void selectInterestingSourceRegion(std::string &SourceLine,
                                   std::string &CaretLine,
                                   std::string &FixItInsertionLine,
                                   unsigned Columns,
                                   const SourceColumnMap &map) {
  unsigned CaretColumns = CaretLine.size();
  unsigned FixItColumns = llvm::sys::locale::columnWidth(FixItInsertionLine);
  unsigned MaxColumns = std::max(static_cast<unsigned>(map.columns()),
                                 std::max(CaretColumns, FixItColumns));
  if (MaxColumns <= Columns)
    return;

  // No special characters are allowed in CaretLine.
  assert(llvm::none_of(CaretLine, [](char c) { return c < ' ' || '~' < c; }));

  // Find the slice that we need to display the full caret line
  // correctly.
  unsigned CaretStart = 0, CaretEnd = CaretLine.size();
  for (; CaretStart != CaretEnd; ++CaretStart)
    if (!isWhitespace(CaretLine[CaretStart]))
      break;

  for (; CaretEnd != CaretStart; --CaretEnd)
    if (!isWhitespace(CaretLine[CaretEnd - 1]))
      break;

  // caret has already been inserted into CaretLine so the above whitespace
  // check is guaranteed to include the caret

  // If we have a fix-it line, make sure the slice includes all of the
  // fix-it information.
  if (!FixItInsertionLine.empty()) {
    unsigned FixItStart = 0, FixItEnd = FixItInsertionLine.size();
    for (; FixItStart != FixItEnd; ++FixItStart)
      if (!isWhitespace(FixItInsertionLine[FixItStart]))
        break;

    for (; FixItEnd != FixItStart; --FixItEnd)
      if (!isWhitespace(FixItInsertionLine[FixItEnd - 1]))
        break;

    // We can safely use the byte offset FixItStart as the column offset
    // because the characters up until FixItStart are all ASCII whitespace
    // characters.
    unsigned FixItStartCol = FixItStart;
    unsigned FixItEndCol =
        llvm::sys::locale::columnWidth(FixItInsertionLine.substr(0, FixItEnd));

    CaretStart = std::min(FixItStartCol, CaretStart);
    CaretEnd = std::max(FixItEndCol, CaretEnd);
  }

  // CaretEnd may have been set at the middle of a character
  // If it's not at a character's first column then advance it past the current
  //   character.
  while (static_cast<int>(CaretEnd) < map.columns() &&
         -1 == map.columnToByte(CaretEnd))
    ++CaretEnd;

  assert((static_cast<int>(CaretStart) > map.columns() ||
          -1 != map.columnToByte(CaretStart)) &&
         "CaretStart must not point to a column in the middle of a source"
         " line character");
  assert((static_cast<int>(CaretEnd) > map.columns() ||
          -1 != map.columnToByte(CaretEnd)) &&
         "CaretEnd must not point to a column in the middle of a source line"
         " character");

  // CaretLine[CaretStart, CaretEnd) contains all of the interesting
  // parts of the caret line. While this slice is smaller than the
  // number of columns we have, try to grow the slice to encompass
  // more context.

  unsigned SourceStart =
      map.columnToByte(std::min<unsigned>(CaretStart, map.columns()));
  unsigned SourceEnd =
      map.columnToByte(std::min<unsigned>(CaretEnd, map.columns()));

  unsigned CaretColumnsOutsideSource =
      CaretEnd - CaretStart -
      (map.byteToColumn(SourceEnd) - map.byteToColumn(SourceStart));

  char const *front_ellipse = "  ...";
  char const *front_space = "     ";
  char const *back_ellipse = "...";
  unsigned ellipses_space = strlen(front_ellipse) + strlen(back_ellipse);

  unsigned TargetColumns = Columns;
  // Give us extra room for the ellipses
  //  and any of the caret line that extends past the source
  if (TargetColumns > ellipses_space + CaretColumnsOutsideSource)
    TargetColumns -= ellipses_space + CaretColumnsOutsideSource;

  while (SourceStart > 0 || SourceEnd < SourceLine.size()) {
    bool ExpandedRegion = false;

    if (SourceStart > 0) {
      unsigned NewStart = map.startOfPreviousColumn(SourceStart);

      // Skip over any whitespace we see here; we're looking for
      // another bit of interesting text.
      while (NewStart && isWhitespace(SourceLine[NewStart]))
        NewStart = map.startOfPreviousColumn(NewStart);

      // Skip over this bit of "interesting" text.
      while (NewStart) {
        unsigned Prev = map.startOfPreviousColumn(NewStart);
        if (isWhitespace(SourceLine[Prev]))
          break;
        NewStart = Prev;
      }

      assert(map.byteToColumn(NewStart) != -1);
      unsigned NewColumns =
          map.byteToColumn(SourceEnd) - map.byteToColumn(NewStart);
      if (NewColumns <= TargetColumns) {
        SourceStart = NewStart;
        ExpandedRegion = true;
      }
    }

    if (SourceEnd < SourceLine.size()) {
      unsigned NewEnd = map.startOfNextColumn(SourceEnd);

      // Skip over any whitespace we see here; we're looking for
      // another bit of interesting text.
      while (NewEnd < SourceLine.size() && isWhitespace(SourceLine[NewEnd]))
        NewEnd = map.startOfNextColumn(NewEnd);

      // Skip over this bit of "interesting" text.
      while (NewEnd < SourceLine.size() && isWhitespace(SourceLine[NewEnd]))
        NewEnd = map.startOfNextColumn(NewEnd);

      assert(map.byteToColumn(NewEnd) != -1);
      unsigned NewColumns =
          map.byteToColumn(NewEnd) - map.byteToColumn(SourceStart);
      if (NewColumns <= TargetColumns) {
        SourceEnd = NewEnd;
        ExpandedRegion = true;
      }
    }

    if (!ExpandedRegion)
      break;
  }

  CaretStart = map.byteToColumn(SourceStart);
  CaretEnd = map.byteToColumn(SourceEnd) + CaretColumnsOutsideSource;

  // [CaretStart, CaretEnd) is the slice we want. Update the various
  // output lines to show only this slice.
  assert(CaretStart != (unsigned)-1 && CaretEnd != (unsigned)-1 &&
         SourceStart != (unsigned)-1 && SourceEnd != (unsigned)-1);
  assert(SourceStart <= SourceEnd);
  assert(CaretStart <= CaretEnd);

  unsigned BackColumnsRemoved =
      map.byteToColumn(SourceLine.size()) - map.byteToColumn(SourceEnd);
  unsigned FrontColumnsRemoved = CaretStart;
  unsigned ColumnsKept = CaretEnd - CaretStart;

  // We checked up front that the line needed truncation
  assert(FrontColumnsRemoved + ColumnsKept + BackColumnsRemoved > Columns);

  // The line needs some truncation, and we'd prefer to keep the front
  //  if possible, so remove the back
  if (BackColumnsRemoved > strlen(back_ellipse))
    SourceLine.replace(SourceEnd, std::string::npos, back_ellipse);

  // If that's enough then we're done
  if (FrontColumnsRemoved + ColumnsKept <= Columns)
    return;

  // Otherwise remove the front as well
  if (FrontColumnsRemoved > strlen(front_ellipse)) {
    SourceLine.replace(0, SourceStart, front_ellipse);
    CaretLine.replace(0, CaretStart, front_space);
    if (!FixItInsertionLine.empty())
      FixItInsertionLine.replace(0, CaretStart, front_space);
  }
}

unsigned skipWhitespace(unsigned Idx, llvm::StringRef Str, unsigned Length) {
  while (Idx < Length && isWhitespace(Str[Idx]))
    ++Idx;
  return Idx;
}

char findMatchingPunctuation(char c) {
  switch (c) {
  case '\'':
    return '\'';
  case '`':
    return '\'';
  case '"':
    return '"';
  case '(':
    return ')';
  case '[':
    return ']';
  case '{':
    return '}';
  default:
    break;
  }

  return 0;
}

unsigned findEndOfWord(unsigned Start, llvm::StringRef Str, unsigned Length,
                       unsigned Column, unsigned Columns) {
  assert(Start < Str.size() && "Invalid start position!");
  unsigned End = Start + 1;

  // If we are already at the end of the string, take that as the word.
  if (End == Str.size())
    return End;

  // Determine if the start of the string is actually opening
  // punctuation, e.g., a quote or parentheses.
  char EndPunct = findMatchingPunctuation(Str[Start]);
  if (!EndPunct) {
    // This is a normal word. Just find the first space character.
    while (End < Length && !isWhitespace(Str[End]))
      ++End;
    return End;
  }

  // We have the start of a balanced punctuation sequence (quotes,
  // parentheses, etc.). Determine the full sequence is.
  llvm::SmallString<16> PunctuationEndStack;
  PunctuationEndStack.push_back(EndPunct);
  while (End < Length && !PunctuationEndStack.empty()) {
    if (Str[End] == PunctuationEndStack.back())
      PunctuationEndStack.pop_back();
    else if (char SubEndPunct = findMatchingPunctuation(Str[End]))
      PunctuationEndStack.push_back(SubEndPunct);

    ++End;
  }

  while (End < Length && !isWhitespace(Str[End]))
    ++End;

  unsigned PunctWordLength = End - Start;
  if ( // If the word fits on this line
      Column + PunctWordLength <= Columns ||
      // ... or the word is "short enough" to take up the next line
      // without too much ugly white space
      PunctWordLength < Columns / 3)
    return End; // Take the whole thing as a single "word".

  // The whole quoted/parenthesized string is too long to print as a
  // single "word". Instead, find the "word" that starts just after
  // the punctuation and use that end-point instead. This will recurse
  // until it finds something small enough to consider a word.
  return findEndOfWord(Start + 1, Str, Length, Column + 1, Columns);
}

bool printWordWrapped(llvm::raw_ostream &OS, llvm::StringRef Str,
                      unsigned Columns, unsigned Column, bool Bold) {
  const unsigned Length = std::min(Str.find('\n'), Str.size());
  bool TextNormal = true;

  bool Wrapped = false;
  for (unsigned WordStart = 0, WordEnd; WordStart < Length;
       WordStart = WordEnd) {
    WordStart = skipWhitespace(WordStart, Str, Length);
    if (WordStart == Length)
      break;

    WordEnd = findEndOfWord(WordStart, Str, Length, Column, Columns);

    unsigned WordLength = WordEnd - WordStart;
    if (Column + WordLength < Columns) {
      if (WordStart) {
        OS << ' ';
        Column += 1;
      }
      applyTemplateHighlighting(OS, Str.substr(WordStart, WordLength),
                                TextNormal, Bold);
      Column += WordLength;
      continue;
    }

    // This word does not fit on the current line, so wrap to the next
    // line.
    OS << '\n';
    OS.indent(WordWrapIndentation);
    applyTemplateHighlighting(OS, Str.substr(WordStart, WordLength), TextNormal,
                              Bold);
    Column = WordWrapIndentation + WordLength;
    Wrapped = true;
  }

  // Append any remaning text from the message with its existing formatting.
  applyTemplateHighlighting(OS, Str.substr(Length), TextNormal, Bold);

  assert(TextNormal && "Text highlighted at end of diagnostic message.");

  return Wrapped;
}

} // namespace

// ===----------------------------------------------------------------------===
// TextDiagnostic public interface
// ===----------------------------------------------------------------------===

TextDiagnostic::TextDiagnostic(llvm::raw_ostream &OS,
                               const LangOptions &LangOpts,
                               DiagnosticOptions *DiagOpts)
    : DiagnosticRenderer(LangOpts, DiagOpts), OS(OS) {}

TextDiagnostic::~TextDiagnostic() {}

__attribute__((cold)) void TextDiagnostic::emitDiagnosticMessage(
    FullSourceLoc Loc, PresumedLoc PLoc, DiagnosticsEngine::Level Level,
    llvm::StringRef Message, llvm::ArrayRef<neverc::CharSourceRange> Ranges,
    DiagOrStoredDiag D) {
  uint64_t StartOfLocationInfo = OS.tell();

  if (Loc.isValid())
    emitDiagnosticLoc(Loc, PLoc, Level, Ranges);

  if (DiagOpts->ShowColors)
    OS.resetColor();

  if (DiagOpts->ShowLevel)
    printDiagnosticLevel(OS, Level, DiagOpts->ShowColors);
  printDiagnosticMessage(OS,
                         /*IsSupplemental*/ Level == DiagnosticsEngine::Note,
                         Message, OS.tell() - StartOfLocationInfo,
                         DiagOpts->MessageLength, DiagOpts->ShowColors);
}

/*static*/ void TextDiagnostic::printDiagnosticLevel(
    llvm::raw_ostream &OS, DiagnosticsEngine::Level Level, bool ShowColors) {
  if (ShowColors) {
    // Print diagnostic category in bold and color
    switch (Level) {
    case DiagnosticsEngine::Ignored:
      llvm_unreachable("Invalid diagnostic type");
    case DiagnosticsEngine::Note:
      OS.changeColor(noteColor, true);
      break;
    case DiagnosticsEngine::Remark:
      OS.changeColor(remarkColor, true);
      break;
    case DiagnosticsEngine::Warning:
      OS.changeColor(warningColor, true);
      break;
    case DiagnosticsEngine::Error:
      OS.changeColor(errorColor, true);
      break;
    case DiagnosticsEngine::Fatal:
      OS.changeColor(fatalColor, true);
      break;
    }
  }

  switch (Level) {
  case DiagnosticsEngine::Ignored:
    llvm_unreachable("Invalid diagnostic type");
  case DiagnosticsEngine::Note:
    OS << "note: ";
    break;
  case DiagnosticsEngine::Remark:
    OS << "remark: ";
    break;
  case DiagnosticsEngine::Warning:
    OS << "warning: ";
    break;
  case DiagnosticsEngine::Error:
    OS << "error: ";
    break;
  case DiagnosticsEngine::Fatal:
    OS << "fatal error: ";
    break;
  }

  if (ShowColors)
    OS.resetColor();
}

/*static*/
void TextDiagnostic::printDiagnosticMessage(llvm::raw_ostream &OS,
                                            bool IsSupplemental,
                                            llvm::StringRef Message,
                                            unsigned CurrentColumn,
                                            unsigned Columns, bool ShowColors) {
  bool Bold = false;
  if (ShowColors && !IsSupplemental) {
    // Print primary diagnostic messages in bold and without color, to visually
    // indicate the transition from continuation notes and other output.
    OS.changeColor(savedColor, true);
    Bold = true;
  }

  if (Columns)
    printWordWrapped(OS, Message, Columns, CurrentColumn, Bold);
  else {
    bool Normal = true;
    applyTemplateHighlighting(OS, Message, Normal, Bold);
    assert(Normal && "Formatting should have returned to normal");
  }

  if (ShowColors)
    OS.resetColor();
  OS << '\n';
}

void TextDiagnostic::emitFilename(llvm::StringRef Filename,
                                  const SourceManager &SM) {
#ifdef _WIN32
  llvm::SmallString<4096> TmpFilename;
#endif
  if (DiagOpts->AbsolutePath) {
    auto File = SM.getFileManager().getOptionalFileRef(Filename);
    if (File) {
      // We want to print a simplified absolute path, i. e. without "dots".
      //
      // The hardest part here are the paths like "<part1>/<link>/../<part2>".
      // On Unix-like systems, we cannot just collapse "<link>/..", because
      // paths are resolved sequentially, and, thereby, the path
      // "<part1>/<part2>" may point to a different location. That is why
      // we use FileManager::getCanonicalName(), which expands all indirections
      // with llvm::sys::fs::real_path() and caches the result.
      //
      // On the other hand, it would be better to preserve as much of the
      // original path as possible, because that helps a user to recognize it.
      // real_path() expands all links, which sometimes too much. Luckily,
      // on Windows we can just use llvm::sys::path::remove_dots(), because,
      // on that system, both aforementioned paths point to the same place.
#ifdef _WIN32
      TmpFilename = File->getName();
      llvm::sys::fs::make_absolute(TmpFilename);
      llvm::sys::path::native(TmpFilename);
      llvm::sys::path::remove_dots(TmpFilename, /* remove_dot_dot */ true);
      Filename = llvm::StringRef(TmpFilename.data(), TmpFilename.size());
#else
      Filename = SM.getFileManager().getCanonicalName(*File);
#endif
    }
  }

  OS << Filename;
}

void TextDiagnostic::emitDiagnosticLoc(FullSourceLoc Loc, PresumedLoc PLoc,
                                       DiagnosticsEngine::Level Level,
                                       llvm::ArrayRef<CharSourceRange> Ranges) {
  if (PLoc.isInvalid()) {
    // At least print the file name if available:
    if (FileID FID = Loc.getFileID(); FID.isValid()) {
      if (OptionalFileEntryRef FE = Loc.getFileEntryRef()) {
        emitFilename(FE->getName(), Loc.getManager());
        OS << ": ";
      }
    }
    return;
  }
  unsigned LineNo = PLoc.getLine();

  if (!DiagOpts->ShowLocation)
    return;

  if (DiagOpts->ShowColors)
    OS.changeColor(savedColor, true);

  emitFilename(PLoc.getFilename(), Loc.getManager());
  switch (DiagOpts->getFormat()) {
  case DiagnosticOptions::NeverC:
    if (DiagOpts->ShowLine)
      OS << ':' << LineNo;
    break;
  case DiagnosticOptions::MSVC:
    OS << '(' << LineNo;
    break;
  case DiagnosticOptions::Vi:
    OS << " +" << LineNo;
    break;
  }

  if (DiagOpts->ShowColumn)
    if (unsigned ColNo = PLoc.getColumn()) {
      if (DiagOpts->getFormat() == DiagnosticOptions::MSVC) {
        OS << ',';
        // Visual Studio 2010 or earlier expects column number to be off by one
        if (LangOpts.MSCompatibilityVersion &&
            !LangOpts.isCompatibleWithMSVC(LangOptions::MSVC2012))
          ColNo--;
      } else
        OS << ':';
      OS << ColNo;
    }
  switch (DiagOpts->getFormat()) {
  case DiagnosticOptions::NeverC:
  case DiagnosticOptions::Vi:
    OS << ':';
    break;
  case DiagnosticOptions::MSVC:
    // MSVC2013 and before print 'file(4) : error'. MSVC2015 gets rid of the
    // space and prints 'file(4): error'.
    OS << ')';
    if (LangOpts.MSCompatibilityVersion &&
        !LangOpts.isCompatibleWithMSVC(LangOptions::MSVC2015))
      OS << ' ';
    OS << ':';
    break;
  }

  if (DiagOpts->ShowSourceRanges && !Ranges.empty()) {
    FileID CaretFileID = Loc.getExpansionLoc().getFileID();
    bool PrintedRange = false;
    const SourceManager &SM = Loc.getManager();

    for (const auto &R : Ranges) {
      // Ignore invalid ranges.
      if (!R.isValid())
        continue;

      SourceLocation B = SM.getExpansionLoc(R.getBegin());
      CharSourceRange ERange = SM.getExpansionRange(R.getEnd());
      SourceLocation E = ERange.getEnd();

      // If the start or end of the range is in another file, just
      // discard it.
      if (SM.getFileID(B) != CaretFileID || SM.getFileID(E) != CaretFileID)
        continue;

      // Add in the length of the token, so that we cover multi-char
      // tokens.
      unsigned TokSize = 0;
      if (ERange.isTokenRange())
        TokSize = SourceScanner::measureTokenLength(E, SM, LangOpts);

      FullSourceLoc BF(B, SM), EF(E, SM);
      OS << '{' << BF.getLineNumber() << ':' << BF.getColumnNumber() << '-'
         << EF.getLineNumber() << ':' << (EF.getColumnNumber() + TokSize)
         << '}';
      PrintedRange = true;
    }

    if (PrintedRange)
      OS << ':';
  }
  OS << ' ';
}

void TextDiagnostic::emitIncludeLocation(FullSourceLoc Loc, PresumedLoc PLoc) {
  if (DiagOpts->ShowLocation && PLoc.isValid()) {
    OS << "In file included from ";
    emitFilename(PLoc.getFilename(), Loc.getManager());
    OS << ':' << PLoc.getLine() << ":\n";
  } else
    OS << "In included file:\n";
}

// ===----------------------------------------------------------------------===
// Source-range and line helpers
// ===----------------------------------------------------------------------===

namespace {

std::optional<std::pair<unsigned, unsigned>>
findLinesForRange(const CharSourceRange &R, FileID FID,
                  const SourceManager &SM) {
  if (!R.isValid())
    return std::nullopt;

  SourceLocation Begin = R.getBegin();
  SourceLocation End = R.getEnd();
  if (SM.getFileID(Begin) != FID || SM.getFileID(End) != FID)
    return std::nullopt;

  return std::make_pair(SM.getExpansionLineNumber(Begin),
                        SM.getExpansionLineNumber(End));
}

std::pair<unsigned, unsigned> maybeAddRange(std::pair<unsigned, unsigned> A,
                                            std::pair<unsigned, unsigned> B,
                                            unsigned MaxRange) {
  // If A is already the maximum size, we're done.
  unsigned Slack = MaxRange - (A.second - A.first + 1);
  if (Slack == 0)
    return A;

  // Easy case: merge succeeds within MaxRange.
  unsigned Min = std::min(A.first, B.first);
  unsigned Max = std::max(A.second, B.second);
  if (Max - Min + 1 <= MaxRange)
    return {Min, Max};

  // If we can't reach B from A within MaxRange, there's nothing to do.
  // Don't add lines to the range that contain nothing interesting.
  if ((B.first > A.first && B.first - A.first + 1 > MaxRange) ||
      (B.second < A.second && A.second - B.second + 1 > MaxRange))
    return A;

  // Otherwise, expand A towards B to produce a range of size MaxRange. We
  // attempt to expand by the same amount in both directions if B strictly
  // contains A.

  // Expand downwards by up to half the available amount, then upwards as
  // much as possible, then downwards as much as possible.
  A.second = std::min(A.second + (Slack + 1) / 2, Max);
  Slack = MaxRange - (A.second - A.first + 1);
  A.first = std::max(Min + Slack, A.first) - Slack;
  A.second = std::min(A.first + MaxRange - 1, Max);
  return A;
}

struct LineRange {
  unsigned LineNo;
  unsigned StartCol;
  unsigned EndCol;
};

void highlightRange(const LineRange &R, const SourceColumnMap &Map,
                    std::string &CaretLine) {
  // Pick the first non-whitespace column.
  unsigned StartColNo = R.StartCol;
  while (StartColNo < Map.getSourceLine().size() &&
         (Map.getSourceLine()[StartColNo] == ' ' ||
          Map.getSourceLine()[StartColNo] == '\t'))
    StartColNo = Map.startOfNextColumn(StartColNo);

  // Pick the last non-whitespace column.
  unsigned EndColNo =
      std::min(static_cast<size_t>(R.EndCol), Map.getSourceLine().size());
  while (EndColNo && (Map.getSourceLine()[EndColNo - 1] == ' ' ||
                      Map.getSourceLine()[EndColNo - 1] == '\t'))
    EndColNo = Map.startOfPreviousColumn(EndColNo);

  // If the start/end passed each other, then we are trying to highlight a
  // range that just exists in whitespace. That most likely means we have
  // a multi-line highlighting range that covers a blank line.
  if (StartColNo > EndColNo)
    return;

  // Fill the range with ~'s.
  StartColNo = Map.byteToContainingColumn(StartColNo);
  EndColNo = Map.byteToContainingColumn(EndColNo);

  assert(StartColNo <= EndColNo && "Invalid range!");
  if (CaretLine.size() < EndColNo)
    CaretLine.resize(EndColNo, ' ');
  std::fill(CaretLine.begin() + StartColNo, CaretLine.begin() + EndColNo, '~');
}

std::string buildFixItInsertionLine(FileID FID, unsigned LineNo,
                                    const SourceColumnMap &map,
                                    llvm::ArrayRef<FixItHint> Hints,
                                    const SourceManager &SM,
                                    const DiagnosticOptions *DiagOpts) {
  std::string FixItInsertionLine;
  if (Hints.empty() || !DiagOpts->ShowFixits)
    return FixItInsertionLine;
  unsigned PrevHintEndCol = 0;

  for (const auto &H : Hints) {
    if (H.CodeToInsert.empty())
      continue;

    // We have an insertion hint. Determine whether the inserted
    // code contains no newlines and is on the same line as the caret.
    std::pair<FileID, unsigned> HintLocInfo =
        SM.getDecomposedExpansionLoc(H.RemoveRange.getBegin());
    if (FID == HintLocInfo.first &&
        LineNo == SM.getLineNumber(HintLocInfo.first, HintLocInfo.second) &&
        llvm::StringRef(H.CodeToInsert).find_first_of("\n\r") ==
            llvm::StringRef::npos) {
      // Insert the new code into the line just below the code
      // that the user wrote.
      // Note: When modifying this function, be very careful about what is a
      // "column" (printed width, platform-dependent) and what is a
      // "byte offset" (SourceManager "column").
      unsigned HintByteOffset =
          SM.getColumnNumber(HintLocInfo.first, HintLocInfo.second) - 1;

      // The hint must start inside the source or right at the end
      assert(HintByteOffset < static_cast<unsigned>(map.bytes()) + 1);
      unsigned HintCol = map.byteToContainingColumn(HintByteOffset);

      // If we inserted a long previous hint, push this one forwards, and add
      // an extra space to show that this is not part of the previous
      // completion. This is sort of the best we can do when two hints appear
      // to overlap.
      //
      // Note that if this hint is located immediately after the previous
      // hint, no space will be added, since the location is more important.
      if (HintCol < PrevHintEndCol)
        HintCol = PrevHintEndCol + 1;

      // This should NOT use HintByteOffset, because the source might have
      // Unicode characters in earlier columns.
      unsigned NewFixItLineSize = FixItInsertionLine.size() +
                                  (HintCol - PrevHintEndCol) +
                                  H.CodeToInsert.size();
      if (NewFixItLineSize > FixItInsertionLine.size())
        FixItInsertionLine.resize(NewFixItLineSize, ' ');

      std::copy(H.CodeToInsert.begin(), H.CodeToInsert.end(),
                FixItInsertionLine.end() - H.CodeToInsert.size());

      PrevHintEndCol = HintCol + llvm::sys::locale::columnWidth(H.CodeToInsert);
    }
  }

  expandTabs(FixItInsertionLine, DiagOpts->TabStop);

  return FixItInsertionLine;
}

unsigned getNumDisplayWidth(unsigned N) {
  unsigned L = 1u, M = 10u;
  while (M <= N && ++L != std::numeric_limits<unsigned>::digits10 + 1)
    M *= 10u;

  return L;
}

llvm::SmallVector<LineRange>
prepareAndFilterRanges(const llvm::SmallVectorImpl<CharSourceRange> &Ranges,
                       const SourceManager &SM,
                       const std::pair<unsigned, unsigned> &Lines, FileID FID,
                       const LangOptions &LangOpts) {
  llvm::SmallVector<LineRange> LineRanges;

  for (const CharSourceRange &R : Ranges) {
    if (R.isInvalid())
      continue;
    SourceLocation Begin = R.getBegin();
    SourceLocation End = R.getEnd();

    unsigned StartLineNo = SM.getExpansionLineNumber(Begin);
    if (StartLineNo > Lines.second || SM.getFileID(Begin) != FID)
      continue;

    unsigned EndLineNo = SM.getExpansionLineNumber(End);
    if (EndLineNo < Lines.first || SM.getFileID(End) != FID)
      continue;

    unsigned StartColumn = SM.getExpansionColumnNumber(Begin);
    unsigned EndColumn = SM.getExpansionColumnNumber(End);
    if (R.isTokenRange())
      EndColumn += SourceScanner::measureTokenLength(End, SM, LangOpts);

    // Only a single line.
    if (StartLineNo == EndLineNo) {
      LineRanges.push_back({StartLineNo, StartColumn - 1, EndColumn - 1});
      continue;
    }

    // Start line.
    LineRanges.push_back({StartLineNo, StartColumn - 1, ~0u});

    // Middle lines.
    for (unsigned S = StartLineNo + 1; S != EndLineNo; ++S)
      LineRanges.push_back({S, 0, ~0u});

    // End line.
    LineRanges.push_back({EndLineNo, 0, EndColumn - 1});
  }

  return LineRanges;
}

} // namespace

// ===----------------------------------------------------------------------===
// Snippet & caret emission
// ===----------------------------------------------------------------------===

__attribute__((cold)) void TextDiagnostic::emitSnippetAndCaret(
    FullSourceLoc Loc, DiagnosticsEngine::Level Level,
    llvm::SmallVectorImpl<CharSourceRange> &Ranges,
    llvm::ArrayRef<FixItHint> Hints) {
  assert(Loc.isValid() && "must have a valid source location here");
  assert(Loc.isFileID() && "must have a file location here");

  // If caret diagnostics are enabled and we have location, we want to
  // emit the caret.  However, we only do this if the location moved
  // from the last diagnostic, if the last diagnostic was a note that
  // was part of a different warning or error diagnostic, or if the
  // diagnostic has ranges.  We don't want to emit the same caret
  // multiple times if one loc has multiple diagnostics.
  if (!DiagOpts->ShowCarets)
    return;
  if (Loc == LastLoc && Ranges.empty() && Hints.empty() &&
      (LastLevel != DiagnosticsEngine::Note || Level == LastLevel))
    return;

  FileID FID = Loc.getFileID();
  const SourceManager &SM = Loc.getManager();

  bool Invalid = false;
  llvm::StringRef BufData = Loc.getBufferData(&Invalid);
  if (Invalid)
    return;
  const char *BufStart = BufData.data();
  const char *BufEnd = BufStart + BufData.size();

  unsigned CaretLineNo = Loc.getLineNumber();
  unsigned CaretColNo = Loc.getColumnNumber();

  // Arbitrarily stop showing snippets when the line is too long.
  static const size_t MaxLineLengthToPrint = 4096;
  if (CaretColNo > MaxLineLengthToPrint)
    return;

  const unsigned MaxLines = DiagOpts->SnippetLineLimit;
  std::pair<unsigned, unsigned> Lines = {CaretLineNo, CaretLineNo};
  unsigned DisplayLineNo = Loc.getPresumedLoc().getLine();
  for (const auto &I : Ranges) {
    if (auto OptionalRange = findLinesForRange(I, FID, SM))
      Lines = maybeAddRange(Lines, *OptionalRange, MaxLines);

    DisplayLineNo =
        std::min(DisplayLineNo, SM.getPresumedLineNumber(I.getBegin()));
  }

  // Our line numbers look like:
  // " [number] | "
  // Where [number] is MaxLineNoDisplayWidth columns
  // and the full thing is therefore MaxLineNoDisplayWidth + 4 columns.
  unsigned MaxLineNoDisplayWidth =
      DiagOpts->ShowLineNumbers
          ? std::max(4u, getNumDisplayWidth(DisplayLineNo + MaxLines))
          : 0;
  auto indentForLineNumbers = [&] {
    if (MaxLineNoDisplayWidth > 0)
      OS.indent(MaxLineNoDisplayWidth + 2) << "| ";
  };

  llvm::SmallVector<LineRange> LineRanges =
      prepareAndFilterRanges(Ranges, SM, Lines, FID, LangOpts);

  for (unsigned LineNo = Lines.first; LineNo != Lines.second + 1;
       ++LineNo, ++DisplayLineNo) {
    const char *LineStart =
        BufStart +
        SM.getDecomposedLoc(SM.translateLineCol(FID, LineNo, 1)).second;
    if (LineStart == BufEnd)
      break;

    const char *LineEnd = LineStart;
    while (*LineEnd != '\n' && *LineEnd != '\r' && LineEnd != BufEnd)
      ++LineEnd;

    if (size_t(LineEnd - LineStart) > MaxLineLengthToPrint)
      return;

    std::string SourceLine(LineStart, LineEnd);
    while (!SourceLine.empty() && SourceLine.back() == '\0' &&
           (LineNo != CaretLineNo || SourceLine.size() > CaretColNo))
      SourceLine.pop_back();

    const SourceColumnMap sourceColMap(SourceLine, DiagOpts->TabStop);

    std::string CaretLine;
    for (const auto &LR : LineRanges) {
      if (LR.LineNo == LineNo)
        highlightRange(LR, sourceColMap, CaretLine);
    }

    if (CaretLineNo == LineNo) {
      size_t Col = sourceColMap.byteToContainingColumn(CaretColNo - 1);
      CaretLine.resize(std::max(Col + 1, CaretLine.size()), ' ');
      CaretLine[Col] = '^';
    }

    std::string FixItInsertionLine = buildFixItInsertionLine(
        FID, LineNo, sourceColMap, Hints, SM, DiagOpts.get());

    unsigned Columns = DiagOpts->MessageLength;
    if (Columns)
      selectInterestingSourceRegion(SourceLine, CaretLine, FixItInsertionLine,
                                    Columns, sourceColMap);

    // If we are in -fdiagnostics-print-source-range-info mode, we are trying
    // to produce easily machine parsable output.  Add a space before the
    // source line and the caret to make it trivial to tell the main diagnostic
    // line from what the user is intended to see.
    if (DiagOpts->ShowSourceRanges && !SourceLine.empty()) {
      SourceLine = ' ' + SourceLine;
      CaretLine = ' ' + CaretLine;
    }

    emitSnippet(SourceLine, MaxLineNoDisplayWidth, DisplayLineNo);

    if (!CaretLine.empty()) {
      indentForLineNumbers();
      if (DiagOpts->ShowColors)
        OS.changeColor(caretColor, true);
      OS << CaretLine << '\n';
      if (DiagOpts->ShowColors)
        OS.resetColor();
    }

    if (!FixItInsertionLine.empty()) {
      indentForLineNumbers();
      if (DiagOpts->ShowColors)
        OS.changeColor(fixitColor, false);
      if (DiagOpts->ShowSourceRanges)
        OS << ' ';
      OS << FixItInsertionLine << '\n';
      if (DiagOpts->ShowColors)
        OS.resetColor();
    }
  }

  emitParseableFixits(Hints, SM);
}

void TextDiagnostic::emitSnippet(llvm::StringRef SourceLine,
                                 unsigned MaxLineNoDisplayWidth,
                                 unsigned LineNo) {
  if (MaxLineNoDisplayWidth > 0) {
    unsigned LineNoDisplayWidth = getNumDisplayWidth(LineNo);
    OS.indent(MaxLineNoDisplayWidth - LineNoDisplayWidth + 1)
        << LineNo << " | ";
  }

  bool PrintReversed = false;
  size_t I = 0;
  while (I < SourceLine.size()) {
    auto [Str, WasPrintable] =
        printableTextForNextCharacter(SourceLine, &I, DiagOpts->TabStop);

    if (DiagOpts->ShowColors) {
      if (WasPrintable == PrintReversed) {
        PrintReversed = !PrintReversed;
        if (PrintReversed)
          OS.reverseColor();
        else
          OS.resetColor();
      }
    }
    OS << Str;
  }

  if (DiagOpts->ShowColors)
    OS.resetColor();

  OS << '\n';
}

void TextDiagnostic::emitParseableFixits(llvm::ArrayRef<FixItHint> Hints,
                                         const SourceManager &SM) {
  if (!DiagOpts->ShowParseableFixits)
    return;

  // We follow FixItRewriter's example in not (yet) handling
  // fix-its in macros.
  for (const auto &H : Hints) {
    if (H.RemoveRange.isInvalid() || H.RemoveRange.getBegin().isMacroID() ||
        H.RemoveRange.getEnd().isMacroID())
      return;
  }

  for (const auto &H : Hints) {
    SourceLocation BLoc = H.RemoveRange.getBegin();
    SourceLocation ELoc = H.RemoveRange.getEnd();

    std::pair<FileID, unsigned> BInfo = SM.getDecomposedLoc(BLoc);
    std::pair<FileID, unsigned> EInfo = SM.getDecomposedLoc(ELoc);

    // Adjust for token ranges.
    if (H.RemoveRange.isTokenRange())
      EInfo.second += SourceScanner::measureTokenLength(ELoc, SM, LangOpts);

    // We specifically do not do word-wrapping or tab-expansion here,
    // because this is supposed to be easy to parse.
    PresumedLoc PLoc = SM.getPresumedLoc(BLoc);
    if (PLoc.isInvalid())
      break;

    OS << "fix-it:\"";
    OS.write_escaped(PLoc.getFilename());
    OS << "\":{" << SM.getLineNumber(BInfo.first, BInfo.second) << ':'
       << SM.getColumnNumber(BInfo.first, BInfo.second) << '-'
       << SM.getLineNumber(EInfo.first, EInfo.second) << ':'
       << SM.getColumnNumber(EInfo.first, EInfo.second) << "}:\"";
    OS.write_escaped(H.CodeToInsert);
    OS << "\"\n";
  }
}
