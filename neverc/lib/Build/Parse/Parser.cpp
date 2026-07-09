#include "neverc/Build/Parser.h"
#include "neverc/Build/StringUtils.h"

#include "llvm/ADT/StringSwitch.h"

#include <cctype>

namespace neverc {
namespace build {

static bool findAssignOp(const std::string &Line, size_t &OpPos,
                          size_t &OpLen, AssignMode &Mode) {
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

    if (C == ':' && I + 1 < Line.size() && Line[I + 1] == '=' &&
        (I == 0 || Line[I - 1] != ':')) {
      OpPos = I;
      OpLen = 2;
      Mode = AssignMode::Simple;
      return true;
    }
    if (C == ':' && I >= 1 && Line[I - 1] == ':' && I + 1 < Line.size() &&
        Line[I + 1] == '=') {
      OpPos = I - 1;
      OpLen = 3;
      Mode = AssignMode::Simple;
      return true;
    }
    if (C == '+' && I + 1 < Line.size() && Line[I + 1] == '=') {
      OpPos = I;
      OpLen = 2;
      Mode = AssignMode::Append;
      return true;
    }
    if (C == '?' && I + 1 < Line.size() && Line[I + 1] == '=') {
      OpPos = I;
      OpLen = 2;
      Mode = AssignMode::Conditional;
      return true;
    }
    if (C == '!' && I + 1 < Line.size() && Line[I + 1] == '=') {
      OpPos = I;
      OpLen = 2;
      Mode = AssignMode::Shell;
      return true;
    }
    if (C == '=') {
      OpPos = I;
      OpLen = 1;
      Mode = AssignMode::Recursive;
      return true;
    }
  }
  return false;
}

Parser::Parser(llvm::StringRef Filename, std::vector<MakefileLine> Lines)
    : Filename(Filename.str()), Lines(std::move(Lines)) {}

std::unique_ptr<MakefileAST> Parser::parse() {
  auto AST = std::make_unique<MakefileAST>();
  size_t Idx = 0;
  while (Idx < Lines.size()) {
    auto S = parseLine(Idx);
    if (S)
      AST->Stmts.push_back(std::move(S));
  }
  return AST;
}

std::unique_ptr<Statement> Parser::parseLine(size_t &Idx) {
  if (Idx >= Lines.size())
    return nullptr;

  const MakefileLine &ML = Lines[Idx];

  if (ML.Type == MakefileLine::Empty || ML.Type == MakefileLine::Comment) {
    ++Idx;
    return nullptr;
  }

  if (ML.Type == MakefileLine::RecipeLine) {
    ++Idx;
    return nullptr;
  }

  if (ML.Type == MakefileLine::Directive) {
    std::string Word = trim(ML.Content);
    std::string First;
    {
      size_t Sp = Word.find_first_of(" \t");
      First = (Sp == std::string::npos) ? Word : Word.substr(0, Sp);
    }

    if (First == "override") {
      std::string Rest = trim(Word.substr(8));
      std::string RestFirst;
      {
        size_t Sp = Rest.find_first_of(" \t");
        RestFirst = (Sp == std::string::npos) ? Rest : Rest.substr(0, Sp);
      }
      if (RestFirst == "define") {
        auto D = parseDefine(Rest, ML.LineNumber, Idx);
        if (D) D->Override = true;
        return D;
      }
      if (RestFirst == "undefine") {
        std::string Name = trim(Rest.substr(8));
        auto U = std::make_unique<UndefineDirective>();
        U->Name = Name;
        U->Override = true;
        ++Idx;
        return U;
      }
      auto A = parseAssignment(Rest, ML.LineNumber);
      if (A) {
        A->Override = true;
        ++Idx;
        return A;
      }
      ++Idx;
      return nullptr;
    }

    if (First == "export") {
      std::string Rest = trim(Word.substr(First.size()));
      size_t DummyOp, DummyLen;
      AssignMode DummyMode;
      if (!Rest.empty() && findAssignOp(Rest, DummyOp, DummyLen, DummyMode)) {
        auto A = parseAssignment(Rest, ML.LineNumber);
        if (A) {
          A->Export = true;
          ++Idx;
          return A;
        }
      }
      auto E = parseExport(Word, ML.LineNumber);
      ++Idx;
      return E;
    }

    if (First == "unexport") {
      auto E = parseExport(Word, ML.LineNumber);
      if (E)
        E->IsUnexport = true;
      ++Idx;
      return E;
    }

    if (First == "undefine") {
      std::string Name = trim(Word.substr(8));
      auto U = std::make_unique<UndefineDirective>();
      U->Name = Name;
      ++Idx;
      return U;
    }

    if (First == "ifeq" || First == "ifneq" || First == "ifdef" ||
        First == "ifndef") {
      return parseConditional(Word, ML.LineNumber, Idx);
    }

    if (First == "include" || First == "-include" || First == "sinclude") {
      auto Inc = parseInclude(Word, ML.LineNumber);
      ++Idx;
      return Inc;
    }

    if (First == "define") {
      return parseDefine(Word, ML.LineNumber, Idx);
    }

    if (First == "else" || First == "endif" || First == "endef") {
      ++Idx;
      return nullptr;
    }

    ++Idx;
    return nullptr;
  }

  if (ML.Type == MakefileLine::Assignment) {
    auto A = parseAssignment(ML.Content, ML.LineNumber);
    ++Idx;
    return A;
  }

  if (ML.Type == MakefileLine::Rule) {
    auto TSV = parseTargetVarAssign(ML.Content, ML.LineNumber, Idx);
    if (TSV)
      return TSV;
    return parseRule(ML.Content, ML.LineNumber, Idx);
  }

  if (ML.Type == MakefileLine::Raw && !ML.Content.empty() &&
      ML.Content.find('$') != std::string::npos) {
    auto E = std::make_unique<Expression>();
    E->Text = ML.Content;
    ++Idx;
    return E;
  }

  ++Idx;
  return nullptr;
}

std::unique_ptr<VarAssign> Parser::parseAssignment(const std::string &Line,
                                                    unsigned LineNo) {
  size_t OpPos, OpLen;
  AssignMode Mode;
  if (!findAssignOp(Line, OpPos, OpLen, Mode))
    return nullptr;

  auto A = std::make_unique<VarAssign>();
  A->Name = trim(Line.substr(0, OpPos));
  A->Mode = Mode;
  A->RawValue = trim(Line.substr(OpPos + OpLen));
  return A;
}

std::unique_ptr<Rule> Parser::parseRule(const std::string &Line,
                                         unsigned LineNo, size_t &Idx) {
  auto R = std::make_unique<Rule>();

  size_t ColonPos = std::string::npos;
  {
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
          continue;
        ColonPos = I;
        break;
      }
    }
  }

  if (ColonPos == std::string::npos) {
    ++Idx;
    return nullptr;
  }

  std::string TargetStr = trim(Line.substr(0, ColonPos));
  size_t PrereqStart = ColonPos + 1;

  std::string PrereqStr = trim(Line.substr(PrereqStart));


  R->Targets = splitWordsRespectingVarRefs(TargetStr);

  {
    size_t SecondColon = std::string::npos;
    int SPDepth = 0;
    for (size_t I = 0; I < PrereqStr.size(); ++I) {
      if (PrereqStr[I] == '$' && I + 1 < PrereqStr.size() &&
          (PrereqStr[I + 1] == '(' || PrereqStr[I + 1] == '{')) {
        ++SPDepth;
        ++I;
      } else if (SPDepth > 0 &&
                 (PrereqStr[I] == ')' || PrereqStr[I] == '}')) {
        --SPDepth;
      } else if (SPDepth == 0 && PrereqStr[I] == ':') {
        if (I + 1 < PrereqStr.size() && PrereqStr[I + 1] == '=')
          break;
        SecondColon = I;
        break;
      }
    }
    if (SecondColon != std::string::npos) {
      std::string TargetPattern = trim(PrereqStr.substr(0, SecondColon));
      std::string PrereqPatternsStr = trim(PrereqStr.substr(SecondColon + 1));
      if (TargetPattern.find('%') != std::string::npos) {
        R->IsStaticPattern = true;
        R->StaticTargetPattern = TargetPattern;
        R->StaticPrereqPatterns = splitWordsRespectingVarRefs(PrereqPatternsStr);
        PrereqStr.clear();
      }
    }
  }

  for (auto &T : R->Targets) {
    if (T.find('%') != std::string::npos)
      R->IsPattern = true;
  }

  // Handle inline recipe after semicolon: target: prereqs ; recipe
  std::string InlineRecipe;
  {
    int Depth = 0;
    for (size_t I = 0; I < PrereqStr.size(); ++I) {
      if (PrereqStr[I] == '$' && I + 1 < PrereqStr.size() &&
          (PrereqStr[I + 1] == '(' || PrereqStr[I + 1] == '{')) {
        ++Depth;
        ++I;
      } else if (Depth > 0 &&
                 (PrereqStr[I] == ')' || PrereqStr[I] == '}')) {
        --Depth;
      } else if (Depth == 0 && PrereqStr[I] == ';') {
        InlineRecipe = trim(PrereqStr.substr(I + 1));
        PrereqStr = trim(PrereqStr.substr(0, I));
        break;
      }
    }
  }

  // Find pipe for order-only prereqs, respecting $() nesting.
  size_t PipePos = std::string::npos;
  {
    int Depth = 0;
    for (size_t I = 0; I < PrereqStr.size(); ++I) {
      if (PrereqStr[I] == '$' && I + 1 < PrereqStr.size() &&
          (PrereqStr[I + 1] == '(' || PrereqStr[I + 1] == '{')) {
        ++Depth;
        ++I;
      } else if (Depth > 0 &&
                 (PrereqStr[I] == ')' || PrereqStr[I] == '}')) {
        --Depth;
      } else if (Depth == 0 && PrereqStr[I] == '|') {
        PipePos = I;
        break;
      }
    }
  }

  // Store prerequisites as single strings; expansion + word-splitting
  // happens later in addRule to correctly handle $(function ...) calls.
  if (PipePos != std::string::npos) {
    std::string Normal = trim(PrereqStr.substr(0, PipePos));
    std::string OrderOnly = trim(PrereqStr.substr(PipePos + 1));
    if (!Normal.empty())
      R->Prerequisites.push_back(Normal);
    if (!OrderOnly.empty())
      R->OrderOnlyPrereqs.push_back(OrderOnly);
  } else {
    if (!PrereqStr.empty())
      R->Prerequisites.push_back(PrereqStr);
  }

  if (!InlineRecipe.empty()) {
    Recipe Rec;
    std::string Cmd = InlineRecipe;
    while (!Cmd.empty() &&
           (Cmd[0] == '@' || Cmd[0] == '-' || Cmd[0] == '+')) {
      if (Cmd[0] == '@')
        Rec.Silent = true;
      else if (Cmd[0] == '-')
        Rec.IgnoreError = true;
      else if (Cmd[0] == '+')
        Rec.Force = true;
      Cmd = Cmd.substr(1);
    }
    Rec.Command = Cmd;
    if (!Rec.Command.empty())
      R->Recipes.push_back(Rec);
  }

  ++Idx;
  while (Idx < Lines.size() && Lines[Idx].Type == MakefileLine::RecipeLine) {
    Recipe Rec;
    std::string Cmd = Lines[Idx].Content;
    while (!Cmd.empty() &&
           (Cmd[0] == '@' || Cmd[0] == '-' || Cmd[0] == '+')) {
      if (Cmd[0] == '@')
        Rec.Silent = true;
      else if (Cmd[0] == '-')
        Rec.IgnoreError = true;
      else if (Cmd[0] == '+')
        Rec.Force = true;
      Cmd = Cmd.substr(1);
    }
    Rec.Command = Cmd;
    if (!Rec.Command.empty())
      R->Recipes.push_back(Rec);
    ++Idx;
  }

  return R;
}

