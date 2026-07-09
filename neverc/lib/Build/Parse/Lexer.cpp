#include "neverc/Build/Lexer.h"
#include "neverc/Build/StringUtils.h"

#include "llvm/ADT/StringSwitch.h"

#include <cctype>

namespace neverc {
namespace build {

Lexer::Lexer(llvm::StringRef Filename, llvm::StringRef Content)
    : Filename(Filename.str()), Input(Content.str()) {}

static bool isDirectiveKeyword(llvm::StringRef Word) {
  return llvm::StringSwitch<bool>(Word)
      .Cases("ifeq", "ifneq", "ifdef", "ifndef", true)
      .Cases("else", "endif", "define", "endef", true)
      .Cases("override", "export", "unexport", "undefine", true)
      .Cases("include", "-include", "sinclude", true)
      .Default(false);
}

static bool hasUnquotedAssignOp(const std::string &Line, size_t &OpPos,
                                 size_t &OpLen) {
  int Parens = 0;
  size_t FoundOp = std::string::npos;
  size_t FoundLen = 0;
  for (size_t I = 0; I < Line.size(); ++I) {
    char C = Line[I];
    if (C == '$' && I + 1 < Line.size() &&
        (Line[I + 1] == '(' || Line[I + 1] == '{')) {
      ++Parens;
      ++I;
      continue;
    }
    if (Parens > 0 && (C == ')' || C == '}')) {
      --Parens;
      continue;
    }
    if (Parens > 0)
      continue;

    if (C == '=' && I > 0 && Line[I - 1] == ':' &&
        (I < 2 || Line[I - 2] != ':')) {
      FoundOp = I - 1;
      FoundLen = 2;
      break;
    }
    if (C == '=' && I > 0 && Line[I - 1] == ':' && I >= 2 &&
        Line[I - 2] == ':') {
      FoundOp = I - 2;
      FoundLen = 3;
      break;
    }
    if (C == '=' && I > 0 && Line[I - 1] == '+') {
      FoundOp = I - 1;
      FoundLen = 2;
      break;
    }
    if (C == '=' && I > 0 && Line[I - 1] == '?') {
      FoundOp = I - 1;
      FoundLen = 2;
      break;
    }
    if (C == '=' && I > 0 && Line[I - 1] == '!') {
      FoundOp = I - 1;
      FoundLen = 2;
      break;
    }
    if (C == '=' && (I == 0 || (Line[I - 1] != ':' && Line[I - 1] != '+' &&
                                 Line[I - 1] != '?' && Line[I - 1] != '!' &&
                                 Line[I - 1] != '<' && Line[I - 1] != '>'))) {
      FoundOp = I;
      FoundLen = 1;
      break;
    }
  }

  if (FoundOp == std::string::npos)
    return false;

  // If a rule-separator colon exists before the assignment operator,
  // this is a target-specific variable (target: VAR = value), not a
  // standalone assignment.  Let classifyLine fall through to the rule check.
  Parens = 0;
  for (size_t I = 0; I < FoundOp; ++I) {
    char C = Line[I];
    if (C == '$' && I + 1 < Line.size() &&
        (Line[I + 1] == '(' || Line[I + 1] == '{')) {
      ++Parens;
      ++I;
      continue;
    }
    if (Parens > 0 && (C == ')' || C == '}')) {
      --Parens;
      continue;
    }
    if (Parens > 0)
      continue;
    if (C == ':') {
      if (I + 1 < Line.size() && Line[I + 1] == '=')
        continue;
      if (I + 1 < Line.size() && Line[I + 1] == ':' &&
          I + 2 < Line.size() && Line[I + 2] == '=')
        continue;
      return false;
    }
  }

  OpPos = FoundOp;
  OpLen = FoundLen;
  return true;
}

static bool hasUnquotedColon(const std::string &Line) {
  int Parens = 0;
  for (size_t I = 0; I < Line.size(); ++I) {
    char C = Line[I];
    if (C == '$' && I + 1 < Line.size() &&
        (Line[I + 1] == '(' || Line[I + 1] == '{')) {
      ++Parens;
      ++I;
      continue;
    }
    if (Parens > 0 && (C == ')' || C == '}')) {
      --Parens;
      continue;
    }
    if (Parens > 0)
      continue;

    if (C == ':') {
      if (I + 1 < Line.size() && Line[I + 1] == '=')
        return false;
      if (I >= 1 && Line[I - 1] == ':' &&
          I + 1 < Line.size() && Line[I + 1] == '=')
        return false;
      return true;
    }
    if (C == '=')
      return false;
  }
  return false;
}

MakefileLine Lexer::classifyLine(const std::string &Line, unsigned LineNo,
                                  bool InRecipe) {
  MakefileLine ML;
  ML.LineNumber = LineNo;
  ML.OriginalContent = Line;

  if (Line.empty()) {
    ML.Type = MakefileLine::Empty;
    return ML;
  }

  if (!Line.empty() && Line[0] == '\t') {
    ML.Type = MakefileLine::RecipeLine;
    ML.Content = Line.substr(1);
    return ML;
  }

  std::string Stripped = Line;
  size_t CommentPos = std::string::npos;
  {
    int Parens = 0;
    for (size_t I = 0; I < Stripped.size(); ++I) {
      if (Stripped[I] == '$' && I + 1 < Stripped.size() &&
          (Stripped[I + 1] == '(' || Stripped[I + 1] == '{')) {
        ++Parens;
        ++I;
      } else if (Parens > 0 &&
                 (Stripped[I] == ')' || Stripped[I] == '}')) {
        --Parens;
      } else if (Parens == 0 && Stripped[I] == '#') {
        CommentPos = I;
        break;
      }
    }
  }
  if (CommentPos == 0) {
    ML.Type = MakefileLine::Comment;
    ML.Content = Stripped;
    return ML;
  }
  if (CommentPos != std::string::npos)
    Stripped = Stripped.substr(0, CommentPos);

  while (!Stripped.empty() &&
         (Stripped.back() == ' ' || Stripped.back() == '\t'))
    Stripped.pop_back();

  if (Stripped.empty()) {
    ML.Type = MakefileLine::Empty;
    return ML;
  }

  std::string Word = neverc::build::firstWord(Stripped);

  if (Word == "-include" || Word == "sinclude") {
    ML.Type = MakefileLine::Directive;
    ML.Content = Stripped;
    return ML;
  }

  if (isDirectiveKeyword(Word)) {
    ML.Type = MakefileLine::Directive;
    ML.Content = Stripped;
    return ML;
  }

  size_t OpPos, OpLen;
  if (hasUnquotedAssignOp(Stripped, OpPos, OpLen)) {
    ML.Type = MakefileLine::Assignment;
    ML.Content = Stripped;
    return ML;
  }

  if (hasUnquotedColon(Stripped)) {
    ML.Type = MakefileLine::Rule;
    ML.Content = Stripped;
    return ML;
  }

  ML.Type = MakefileLine::Raw;
  ML.Content = Stripped;
  return ML;
}

std::vector<MakefileLine> Lexer::lex() {
  joinContinuationLines();

  std::vector<MakefileLine> Result;
  std::vector<std::string> RawLines;

  size_t Pos = 0;
  while (Pos < Input.size()) {
    size_t End = Input.find('\n', Pos);
    if (End == std::string::npos)
      End = Input.size();
    RawLines.push_back(Input.substr(Pos, End - Pos));
    Pos = End + 1;
  }

  bool InRecipe = false;
  for (unsigned I = 0; I < RawLines.size(); ++I) {
    MakefileLine ML = classifyLine(RawLines[I], I + 1, InRecipe);
    if (ML.Type == MakefileLine::Rule)
      InRecipe = true;
    else if (ML.Type != MakefileLine::RecipeLine &&
             ML.Type != MakefileLine::Empty &&
             ML.Type != MakefileLine::Comment)
      InRecipe = false;
    Result.push_back(std::move(ML));
  }

  return Result;
}

void Lexer::joinContinuationLines() {
  std::string Result;
  Result.reserve(Input.size());

  size_t I = 0;
  while (I < Input.size()) {
    if (Input[I] == '\\' && I + 1 < Input.size() && Input[I + 1] == '\n') {
      Result += ' ';
      I += 2;
      while (I < Input.size() && (Input[I] == ' ' || Input[I] == '\t'))
        ++I;
    } else {
      Result += Input[I];
      ++I;
    }
  }
  Input = std::move(Result);
}

} // namespace build
} // namespace neverc
