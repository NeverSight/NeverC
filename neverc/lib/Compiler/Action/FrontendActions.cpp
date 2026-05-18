#include "neverc/Compiler/FrontendActions.h"
#include "neverc/Compiler/CompilerInstance.h"
#include "neverc/Compiler/FrontendDiag.h"
#include "neverc/Foundation/Core/FileManager.h"
#include "neverc/Foundation/LangOpts/LangStandard.h"
#include "neverc/Foundation/Target/TargetInfo.h"
#include "neverc/Scan/PrepEngine.h"
#include "neverc/Tree/Core/TreeConsumer.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

using namespace neverc;

SyntaxOnlyAction::~SyntaxOnlyAction() {}

std::unique_ptr<TreeConsumer>
SyntaxOnlyAction::CreateTreeConsumer(CompilerInstance &CI,
                                     llvm::StringRef InFile) {
  return std::make_unique<TreeConsumer>();
}

void PreprocessOnlyAction::ExecuteAction() {
  PrepEngine &PP = getCompilerInstance().getPrepEngine();

  // Ignore unknown pragmas.
  PP.IgnorePragmas();

  Token Tok;
  // Start parsing the specified input file.
  PP.InitMainInput();
  do {
    PP.Lex(Tok);
  } while (Tok.isNot(tok::eof));
}

void PrintPreprocessedAction::ExecuteAction() {
  CompilerInstance &CI = getCompilerInstance();
  // Output file may need to be set to 'Binary', to avoid converting Unix style
  // line feeds (<LF>) to Microsoft style line feeds (<CR><LF>) on Windows.
  //
  // Look to see what type of line endings the file uses. If there's a
  // CRLF, then we won't open the file up in binary mode. If there is
  // just an LF or CR, then we will open the file up in binary mode.
  // In this fashion, the output format should match the input format, unless
  // the input format has inconsistent line endings.
  //
  // This should be a relatively fast operation since most files won't have
  // all of their source code on a single line. However, that is still a
  // concern, so if we scan for too long, we'll just assume the file should
  // be opened in binary mode.

  bool BinaryMode = false;
  if (llvm::Triple(LLVM_HOST_TRIPLE).isOSWindows()) {
    BinaryMode = true;
    const SourceManager &SM = CI.getSourceManager();
    if (std::optional<llvm::MemoryBufferRef> Buffer =
            SM.getBufferOrNone(SM.getMainFileID())) {
      const char *cur = Buffer->getBufferStart();
      const char *end = Buffer->getBufferEnd();
      const char *next = (cur != end) ? cur + 1 : end;

      // Limit ourselves to only scanning 256 characters into the source
      // file.  This is mostly a check in case the file has no
      // newlines whatsoever.
      if (end - cur > 256)
        end = cur + 256;

      while (next < end) {
        if (*cur == 0x0D) {  // CR
          if (*next == 0x0A) // CRLF
            BinaryMode = false;

          break;
        } else if (*cur == 0x0A) // LF
          break;

        ++cur;
        ++next;
      }
    }
  }

  std::unique_ptr<llvm::raw_ostream> OS =
      CI.createDefaultOutputFile(BinaryMode, getCurrentFileOrBufferName());
  if (!OS)
    return;

  DoPrintPreprocessedInput(CI.getPrepEngine(), OS.get(),
                           CI.getPrepOutputOpts());
}