static void parseConditionalArgs(llvm::StringRef Keyword,
                                  const std::string &Rest,
                                  Conditional &C) {
  C.CondKind = llvm::StringSwitch<Conditional::Kind>(Keyword)
                   .Case("ifeq", Conditional::IfEq)
                   .Case("ifneq", Conditional::IfNeq)
                   .Case("ifdef", Conditional::IfDef)
                   .Case("ifndef", Conditional::IfNDef)
                   .Default(Conditional::IfEq);

  if (C.CondKind == Conditional::IfDef ||
      C.CondKind == Conditional::IfNDef) {
    C.Arg1 = Rest;
  } else {
    if (!Rest.empty() && Rest[0] == '(') {
      size_t Comma = std::string::npos;
      int Depth = 0;
      for (size_t I = 1; I < Rest.size(); ++I) {
        if (Rest[I] == '(') ++Depth;
        else if (Rest[I] == ')') {
          if (Depth == 0) break;
          --Depth;
        } else if (Rest[I] == ',' && Depth == 0) {
          Comma = I;
          break;
        }
      }
      if (Comma != std::string::npos) {
        C.Arg1 = trim(Rest.substr(1, Comma - 1));
        size_t End = Rest.rfind(')');
        if (End != std::string::npos && End > Comma)
          C.Arg2 = trim(Rest.substr(Comma + 1, End - Comma - 1));
      }
    } else if (!Rest.empty() &&
               (Rest[0] == '\'' || Rest[0] == '"')) {
      char Q = Rest[0];
      size_t End1 = Rest.find(Q, 1);
      if (End1 != std::string::npos) {
        C.Arg1 = Rest.substr(1, End1 - 1);
        size_t Start2 = Rest.find(Q, End1 + 1);
        if (Start2 != std::string::npos) {
          size_t End2 = Rest.find(Q, Start2 + 1);
          if (End2 != std::string::npos)
            C.Arg2 = Rest.substr(Start2 + 1, End2 - Start2 - 1);
        }
      }
    }
  }
}

