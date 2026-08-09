#include <gtest/gtest.h>

#include "neverc/Build/AST.h"
#include "neverc/Build/BuiltinCommands.h"
#include "neverc/Build/DepGraph.h"
#include "neverc/Build/Function.h"
#include "neverc/Build/Lexer.h"
#include "neverc/Build/Parser.h"
#include "neverc/Build/Platform.h"
#include "neverc/Build/RuleDB.h"
#include "neverc/Build/VariableEnv.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include <fstream>
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace neverc::build;

// ---------------------------------------------------------------------------
// Helper: lex + parse a Makefile string and process the AST into Env/Rules.
// Mirrors the processAST logic in Core/BuildDriver.cpp.
// ---------------------------------------------------------------------------
namespace {

void processStatements(const std::vector<std::unique_ptr<Statement>> &Stmts,
                       VariableEnv &Env, RuleDB &Rules,
                       FunctionRegistry &FuncReg);

void processAST(MakefileAST &AST, VariableEnv &Env, RuleDB &Rules,
                FunctionRegistry &FuncReg) {
  processStatements(AST.Stmts, Env, Rules, FuncReg);
}

void processStatements(const std::vector<std::unique_ptr<Statement>> &Stmts,
                       VariableEnv &Env, RuleDB &Rules,
                       FunctionRegistry &FuncReg) {
  for (auto &S : Stmts) {
    switch (S->Kind) {
    case StmtKind::VarAssign: {
      auto *VA = static_cast<VarAssign *>(S.get());
      std::string Name = Env.expand(VA->Name);
      std::string Value = VA->RawValue;
      VariableEnv::Origin Orig = VA->Override ? VariableEnv::Origin::Override
                                              : VariableEnv::Origin::File;
      switch (VA->Mode) {
      case AssignMode::Recursive:
        Env.set(Name, Value, AssignMode::Recursive, Orig);
        break;
      case AssignMode::Simple:
        Env.set(Name, Env.expand(Value), AssignMode::Simple, Orig);
        break;
      case AssignMode::Conditional:
        Env.conditionalSet(Name, Value);
        break;
      case AssignMode::Append: {
        auto ExistingIt = Env.vars().find(Name);
        if (ExistingIt != Env.vars().end() &&
            ExistingIt->second.Orig == VariableEnv::Origin::CommandLine &&
            Orig != VariableEnv::Origin::Override)
          break;
        if (ExistingIt != Env.vars().end() &&
            ExistingIt->second.Mode == AssignMode::Simple)
          Env.append(Name, Env.expand(Value));
        else
          Env.append(Name, Value);
        break;
      }
      case AssignMode::Shell:
        break;
      }
      if (VA->Export)
        Env.setExport(Name);
      break;
    }
    case StmtKind::Rule: {
      auto *R = static_cast<Rule *>(S.get());
      Rules.addRule(*R, Env);
      break;
    }
    case StmtKind::Conditional: {
      auto *C = static_cast<Conditional *>(S.get());
      bool Result = false;
      switch (C->CondKind) {
      case Conditional::IfEq:
        Result = Env.expand(C->Arg1) == Env.expand(C->Arg2);
        break;
      case Conditional::IfNeq:
        Result = Env.expand(C->Arg1) != Env.expand(C->Arg2);
        break;
      case Conditional::IfDef: {
        std::string Name = Env.expand(C->Arg1);
        Result = Env.isDefined(Name) && !Env.rawValue(Name).empty();
        break;
      }
      case Conditional::IfNDef: {
        std::string Name = Env.expand(C->Arg1);
        Result = !Env.isDefined(Name) || Env.rawValue(Name).empty();
        break;
      }
      }
      if (Result)
        processStatements(C->ThenBranch, Env, Rules, FuncReg);
      else
        processStatements(C->ElseBranch, Env, Rules, FuncReg);
      break;
    }
    case StmtKind::DefineBlock: {
      auto *D = static_cast<DefineBlock *>(S.get());
      VariableEnv::Origin Orig = D->Override ? VariableEnv::Origin::Override
                                             : VariableEnv::Origin::File;
      switch (D->Mode) {
      case AssignMode::Simple:
        Env.set(D->Name, Env.expand(D->Body), AssignMode::Simple, Orig);
        break;
      case AssignMode::Append:
        Env.append(D->Name, D->Body);
        break;
      case AssignMode::Conditional:
        Env.conditionalSet(D->Name, D->Body);
        break;
      default:
        Env.set(D->Name, D->Body, AssignMode::Recursive, Orig);
        break;
      }
      break;
    }
    case StmtKind::ExportDirective: {
      auto *E = static_cast<ExportDirective *>(S.get());
      if (E->IsUnexport) {
        for (auto &Name : E->Names)
          Env.setExport(Env.expand(Name), false);
      } else if (E->ExportAll) {
        Env.setExportAll(true);
        for (auto &Entry : Env.vars())
          Env.setExport(Entry.first().str());
      } else {
        for (auto &Name : E->Names)
          Env.setExport(Env.expand(Name));
      }
      break;
    }
    case StmtKind::UndefineDirective: {
      auto *U = static_cast<UndefineDirective *>(S.get());
      std::string Name = Env.expand(U->Name);
      if (U->Override)
        Env.undefine(Name);
      else {
        auto It = Env.vars().find(Name);
        if (It == Env.vars().end() ||
            It->second.Orig != VariableEnv::Origin::CommandLine)
          Env.undefine(Name);
      }
      break;
    }
    case StmtKind::TargetVarAssign: {
      auto *TV = static_cast<TargetVarAssign *>(S.get());
      for (auto &RawTarget : TV->Targets) {
        std::string Target = Env.expand(RawTarget);
        TargetVarOverride Ov;
        Ov.VarName = TV->VarName;
        Ov.RawValue = TV->RawValue;
        Ov.Mode = TV->Mode;
        Rules.addTargetVar(Target, Ov);
      }
      break;
    }
    case StmtKind::Expression: {
      auto *E = static_cast<Expression *>(S.get());
      Env.expand(E->Text);
      break;
    }
    }
  }
}

struct ParsedMakefile {
  VariableEnv Env;
  FunctionRegistry FuncReg;
  RuleDB Rules;

  bool parse(const std::string &Content) {
    Env.setFunctionRegistry(&FuncReg);
    Env.setEvalCallback([this](const std::string &Text) {
      Lexer EvalL("<eval>", Text);
      auto EvalLines = EvalL.lex();
      Parser EvalP("<eval>", std::move(EvalLines));
      auto EvalAST = EvalP.parse();
      if (EvalAST)
        processAST(*EvalAST, Env, Rules, FuncReg);
    });

    Lexer L("<test>", Content);
    auto Lines = L.lex();
    if (L.hadError())
      return false;
    Parser P("<test>", std::move(Lines));
    auto AST = P.parse();
    if (!AST || P.hadError())
      return false;
    processAST(*AST, Env, Rules, FuncReg);
    return true;
  }
};

} // namespace

// ===== Lexer Tests =====

class BuildLexerTest : public ::testing::Test {};

TEST_F(BuildLexerTest, KernelVersionAssignment) {
  std::string Input = "VERSION = 5\n"
                      "PATCHLEVEL = 10\n"
                      "SUBLEVEL = 0\n"
                      "EXTRAVERSION =\n"
                      "NAME = Kleptomaniac Octopus\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  ASSERT_FALSE(L.hadError());
  ASSERT_EQ(Lines.size(), 5u);
  for (auto &ML : Lines)
    EXPECT_EQ(ML.Type, MakefileLine::Assignment);
}

TEST_F(BuildLexerTest, PhonyRule) {
  std::string Input = ".PHONY: all clean install\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  ASSERT_FALSE(L.hadError());
  ASSERT_EQ(Lines.size(), 1u);
  EXPECT_EQ(Lines[0].Type, MakefileLine::Rule);
}

TEST_F(BuildLexerTest, PatternRule) {
  std::string Input = "%.o: %.c\n"
                      "\t$(CC) $(CFLAGS) -c -o $@ $<\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  ASSERT_FALSE(L.hadError());
  ASSERT_EQ(Lines.size(), 2u);
  EXPECT_EQ(Lines[0].Type, MakefileLine::Rule);
  EXPECT_EQ(Lines[1].Type, MakefileLine::RecipeLine);
}

TEST_F(BuildLexerTest, ConditionalDirective) {
  std::string Input = "ifeq ($(ARCH),x86)\n"
                      "KBUILD_CFLAGS += -m64\n"
                      "endif\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  ASSERT_FALSE(L.hadError());
  ASSERT_EQ(Lines.size(), 3u);
  EXPECT_EQ(Lines[0].Type, MakefileLine::Directive);
  EXPECT_EQ(Lines[1].Type, MakefileLine::Assignment);
  EXPECT_EQ(Lines[2].Type, MakefileLine::Directive);
}

TEST_F(BuildLexerTest, ContinuationLine) {
  std::string Input = "CFLAGS = -Wall \\\n"
                      "  -Werror \\\n"
                      "  -O2\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  ASSERT_FALSE(L.hadError());
  ASSERT_EQ(Lines.size(), 1u);
  EXPECT_EQ(Lines[0].Type, MakefileLine::Assignment);
  EXPECT_NE(Lines[0].Content.find("-Wall"), std::string::npos);
  EXPECT_NE(Lines[0].Content.find("-O2"), std::string::npos);
}

TEST_F(BuildLexerTest, CommentLine) {
  std::string Input = "# Linux kernel top Makefile\n"
                      "VERSION = 5\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  ASSERT_FALSE(L.hadError());
  ASSERT_EQ(Lines.size(), 2u);
  EXPECT_EQ(Lines[0].Type, MakefileLine::Comment);
  EXPECT_EQ(Lines[1].Type, MakefileLine::Assignment);
}

TEST_F(BuildLexerTest, DefineBlock) {
  std::string Input = "define filechk_kernel.release\n"
                      "\techo \"$(KERNELVERSION)$$($(CONFIG_SHELL))\"\n"
                      "endef\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  ASSERT_FALSE(L.hadError());
  EXPECT_EQ(Lines[0].Type, MakefileLine::Directive);
}

TEST_F(BuildLexerTest, ExportDirective) {
  std::string Input = "export VERSION PATCHLEVEL\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  ASSERT_FALSE(L.hadError());
  ASSERT_EQ(Lines.size(), 1u);
  EXPECT_EQ(Lines[0].Type, MakefileLine::Directive);
}

TEST_F(BuildLexerTest, InlineRecipe) {
  std::string Input = "vmlinux: scripts/link-vmlinux.sh ; @echo done\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  ASSERT_FALSE(L.hadError());
  ASSERT_EQ(Lines.size(), 1u);
  EXPECT_EQ(Lines[0].Type, MakefileLine::Rule);
}

// ===== Parser Tests =====

class BuildParserTest : public ::testing::Test {};

TEST_F(BuildParserTest, SimpleAssignment) {
  std::string Input = "CC := gcc\nLD = ld\nEXTRA ?= extra\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_FALSE(P.hadError());
  ASSERT_EQ(AST->Stmts.size(), 3u);

  auto *S0 = static_cast<VarAssign *>(AST->Stmts[0].get());
  EXPECT_EQ(S0->Name, "CC");
  EXPECT_EQ(S0->Mode, AssignMode::Simple);
  EXPECT_EQ(S0->RawValue, "gcc");

  auto *S1 = static_cast<VarAssign *>(AST->Stmts[1].get());
  EXPECT_EQ(S1->Name, "LD");
  EXPECT_EQ(S1->Mode, AssignMode::Recursive);
  EXPECT_EQ(S1->RawValue, "ld");

  auto *S2 = static_cast<VarAssign *>(AST->Stmts[2].get());
  EXPECT_EQ(S2->Name, "EXTRA");
  EXPECT_EQ(S2->Mode, AssignMode::Conditional);
}

TEST_F(BuildParserTest, AppendAssignment) {
  std::string Input = "CFLAGS := -Wall\nCFLAGS += -Werror\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_EQ(AST->Stmts.size(), 2u);

  auto *S1 = static_cast<VarAssign *>(AST->Stmts[1].get());
  EXPECT_EQ(S1->Name, "CFLAGS");
  EXPECT_EQ(S1->Mode, AssignMode::Append);
  EXPECT_EQ(S1->RawValue, "-Werror");
}

