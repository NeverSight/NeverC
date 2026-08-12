//===- MakefileInterpreter.cpp - Evaluate parsed makefiles ----------------===//
//
// Part of the NeverC Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "neverc/Build/MakefileInterpreter.h"

#include "neverc/Build/AST.h"
#include "neverc/Build/BuildConstants.h"
#include "neverc/Build/Function.h"
#include "neverc/Build/Lexer.h"
#include "neverc/Build/Parser.h"
#include "neverc/Build/Platform.h"
#include "neverc/Build/RuleDB.h"
#include "neverc/Build/VariableEnv.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace neverc {
namespace build {
namespace {

class InterpreterImpl {
public:
  InterpreterImpl(VariableEnv &Env, RuleDB &Rules,
                  FunctionRegistry &Functions)
      : Env(Env), Rules(Rules), Functions(Functions) {}

  void process(MakefileAST &AST) { processStatements(AST.Stmts); }

private:
  void processStatements(
      const std::vector<std::unique_ptr<Statement>> &Statements) {
    for (const auto &Statement : Statements) {
      switch (Statement->Kind) {
      case StmtKind::VarAssign: {
        auto *Assignment = static_cast<VarAssign *>(Statement.get());
        std::string Name = Env.expand(Assignment->Name);
        std::string Value = Assignment->RawValue;
        VariableEnv::Origin Origin =
            Assignment->Override ? VariableEnv::Origin::Override
                                 : VariableEnv::Origin::File;
        switch (Assignment->Mode) {
        case AssignMode::Recursive:
          Env.set(Name, Value, AssignMode::Recursive, Origin);
          break;
        case AssignMode::Simple:
          Env.set(Name, Env.expand(Value), AssignMode::Simple, Origin);
          break;
        case AssignMode::Conditional:
          Env.conditionalSet(Name, Value);
          break;
        case AssignMode::Append: {
          auto Existing = Env.vars().find(Name);
          if (Existing != Env.vars().end() &&
              Existing->second.Orig == VariableEnv::Origin::CommandLine &&
              Origin != VariableEnv::Origin::Override)
            break;
          if (Existing != Env.vars().end() &&
              Existing->second.Mode == AssignMode::Simple)
            Env.append(Name, Env.expand(Value));
          else
            Env.append(Name, Value);
          break;
        }
        case AssignMode::Shell: {
          auto Result = platform::shellExecute(Value);
          std::string Output = Result.Output;
          std::replace(Output.begin(), Output.end(), '\n', ' ');
          Env.set(Name, Output, AssignMode::Simple, Origin);
          break;
        }
        }
        if (Assignment->Export)
          Env.setExport(Name);
        break;
      }
      case StmtKind::Rule: {
        auto *ParsedRule = static_cast<Rule *>(Statement.get());
        Rules.addRule(*ParsedRule, Env);
        break;
      }
      case StmtKind::Conditional: {
        auto *Condition = static_cast<Conditional *>(Statement.get());
        bool Result = false;
        switch (Condition->CondKind) {
        case Conditional::IfEq:
          Result =
              Env.expand(Condition->Arg1) == Env.expand(Condition->Arg2);
          break;
        case Conditional::IfNeq:
          Result =
              Env.expand(Condition->Arg1) != Env.expand(Condition->Arg2);
          break;
        case Conditional::IfDef: {
          std::string Name = Env.expand(Condition->Arg1);
          Result = Env.isDefined(Name) && !Env.rawValue(Name).empty();
          break;
        }
        case Conditional::IfNDef: {
          std::string Name = Env.expand(Condition->Arg1);
          Result = !Env.isDefined(Name) || Env.rawValue(Name).empty();
          break;
        }
        }
        if (Result)
          processStatements(Condition->ThenBranch);
        else
          processStatements(Condition->ElseBranch);
        break;
      }
      case StmtKind::Include: {
        auto *Directive = static_cast<Include *>(Statement.get());
        for (const std::string &File : Directive->Files) {
          std::string Expanded = Env.expand(File);

          // Expansion may produce multiple space-separated filenames
          // (for example, $(wildcard *.mk) -> "a.mk b.mk").
          std::vector<std::string> Paths;
          std::istringstream Stream(Expanded);
          std::string Word;
          while (Stream >> Word) {
            if (Word.find('*') != std::string::npos ||
                Word.find('?') != std::string::npos) {
              auto Matches = platform::globFiles(Word);
              Paths.insert(Paths.end(), Matches.begin(), Matches.end());
            } else {
              Paths.push_back(Word);
            }
          }

          for (const std::string &Path : Paths) {
            auto Buffer = llvm::MemoryBuffer::getFile(Path);
            if (!Buffer) {
              if (!Directive->Optional)
                llvm::errs() << constants::ToolName << ": " << Path
                             << ": No such file or directory\n";
              continue;
            }

            std::string MakefileList =
                Env.get(constants::VarMakefileList.str());
            if (!MakefileList.empty())
              MakefileList += ' ';
            MakefileList += Path;
            Env.set(constants::VarMakefileList.str(), MakefileList,
                    AssignMode::Simple, VariableEnv::Origin::Default);

            std::string Content = (*Buffer)->getBuffer().str();
            Lexer Lex(Path, Content);
            auto Lines = Lex.lex();
            Parser Parse(Path, std::move(Lines));
            auto IncludedAST = Parse.parse();
            if (IncludedAST)
              process(*IncludedAST);
          }
        }
        break;
      }
      case StmtKind::DefineBlock: {
        auto *Definition = static_cast<DefineBlock *>(Statement.get());
        VariableEnv::Origin Origin =
            Definition->Override ? VariableEnv::Origin::Override
                                 : VariableEnv::Origin::File;
        switch (Definition->Mode) {
        case AssignMode::Simple:
          Env.set(Definition->Name, Env.expand(Definition->Body),
                  AssignMode::Simple, Origin);
          break;
        case AssignMode::Append: {
          auto Existing = Env.vars().find(Definition->Name);
          if (Existing != Env.vars().end() &&
              Existing->second.Orig == VariableEnv::Origin::CommandLine &&
              Origin != VariableEnv::Origin::Override)
            break;
          Env.append(Definition->Name, Definition->Body);
          break;
        }
        case AssignMode::Conditional:
          Env.conditionalSet(Definition->Name, Definition->Body);
          break;
        default:
          Env.set(Definition->Name, Definition->Body, AssignMode::Recursive,
                  Origin);
          break;
        }
        break;
      }
      case StmtKind::ExportDirective: {
        auto *Directive = static_cast<ExportDirective *>(Statement.get());
        if (Directive->IsUnexport) {
          for (const std::string &Name : Directive->Names)
            Env.setExport(Env.expand(Name), false);
        } else if (Directive->ExportAll) {
          Env.setExportAll(true);
          for (const auto &Entry : Env.vars())
            Env.setExport(Entry.first().str());
        } else {
          for (const std::string &Name : Directive->Names)
            Env.setExport(Env.expand(Name));
        }
        break;
      }
      case StmtKind::UndefineDirective: {
        auto *Directive = static_cast<UndefineDirective *>(Statement.get());
        std::string Name = Env.expand(Directive->Name);
        if (Directive->Override) {
          Env.undefine(Name);
        } else {
          auto Existing = Env.vars().find(Name);
          if (Existing == Env.vars().end() ||
              Existing->second.Orig != VariableEnv::Origin::CommandLine)
            Env.undefine(Name);
        }
        break;
      }
      case StmtKind::TargetVarAssign: {
        auto *Assignment =
            static_cast<TargetVarAssign *>(Statement.get());
        for (const std::string &RawTarget : Assignment->Targets) {
          TargetVarOverride Override;
          Override.VarName = Assignment->VarName;
          Override.RawValue = Assignment->RawValue;
          Override.Mode = Assignment->Mode;
          Rules.addTargetVar(Env.expand(RawTarget), Override);
        }
        break;
      }
      case StmtKind::Expression: {
        auto *ExpressionStatement =
            static_cast<Expression *>(Statement.get());
        Env.expand(ExpressionStatement->Text);
        break;
      }
      }
    }
  }

  VariableEnv &Env;
  RuleDB &Rules;
  FunctionRegistry &Functions;
};

} // namespace

MakefileInterpreter::MakefileInterpreter(VariableEnv &Env, RuleDB &Rules,
                                         FunctionRegistry &Functions)
    : Env(Env), Rules(Rules), Functions(Functions) {}

void MakefileInterpreter::process(MakefileAST &AST) {
  InterpreterImpl(Env, Rules, Functions).process(AST);
}

} // namespace build
} // namespace neverc