std::unique_ptr<Conditional> Parser::parseConditional(const std::string &Line,
                                                       unsigned LineNo,
                                                       size_t &Idx) {
  auto Root = std::make_unique<Conditional>();

  std::string Word = trim(Line);
  std::string Keyword;
  {
    size_t Sp = Word.find_first_of(" \t");
    Keyword = (Sp == std::string::npos) ? Word : Word.substr(0, Sp);
  }

  std::string Rest = trim(Word.substr(Keyword.size()));
  parseConditionalArgs(Keyword, Rest, *Root);

  ++Idx;
  int Depth = 1;
  bool InElse = false;
  // Tracks the current deepest conditional in an "else ifeq" chain.
  Conditional *Cur = Root.get();

  while (Idx < Lines.size() && Depth > 0) {
    const MakefileLine &ML = Lines[Idx];
    std::string First;
    if (ML.Type == MakefileLine::Directive) {
      std::string T = trim(ML.Content);
      size_t Sp = T.find_first_of(" \t");
      First = (Sp == std::string::npos) ? T : T.substr(0, Sp);
    }

    if (First == "ifeq" || First == "ifneq" || First == "ifdef" ||
        First == "ifndef") {
      if (Depth == 1) {
        auto S = parseLine(Idx);
        if (S) {
          if (InElse)
            Cur->ElseBranch.push_back(std::move(S));
          else
            Cur->ThenBranch.push_back(std::move(S));
        }
        continue;
      }
      ++Depth;
      ++Idx;
      continue;
    }

    if (First == "else" && Depth == 1) {
      std::string ElseRest = trim(trim(ML.Content).substr(4));
      std::string ElseFirst;
      {
        size_t Sp = ElseRest.find_first_of(" \t");
        ElseFirst = (Sp == std::string::npos) ? ElseRest
                                               : ElseRest.substr(0, Sp);
      }
      if (ElseFirst == "ifeq" || ElseFirst == "ifneq" ||
          ElseFirst == "ifdef" || ElseFirst == "ifndef") {
        // "else ifeq (...)" — create a chained conditional in the else
        // branch and redirect subsequent statements to it.
        auto Next = std::make_unique<Conditional>();
        std::string NextRest = trim(ElseRest.substr(ElseFirst.size()));
        parseConditionalArgs(ElseFirst, NextRest, *Next);
        Conditional *NextPtr = Next.get();
        Cur->ElseBranch.push_back(std::move(Next));
        Cur = NextPtr;
        InElse = false;
        ++Idx;
        continue;
      }
      InElse = true;
      ++Idx;
      continue;
    }

    if (First == "endif") {
      --Depth;
      if (Depth == 0) {
        ++Idx;
        break;
      }
      ++Idx;
      continue;
    }

    if (Depth == 1) {
      auto S = parseLine(Idx);
      if (S) {
        if (InElse)
          Cur->ElseBranch.push_back(std::move(S));
        else
          Cur->ThenBranch.push_back(std::move(S));
      }
    } else {
      ++Idx;
    }
  }

  return Root;
}