TEST_F(BuildParserTest, RuleWithRecipes) {
  std::string Input = "vmlinux: init/main.o kernel/core.o\n"
                      "\t$(LD) -o $@ $^\n"
                      "\t@echo 'Kernel: $@ is ready'\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_EQ(AST->Stmts.size(), 1u);

  auto *R = static_cast<Rule *>(AST->Stmts[0].get());
  ASSERT_EQ(R->Targets.size(), 1u);
  EXPECT_EQ(R->Targets[0], "vmlinux");
  ASSERT_EQ(R->Recipes.size(), 2u);
  EXPECT_FALSE(R->Recipes[0].Silent);
  EXPECT_TRUE(R->Recipes[1].Silent);
}

TEST_F(BuildParserTest, PatternRule) {
  std::string Input = "%.o: %.c\n"
                      "\t$(CC) $(CFLAGS) -c -o $@ $<\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_EQ(AST->Stmts.size(), 1u);

  auto *R = static_cast<Rule *>(AST->Stmts[0].get());
  EXPECT_TRUE(R->IsPattern);
}

TEST_F(BuildParserTest, IfdefConditional) {
  std::string Input = "ifdef CONFIG_MODULES\n"
                      "MOD_FLAGS := -DMODULE\n"
                      "else\n"
                      "MOD_FLAGS :=\n"
                      "endif\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_EQ(AST->Stmts.size(), 1u);

  auto *C = static_cast<Conditional *>(AST->Stmts[0].get());
  EXPECT_EQ(C->CondKind, Conditional::IfDef);
  EXPECT_EQ(C->Arg1, "CONFIG_MODULES");
  EXPECT_EQ(C->ThenBranch.size(), 1u);
  EXPECT_EQ(C->ElseBranch.size(), 1u);
}

TEST_F(BuildParserTest, IfeqConditional) {
  std::string Input = "ifeq ($(ARCH),arm64)\n"
                      "CROSS := aarch64-linux-gnu-\n"
                      "endif\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_EQ(AST->Stmts.size(), 1u);

  auto *C = static_cast<Conditional *>(AST->Stmts[0].get());
  EXPECT_EQ(C->CondKind, Conditional::IfEq);
  EXPECT_EQ(C->Arg1, "$(ARCH)");
  EXPECT_EQ(C->Arg2, "arm64");
  EXPECT_EQ(C->ThenBranch.size(), 1u);
  EXPECT_TRUE(C->ElseBranch.empty());
}

TEST_F(BuildParserTest, DefineBlock) {
  std::string Input = "define cmd_link_vmlinux\n"
                      "$(LD) -o $@ $(LDFLAGS) $^\n"
                      "endef\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_EQ(AST->Stmts.size(), 1u);

  auto *D = static_cast<DefineBlock *>(AST->Stmts[0].get());
  EXPECT_EQ(D->Name, "cmd_link_vmlinux");
  EXPECT_NE(D->Body.find("$(LD)"), std::string::npos);
}

TEST_F(BuildParserTest, ExportDirective) {
  std::string Input = "export ARCH CROSS_COMPILE\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_EQ(AST->Stmts.size(), 1u);

  auto *E = static_cast<ExportDirective *>(AST->Stmts[0].get());
  ASSERT_EQ(E->Names.size(), 2u);
  EXPECT_EQ(E->Names[0], "ARCH");
  EXPECT_EQ(E->Names[1], "CROSS_COMPILE");
}

TEST_F(BuildParserTest, OverrideAssignment) {
  std::string Input = "override CFLAGS += -fno-strict-aliasing\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_EQ(AST->Stmts.size(), 1u);

  auto *A = static_cast<VarAssign *>(AST->Stmts[0].get());
  EXPECT_TRUE(A->Override);
  EXPECT_EQ(A->Name, "CFLAGS");
  EXPECT_EQ(A->Mode, AssignMode::Append);
}

TEST_F(BuildParserTest, OrderOnlyPrereqs) {
  std::string Input = "kernel/core.o: kernel/core.c | include/generated\n"
                      "\t$(CC) -c -o $@ $<\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_EQ(AST->Stmts.size(), 1u);

  auto *R = static_cast<Rule *>(AST->Stmts[0].get());
  EXPECT_EQ(R->Targets[0], "kernel/core.o");
  EXPECT_FALSE(R->OrderOnlyPrereqs.empty());
}

TEST_F(BuildParserTest, InlineRecipeAfterSemicolon) {
  std::string Input = "help: ; @echo 'Run make all'\n";
  Lexer L("<test>", Input);
  auto Lines = L.lex();
  Parser P("<test>", std::move(Lines));
  auto AST = P.parse();
  ASSERT_TRUE(AST);
  ASSERT_EQ(AST->Stmts.size(), 1u);

  auto *R = static_cast<Rule *>(AST->Stmts[0].get());
  ASSERT_EQ(R->Recipes.size(), 1u);
  EXPECT_TRUE(R->Recipes[0].Silent);
  EXPECT_NE(R->Recipes[0].Command.find("echo"), std::string::npos);
}

// ===== VariableEnv Tests =====

class BuildVarEnvTest : public ::testing::Test {};

TEST_F(BuildVarEnvTest, KernelVersionExpansion) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "KERNELVERSION = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"));

  EXPECT_EQ(M.Env.get("VERSION"), "5");
  EXPECT_EQ(M.Env.get("PATCHLEVEL"), "10");
  EXPECT_EQ(M.Env.get("SUBLEVEL"), "0");
  EXPECT_EQ(M.Env.get("KERNELVERSION"), "5.10.0");
}

TEST_F(BuildVarEnvTest, SimpleVsRecursive) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "A = hello\n"
      "B := $(A) world\n"
      "C = $(A) world\n"
      "A = changed\n"));

  // B was := (simple), captured A=hello at definition time.
  EXPECT_EQ(M.Env.get("B"), "hello world");
  // C was = (recursive), picks up current A=changed.
  EXPECT_EQ(M.Env.get("C"), "changed world");
}

TEST_F(BuildVarEnvTest, ConditionalAssignment) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "ARCH ?= x86\n"
      "ARCH ?= arm64\n"));

  EXPECT_EQ(M.Env.get("ARCH"), "x86");
}

TEST_F(BuildVarEnvTest, AppendToSimple) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "CFLAGS := -Wall\n"
      "CFLAGS += -Werror\n"
      "CFLAGS += -O2\n"));

  EXPECT_EQ(M.Env.get("CFLAGS"), "-Wall -Werror -O2");
}

TEST_F(BuildVarEnvTest, CommandLineOverridesFile) {
  ParsedMakefile M;
  M.Env.setFunctionRegistry(&M.FuncReg);
  M.Env.setCommandLineVar("ARCH", "arm64");
  // File assignment should be ignored because command-line has higher priority.
  ASSERT_TRUE(M.parse("ARCH = x86\n"));
  EXPECT_EQ(M.Env.get("ARCH"), "arm64");
}

TEST_F(BuildVarEnvTest, OverrideBeatsCommandLine) {
  ParsedMakefile M;
  M.Env.setFunctionRegistry(&M.FuncReg);
  M.Env.setCommandLineVar("EXTRA_CFLAGS", "");
  ASSERT_TRUE(M.parse("override EXTRA_CFLAGS = -DDEBUG\n"));
  EXPECT_EQ(M.Env.get("EXTRA_CFLAGS"), "-DDEBUG");
}

TEST_F(BuildVarEnvTest, IfdefThenBranch) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "CONFIG_SMP = y\n"
      "ifdef CONFIG_SMP\n"
      "SMP_FLAGS := -DSMP\n"
      "else\n"
      "SMP_FLAGS :=\n"
      "endif\n"));

  EXPECT_EQ(M.Env.get("SMP_FLAGS"), "-DSMP");
}

TEST_F(BuildVarEnvTest, IfdefElseBranch) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "ifdef CONFIG_SMP\n"
      "SMP_FLAGS := -DSMP\n"
      "else\n"
      "SMP_FLAGS := -DNOSMP\n"
      "endif\n"));

  EXPECT_EQ(M.Env.get("SMP_FLAGS"), "-DNOSMP");
}

TEST_F(BuildVarEnvTest, IfeqArchSelection) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "ARCH := x86\n"
      "ifeq ($(ARCH),x86)\n"
      "BITS := 64\n"
      "MACHINE := x86_64\n"
      "endif\n"));

  EXPECT_EQ(M.Env.get("BITS"), "64");
  EXPECT_EQ(M.Env.get("MACHINE"), "x86_64");
}

TEST_F(BuildVarEnvTest, IfneqConditional) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "ARCH := arm64\n"
      "ifneq ($(ARCH),x86)\n"
      "CROSS_COMPILE := aarch64-linux-gnu-\n"
      "endif\n"));

  EXPECT_EQ(M.Env.get("CROSS_COMPILE"), "aarch64-linux-gnu-");
}

TEST_F(BuildVarEnvTest, NestedConditionals) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "ARCH := x86\n"
      "CONFIG_64BIT := y\n"
      "ifeq ($(ARCH),x86)\n"
      "ifdef CONFIG_64BIT\n"
      "MACHINE := x86_64\n"
      "else\n"
      "MACHINE := i386\n"
      "endif\n"
      "endif\n"));

  EXPECT_EQ(M.Env.get("MACHINE"), "x86_64");
}

TEST_F(BuildVarEnvTest, ExportMarksVariable) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "ARCH := x86\n"
      "export ARCH\n"));

  auto It = M.Env.vars().find("ARCH");
  ASSERT_NE(It, M.Env.vars().end());
  EXPECT_TRUE(It->second.Exported);
}

TEST_F(BuildVarEnvTest, DefineBlockExpansion) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "define newline\n"
      "\n"
      "\n"
      "endef\n"
      "GREETING := hello\n"));

  EXPECT_TRUE(M.Env.isDefined("newline"));
  EXPECT_EQ(M.Env.get("GREETING"), "hello");
}

TEST_F(BuildVarEnvTest, SubstitutionRef) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "obj-y := fork.o exec.o signal.o\n"
      "src-y := $(obj-y:.o=.c)\n"));

  EXPECT_EQ(M.Env.get("src-y"), "fork.c exec.c signal.c");
}

// ===== Function Tests =====

class BuildFunctionTest : public ::testing::Test {};

TEST_F(BuildFunctionTest, Patsubst) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "obj-y := fork.o exec.o signal.o\n"
      "src-y := $(patsubst %.o,%.c,$(obj-y))\n"));

  EXPECT_EQ(M.Env.get("src-y"), "fork.c exec.c signal.c");
}

TEST_F(BuildFunctionTest, Addprefix) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "dirs := init kernel mm\n"
      "subdirs := $(addprefix src/,$(dirs))\n"));

  EXPECT_EQ(M.Env.get("subdirs"), "src/init src/kernel src/mm");
}

TEST_F(BuildFunctionTest, Addsuffix) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "mods := ext4 btrfs xfs\n"
      "mod-objs := $(addsuffix .o,$(mods))\n"));

  EXPECT_EQ(M.Env.get("mod-objs"), "ext4.o btrfs.o xfs.o");
}

TEST_F(BuildFunctionTest, Filter) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "files := main.c util.c main.h util.h config.o\n"
      "sources := $(filter %.c,$(files))\n"
      "headers := $(filter %.h,$(files))\n"));

  EXPECT_EQ(M.Env.get("sources"), "main.c util.c");
  EXPECT_EQ(M.Env.get("headers"), "main.h util.h");
}

TEST_F(BuildFunctionTest, FilterOut) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "CFLAGS := -Wall -Wextra -Wno-unused -O2\n"
      "CFLAGS_CLEAN := $(filter-out -Wno-unused,$(CFLAGS))\n"));

  EXPECT_EQ(M.Env.get("CFLAGS_CLEAN"), "-Wall -Wextra -O2");
}

TEST_F(BuildFunctionTest, Sort) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "list := mm kernel init kernel\n"
      "sorted := $(sort $(list))\n"));

  EXPECT_EQ(M.Env.get("sorted"), "init kernel mm");
}

TEST_F(BuildFunctionTest, Foreach) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "subdirs := init kernel mm\n"
      "obj-dirs := $(foreach d,$(subdirs),$(d)/built-in.a)\n"));

  EXPECT_EQ(M.Env.get("obj-dirs"), "init/built-in.a kernel/built-in.a mm/built-in.a");
}

TEST_F(BuildFunctionTest, CallFunction) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "cc-option = $(1)\n"
      "CFLAGS := $(call cc-option,-fstack-protector)\n"));

  EXPECT_EQ(M.Env.get("CFLAGS"), "-fstack-protector");
}

TEST_F(BuildFunctionTest, Subst) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "KERNELRELEASE := 5.10.0\n"
      "dashed := $(subst .,-,$(KERNELRELEASE))\n"));

  EXPECT_EQ(M.Env.get("dashed"), "5-10-0");
}

