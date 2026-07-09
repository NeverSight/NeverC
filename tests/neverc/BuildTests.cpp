#include <gtest/gtest.h>

#include "neverc/Build/AST.h"
#include "neverc/Build/DepGraph.h"
#include "neverc/Build/Function.h"
#include "neverc/Build/Lexer.h"
#include "neverc/Build/Parser.h"
#include "neverc/Build/RuleDB.h"
#include "neverc/Build/VariableEnv.h"

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