std::unique_ptr<Include> Parser::parseInclude(const std::string &Line,
                                               unsigned LineNo) {
  auto Inc = std::make_unique<Include>();

  std::string Word = trim(Line);
  std::string Keyword;
  {
    size_t Sp = Word.find_first_of(" \t");
    Keyword = (Sp == std::string::npos) ? Word : Word.substr(0, Sp);
  }

  Inc->Optional = (Keyword == "-include" || Keyword == "sinclude");

  std::string Rest = trim(Word.substr(Keyword.size()));
  Inc->Files = splitWordsRespectingVarRefs(Rest);
  return Inc;
}

std::unique_ptr<DefineBlock> Parser::parseDefine(const std::string &Line,
                                                  unsigned LineNo,
                                                  size_t &Idx) {
  auto D = std::make_unique<DefineBlock>();

  std::string Word = trim(Line);
  std::string Rest = trim(Word.substr(6)); // skip "define"
  D->Mode = AssignMode::Recursive;

  size_t Sp = Rest.find_first_of(" \t");
  if (Sp != std::string::npos) {
    D->Name = Rest.substr(0, Sp);
    std::string OpStr = trim(Rest.substr(Sp));
    if (OpStr == "=" || OpStr.empty())
      D->Mode = AssignMode::Recursive;
    else if (OpStr == ":=" || OpStr == "::=")
      D->Mode = AssignMode::Simple;
    else if (OpStr == "+=")
      D->Mode = AssignMode::Append;
    else if (OpStr == "?=")
      D->Mode = AssignMode::Conditional;
  } else {
    D->Name = Rest;
  }

  ++Idx;
  std::string Body;
  while (Idx < Lines.size()) {
    std::string T = trim(Lines[Idx].Content.empty() ?
                          Lines[Idx].OriginalContent : Lines[Idx].Content);
    if (T == "endef") {
      ++Idx;
      break;
    }
    if (!Body.empty())
      Body += '\n';
    Body += Lines[Idx].OriginalContent;
    ++Idx;
  }
  D->Body = Body;

  return D;
}