TEST_F(BuildFunctionTest, DirNotdir) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "path := kernel/sched/core.c\n"
      "d := $(dir $(path))\n"
      "f := $(notdir $(path))\n"));

  EXPECT_EQ(M.Env.get("d"), "kernel/sched/");
  EXPECT_EQ(M.Env.get("f"), "core.c");
}

TEST_F(BuildFunctionTest, BasenameAndSuffix) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "files := main.c util.h lib.o\n"
      "bases := $(basename $(files))\n"
      "suffixes := $(suffix $(files))\n"));

  EXPECT_EQ(M.Env.get("bases"), "main util lib");
  EXPECT_EQ(M.Env.get("suffixes"), ".c .h .o");
}

TEST_F(BuildFunctionTest, WordFunctions) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "list := alpha beta gamma delta\n"
      "w2 := $(word 2,$(list))\n"
      "wl := $(wordlist 2,3,$(list))\n"
      "wc := $(words $(list))\n"
      "first := $(firstword $(list))\n"
      "last := $(lastword $(list))\n"));

  EXPECT_EQ(M.Env.get("w2"), "beta");
  EXPECT_EQ(M.Env.get("wl"), "beta gamma");
  EXPECT_EQ(M.Env.get("wc"), "4");
  EXPECT_EQ(M.Env.get("first"), "alpha");
  EXPECT_EQ(M.Env.get("last"), "delta");
}

TEST_F(BuildFunctionTest, Strip) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse("VAR := $(strip   a   b   c   )\n"));
  EXPECT_EQ(M.Env.get("VAR"), "a b c");
}

TEST_F(BuildFunctionTest, Findstring) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "result := $(findstring x86,x86_64-linux-gnu)\n"
      "nope := $(findstring arm,x86_64-linux-gnu)\n"));

  EXPECT_EQ(M.Env.get("result"), "x86");
  EXPECT_EQ(M.Env.get("nope"), "");
}

TEST_F(BuildFunctionTest, IfFunction) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "HAVE_GCC := yes\n"
      "CC := $(if $(HAVE_GCC),gcc,clang)\n"));

  EXPECT_EQ(M.Env.get("CC"), "gcc");
}

TEST_F(BuildFunctionTest, OrAndFunctions) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "A :=\n"
      "B := fallback\n"
      "result := $(or $(A),$(B))\n"
      "X := one\n"
      "Y := two\n"
      "both := $(and $(X),$(Y))\n"));

  EXPECT_EQ(M.Env.get("result"), "fallback");
  EXPECT_EQ(M.Env.get("both"), "two");
}

TEST_F(BuildFunctionTest, EvalGeneratesRules) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "define make-module\n"
      "$(1).o: $(1).c\n"
      "endef\n"
      "$(eval $(call make-module,driver))\n"));

  auto *R = M.Rules.findRule("driver.o");
  ASSERT_NE(R, nullptr);
  ASSERT_FALSE(R->Prerequisites.empty());
  EXPECT_EQ(R->Prerequisites[0], "driver.c");
}

TEST_F(BuildFunctionTest, OriginFunction) {
  ParsedMakefile M;
  M.Env.setFunctionRegistry(&M.FuncReg);
  M.Env.setCommandLineVar("CMD_VAR", "fromcli");
  ASSERT_TRUE(M.parse(
      "FILE_VAR := fromfile\n"
      "o1 := $(origin CMD_VAR)\n"
      "o2 := $(origin FILE_VAR)\n"
      "o3 := $(origin UNDEFINED_VAR)\n"));

  EXPECT_EQ(M.Env.get("o1"), "command line");
  EXPECT_EQ(M.Env.get("o2"), "file");
  EXPECT_EQ(M.Env.get("o3"), "undefined");
}

// ===== RuleDB Tests =====

class BuildRuleDBTest : public ::testing::Test {};

TEST_F(BuildRuleDBTest, PhonyTargets) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      ".PHONY: all clean modules\n"
      "all: vmlinux modules\n"
      "\t@echo 'Build complete'\n"
      "clean:\n"
      "\trm -f *.o vmlinux\n"));

  EXPECT_TRUE(M.Rules.isPhony("all"));
  EXPECT_TRUE(M.Rules.isPhony("clean"));
  EXPECT_TRUE(M.Rules.isPhony("modules"));
  EXPECT_FALSE(M.Rules.isPhony("vmlinux"));
}

TEST_F(BuildRuleDBTest, DefaultTarget) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "all: vmlinux\n"
      "vmlinux: init/main.o\n"));

  EXPECT_EQ(M.Rules.defaultTarget(), "all");
}

TEST_F(BuildRuleDBTest, ExplicitRuleResolution) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "vmlinux: init/main.o kernel/core.o\n"
      "\t$(LD) -o $@ $^\n"));

  auto *R = M.Rules.findRule("vmlinux");
  ASSERT_NE(R, nullptr);
  EXPECT_EQ(R->Target, "vmlinux");
  ASSERT_EQ(R->Prerequisites.size(), 2u);
  EXPECT_EQ(R->Prerequisites[0], "init/main.o");
  EXPECT_EQ(R->Prerequisites[1], "kernel/core.o");
  ASSERT_EQ(R->Recipes.size(), 1u);
}

TEST_F(BuildRuleDBTest, VariableExpandedPrereqs) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "core-y := init kernel mm\n"
      "core-obj := $(addsuffix /built-in.a,$(core-y))\n"
      "vmlinux: $(core-obj)\n"
      "\t$(LD) -o $@ $^\n"));

  auto *R = M.Rules.findRule("vmlinux");
  ASSERT_NE(R, nullptr);
  ASSERT_EQ(R->Prerequisites.size(), 3u);
  EXPECT_EQ(R->Prerequisites[0], "init/built-in.a");
  EXPECT_EQ(R->Prerequisites[1], "kernel/built-in.a");
  EXPECT_EQ(R->Prerequisites[2], "mm/built-in.a");
}

TEST_F(BuildRuleDBTest, TargetSpecificVariable) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "vmlinux: CFLAGS += -DVMLINUX\n"
      "vmlinux: init/main.o\n"
      "\t$(LD) -o $@ $^\n"));

  auto *TV = M.Rules.getTargetVars("vmlinux");
  ASSERT_NE(TV, nullptr);
  ASSERT_EQ(TV->size(), 1u);
  EXPECT_EQ((*TV)[0].VarName, "CFLAGS");
  EXPECT_EQ((*TV)[0].RawValue, "-DVMLINUX");
}

TEST_F(BuildRuleDBTest, MultipleTargetsSameRule) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "init/main.o kernel/core.o: %.o:\n"
      "\techo building\n"));

  // Both targets should have a rule.
  EXPECT_NE(M.Rules.findRule("init/main.o"), nullptr);
  EXPECT_NE(M.Rules.findRule("kernel/core.o"), nullptr);
}

// ===== DepGraph Tests =====

class BuildDepGraphTest : public ::testing::Test {};

TEST_F(BuildDepGraphTest, SimpleChain) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      ".PHONY: all\n"
      "all: vmlinux\n"
      "vmlinux: init/main.o\n"
      "\t$(LD) -o vmlinux init/main.o\n"));

  DepGraph G;
  ASSERT_TRUE(G.build("all", M.Rules));

  EXPECT_TRUE(G.hasNode("all"));
  EXPECT_TRUE(G.hasNode("vmlinux"));
  EXPECT_TRUE(G.hasNode("init/main.o"));

  auto *AllNode = G.getNode("all");
  ASSERT_NE(AllNode, nullptr);
  EXPECT_TRUE(AllNode->IsPhony);
  ASSERT_EQ(AllNode->Dependencies.size(), 1u);
  EXPECT_EQ(AllNode->Dependencies[0], "vmlinux");
}

TEST_F(BuildDepGraphTest, CycleDetection) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "a: b\n"
      "\techo a\n"
      "b: c\n"
      "\techo b\n"
      "c: a\n"
      "\techo c\n"));

  DepGraph G;
  EXPECT_FALSE(G.build("a", M.Rules));
  EXPECT_TRUE(G.hasCycle());
}

TEST_F(BuildDepGraphTest, TopologicalLayers) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      ".PHONY: all\n"
      "all: vmlinux\n"
      "vmlinux: kernel.o init.o\n"
      "\t$(LD) -o vmlinux kernel.o init.o\n"));

  DepGraph G;
  ASSERT_TRUE(G.build("all", M.Rules));
  auto Layers = G.topologicalLayers();
  EXPECT_GE(Layers.size(), 2u);
}

TEST_F(BuildDepGraphTest, OrderOnlyDeps) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "kernel.o: kernel.c | include/generated\n"
      "\tgcc -c kernel.c\n"));

  DepGraph G;
  ASSERT_TRUE(G.build("kernel.o", M.Rules));

  auto *N = G.getNode("kernel.o");
  ASSERT_NE(N, nullptr);
  EXPECT_EQ(N->Dependencies.size(), 1u);
  EXPECT_EQ(N->Dependencies[0], "kernel.c");
  EXPECT_EQ(N->OrderOnlyDeps.size(), 1u);
  EXPECT_EQ(N->OrderOnlyDeps[0], "include/generated");
}

// ===== Integration: Simplified Linux 5.10 Kernel Makefile =====

class BuildKernelMakefileTest : public ::testing::Test {};

TEST_F(BuildKernelMakefileTest, KernelTopMakefileX86) {
  // Simplified Linux 5.10 top-level Makefile for x86_64
  const char *Makefile = R"(
# SPDX-License-Identifier: GPL-2.0
VERSION = 5
PATCHLEVEL = 10
SUBLEVEL = 0
EXTRAVERSION =
NAME = Kleptomaniac Octopus

KERNELVERSION = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)

ARCH ?= x86
CROSS_COMPILE ?=

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
AS := $(CROSS_COMPILE)as
AR := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy

export ARCH CROSS_COMPILE CC LD

KBUILD_CFLAGS := -Wall -Wundef -Werror=strict-prototypes
KBUILD_CFLAGS += -fno-strict-aliasing -fno-common

ifeq ($(ARCH),x86)
KBUILD_CFLAGS += -m64 -mno-red-zone
MACHINE := x86_64
endif

ifeq ($(ARCH),arm64)
KBUILD_CFLAGS += -mgeneral-regs-only
MACHINE := aarch64
endif

ifdef CONFIG_STACK_PROTECTOR
KBUILD_CFLAGS += -fstack-protector-strong
endif

KBUILD_CFLAGS += -O2

# Object file lists
init-y := init/main.o init/version.o
core-y := kernel/fork.o kernel/exec.o kernel/signal.o
mm-y := mm/page_alloc.o mm/slab.o
net-y := net/socket.o net/core.o

vmlinux-deps := $(init-y) $(core-y) $(mm-y)
vmlinux-extra := $(net-y)

all-obj := $(vmlinux-deps) $(vmlinux-extra)
all-src := $(patsubst %.o,%.c,$(all-obj))
all-dirs := $(sort $(dir $(all-obj)))

.PHONY: all clean mrproper help modules_install

all: vmlinux

vmlinux: $(vmlinux-deps) $(vmlinux-extra)
	$(LD) -o $@ $^

clean:
	rm -f $(all-obj) vmlinux

