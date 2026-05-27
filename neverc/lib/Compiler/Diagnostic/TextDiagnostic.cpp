#include "neverc/Compiler/TextDiagnostic.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/Core/SourceManager.h"
#include "neverc/Foundation/Diagnostic/DiagnosticOptions.h"
#include "neverc/Scan/SourceScanner.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Word-wrap helpers
// ===----------------------------------------------------------------------===

namespace {

const enum llvm::raw_ostream::Colors noteColor = llvm::raw_ostream::CYAN;
const enum llvm::raw_ostream::Colors remarkColor = llvm::raw_ostream::BLUE;
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