std::unique_ptr<ExportDirective> Parser::parseExport(const std::string &Line,
                                                      unsigned LineNo) {
  auto E = std::make_unique<ExportDirective>();

  std::string Word = trim(Line);
  std::string Keyword;
  {
    size_t Sp = Word.find_first_of(" \t");
    Keyword = (Sp == std::string::npos) ? Word : Word.substr(0, Sp);
  }

  std::string Rest = trim(Word.substr(Keyword.size()));
  if (Rest.empty()) {
    E->ExportAll = true;
  } else {
    E->Names = splitWords(Rest);
  }
  return E;
}

std::unique_ptr<TargetVarAssign>
Parser::parseTargetVarAssign(const std::string &Line, unsigned LineNo,
                              size_t &Idx) {
  size_t ColonPos = std::string::npos;
  {
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
          continue;
        ColonPos = I;
        break;
      }
    }
  }
  if (ColonPos == std::string::npos)
    return nullptr;

  std::string After = trim(Line.substr(ColonPos + 1));
  size_t OpPos, OpLen;
  AssignMode Mode;
  if (!findAssignOp(After, OpPos, OpLen, Mode))
    return nullptr;

  auto TSV = std::make_unique<TargetVarAssign>();
  TSV->Targets = splitWordsRespectingVarRefs(trim(Line.substr(0, ColonPos)));
  TSV->VarName = trim(After.substr(0, OpPos));
  TSV->Mode = Mode;
  TSV->RawValue = trim(After.substr(OpPos + OpLen));
  ++Idx;
  while (Idx < Lines.size() && Lines[Idx].Type == MakefileLine::RecipeLine)
    ++Idx;
  return TSV;
}

void Parser::error(unsigned LineNo, const std::string &Msg) {
  HadError = true;
  ErrorMsg = Filename + ":" + std::to_string(LineNo) + ": " + Msg;
}

bool Parser::isAssignment(const std::string &Line) const {
  size_t Op, Len;
  AssignMode M;
  return findAssignOp(Line, Op, Len, M);
}

bool Parser::isRule(const std::string &Line) const {
  int Parens = 0;
  for (size_t I = 0; I < Line.size(); ++I) {
    char C = Line[I];
    if (C == '$' && I + 1 < Line.size() &&
        (Line[I + 1] == '(' || Line[I + 1] == '{')) {
      ++Parens;
      ++I;
    } else if (Parens > 0 && (C == ')' || C == '}')) {
      --Parens;
    } else if (Parens == 0 && C == ':') {
      if (I + 1 < Line.size() && Line[I + 1] == '=')
        return false;
      return true;
    } else if (Parens == 0 && C == '=') {
      return false;
    }
  }
  return false;
}

} // namespace build
} // namespace neverc