help:
	@echo 'Cleaning targets:'
	@echo '  clean       - Remove generated files'
	@echo '  mrproper    - Remove all generated files + config'
)";

  ParsedMakefile M;
  ASSERT_TRUE(M.parse(Makefile));

  // Kernel version
  EXPECT_EQ(M.Env.get("KERNELVERSION"), "5.10.0");
  EXPECT_EQ(M.Env.get("NAME"), "Kleptomaniac Octopus");

  // Architecture defaults to x86
  EXPECT_EQ(M.Env.get("ARCH"), "x86");
  EXPECT_EQ(M.Env.get("MACHINE"), "x86_64");

  // Toolchain (no cross-compile prefix)
  EXPECT_EQ(M.Env.get("CC"), "gcc");
  EXPECT_EQ(M.Env.get("LD"), "ld");

  // CFLAGS accumulated correctly with x86-specific flags
  std::string CFlags = M.Env.get("KBUILD_CFLAGS");
  EXPECT_NE(CFlags.find("-Wall"), std::string::npos);
  EXPECT_NE(CFlags.find("-m64"), std::string::npos);
  EXPECT_NE(CFlags.find("-mno-red-zone"), std::string::npos);
  EXPECT_NE(CFlags.find("-O2"), std::string::npos);
  // arm64 flags should NOT be present
  EXPECT_EQ(CFlags.find("-mgeneral-regs-only"), std::string::npos);
  // CONFIG_STACK_PROTECTOR not defined, so no stack protector
  EXPECT_EQ(CFlags.find("-fstack-protector"), std::string::npos);

  // Object lists
  std::string VmlinuxDeps = M.Env.get("vmlinux-deps");
  EXPECT_NE(VmlinuxDeps.find("init/main.o"), std::string::npos);
  EXPECT_NE(VmlinuxDeps.find("kernel/fork.o"), std::string::npos);
  EXPECT_NE(VmlinuxDeps.find("mm/page_alloc.o"), std::string::npos);

  // patsubst .o -> .c
  std::string AllSrc = M.Env.get("all-src");
  EXPECT_NE(AllSrc.find("init/main.c"), std::string::npos);
  EXPECT_NE(AllSrc.find("kernel/fork.c"), std::string::npos);

  // sort + dir extracts unique directories
  std::string AllDirs = M.Env.get("all-dirs");
  EXPECT_NE(AllDirs.find("init/"), std::string::npos);
  EXPECT_NE(AllDirs.find("kernel/"), std::string::npos);
  EXPECT_NE(AllDirs.find("mm/"), std::string::npos);
  EXPECT_NE(AllDirs.find("net/"), std::string::npos);

  // Rule DB checks
  EXPECT_TRUE(M.Rules.isPhony("all"));
  EXPECT_TRUE(M.Rules.isPhony("clean"));
  EXPECT_TRUE(M.Rules.isPhony("help"));
  EXPECT_FALSE(M.Rules.isPhony("vmlinux"));
  EXPECT_EQ(M.Rules.defaultTarget(), "all");

  auto *VmlinuxRule = M.Rules.findRule("vmlinux");
  ASSERT_NE(VmlinuxRule, nullptr);
  EXPECT_GE(VmlinuxRule->Prerequisites.size(), 5u);

  // Export checks
  auto ArchIt = M.Env.vars().find("ARCH");
  ASSERT_NE(ArchIt, M.Env.vars().end());
  EXPECT_TRUE(ArchIt->second.Exported);
  auto CCIt = M.Env.vars().find("CC");
  ASSERT_NE(CCIt, M.Env.vars().end());
  EXPECT_TRUE(CCIt->second.Exported);

  // DepGraph check
  DepGraph G;
  ASSERT_TRUE(G.build("all", M.Rules));
  EXPECT_TRUE(G.hasNode("all"));
  EXPECT_TRUE(G.hasNode("vmlinux"));
  EXPECT_FALSE(G.hasCycle());
}

TEST_F(BuildKernelMakefileTest, KernelTopMakefileArm64CrossCompile) {
  const char *Makefile = R"(
VERSION = 5
PATCHLEVEL = 10
SUBLEVEL = 0

ARCH ?= arm64
CROSS_COMPILE ?= aarch64-linux-gnu-

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld

KBUILD_CFLAGS := -Wall -Wundef
KBUILD_CFLAGS += -fno-common

ifeq ($(ARCH),x86)
KBUILD_CFLAGS += -m64
MACHINE := x86_64
endif

ifeq ($(ARCH),arm64)
KBUILD_CFLAGS += -mgeneral-regs-only
MACHINE := aarch64
endif

KBUILD_CFLAGS += -O2

.PHONY: all
all: Image

Image: arch/arm64/boot/Image
	@echo 'Kernel: $@ is ready'
)";

  ParsedMakefile M;
  ASSERT_TRUE(M.parse(Makefile));

  EXPECT_EQ(M.Env.get("ARCH"), "arm64");
  EXPECT_EQ(M.Env.get("CROSS_COMPILE"), "aarch64-linux-gnu-");
  EXPECT_EQ(M.Env.get("CC"), "aarch64-linux-gnu-gcc");
  EXPECT_EQ(M.Env.get("LD"), "aarch64-linux-gnu-ld");
  EXPECT_EQ(M.Env.get("MACHINE"), "aarch64");

  std::string CFlags = M.Env.get("KBUILD_CFLAGS");
  EXPECT_NE(CFlags.find("-mgeneral-regs-only"), std::string::npos);
  EXPECT_EQ(CFlags.find("-m64"), std::string::npos);
}

TEST_F(BuildKernelMakefileTest, KernelCmdlineOverridesArch) {
  const char *Makefile = R"(
ARCH ?= x86
CROSS_COMPILE ?=

CC := $(CROSS_COMPILE)gcc

ifeq ($(ARCH),arm64)
MACHINE := aarch64
CROSS_COMPILE := aarch64-linux-gnu-
else
MACHINE := x86_64
endif
)";

  ParsedMakefile M;
  M.Env.setFunctionRegistry(&M.FuncReg);
  M.Env.setCommandLineVar("ARCH", "arm64");
  ASSERT_TRUE(M.parse(Makefile));

  EXPECT_EQ(M.Env.get("ARCH"), "arm64");
  EXPECT_EQ(M.Env.get("MACHINE"), "aarch64");
}

TEST_F(BuildKernelMakefileTest, KbuildStyleModuleList) {
  const char *Makefile = R"(
obj-y := core.o
obj-y += sched.o

CONFIG_CGROUPS = y
ifdef CONFIG_CGROUPS
obj-y += cgroup.o
endif

ifndef CONFIG_NO_HZ
obj-y += tick.o
endif

all-objs := $(obj-y)
all-srcs := $(patsubst %.o,%.c,$(all-objs))
obj-count := $(words $(all-objs))

.PHONY: all
all: built-in.a

built-in.a: $(all-objs)
	$(AR) rcs $@ $^
)";

  ParsedMakefile M;
  ASSERT_TRUE(M.parse(Makefile));

  std::string Objs = M.Env.get("all-objs");
  EXPECT_NE(Objs.find("core.o"), std::string::npos);
  EXPECT_NE(Objs.find("sched.o"), std::string::npos);
  EXPECT_NE(Objs.find("cgroup.o"), std::string::npos);
  EXPECT_NE(Objs.find("tick.o"), std::string::npos);
  EXPECT_EQ(M.Env.get("obj-count"), "4");

  std::string Srcs = M.Env.get("all-srcs");
  EXPECT_NE(Srcs.find("core.c"), std::string::npos);

  auto *Rule = M.Rules.findRule("built-in.a");
  ASSERT_NE(Rule, nullptr);
  EXPECT_EQ(Rule->Prerequisites.size(), 4u);
}

TEST_F(BuildKernelMakefileTest, ElseIfeqChain) {
  const char *Makefile = R"(
ARCH := arm64

ifeq ($(ARCH),x86)
BITS := 64
ARCH_DIR := arch/x86
else ifeq ($(ARCH),arm64)
BITS := 64
ARCH_DIR := arch/arm64
else ifeq ($(ARCH),mips)
BITS := 32
ARCH_DIR := arch/mips
else
BITS := unknown
ARCH_DIR := arch/unknown
endif
)";

  ParsedMakefile M;
  ASSERT_TRUE(M.parse(Makefile));

  EXPECT_EQ(M.Env.get("BITS"), "64");
  EXPECT_EQ(M.Env.get("ARCH_DIR"), "arch/arm64");
}

TEST_F(BuildKernelMakefileTest, ForeachEvalGeneratesPerModuleRules) {
  const char *Makefile = R"(
modules := ext4 btrfs xfs

define build-module
$(1).ko: $(1).o
	$(LD) -r -o $(1).ko $(1).o
endef

$(foreach m,$(modules),$(eval $(call build-module,$(m))))

.PHONY: all
all: $(addsuffix .ko,$(modules))
)";

  ParsedMakefile M;
  ASSERT_TRUE(M.parse(Makefile));

  // Each module should have a .ko rule
  for (const char *Mod : {"ext4", "btrfs", "xfs"}) {
    std::string KO = std::string(Mod) + ".ko";
    auto *R = M.Rules.findRule(KO);
    ASSERT_NE(R, nullptr) << "Missing rule for " << KO;
    ASSERT_FALSE(R->Prerequisites.empty());
    EXPECT_EQ(R->Prerequisites[0], std::string(Mod) + ".o");
  }

  // Default target "all" depends on all .ko files
  auto *AllRule = M.Rules.findRule("all");
  ASSERT_NE(AllRule, nullptr);
  EXPECT_EQ(AllRule->Prerequisites.size(), 3u);
}

TEST_F(BuildKernelMakefileTest, UndefineDirective) {
  ParsedMakefile M;
  ASSERT_TRUE(M.parse(
      "TEMP := something\n"
      "undefine TEMP\n"));

  EXPECT_FALSE(M.Env.isDefined("TEMP"));
}

// ===== Portable recipe builtins =====

class BuildBuiltinCommandTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::error_code EC =
        llvm::sys::fs::createUniqueDirectory("neverc-build-rm", Dir);
    ASSERT_FALSE(EC) << EC.message();
  }

  void TearDown() override {
    if (!Dir.empty())
      llvm::sys::fs::remove_directories(Dir);
  }

  std::string pathInDir(llvm::StringRef Name) const {
    llvm::SmallString<256> P(Dir);
    llvm::sys::path::append(P, Name);
    return std::string(P);
  }

  void writeFile(llvm::StringRef Name, llvm::StringRef Contents = "x") const {
    // LLVM's file APIs widen UTF-8 paths on Windows. The binary flag also
    // preserves fixtures that intentionally embed CRLF or bare LF bytes.
    std::error_code EC;
    llvm::raw_fd_ostream Out(pathInDir(Name), EC, llvm::sys::fs::OF_None);
    ASSERT_FALSE(EC) << EC.message();
    Out.write(Contents.data(), Contents.size());
    Out.close();
    ASSERT_FALSE(Out.has_error());
  }

  static std::string readFileBinary(llvm::StringRef Path) {
    auto Buffer = llvm::MemoryBuffer::getFile(Path, /*IsText=*/false);
    EXPECT_TRUE(Buffer) << Buffer.getError().message();
    if (!Buffer)
      return {};
    return Buffer.get()->getBuffer().str();
  }

  llvm::SmallString<256> Dir;
};

TEST_F(BuildBuiltinCommandTest, RmForceRemovesExistingAndMissing) {
  writeFile("a.o");
  writeFile("b.o");
  const std::string A = pathInDir("a.o");
  const std::string B = pathInDir("b.o");
  const std::string Missing = pathInDir("nope.o");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute(
      "rm -f " + A + " " + B + " " + Missing, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists(A));
  EXPECT_FALSE(platform::fileExists(B));
}

TEST_F(BuildBuiltinCommandTest, RmForceGlobMatchesMakeCleanShape) {
  writeFile("one.o");
  writeFile("two.o");
  writeFile("keep.c");
  const std::string Stamp = pathInDir(".nvk-build-flags");
  {
    std::ofstream Out(Stamp);
    ASSERT_TRUE(Out.good());
    Out << "KERNEL=510";
  }

  // Run from the temp directory so `*.o` expands like example clean recipes.
  const std::string OldCwd = platform::getCwd();
  ASSERT_TRUE(platform::changeCwd(std::string(Dir)));
  auto RestoreCwd = [&]() { platform::changeCwd(OldCwd); };

  int Exit = -1;
  const bool Handled = builtins::tryExecute("rm -f *.o .nvk-build-flags", Exit);
  EXPECT_TRUE(Handled);
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists("one.o"));
  EXPECT_FALSE(platform::fileExists("two.o"));
  EXPECT_FALSE(platform::fileExists(".nvk-build-flags"));
  EXPECT_TRUE(platform::fileExists("keep.c"));
  RestoreCwd();
}

TEST_F(BuildBuiltinCommandTest, RmWithoutForceFailsOnMissing) {
  const std::string Missing = pathInDir("missing.o");
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("rm " + Missing, Exit));
  EXPECT_NE(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, RmRecursiveRemovesDirectory) {
  const std::string Nested = pathInDir("outdir");
  ASSERT_FALSE(llvm::sys::fs::create_directory(Nested));
  {
    std::ofstream Out(pathInDir("outdir/x.txt"));
    ASSERT_TRUE(Out.good());
    Out << "x";
  }

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("rm -rf " + Nested, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists(Nested));
}

TEST_F(BuildBuiltinCommandTest, UnknownProgramFallsBackToShell) {
  int Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("no-such-neverc-builtin", Exit));
  EXPECT_EQ(Exit, 123);
}

TEST_F(BuildBuiltinCommandTest, TrueAndFalseSetExitCodes) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("true", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("false", Exit));
  EXPECT_EQ(Exit, 1);
}

TEST_F(BuildBuiltinCommandTest, QuotedGlobStaysLiteral) {
  writeFile("a.o");
  const std::string OldCwd = platform::getCwd();
  ASSERT_TRUE(platform::changeCwd(std::string(Dir)));
  int Exit = -1;
  // Quoted patterns must not expand; with -f a missing literal "*.o" is OK.
  ASSERT_TRUE(builtins::tryExecute("rm -f \"*.o\"", Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists("a.o"));
  platform::changeCwd(OldCwd);
}

TEST_F(BuildBuiltinCommandTest, DoubleQuotedShellExpansionFallsBack) {
  int Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("echo \"$HOME\"", Exit));
  EXPECT_EQ(Exit, 123);
}

