#include "PrintPPOutputPrepObserver.h"
#include "neverc/Foundation/Core/CharInfo.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "llvm/ADT/StringRef.h"
using namespace neverc;

namespace {
void outputPrintable(llvm::raw_ostream *OS, llvm::StringRef Str) {
  for (unsigned char Char : Str) {
    if (isPrintable(Char) && Char != '\\' && Char != '"')
      *OS << (char)Char;
    else
      *OS << '\\' << (char)('0' + ((Char >> 6) & 7))
          << (char)('0' + ((Char >> 3) & 7)) << (char)('0' + ((Char >> 0) & 7));
  }
}
} // anonymous namespace

void PrintPPOutputPrepObserver::PragmaMessage(SourceLocation Loc,
                                              llvm::StringRef Namespace,
                                              PragmaMessageKind Kind,
                                              llvm::StringRef Str) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma ";
  if (!Namespace.empty())
    *OS << Namespace << ' ';
  switch (Kind) {
  case PMK_Message:
    *OS << "message(\"";
    break;
  case PMK_Warning:
    *OS << "warning \"";
    break;
  case PMK_Error:
    *OS << "error \"";
    break;
  }

  outputPrintable(OS, Str);
  *OS << '"';
  if (Kind == PMK_Message)
    *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaDebug(SourceLocation Loc,
                                            llvm::StringRef DebugType) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);

  *OS << "#pragma neverc __debug ";
  *OS << DebugType;

  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaDiagnosticPush(
    SourceLocation Loc, llvm::StringRef Namespace) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma " << Namespace << " diagnostic push";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaDiagnosticPop(SourceLocation Loc,
                                                    llvm::StringRef Namespace) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma " << Namespace << " diagnostic pop";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaDiagnostic(SourceLocation Loc,
                                                 llvm::StringRef Namespace,
                                                 diag::Severity Map,
                                                 llvm::StringRef Str) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma " << Namespace << " diagnostic ";
  switch (Map) {
  case diag::Severity::Remark:
    *OS << "remark";
    break;
  case diag::Severity::Warning:
    *OS << "warning";
    break;
  case diag::Severity::Error:
    *OS << "error";
    break;
  case diag::Severity::Ignored:
    *OS << "ignored";
    break;
  case diag::Severity::Fatal:
    *OS << "fatal";
    break;
  }
  *OS << " \"" << Str << '"';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaWarning(
    SourceLocation Loc, PragmaWarningSpecifier WarningSpec,
    llvm::ArrayRef<int> Ids) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);

  *OS << "#pragma warning(";
  switch (WarningSpec) {
  case PWS_Default:
    *OS << "default";
    break;
  case PWS_Disable:
    *OS << "disable";
    break;
  case PWS_Error:
    *OS << "error";
    break;
  case PWS_Once:
    *OS << "once";
    break;
  case PWS_Suppress:
    *OS << "suppress";
    break;
  case PWS_Level1:
    *OS << '1';
    break;
  case PWS_Level2:
    *OS << '2';
    break;
  case PWS_Level3:
    *OS << '3';
    break;
  case PWS_Level4:
    *OS << '4';
    break;
  }
  *OS << ':';

  for (llvm::ArrayRef<int>::iterator I = Ids.begin(), E = Ids.end(); I != E;
       ++I)
    *OS << ' ' << *I;
  *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaWarningPush(SourceLocation Loc,
                                                  int Level) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma warning(push";
  if (Level >= 0)
    *OS << ", " << Level;
  *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaWarningPop(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma warning(pop)";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaExecCharsetPush(SourceLocation Loc,
                                                      llvm::StringRef Str) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma character_execution_set(push";
  if (!Str.empty())
    *OS << ", " << Str;
  *OS << ')';
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaExecCharsetPop(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma character_execution_set(pop)";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaAssumeNonNullBegin(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma neverc assume_nonnull begin";
  setEmittedDirectiveOnThisLine();
}

void PrintPPOutputPrepObserver::PragmaAssumeNonNullEnd(SourceLocation Loc) {
  MoveToLine(Loc, /*RequireStartOfLine=*/true);
  *OS << "#pragma neverc assume_nonnull end";
  setEmittedDirectiveOnThisLine();
}