TEST_F(BuildBuiltinCommandTest, CpMvCatPwdPrintfBasics) {
  writeFile("src.txt", "hello");
  const std::string Src = pathInDir("src.txt");
  const std::string Dst = pathInDir("dst.txt");
  const std::string Moved = pathInDir("moved.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("cp " + Src + " " + Dst, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Dst));

  ASSERT_TRUE(builtins::tryExecute("mv " + Dst + " " + Moved, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists(Dst));
  EXPECT_TRUE(platform::fileExists(Moved));

  ASSERT_TRUE(builtins::tryExecute("cat " + Moved, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("pwd", Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("printf '%s\\n' ok", Exit));
  EXPECT_EQ(Exit, 0);
}

#ifndef _WIN32
TEST_F(BuildBuiltinCommandTest, LnAndChmodBasics) {
  writeFile("target.txt", "x");
  const std::string Target = pathInDir("target.txt");
  const std::string Link = pathInDir("link.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("ln -s " + Target + " " + Link, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Link));

  // Removing a symlink must not require -r and must not delete the target.
  ASSERT_TRUE(builtins::tryExecute("rm " + Link, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists(Link));
  EXPECT_TRUE(platform::fileExists(Target));

  ASSERT_TRUE(builtins::tryExecute("chmod 644 " + Target, Exit));
  EXPECT_EQ(Exit, 0);
}
#endif

TEST_F(BuildBuiltinCommandTest, UnsupportedRmFlagFallsBackToShell) {
  int Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("rm -v file.o", Exit));
  EXPECT_EQ(Exit, 123);
}

TEST_F(BuildBuiltinCommandTest, UnquotedShellMetacharactersFallBackToShell) {
  int Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("rm -f a.o | true", Exit));
  EXPECT_EQ(Exit, 123);
}

TEST_F(BuildBuiltinCommandTest, EchoHandlesQuotedTextWithMetacharacters) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute(
      "echo \"Module loaded. Check: lsmod | grep foo\"", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, SleepAcceptsFractionalSeconds) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("sleep 0.01", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, MkdirParentsIsIdempotent) {
  const std::string Nested = pathInDir("a/b/c");
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("mkdir -p " + Nested, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("mkdir -p " + Nested, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(llvm::sys::fs::is_directory(Nested));
}

TEST_F(BuildBuiltinCommandTest, TouchCreatesAndUpdates) {
  const std::string Path = pathInDir("stamp.txt");
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("touch " + Path, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Path));
  ASSERT_TRUE(builtins::tryExecute("touch " + Path, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, FileBuiltinsSupportMultilingualUtf8Paths) {
  struct LocaleCase {
    const char *Locale;
    const char *Stem;
    const char *Contents;
  };
  // Cover every locale maintained under docs; the non-BMP emoji case also
  // exercises UTF-8/UTF-16 surrogate conversion on Windows.
  const LocaleCase Cases[] = {
      {"en", u8"english", u8"Hello, world\n"},
      {"zh-CN", u8"\u7B80\u4F53\u4E2D\u6587",
       u8"\u4F60\u597D\uFF0C\u4E16\u754C\n"},
      {"zh-TW", u8"\u7E41\u9AD4\u4E2D\u6587",
       u8"\u4F60\u597D\uFF0C\u4E16\u754C\n"},
      {"ja", u8"\u65E5\u672C\u8A9E",
       u8"\u3053\u3093\u306B\u3061\u306F\u4E16\u754C\n"},
      {"ko", u8"\uD55C\uAD6D\uC5B4",
       u8"\uC548\uB155\uD558\uC138\uC694, \uC138\uACC4\n"},
      {"fr", u8"fran\u00E7ais", u8"Bonjour le monde\n"},
      {"de", u8"gr\u00F6\u00DFe", u8"Hallo Welt\n"},
      {"es", u8"espa\u00F1ol", u8"Hola, mundo\n"},
      {"it", u8"citt\u00E0-italiana", u8"Ciao, mondo\n"},
      {"ru", u8"\u0440\u0443\u0441\u0441\u043A\u0438\u0439",
       u8"\u041F\u0440\u0438\u0432\u0435\u0442, \u043C\u0438\u0440\n"},
      {"ar", u8"\u0627\u0644\u0639\u0631\u0628\u064A\u0629",
       u8"\u0645\u0631\u062D\u0628\u064B\u0627 "
       u8"\u0628\u0627\u0644\u0639\u0627\u0644\u0645\n"},
      {"emoji", u8"\u5168\u7403-\U0001F30D", u8"\U0001F30D\U0001F600\n"},
  };

  for (const LocaleCase &Case : Cases) {
    SCOPED_TRACE(Case.Locale);
    const std::string SourceName = std::string(Case.Stem) + "-source.txt";
    const std::string CopyName = std::string(Case.Stem) + "-copy.txt";
    const std::string Source = pathInDir(SourceName);
    const std::string Copy = pathInDir(CopyName);
    writeFile(SourceName, Case.Contents);

    int Exit = -1;
    ASSERT_TRUE(builtins::tryExecute("touch '" + Source + "'", Exit));
    ASSERT_EQ(Exit, 0);
    ASSERT_TRUE(
        builtins::tryExecute("cp -p '" + Source + "' '" + Copy + "'", Exit));
    ASSERT_EQ(Exit, 0);
    EXPECT_EQ(readFileBinary(Copy), Case.Contents);

    ASSERT_TRUE(
        builtins::tryExecute("rm -f '" + Source + "' '" + Copy + "'", Exit));
    ASSERT_EQ(Exit, 0);
    EXPECT_FALSE(platform::fileExists(Source));
    EXPECT_FALSE(platform::fileExists(Copy));
  }
}

TEST_F(BuildBuiltinCommandTest, RmRecursiveGlobRemovesDirectories) {
  const std::string Nested = pathInDir("outdir");
  ASSERT_FALSE(llvm::sys::fs::create_directory(Nested));
  {
    std::ofstream Out(pathInDir("outdir/x.txt"));
    ASSERT_TRUE(Out.good());
    Out << "x";
  }

  const std::string OldCwd = platform::getCwd();
  ASSERT_TRUE(platform::changeCwd(std::string(Dir)));
  int Exit = -1;
  const bool Handled = builtins::tryExecute("rm -rf outdir", Exit);
  platform::changeCwd(OldCwd);
  ASSERT_TRUE(Handled);
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists(Nested));
}

TEST_F(BuildBuiltinCommandTest, SleepRejectsNaN) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("sleep nan", Exit));
  EXPECT_NE(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, BasenameDirnameAndColon) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("basename /tmp/foo.o .o", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("dirname /tmp/foo.o", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute(": ignored args", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, TestAndBracketBasics) {
  writeFile("exists.txt", "x");
  const std::string Path = pathInDir("exists.txt");
  const std::string Missing = pathInDir("missing.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("test -f " + Path, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("test -f " + Missing, Exit));
  EXPECT_EQ(Exit, 1);
  ASSERT_TRUE(builtins::tryExecute("[ -e " + Path + " ]", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("test abc = abc", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("test 2 -lt 3", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("test ! -f " + Missing, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, InstallDashDCreatesParents) {
  const std::string Nested = pathInDir("inst/a/b");
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("install -d " + Nested, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(llvm::sys::fs::is_directory(Nested));
}

TEST_F(BuildBuiltinCommandTest, CpRejectsRecursiveSelfCopy) {
  const std::string Nested = pathInDir("tree");
  ASSERT_FALSE(llvm::sys::fs::create_directory(Nested));
  {
    std::ofstream Out(pathInDir("tree/x.txt"));
    ASSERT_TRUE(Out.good());
    Out << "x";
  }
  const std::string Dest = pathInDir("tree/nested");
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("cp -r " + Nested + " " + Dest, Exit));
  EXPECT_NE(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, CpGlobMultiSourceRequiresDirectoryDest) {
  writeFile("a.txt", "a");
  writeFile("b.txt", "b");
  const std::string Dest = pathInDir("out.txt");
  const std::string OldCwd = platform::getCwd();
  ASSERT_TRUE(platform::changeCwd(std::string(Dir)));
  int Exit = -1;
  const bool Handled = builtins::tryExecute("cp a.txt b.txt out.txt", Exit);
  platform::changeCwd(OldCwd);
  ASSERT_TRUE(Handled);
  EXPECT_NE(Exit, 0);
  EXPECT_FALSE(platform::fileExists(Dest));
}

TEST_F(BuildBuiltinCommandTest, HeadTailWcUnameSeqBasics) {
  writeFile("lines.txt", "one\ntwo\nthree\nfour\n");
  const std::string Path = pathInDir("lines.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("head -n 2 " + Path, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("tail -n 1 " + Path, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("wc -l " + Path, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("uname -s", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("seq 1 3", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("realpath " + Path, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, CmpEqualAndDiffer) {
  writeFile("a.bin", "same");
  writeFile("b.bin", "same");
  writeFile("c.bin", "diff");
  const std::string A = pathInDir("a.bin");
  const std::string B = pathInDir("b.bin");
  const std::string C = pathInDir("c.bin");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("cmp " + A + " " + B, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("cmp " + A + " " + C, Exit));
  EXPECT_EQ(Exit, 1);
}

#ifndef _WIN32
TEST_F(BuildBuiltinCommandTest, ReadlinkBasics) {
  writeFile("target.txt", "x");
  const std::string Target = pathInDir("target.txt");
  const std::string Link = pathInDir("link.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("ln -s " + Target + " " + Link, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("readlink " + Link, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("readlink -f " + Link, Exit));
  EXPECT_EQ(Exit, 0);
}
#endif

TEST_F(BuildBuiltinCommandTest, HashCommentIsStrippedLikeShell) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("echo foo # bar should be ignored", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, EchoExpandsUnquotedGlobs) {
  writeFile("a.o");
  writeFile("b.o");
  const std::string OldCwd = platform::getCwd();
  ASSERT_TRUE(platform::changeCwd(std::string(Dir)));
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("echo *.o", Exit));
  platform::changeCwd(OldCwd);
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, BasenameDirnameRootPath) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("basename /", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("dirname /", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, BasenameDirnameStripTrailingSlash) {
  // Capture stdout is not wired; just ensure POSIX arity/exit and handling.
  // `basename /tmp/foo/` must not become "." (LLVM filename quirk).
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("basename /tmp/foo/", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("dirname /tmp/foo/", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("dirname foo/", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, TouchUpdatesDirectory) {
  const std::string Nested = pathInDir("touchdir");
  ASSERT_FALSE(llvm::sys::fs::create_directory(Nested));
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("touch " + Nested, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(llvm::sys::fs::is_directory(Nested));
}

TEST_F(BuildBuiltinCommandTest, TestRWXGrepVSortRStatPaste) {
  writeFile("rw.txt", "alpha\nbravo\ncharlie\n");
  writeFile("a.txt", "a1\na2\n");
  writeFile("b.txt", "b1\nb2\n");
  writeFile("sort.txt", "b\na\nc\n");
  const std::string Rw = pathInDir("rw.txt");
  const std::string A = pathInDir("a.txt");
  const std::string B = pathInDir("b.txt");
  const std::string SortPath = pathInDir("sort.txt");

#ifndef _WIN32
  ASSERT_FALSE(llvm::sys::fs::setPermissions(
      Rw, static_cast<llvm::sys::fs::perms>(0644)));
#endif

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("test -r " + Rw, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("test -w " + Rw, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("grep -Fv alpha " + Rw, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("sort -r " + SortPath, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("stat -c %s " + Rw, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("paste -d : " + A + " " + B, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, LsWhichNprocExprCutDate) {
  writeFile("keep.txt", "a:b:c\n");
  const std::string Path = pathInDir("keep.txt");
  const std::string OldCwd = platform::getCwd();
  ASSERT_TRUE(platform::changeCwd(std::string(Dir)));

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("ls -1", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("nproc", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("expr 2 + 3", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("expr 0 + 0", Exit));
  EXPECT_EQ(Exit, 1);
  ASSERT_TRUE(builtins::tryExecute("cut -d : -f 2 keep.txt", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("date +%Y", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("hostname", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("whoami", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("which ls", Exit));
  // which may be 0 or 1 depending on PATH; only require it was handled.
  EXPECT_TRUE(Exit == 0 || Exit == 1);

  platform::changeCwd(OldCwd);
  (void)Path;
}

TEST_F(BuildBuiltinCommandTest, MktempCreatesFileAndDirectory) {
  int Exit = -1;
  // Keep artifacts under the test temp dir so TearDown removes them.
  ASSERT_TRUE(builtins::tryExecute(
      "mktemp " + pathInDir("file.XXXXXXXX"), Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute(
      "mktemp -d " + pathInDir("dir.XXXXXXXX"), Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, RmForceWithNoOperandsIsOk) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("rm -f", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, MktempPreservesNonTrailingX) {
  int Exit = -1;
  const std::string Template = pathInDir("fileX.XXXXXXXX");
  ASSERT_TRUE(builtins::tryExecute("mktemp " + Template, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, CatWcHeadWithoutFilesFallBack) {
  int Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("cat", Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("wc -l", Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("head -n 1", Exit));
  EXPECT_EQ(Exit, 123);
}

TEST_F(BuildBuiltinCommandTest, RmdirIdSyncSortUniqAndInstallFile) {
  writeFile("src.txt", "b\na\na\nc\n");
  const std::string Src = pathInDir("src.txt");
  const std::string Dest = pathInDir("dst.txt");
  const std::string Nested = pathInDir("p/q");
  ASSERT_FALSE(llvm::sys::fs::create_directories(Nested));

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("install -m 644 " + Src + " " + Dest, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Dest));

  ASSERT_TRUE(builtins::tryExecute("sort -u " + Src, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("uniq " + Src, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("sync", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("rmdir " + Nested, Exit));
  EXPECT_EQ(Exit, 0);

#ifndef _WIN32
  ASSERT_TRUE(builtins::tryExecute("id -u", Exit));
  EXPECT_EQ(Exit, 0);
#endif
}

TEST_F(BuildBuiltinCommandTest, CpForceSameFileDoesNotDestroySource) {
  writeFile("same.txt", "keep-me");
  const std::string Path = pathInDir("same.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("cp -f " + Path + " " + Path, Exit));
  EXPECT_NE(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Path));
  {
    std::ifstream In(Path);
    std::string Contents;
    std::getline(In, Contents);
    EXPECT_EQ(Contents, "keep-me");
  }

  ASSERT_TRUE(builtins::tryExecute("install -m 644 " + Path + " " + Path, Exit));
  EXPECT_NE(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Path));
}

TEST_F(BuildBuiltinCommandTest, MvSameFileIsSuccessfulNoOp) {
  writeFile("same.txt", "keep-me");
  const std::string Path = pathInDir("same.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("mv -f " + Path + " " + Path, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Path));
}

TEST_F(BuildBuiltinCommandTest, RmPreservesFilesystemRoot) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("rm -rf /", Exit));
  EXPECT_NE(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("rm -rf //", Exit));
  EXPECT_NE(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, RmRefusesDotAndDotDot) {
  writeFile("keep.txt", "safe");
  const std::string Keep = pathInDir("keep.txt");
  const std::string OldCwd = platform::getCwd();
  ASSERT_TRUE(platform::changeCwd(std::string(Dir)));

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("rm -rf .", Exit));
  EXPECT_NE(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("rm -rf ..", Exit));
  EXPECT_NE(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("rm -rf ./.", Exit));
  EXPECT_NE(Exit, 0);

  platform::changeCwd(OldCwd);
  EXPECT_TRUE(platform::fileExists(Keep));
}

TEST_F(BuildBuiltinCommandTest, PrintfReusesFormatAndCmpSilent) {
  writeFile("a.txt", "same");
  writeFile("b.txt", "same");
  writeFile("c.txt", "diff");
  const std::string A = pathInDir("a.txt");
  const std::string B = pathInDir("b.txt");
  const std::string C = pathInDir("c.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("printf '%s\\n' one two", Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("cmp -s " + A + " " + B, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("cmp -s " + A + " " + C, Exit));
  EXPECT_EQ(Exit, 1);
}

TEST_F(BuildBuiltinCommandTest, TestNewerThanAndUnlinkRevFold) {
  writeFile("old.txt", "old");
  writeFile("new.txt", "new");
  writeFile("rev.txt", "ab\ncd\n");
  writeFile("fold.txt", "abcdefghij");
  const std::string Old = pathInDir("old.txt");
  const std::string New = pathInDir("new.txt");
  const std::string Rev = pathInDir("rev.txt");
  const std::string Fold = pathInDir("fold.txt");
  const std::string Doomed = pathInDir("doomed.txt");
  writeFile("doomed.txt", "x");

  int Exit = -1;
  // Ensure New is strictly newer than Old for -nt.
  ASSERT_TRUE(builtins::tryExecute("sleep 0.05", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("touch " + New, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("test " + New + " -nt " + Old, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("test " + Old + " -ot " + New, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("unlink " + Doomed, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists(Doomed));

  ASSERT_TRUE(builtins::tryExecute("rev " + Rev, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("fold -w 4 " + Fold, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, DiffChecksumTruncateArchBasics) {
  writeFile("a.txt", "same");
  writeFile("b.txt", "same");
  writeFile("c.txt", "diff");
  const std::string A = pathInDir("a.txt");
  const std::string B = pathInDir("b.txt");
  const std::string C = pathInDir("c.txt");
  const std::string Empty = pathInDir("empty.bin");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("diff -q " + A + " " + B, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("diff -q " + A + " " + C, Exit));
  EXPECT_EQ(Exit, 1);

  ASSERT_TRUE(builtins::tryExecute("md5sum " + A, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("sha1sum " + A, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("sha256sum " + A, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("truncate -s 0 " + Empty, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Empty));
  ASSERT_TRUE(builtins::tryExecute("truncate -s 4K " + Empty, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("arch", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, ShellExpansionsAndRelativeTruncateFallBack) {
  int Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("rm -f ~", Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("echo (oops)", Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("expr 2 * 3", Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("truncate -s +10 "
                                    + pathInDir("x.bin"), Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("truncate -s -10 "
                                    + pathInDir("x.bin"), Exit));
  EXPECT_EQ(Exit, 123);
}

TEST_F(BuildBuiltinCommandTest, GrepDuAndCommandVBasics) {
  writeFile("hay.txt", "alpha\nBravo\ncharlie\n");
  const std::string Path = pathInDir("hay.txt");
  const std::string Nested = pathInDir("tree");
  ASSERT_FALSE(llvm::sys::fs::create_directory(Nested));
  {
    std::ofstream Out(pathInDir("tree/x.bin"), std::ios::binary);
    ASSERT_TRUE(Out.good());
    Out << "abcd";
  }

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("grep -Fq alpha " + Path, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("grep -Fq missing " + Path, Exit));
  EXPECT_EQ(Exit, 1);
  ASSERT_TRUE(builtins::tryExecute("grep -Eiq '^bravo$' " + Path, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("du -sk " + Nested, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("command -v ls", Exit));
  EXPECT_TRUE(Exit == 0 || Exit == 1);
}

TEST_F(BuildBuiltinCommandTest, RmdirDoesNotDeleteRegularFiles) {
  writeFile("keep.txt", "data");
  const std::string Path = pathInDir("keep.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("rmdir " + Path, Exit));
  EXPECT_NE(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Path));
}

TEST_F(BuildBuiltinCommandTest, InstallClusteredDashDM) {
  const std::string Nested = pathInDir("inst2/a");
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("install -dm755 " + Nested, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(llvm::sys::fs::is_directory(Nested));
}

TEST_F(BuildBuiltinCommandTest, TacNlBase64CksumTypeGetconf) {
  writeFile("lines.txt", "one\ntwo\nthree\n");
  writeFile("bin.txt", "hi");
  const std::string Lines = pathInDir("lines.txt");
  const std::string Bin = pathInDir("bin.txt");
  const std::string B64 = pathInDir("bin.b64");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("tac " + Lines, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("nl " + Lines, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("base64 " + Bin, Exit));
  EXPECT_EQ(Exit, 0);
  // Encode to a file via shell fallback is not needed: write known base64.
  {
    std::ofstream Out(B64, std::ios::binary);
    ASSERT_TRUE(Out.good());
    Out << "aGk=\n"; // "hi"
  }
  ASSERT_TRUE(builtins::tryExecute("base64 -d " + B64, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("cksum " + Bin, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("type -p ls", Exit));
  EXPECT_TRUE(Exit == 0 || Exit == 1);
  ASSERT_TRUE(builtins::tryExecute("getconf NPROCESSORS_ONLN", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, TailFromLineFormFallsBack) {
  writeFile("lines.txt", "1\n2\n3\n4\n");
  const std::string Path = pathInDir("lines.txt");
  int Exit = 123;
  // `tail -n +N` means start-at-line, not last-N — must not be claimed.
  EXPECT_FALSE(builtins::tryExecute("tail -n +2 " + Path, Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("tail +2 " + Path, Exit));
  EXPECT_EQ(Exit, 123);
}

TEST_F(BuildBuiltinCommandTest, RmEmptyOperandMatchesForceSemantics) {
  int Exit = -1;
  // GNU/BSD: `rm -f ''` is a successful no-op; without `-f` it fails.
  ASSERT_TRUE(builtins::tryExecute("rm -f ''", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("rm ''", Exit));
  EXPECT_NE(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, SplitStringsAndLognameBasics) {
  writeFile("nums.txt", "1\n2\n3\n4\n");
  writeFile("empty.txt", "");
  {
    // No trailing newline on the final line — split must preserve that.
    std::ofstream Out(pathInDir("nonl.txt"), std::ios::binary);
    ASSERT_TRUE(Out.good());
    Out << "a\nb\nc";
  }
  writeFile("bin.txt", std::string("xx\0hello!\0yy", 12));
  const std::string Nums = pathInDir("nums.txt");
  const std::string Empty = pathInDir("empty.txt");
  const std::string Nonl = pathInDir("nonl.txt");
  const std::string Bin = pathInDir("bin.txt");
  const std::string Prefix = pathInDir("part");
  const std::string EmptyPrefix = pathInDir("empty_");
  const std::string NonlPrefix = pathInDir("nonl_");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("split -l 2 " + Nums + " " + Prefix, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Prefix + "aa"));
  EXPECT_TRUE(platform::fileExists(Prefix + "ab"));

  // Classic split: empty input creates no output files.
  ASSERT_TRUE(
      builtins::tryExecute("split -l 2 " + Empty + " " + EmptyPrefix, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists(EmptyPrefix + "aa"));

  ASSERT_TRUE(
      builtins::tryExecute("split -l 2 " + Nonl + " " + NonlPrefix, Exit));
  EXPECT_EQ(Exit, 0);
  {
    std::ifstream A(NonlPrefix + "aa", std::ios::binary);
    std::ifstream B(NonlPrefix + "ab", std::ios::binary);
    std::string ABody((std::istreambuf_iterator<char>(A)), {});
    std::string BBody((std::istreambuf_iterator<char>(B)), {});
    EXPECT_EQ(ABody, "a\nb\n");
    EXPECT_EQ(BBody, "c"); // must NOT gain a trailing newline
  }

  ASSERT_TRUE(builtins::tryExecute("strings " + Bin, Exit));
  EXPECT_EQ(Exit, 0);
  Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("strings -", Exit));
  EXPECT_EQ(Exit, 123);

#ifndef _WIN32
  // POSIX logname(1) uses getlogin() only. That commonly fails in CI /
  // non-login sessions; the builtin then falls back to the host tool.
  if (::getlogin()) {
    ASSERT_TRUE(builtins::tryExecute("logname", Exit));
    EXPECT_EQ(Exit, 0);
  } else {
    EXPECT_FALSE(builtins::tryExecute("logname", Exit));
  }
#endif
}

TEST_F(BuildBuiltinCommandTest, OdExpandCommBasics) {
  writeFile("hex.bin", std::string("AB\n", 3));
  writeFile("tabs.txt", "a\tb\n");
  writeFile("comm_a.txt", "a\nb\nc\n");
  writeFile("comm_b.txt", "b\nc\nd\n");
  const std::string Hex = pathInDir("hex.bin");
  const std::string Tabs = pathInDir("tabs.txt");
  const std::string A = pathInDir("comm_a.txt");
  const std::string B = pathInDir("comm_b.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("od -An -tx1 " + Hex, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("od -An -c " + Hex, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("expand -t 4 " + Tabs, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("comm -12 " + A + " " + B, Exit));
  EXPECT_EQ(Exit, 0);

  Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("od -tx1 " + Hex, Exit)); // needs -An
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("expand", Exit)); // stdin
  EXPECT_EQ(Exit, 123);
}

#ifndef _WIN32
TEST_F(BuildBuiltinCommandTest, StatDoesNotFollowSymlink) {
  writeFile("stat_tgt.txt", "abcd");
  const std::string Target = pathInDir("stat_tgt.txt");
  const std::string Link = pathInDir("stat_link");
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("ln -s " + Target + " " + Link, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("stat -c %F " + Link, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, CpRecursiveFollowsSymlinkDirectory) {
  const std::string SrcDir = pathInDir("cp_src_dir");
  const std::string LinkDir = pathInDir("cp_link_dir");
  const std::string DestDir = pathInDir("cp_dest_dir");
  ASSERT_FALSE(llvm::sys::fs::create_directory(SrcDir));
  {
    std::ofstream Out(pathInDir("cp_src_dir/x.txt"));
    ASSERT_TRUE(Out.good());
    Out << "payload";
  }
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("ln -s " + SrcDir + " " + LinkDir, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("cp -r " + LinkDir + " " + DestDir, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(pathInDir("cp_dest_dir/x.txt")));
}

TEST_F(BuildBuiltinCommandTest, CpRecursiveRejectsSymlinkDirectoryCycle) {
  const std::string Tree = pathInDir("cycle_tree");
  const std::string Dest = pathInDir("cycle_dest");
  ASSERT_FALSE(llvm::sys::fs::create_directory(Tree));
  {
    std::ofstream Out(pathInDir("cycle_tree/keep.txt"));
    ASSERT_TRUE(Out.good());
    Out << "x";
  }
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute(
      "ln -s " + Tree + " " + pathInDir("cycle_tree/up"), Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("cp -r " + Tree + " " + Dest, Exit));
  EXPECT_NE(Exit, 0);
}
#endif

TEST_F(BuildBuiltinCommandTest, BasenameEmptyIsDot) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("basename ''", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, UnexpandJoinFactorLinkAndFoldDefault) {
  writeFile("spaces.txt", "        hi\n");
  writeFile("join_a.txt", "a 1\nb 2\n");
  writeFile("join_b.txt", "a x\nb y\n");
  writeFile("fold.txt", std::string(90, 'x') + "\n");
  writeFile("link_src.txt", "payload");
  const std::string Spaces = pathInDir("spaces.txt");
  const std::string JA = pathInDir("join_a.txt");
  const std::string JB = pathInDir("join_b.txt");
  const std::string Fold = pathInDir("fold.txt");
  const std::string Src = pathInDir("link_src.txt");
  const std::string Dst = pathInDir("link_dst.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("unexpand -t 8 " + Spaces, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("join " + JA + " " + JB, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("factor 12", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("fold " + Fold, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("link " + Src + " " + Dst, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Dst));
}

TEST_F(BuildBuiltinCommandTest, BraceExpansionFallsBackToShell) {
  int Exit = -1;
  // Builtins must not claim brace expansion; otherwise `rm -f {a,b}.o` would
  // treat the braces as a literal filename and skip deleting a.o/b.o.
  EXPECT_FALSE(builtins::tryExecute("rm -f {a,b}.o", Exit));
}

TEST_F(BuildBuiltinCommandTest, PrintfEndOfOptionsAndEgrepFgrep) {
  writeFile("egrep.txt", "alpha\nbeta\n");
  const std::string Path = pathInDir("egrep.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("printf -- '%s\\n' ok", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("egrep 'b[a-z]+' " + Path, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("fgrep beta " + Path, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, Dos2UnixUnix2DosAndShuf) {
  writeFile("crlf.txt", "a\r\nb\r\n");
  writeFile("shuf.txt", "1\n2\n3\n4\n");
  const std::string Crlf = pathInDir("crlf.txt");
  const std::string Shuf = pathInDir("shuf.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("dos2unix " + Crlf, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_EQ(readFileBinary(Crlf), "a\nb\n");
  ASSERT_TRUE(builtins::tryExecute("unix2dos " + Crlf, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_EQ(readFileBinary(Crlf), "a\r\nb\r\n");
  ASSERT_TRUE(builtins::tryExecute("shuf " + Shuf, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, PasteEmptyDelimiterConcatenates) {
  writeFile("pa.txt", "a\nb\n");
  writeFile("pb.txt", "1\n2\n");
  const std::string A = pathInDir("pa.txt");
  const std::string B = pathInDir("pb.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("paste -d '' " + A + " " + B, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, QuotedOptionsStillParseAsFlags) {
  // Shell strips quotes before argv reaches the program, so quoted option
  // spellings must still be recognized (Quoted is only for glob expansion).
  writeFile("keep.txt", "x");
  const std::string Keep = pathInDir("keep.txt");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("echo \"-n\" ok", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("rm \"-f\" " + Keep, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists(Keep));
}

TEST_F(BuildBuiltinCommandTest, TouchNoCreateInstallTargetAndTestEf) {
  writeFile("src.txt", "payload");
  const std::string Src = pathInDir("src.txt");
  const std::string Missing = pathInDir("missing.txt");
  const std::string DestDir = pathInDir("inst_t");
  ASSERT_FALSE(llvm::sys::fs::create_directory(DestDir));

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("touch -c " + Missing, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists(Missing));

  ASSERT_TRUE(builtins::tryExecute("install -t " + DestDir + " " + Src, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(pathInDir("inst_t/src.txt")));

  ASSERT_TRUE(builtins::tryExecute("test " + Src + " -ef " + Src, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute(
      "test " + Src + " -ef " + pathInDir("inst_t/src.txt"), Exit));
  EXPECT_EQ(Exit, 1);
}

TEST_F(BuildBuiltinCommandTest, MkdirModeCpPreserveAndYes) {
  const std::string Dir = pathInDir("moded");
  writeFile("stamp.txt", "old");
  const std::string Src = pathInDir("stamp.txt");
  const std::string Dst = pathInDir("stamp_copy.txt");

  int Exit = -1;
#ifndef _WIN32
  ASSERT_TRUE(builtins::tryExecute("mkdir -m 700 " + Dir, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(llvm::sys::fs::is_directory(Dir));
#endif

  ASSERT_TRUE(builtins::tryExecute("cp -p " + Src + " " + Dst, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Dst));

  // Piped `yes` must stay on the host shell; unsupported flags fall back too.
  Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("yes | head -n 1", Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("yes --help", Exit));
  EXPECT_EQ(Exit, 123);
}

TEST_F(BuildBuiltinCommandTest, JoinCartesianProductOnDuplicateKeys) {
  writeFile("join_a2.txt", "k 1\nk 2\n");
  writeFile("join_b2.txt", "k x\nk y\n");
  const std::string A = pathInDir("join_a2.txt");
  const std::string B = pathInDir("join_b2.txt");

  int Exit = -1;
  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("join " + A + " " + B, Exit));
  const std::string Out = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Exit, 0);
  EXPECT_EQ(Out, "k 1 x\nk 1 y\nk 2 x\nk 2 y\n");
}

TEST_F(BuildBuiltinCommandTest, GrepNSortNUniqCBasenameAAndCpA) {
  writeFile("nums.txt", "10\n2\n1\n");
  writeFile("dup.txt", "a\na\nb\n");
  writeFile("grep.txt", "alpha\nbeta\ngamma\n");
  writeFile("cp_src.txt", "body");
  const std::string Nums = pathInDir("nums.txt");
  const std::string Dup = pathInDir("dup.txt");
  const std::string Grep = pathInDir("grep.txt");
  const std::string Src = pathInDir("cp_src.txt");
  const std::string Dst = pathInDir("cp_a_dst.txt");
  const std::string Nested = pathInDir("pm755");

  int Exit = -1;
  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("grep -Fn -n beta " + Grep, Exit));
  std::string Out = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Exit, 0);
  EXPECT_EQ(Out, "2:beta\n");

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("sort -n " + Nums, Exit));
  Out = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Exit, 0);
  EXPECT_EQ(Out, "1\n2\n10\n");

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("uniq -c " + Dup, Exit));
  Out = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Exit, 0);
  EXPECT_EQ(Out, "      2 a\n      1 b\n");

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("basename -a /tmp/a.o /tmp/b.o", Exit));
  Out = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Exit, 0);
  EXPECT_EQ(Out, "a.o\nb.o\n");

  ASSERT_TRUE(builtins::tryExecute("cp -a " + Src + " " + Dst, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Dst));

#ifndef _WIN32
  ASSERT_TRUE(builtins::tryExecute("mkdir -pm755 " + Nested, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(llvm::sys::fs::is_directory(Nested));
#endif
}

TEST_F(BuildBuiltinCommandTest, GroupsAndTtyBasics) {
  int Exit = -1;
#ifndef _WIN32
  ASSERT_TRUE(builtins::tryExecute("groups", Exit));
  EXPECT_EQ(Exit, 0);
#endif
  // In CI / gtest, stdin is usually not a tty: builtin must still handle it.
  ASSERT_TRUE(builtins::tryExecute("tty", Exit));
  EXPECT_EQ(Exit, 1);
}

TEST_F(BuildBuiltinCommandTest, StdinDashFallsBackAndPwdRejectsArgs) {
  int Exit = -1;
  EXPECT_FALSE(builtins::tryExecute("cat -", Exit));
  EXPECT_FALSE(builtins::tryExecute("wc -l -", Exit));
  EXPECT_FALSE(builtins::tryExecute("md5sum -", Exit));
  EXPECT_FALSE(builtins::tryExecute("sort -", Exit));
  EXPECT_FALSE(builtins::tryExecute("head -n 1 -", Exit));

  ASSERT_TRUE(builtins::tryExecute("pwd extra", Exit));
  EXPECT_EQ(Exit, 1);
}

TEST_F(BuildBuiltinCommandTest, InstallDashDTestAoRmDashDashAndXxdDf) {
  writeFile("payload.txt", "payload\n");
  const std::string Src = pathInDir("payload.txt");
  const std::string Dest = pathInDir("nested/bin/tool");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("install -Dm644 " + Src + " " + Dest, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Dest));

  // Operand begins with '-' and must be accepted after `--`.
  const std::string DashFile = pathInDir("-weird");
  {
    std::ofstream Out(DashFile);
    ASSERT_TRUE(Out.good());
    Out << "x\n";
  }
  ASSERT_TRUE(builtins::tryExecute("rm -f -- " + DashFile, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_FALSE(platform::fileExists(DashFile));

  ASSERT_TRUE(builtins::tryExecute("test -f " + Src + " -a -f " + Dest, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute(
      "test -f " + Src + " -a -f " + pathInDir("missing"), Exit));
  EXPECT_EQ(Exit, 1);

  writeFile("hex.bin", "AB");
  ASSERT_TRUE(builtins::tryExecute("xxd -p " + pathInDir("hex.bin"), Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("uname -v", Exit));
  EXPECT_EQ(Exit, 0);

#ifndef _WIN32
  ASSERT_TRUE(builtins::tryExecute("df -kP .", Exit));
  EXPECT_EQ(Exit, 0);
#else
  EXPECT_FALSE(builtins::tryExecute("df -kP .", Exit));
#endif
}

TEST_F(BuildBuiltinCommandTest, TestNegationBindsTighterThanAnd) {
  // POSIX: `! A -a B` == `(! A) -a B`, not `!(A -a B)`.
  // Both missing: (!false) && false == false.
  const std::string MissingA = pathInDir("no_a");
  const std::string MissingB = pathInDir("no_b");
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute(
      "test ! -f " + MissingA + " -a -f " + MissingB, Exit));
  EXPECT_EQ(Exit, 1);

  writeFile("yes.txt", "x");
  const std::string Yes = pathInDir("yes.txt");
  // (!false) && true == true when left is missing and right exists.
  ASSERT_TRUE(builtins::tryExecute(
      "test ! -f " + MissingA + " -a -f " + Yes, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, CpMvInstallStdinDashFallBack) {
  int Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("cp - " + pathInDir("out.txt"), Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("mv - " + pathInDir("out.txt"), Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute(
      "install -m 644 - " + pathInDir("out.txt"), Exit));
  EXPECT_EQ(Exit, 123);
}

TEST_F(BuildBuiltinCommandTest, TouchRefAndDdBasics) {
  writeFile("ref.txt", "old");
  writeFile("src.bin", "hello-dd");
  const std::string Ref = pathInDir("ref.txt");
  const std::string New = pathInDir("new.txt");
  const std::string Src = pathInDir("src.bin");
  const std::string Dst = pathInDir("dst.bin");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("touch " + New, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("touch -r " + Ref + " " + New, Exit));
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute(
      "dd if=" + Src + " of=" + Dst + " bs=4 count=1", Exit));
  EXPECT_EQ(Exit, 0);
  {
    std::ifstream In(Dst, std::ios::binary);
    std::string Body((std::istreambuf_iterator<char>(In)), {});
    EXPECT_EQ(Body, "hell");
  }

  Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("dd if=- of=" + Dst, Exit));
  EXPECT_EQ(Exit, 123);
}

#ifndef _WIN32
TEST_F(BuildBuiltinCommandTest, ChownChgrpNumericBasics) {
  writeFile("own.txt", "x");
  const std::string Path = pathInDir("own.txt");
  int Exit = -1;
  // Use the current euid/egid so the call is permission-safe in tests.
  ASSERT_TRUE(builtins::tryExecute(
      "chown " + std::to_string(::geteuid()) + " " + Path, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute(
      "chgrp " + std::to_string(::getegid()) + " " + Path, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, ChownUserColonLoginGroupFallsBack) {
  writeFile("own2.txt", "x");
  const std::string Path = pathInDir("own2.txt");
  int Exit = 123;
  // GNU `chown user:` sets the login group; we must not claim it.
  EXPECT_FALSE(builtins::tryExecute("chown root: " + Path, Exit));
  EXPECT_EQ(Exit, 123);
}
#endif

TEST_F(BuildBuiltinCommandTest, TypeBareFallsBackTypePWorks) {
  int Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("type ls", Exit));
  EXPECT_EQ(Exit, 123);
  ASSERT_TRUE(builtins::tryExecute("type -p ls", Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, HeadEmptyAndNoTrailingNewline) {
  writeFile("empty.txt", "");
  writeFile("nonl.txt", "abc");
  const std::string Empty = pathInDir("empty.txt");
  const std::string Nonl = pathInDir("nonl.txt");

  int Exit = -1;
  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("head -n 1 " + Empty, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "");
  EXPECT_EQ(Exit, 0);

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("head -n 1 " + Nonl, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "abc");
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, BasenameSDirnameMultiCpNAndFileHexdump) {
  writeFile("keep.txt", "old");
  writeFile("src.txt", "new");
  writeFile("text.txt", "hello\n");
  writeFile("bin.txt", std::string("AB", 2));
  const std::string Keep = pathInDir("keep.txt");
  const std::string Src = pathInDir("src.txt");
  const std::string Text = pathInDir("text.txt");
  const std::string Bin = pathInDir("bin.txt");
  const std::string Nested = pathInDir("subdir");

  int Exit = -1;
  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("basename -s .o /tmp/foo.o /tmp/bar.o", Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "foo\nbar\n");
  EXPECT_EQ(Exit, 0);

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("dirname /tmp/a /tmp/b/c", Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "/tmp\n/tmp/b\n");
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("cp -n " + Src + " " + Keep, Exit));
  EXPECT_EQ(Exit, 0);
  {
    std::ifstream In(Keep);
    std::string Body((std::istreambuf_iterator<char>(In)), {});
    EXPECT_EQ(Body, "old");
  }

  ASSERT_FALSE(llvm::sys::fs::create_directory(Nested));
  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("file " + Text + " " + Nested, Exit));
  const std::string FileOut = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Exit, 0);
  EXPECT_NE(FileOut.find("ASCII text"), std::string::npos);
  EXPECT_NE(FileOut.find("directory"), std::string::npos);

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("hexdump -C " + Bin, Exit));
  const std::string HexOut = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Exit, 0);
  EXPECT_NE(HexOut.find("41 42"), std::string::npos);
  EXPECT_NE(HexOut.find("|AB|"), std::string::npos);

  ASSERT_TRUE(builtins::tryExecute("touch -m " + Keep, Exit));
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, CpRejectsTrailingSlashSelfCopy) {
  const std::string Nested = pathInDir("tree");
  ASSERT_FALSE(llvm::sys::fs::create_directory(Nested));
  {
    std::ofstream Out(pathInDir("tree/x.txt"));
    ASSERT_TRUE(Out.good());
    Out << "x";
  }
  const std::string Dest = pathInDir("tree/nested");
  int Exit = -1;
  // Trailing slash on the source must not bypass the self-copy guard.
  ASSERT_TRUE(builtins::tryExecute("cp -r " + Nested + "/ " + Dest, Exit));
  EXPECT_NE(Exit, 0);
  EXPECT_FALSE(platform::fileExists(Dest));
}

TEST_F(BuildBuiltinCommandTest, SedSubstituteAndInPlace) {
  writeFile("msg.txt", "hello world\nhello neverc\n");
  const std::string Path = pathInDir("msg.txt");
  int Exit = -1;

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("sed 's/hello/hi/g' " + Path, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "hi world\nhi neverc\n");
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("sed -i 's/world/earth/' " + Path, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_EQ(readFileBinary(Path), "hello earth\nhello neverc\n");

  // Backrefs are intentionally left to the host sed.
  EXPECT_FALSE(builtins::tryExecute("sed 's/h\\(e\\)llo/H\\1/' " + Path, Exit));
}

TEST_F(BuildBuiltinCommandTest, SedBreEreMismatchFallsBack) {
  writeFile("plus.txt", "aa\n");
  const std::string Path = pathInDir("plus.txt");
  int Exit = 123;
  // Unescaped `+` is literal in BRE (host sed) but quantifier in ERE
  // (llvm::Regex). Claiming it would silently disagree with /usr/bin/sed.
  EXPECT_FALSE(builtins::tryExecute("sed 's/a+/x/' " + Path, Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("sed 's/a|b/x/' " + Path, Exit));
  EXPECT_EQ(Exit, 123);
}

TEST_F(BuildBuiltinCommandTest, SortNuUniquesByNumericKey) {
  writeFile("nums.txt", "01\n1\n2\n");
  const std::string Path = pathInDir("nums.txt");
  int Exit = -1;
  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("sort -nu " + Path, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "01\n2\n");
  EXPECT_EQ(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, Sha512sumHeadBytesCatDashDash) {
  writeFile("abc.txt", "abc");
  writeFile("bytes.txt", "0123456789");
  const std::string Abc = pathInDir("abc.txt");
  const std::string Bytes = pathInDir("bytes.txt");
  const std::string DashFile = pathInDir("-dash.txt");
  {
    std::ofstream Out(DashFile);
    ASSERT_TRUE(Out.good());
    Out << "dash-body";
  }

  int Exit = -1;
  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("sha512sum " + Abc, Exit));
  const std::string Out = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(llvm::StringRef(Out).starts_with(
      "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
      "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f  "));

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("head -c 4 " + Bytes, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "0123");
  EXPECT_EQ(Exit, 0);

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("tail -c 3 " + Bytes, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "789");
  EXPECT_EQ(Exit, 0);

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("cat -- " + DashFile, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "dash-body");
  EXPECT_EQ(Exit, 0);

  Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("factor 1000000000001", Exit));
  EXPECT_EQ(Exit, 123);
}

#ifndef _WIN32
TEST_F(BuildBuiltinCommandTest, LnSfnReplacesSymlinkToDirectory) {
  const std::string DirA = pathInDir("lna");
  const std::string DirB = pathInDir("lnb");
  const std::string Link = pathInDir("lnlink");
  ASSERT_FALSE(llvm::sys::fs::create_directory(DirA));
  ASSERT_FALSE(llvm::sys::fs::create_directory(DirB));
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("ln -s " + DirA + " " + Link, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("ln -sfn " + DirB + " " + Link, Exit));
  EXPECT_EQ(Exit, 0);
  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("readlink " + Link, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), DirB + "\n");
  EXPECT_EQ(Exit, 0);
}
#endif

TEST_F(BuildBuiltinCommandTest, MktempRejectsMultipleTemplates) {
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute(
      "mktemp " + pathInDir("a.XXXXXXXX") + " " + pathInDir("b.XXXXXXXX"),
      Exit));
  EXPECT_NE(Exit, 0);
}

TEST_F(BuildBuiltinCommandTest, MkdirTouchDashDashAndCpUpdate) {
  writeFile("older.txt", "old");
  writeFile("newer.txt", "new");
  const std::string Older = pathInDir("older.txt");
  const std::string Newer = pathInDir("newer.txt");
  const std::string DashDir = pathInDir("-dashdir");
  const std::string DashFile = pathInDir("-dashfile");

  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("mkdir -- " + DashDir, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(llvm::sys::fs::is_directory(DashDir));

  ASSERT_TRUE(builtins::tryExecute("touch -- " + DashFile, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(DashFile));

  // Make Older strictly older than Newer, then cp -u must not overwrite Newer.
  ASSERT_TRUE(builtins::tryExecute("sleep 0.05", Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("touch " + Newer, Exit));
  EXPECT_EQ(Exit, 0);
  ASSERT_TRUE(builtins::tryExecute("cp -u " + Older + " " + Newer, Exit));
  EXPECT_EQ(Exit, 0);
  {
    std::ifstream In(Newer);
    std::string Body((std::istreambuf_iterator<char>(In)), {});
    EXPECT_EQ(Body, "new");
  }

  // Destination missing: cp -u still copies.
  const std::string Copied = pathInDir("copied.txt");
  ASSERT_TRUE(builtins::tryExecute("cp -u " + Older + " " + Copied, Exit));
  EXPECT_EQ(Exit, 0);
  EXPECT_TRUE(platform::fileExists(Copied));
}

TEST_F(BuildBuiltinCommandTest, FmtAndTsortBasics) {
  writeFile("fmt.txt", "one two three four five six seven eight\n");
  writeFile("tsort.txt", "a b\nb c\na c\n");
  writeFile("tsort_cycle.txt", "a b\nb a\n");
  const std::string FmtPath = pathInDir("fmt.txt");
  const std::string TsortPath = pathInDir("tsort.txt");
  const std::string CyclePath = pathInDir("tsort_cycle.txt");

  int Exit = -1;
  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("fmt -w 10 " + FmtPath, Exit));
  const std::string FmtOut = testing::internal::GetCapturedStdout();
  EXPECT_EQ(Exit, 0);
  EXPECT_NE(FmtOut.find('\n'), std::string::npos);
  EXPECT_TRUE(llvm::StringRef(FmtOut).contains("one two"));

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("tsort " + TsortPath, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "a\nb\nc\n");
  EXPECT_EQ(Exit, 0);

  ASSERT_TRUE(builtins::tryExecute("tsort " + CyclePath, Exit));
  EXPECT_NE(Exit, 0);

  // stdin-only forms must not be claimed.
  Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("fmt -w 10", Exit));
  EXPECT_EQ(Exit, 123);
  EXPECT_FALSE(builtins::tryExecute("tsort", Exit));
  EXPECT_EQ(Exit, 123);
}

#ifndef _WIN32
TEST_F(BuildBuiltinCommandTest, MkfifoBasics) {
  const std::string Fifo = pathInDir("pipe.fifo");
  int Exit = -1;
  ASSERT_TRUE(builtins::tryExecute("mkfifo -m 600 " + Fifo, Exit));
  EXPECT_EQ(Exit, 0);
  llvm::sys::fs::file_status Status;
  ASSERT_FALSE(llvm::sys::fs::status(Fifo, Status, /*follow=*/false));
  EXPECT_EQ(Status.type(), llvm::sys::fs::file_type::fifo_file);
}
#endif

TEST_F(BuildBuiltinCommandTest, ExprLengthSubstrMd5QAndTr) {
  writeFile("abc.txt", "abc");
  writeFile("crlf.txt", "a\r\nb\r\n");
  writeFile("case.txt", "AbC");
  const std::string Abc = pathInDir("abc.txt");
  const std::string Crlf = pathInDir("crlf.txt");
  const std::string Case = pathInDir("case.txt");

  int Exit = -1;
  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("expr length neverc", Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "6\n");
  EXPECT_EQ(Exit, 0);

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("expr substr neverc 2 3", Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "eve\n");
  EXPECT_EQ(Exit, 0);

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("expr substr neverc 0 3", Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "\n");
  EXPECT_EQ(Exit, 1);

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("md5 -q " + Abc, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(),
            "900150983cd24fb0d6963f7d28e17f72\n");
  EXPECT_EQ(Exit, 0);

  // Verbose Darwin md5 form stays on the host tool.
  Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("md5 " + Abc, Exit));
  EXPECT_EQ(Exit, 123);

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("tr -d '\\r' " + Crlf, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "a\nb\n");
  EXPECT_EQ(Exit, 0);

  testing::internal::CaptureStdout();
  ASSERT_TRUE(builtins::tryExecute("tr 'A-Z' 'a-z' " + Case, Exit));
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "abc");
  EXPECT_EQ(Exit, 0);

  // stdin-only GNU tr form must not be claimed.
  Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("tr -d '\\r'", Exit));
  EXPECT_EQ(Exit, 123);

  // touch -r with an unquoted glob must fall back (arity would change).
  Exit = 123;
  EXPECT_FALSE(builtins::tryExecute("touch -r *.txt " + Abc, Exit));
  EXPECT_EQ(Exit, 123);
}
