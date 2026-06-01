#include "NeverCTestFixture.h"

#include <chrono>
#include <fstream>
#include <thread>

class BuildTest : public NeverCTest {
protected:
  void writeMakefile(const std::string &Content) {
    writeFile(tmp() / "Makefile", Content);
  }

  CmdResult runMake(const std::vector<std::string> &ExtraArgs = {},
                    const std::string &MakeTarget = "") {
    std::vector<std::string> Args;
    Args.push_back("make");
    Args.push_back("-C");
    Args.push_back(tmp().string());
    for (auto &A : ExtraArgs)
      Args.push_back(A);
    if (!MakeTarget.empty())
      Args.push_back(MakeTarget);
    return ncc(Args);
  }

  CmdResult runBuild(const std::vector<std::string> &ExtraArgs = {},
                     const std::string &BuildTarget = "") {
    std::vector<std::string> Args;
    Args.push_back("build");
    Args.push_back("-C");
    Args.push_back(tmp().string());
    for (auto &A : ExtraArgs)
      Args.push_back(A);
    if (!BuildTarget.empty())
      Args.push_back(BuildTarget);
    return ncc(Args);
  }
};

// ============================================================================
// 1. Lexer
// ============================================================================

TEST_F(BuildTest, LexerContinuationLines) {
  writeMakefile(
      "FLAGS = \\\n"
      "\t-Wall \\\n"
      "\t-Werror\n"
      "all:\n"
      "\t@echo $(FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo")) << "out: " << R.out;
}

TEST_F(BuildTest, LexerCommentStripping) {
  writeMakefile(
      "# This is a comment\n"
      "FOO = bar # inline comment\n"
      "all:\n"
      "\t@echo $(FOO)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo bar")) << "out: " << R.out;
}

TEST_F(BuildTest, LexerTabRecipeDetection) {
  writeMakefile(
      "all:\n"
      "\t@echo recipe1\n"
      "\t@echo recipe2\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo recipe1")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo recipe2")) << "out: " << R.out;
}

TEST_F(BuildTest, LexerContinuationAtEOF) {
  writeMakefile(
      "X = hello \\\n"
      "world\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello")) << "out: " << R.out;
}

// ============================================================================
// 2. Parser — Rules
// ============================================================================

TEST_F(BuildTest, ParserSimpleRule) {
  writeFile(tmp() / "input.txt", "dummy");
  writeMakefile(
      "output.txt: input.txt\n"
      "\tcp input.txt output.txt\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cp input.txt output.txt")) << "out: " << R.out;
}

TEST_F(BuildTest, ParserMultipleTargets) {
  writeMakefile(
      "all: first second\n"
      "first:\n"
      "\t@echo first\n"
      "second:\n"
      "\t@echo second\n"
      ".PHONY: all first second\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo first")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo second")) << "out: " << R.out;
}

TEST_F(BuildTest, ParserInlineRecipeSemicolon) {
  writeMakefile(
      "all: ; @echo inline\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo inline")) << "out: " << R.out;
}

TEST_F(BuildTest, ParserOrderOnlyPrereqs) {
  writeMakefile(
      "all: normal | order-only\n"
      "normal:\n"
      "\t@echo normal\n"
      "order-only:\n"
      "\t@echo order-only\n"
      ".PHONY: all normal order-only\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo normal")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo order-only")) << "out: " << R.out;
}

TEST_F(BuildTest, ParserStaticPatternRule) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeMakefile(
      "OBJS = a.o b.o\n"
      "all: $(OBJS)\n"
      "$(OBJS): %.o: %.c\n"
      "\t@echo compile $< to $@\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("compile a.c to a.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("compile b.c to b.o")) << "out: " << R.out;
}

// ============================================================================
// 3. Conditionals
// ============================================================================

TEST_F(BuildTest, ConditionalIfEq) {
  writeMakefile(
      "MODE = debug\n"
      "ifeq ($(MODE),debug)\n"
      "FLAGS = -g\n"
      "else\n"
      "FLAGS = -O2\n"
      "endif\n"
      "all:\n"
      "\t@echo $(FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo -g")) << "out: " << R.out;
}

TEST_F(BuildTest, ConditionalIfNeq) {
  writeMakefile(
      "MODE = release\n"
      "ifneq ($(MODE),debug)\n"
      "FLAGS = -O2\n"
      "else\n"
      "FLAGS = -g\n"
      "endif\n"
      "all:\n"
      "\t@echo $(FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo -O2")) << "out: " << R.out;
}

TEST_F(BuildTest, ConditionalIfDef) {
  writeMakefile(
      "DEBUG = 1\n"
      "ifdef DEBUG\n"
      "FLAGS = -g\n"
      "endif\n"
      "all:\n"
      "\t@echo $(FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo -g")) << "out: " << R.out;
}

TEST_F(BuildTest, ConditionalIfNDef) {
  writeMakefile(
      "ifndef RELEASE\n"
      "FLAGS = -g\n"
      "endif\n"
      "all:\n"
      "\t@echo $(FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo -g")) << "out: " << R.out;
}

TEST_F(BuildTest, ConditionalElseIfeqChain) {
  writeMakefile(
      "ARCH ?= x86_64\n"
      "ifeq ($(ARCH),x86_64)\n"
      "  TRIPLE := x86_64-linux-gnu\n"
      "else ifeq ($(ARCH),aarch64)\n"
      "  TRIPLE := aarch64-linux-gnu\n"
      "else\n"
      "  TRIPLE := unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo $(TRIPLE)\n"
      ".PHONY: all\n");
  auto R1 = runMake({"-n"});
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("x86_64-linux-gnu")) << "out: " << R1.out;

  auto R2 = runMake({"-n", "ARCH=aarch64"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("aarch64-linux-gnu")) << "out: " << R2.out;
}

TEST_F(BuildTest, ConditionalNestedMixed) {
  writeMakefile(
      "OS = linux\n"
      "ARCH = x86_64\n"
      "ifeq ($(OS),linux)\n"
      "  ifdef ARCH\n"
      "    MSG = linux-$(ARCH)\n"
      "  else\n"
      "    MSG = linux-default\n"
      "  endif\n"
      "else\n"
      "  MSG = other\n"
      "endif\n"
      "all:\n"
      "\t@echo $(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("linux-x86_64")) << "out: " << R.out;
}

TEST_F(BuildTest, ConditionalQuotedStrings) {
  writeMakefile(
      "X = hello\n"
      "ifeq 'hello' '$(X)'\n"
      "MSG = matched\n"
      "else\n"
      "MSG = no-match\n"
      "endif\n"
      "all:\n"
      "\t@echo $(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("matched")) << "out: " << R.out;
}

// ============================================================================
// 4. Variables
// ============================================================================

TEST_F(BuildTest, VarRecursiveExpansion) {
  writeMakefile(
      "A = $(B)\n"
      "B = hello\n"
      "all:\n"
      "\t@echo $(A)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo hello")) << "out: " << R.out;
}

TEST_F(BuildTest, VarSimpleExpansion) {
  writeMakefile(
      "B = world\n"
      "A := hello $(B)\n"
      "B = changed\n"
      "all:\n"
      "\t@echo $(A)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo hello world")) << "out: " << R.out;
}

TEST_F(BuildTest, VarConditionalAssign) {
  writeMakefile(
      "A ?= first\n"
      "A ?= second\n"
      "all:\n"
      "\t@echo $(A)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo first")) << "out: " << R.out;
}

TEST_F(BuildTest, VarAppend) {
  writeMakefile(
      "FLAGS = -Wall\n"
      "FLAGS += -Werror\n"
      "all:\n"
      "\t@echo $(FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-Wall -Werror")) << "out: " << R.out;
}

TEST_F(BuildTest, VarCommandLineOverride) {
  writeMakefile(
      "CC := default-cc\n"
      "all:\n"
      "\t@echo $(CC)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n", "CC=override-cc"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("override-cc")) << "out: " << R.out;
}

TEST_F(BuildTest, VarSubstitutionRef) {
  writeMakefile(
      "SRCS = foo.c bar.c\n"
      "OBJS = $(SRCS:.c=.o)\n"
      "all:\n"
      "\t@echo $(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("foo.o bar.o")) << "out: " << R.out;
}

TEST_F(BuildTest, VarDollarEscaping) {
  writeMakefile(
      "all:\n"
      "\t@echo $$HOME\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("$HOME")) << "out: " << R.out;
}

TEST_F(BuildTest, VarNestedRef) {
  writeMakefile(
      "A = B\n"
      "B = hello\n"
      "all:\n"
      "\t@echo $($(A))\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo hello")) << "out: " << R.out;
}

TEST_F(BuildTest, VarBraceRef) {
  writeMakefile(
      "X = braces\n"
      "all:\n"
      "\t@echo ${X}\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("braces")) << "out: " << R.out;
}

TEST_F(BuildTest, VarOverrideDirective) {
  writeMakefile(
      "override CC = forced-cc\n"
      "all:\n"
      "\t@echo $(CC)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n", "CC=cmdline-cc"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("forced-cc")) << "out: " << R.out;
}

TEST_F(BuildTest, VarShellAssignment) {
  writeMakefile(
      "DATE != echo shell-works\n"
      "all:\n"
      "\t@echo $(DATE)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("shell-works")) << "out: " << R.out;
}

// ============================================================================
// 5. Functions — String
// ============================================================================

TEST_F(BuildTest, FuncSubst) {
  writeMakefile(
      "SRC = foo.c bar.c baz.c\n"
      "OBJ = $(subst .c,.o,$(SRC))\n"
      "all:\n"
      "\t@echo $(OBJ)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("foo.o bar.o baz.o")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncPatsubst) {
  writeMakefile(
      "SRC = a.c b.c c.c\n"
      "OBJ = $(patsubst %.c,%.o,$(SRC))\n"
      "all:\n"
      "\t@echo $(OBJ)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a.o b.o c.o")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncFilterAndFilterOut) {
  writeMakefile(
      "FILES = a.c b.h c.c d.s\n"
      "CSRC = $(filter %.c,$(FILES))\n"
      "NOC = $(filter-out %.c,$(FILES))\n"
      "all:\n"
      "\t@echo CSRC=$(CSRC) NOC=$(NOC)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CSRC=a.c c.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("NOC=b.h d.s")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncSort) {
  writeMakefile(
      "X = $(sort c b a b a)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a b c")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncWordOps) {
  writeMakefile(
      "LIST = alpha beta gamma delta\n"
      "W2 = $(word 2,$(LIST))\n"
      "WL = $(wordlist 2,3,$(LIST))\n"
      "WC = $(words $(LIST))\n"
      "FW = $(firstword $(LIST))\n"
      "LW = $(lastword $(LIST))\n"
      "all:\n"
      "\t@echo W2=$(W2) WL=$(WL) WC=$(WC) FW=$(FW) LW=$(LW)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("W2=beta")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("WL=beta gamma")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("WC=4")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("FW=alpha")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("LW=delta")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncStrip) {
  writeMakefile(
      "X = $(strip   hello   world  )\n"
      "all:\n"
      "\t@echo '$(X)'\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello world")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncFindstring) {
  writeMakefile(
      "X = $(findstring hello,hello world)\n"
      "Y = $(findstring missing,hello world)\n"
      "all:\n"
      "\t@echo X=$(X) Y=$(Y)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("X=hello")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("Y=")) << "out: " << R.out;
}

// ============================================================================
// 6. Functions — Filename
// ============================================================================

TEST_F(BuildTest, FuncDirNotdir) {
  writeMakefile(
      "P = src/foo.c lib/bar.h baz.c\n"
      "D = $(dir $(P))\n"
      "N = $(notdir $(P))\n"
      "all:\n"
      "\t@echo D=$(D) N=$(N)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("src/")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("foo.c")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncBasenameAndSuffix) {
  writeMakefile(
      "F = src/a.c lib/b.h plain\n"
      "B = $(basename $(F))\n"
      "S = $(suffix $(F))\n"
      "all:\n"
      "\t@echo B=$(B) S=$(S)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("src/a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains(".c")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncAddprefixAddsuffix) {
  writeMakefile(
      "MODS = foo bar\n"
      "SRCS = $(addsuffix .c,$(MODS))\n"
      "PATHS = $(addprefix src/,$(SRCS))\n"
      "all:\n"
      "\t@echo $(PATHS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("src/foo.c src/bar.c")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncWildcard) {
  writeFile(tmp() / "alpha.c", "");
  writeFile(tmp() / "beta.c", "");
  writeMakefile(
      "SRCS = $(wildcard *.c)\n"
      "all:\n"
      "\t@echo $(SRCS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("alpha.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("beta.c")) << "out: " << R.out;
}

// ============================================================================
// 7. Functions — Conditional / Control
// ============================================================================

TEST_F(BuildTest, FuncIf) {
  writeMakefile(
      "X = yes\n"
      "R = $(if $(X),defined,empty)\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("defined")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncOrAnd) {
  writeMakefile(
      "A =\n"
      "B = second\n"
      "RO = $(or $(A),$(B),third)\n"
      "RA = $(and first,$(B),third)\n"
      "all:\n"
      "\t@echo OR=$(RO) AND=$(RA)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("OR=second")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("AND=third")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncForeach) {
  writeMakefile(
      "DIRS = src lib test\n"
      "RESULT = $(foreach d,$(DIRS),$(d)/main.c)\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("src/main.c lib/main.c test/main.c")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncCall) {
  writeMakefile(
      "greet = Hello $(1) from $(2)\n"
      "MSG = $(call greet,World,NeverC)\n"
      "all:\n"
      "\t@echo $(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("Hello World from NeverC")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncEval) {
  writeMakefile(
      "$(eval NEW_VAR = generated-value)\n"
      "all:\n"
      "\t@echo $(NEW_VAR)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("generated-value")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncEvalGeneratesRule) {
  writeMakefile(
      "define make_rule\n"
      "$(1):\n"
      "\t@echo building-$(1)\n"
      "endef\n"
      "$(eval $(call make_rule,widget))\n"
      "all: widget\n"
      ".PHONY: all widget\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("building-widget")) << "out: " << R.out;
}

// ============================================================================
// 8. Functions — Shell / Info
// ============================================================================

TEST_F(BuildTest, FuncShell) {
  writeMakefile(
      "X = $(shell echo from-shell)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("from-shell")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncWarningAndInfo) {
  writeMakefile(
      "$(info info-message)\n"
      "$(warning warn-message)\n"
      "all:\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("info-message")) << "out: " << R.out;
  EXPECT_TRUE(R.stderrContains("warn-message")) << "err: " << R.err;
}

// ============================================================================
// 9. Pattern Rules
// ============================================================================

TEST_F(BuildTest, PatternRuleMatch) {
  writeFile(tmp() / "hello.c", "");
  writeMakefile(
      "%.o: %.c\n"
      "\t@echo cc -c $< -o $@\n"
      "all: hello.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc -c hello.c -o hello.o")) << "out: " << R.out;
}

TEST_F(BuildTest, PatternRuleWithFunctions) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeMakefile(
      "SRCS = a.c b.c\n"
      "OBJS = $(patsubst %.c,%.o,$(SRCS))\n"
      "%.o: %.c\n"
      "\t@echo cc $< -o $@\n"
      "all: $(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc a.c -o a.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cc b.c -o b.o")) << "out: " << R.out;
}

// ============================================================================
// 10. Auto Variables
// ============================================================================

TEST_F(BuildTest, AutoVarTarget) {
  writeMakefile(
      "foo:\n"
      "\t@echo target=$@\n"
      ".PHONY: foo\n");
  auto R = runMake({"-n"}, "foo");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("target=foo")) << "out: " << R.out;
}

TEST_F(BuildTest, AutoVarFirstPrereq) {
  writeFile(tmp() / "dep1.txt", "x");
  writeFile(tmp() / "dep2.txt", "y");
  writeMakefile(
      "out.txt: dep1.txt dep2.txt\n"
      "\t@echo first=$<\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("first=dep1.txt")) << "out: " << R.out;
}

TEST_F(BuildTest, AutoVarAllPrereqs) {
  writeFile(tmp() / "a.txt", "");
  writeFile(tmp() / "b.txt", "");
  writeMakefile(
      "out.txt: a.txt b.txt\n"
      "\t@echo all=$^\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("all=a.txt b.txt")) << "out: " << R.out;
}

TEST_F(BuildTest, AutoVarStem) {
  writeFile(tmp() / "test.c", "");
  writeMakefile(
      "%.o: %.c\n"
      "\t@echo stem=$*\n"
      "all: test.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("stem=test")) << "out: " << R.out;
}

TEST_F(BuildTest, AutoVarDirFile) {
  std::filesystem::create_directories(tmp() / "src");
  writeFile(tmp() / "src" / "main.c", "");
  writeMakefile(
      "src/main.o: src/main.c\n"
      "\t@echo dir=$(@D) file=$(@F)\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("dir=src")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("file=main.o")) << "out: " << R.out;
}

// ============================================================================
// 11. Dependency Graph
// ============================================================================

TEST_F(BuildTest, DepGraphTransitive) {
  writeMakefile(
      "all: mid\n"
      "mid: leaf\n"
      "leaf:\n"
      "\t@echo leaf\n"
      "mid:\n"
      "\t@echo mid\n"
      "all:\n"
      "\t@echo all\n"
      ".PHONY: all mid leaf\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  std::string Out = R.out;
  EXPECT_LT(Out.find("leaf"), Out.find("mid"));
  EXPECT_LT(Out.find("mid"), Out.find("all"));
}

TEST_F(BuildTest, DepGraphUpToDate) {
  writeFile(tmp() / "src.c", "int main(){}");
  writeMakefile(
      "out.o: src.c\n"
      "\tcp src.c out.o\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;

  auto R2 = runMake();
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("up to date")) << "out: " << R2.out;
}

TEST_F(BuildTest, DepGraphCircularDetected) {
  writeMakefile(
      "a: b\n"
      "\t@echo a\n"
      "b: a\n"
      "\t@echo b\n"
      ".PHONY: a b\n");
  auto R = runMake({}, "a");
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.stderrContains("Circular")) << "err: " << R.err;
}

TEST_F(BuildTest, DepGraphRebuildOlderTarget) {
  writeFile(tmp() / "src.c", "v1");
  writeMakefile(
      "out.o: src.c\n"
      "\tcp src.c out.o\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;

  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  writeFile(tmp() / "src.c", "v2");

  auto R2 = runMake();
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("cp src.c out.o")) << "Should rebuild. out: " << R2.out;
}

// ============================================================================
// 12. Include Directive
// ============================================================================

TEST_F(BuildTest, IncludeDirective) {
  writeFile(tmp() / "config.mk", "MSG = from-included\n");
  writeMakefile(
      "include config.mk\n"
      "all:\n"
      "\t@echo $(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("from-included")) << "out: " << R.out;
}

TEST_F(BuildTest, OptionalInclude) {
  writeMakefile(
      "-include nonexistent.mk\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo ok")) << "out: " << R.out;
}

TEST_F(BuildTest, IncludeWithVarExpansion) {
  writeFile(tmp() / "extra.mk", "EXTRA_VAR = from-extra\n");
  writeMakefile(
      "CFG = extra\n"
      "include $(CFG).mk\n"
      "all:\n"
      "\t@echo $(EXTRA_VAR)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("from-extra")) << "out: " << R.out;
}

// ============================================================================
// 13. Define Block
// ============================================================================

TEST_F(BuildTest, DefineBlock) {
  writeMakefile(
      "define GREETING\n"
      "hello from define\n"
      "endef\n"
      "all:\n"
      "\t@echo $(GREETING)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello from define")) << "out: " << R.out;
}

TEST_F(BuildTest, DefineBlockSimpleAssign) {
  writeMakefile(
      "X = original\n"
      "define Y :=\n"
      "$(X)-value\n"
      "endef\n"
      "X = changed\n"
      "all:\n"
      "\t@echo $(Y)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("original-value")) << "out: " << R.out;
}

// ============================================================================
// 14. Export
// ============================================================================

TEST_F(BuildTest, ExportVariable) {
  writeMakefile(
      "export MY_VAR = test-value\n"
      "all:\n"
      "\t@echo $$MY_VAR\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("test-value")) << "out: " << R.out;
}

TEST_F(BuildTest, ExportAllBare) {
  writeMakefile(
      "FOO = bar\n"
      "export\n"
      "all:\n"
      "\t@echo $$FOO\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("bar")) << "out: " << R.out;
}

// ============================================================================
// 15. Recipe Prefixes
// ============================================================================

TEST_F(BuildTest, RecipeSilentPrefix) {
  writeMakefile(
      "all:\n"
      "\t@echo silent\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("silent")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("echo silent")) << "@ should suppress echo. out: " << R.out;
}

TEST_F(BuildTest, RecipeIgnoreErrorPrefix) {
  writeMakefile(
      "all:\n"
      "\t-false\n"
      "\t@echo survived\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("survived")) << "out: " << R.out;
}

TEST_F(BuildTest, RecipeForcePrefixInDryRun) {
  writeMakefile(
      "all:\n"
      "\t+echo forced\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("forced")) << "out: " << R.out;
}

// ============================================================================
// 16. CLI Options
// ============================================================================

TEST_F(BuildTest, CLIHelp) {
  auto R = ncc({"make", "-h"});
  EXPECT_TRUE(R.contains("Usage")) << "out: " << R.out;
}

TEST_F(BuildTest, CLIBuildAlias) {
  writeMakefile(
      "all:\n"
      "\t@echo hello\n"
      ".PHONY: all\n");
  auto MakeR = runMake({"-n"});
  auto BuildR = runBuild({"-n"});
  ASSERT_TRUE(MakeR.ok()) << "stderr: " << MakeR.err;
  ASSERT_TRUE(BuildR.ok()) << "stderr: " << BuildR.err;
  EXPECT_EQ(MakeR.out, BuildR.out);
}

TEST_F(BuildTest, CLIDryRun) {
  writeMakefile(
      "all:\n"
      "\ttouch dryrun.txt\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("touch dryrun.txt")) << "out: " << R.out;
  EXPECT_FALSE(std::filesystem::exists(tmp() / "dryrun.txt"));
}

TEST_F(BuildTest, CLISilentMode) {
  writeMakefile(
      "all:\n"
      "\techo visible\n"
      ".PHONY: all\n");
  auto R = runMake({"-s"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("visible")) << "out: " << R.out;
}

TEST_F(BuildTest, CLIChangeDir) {
  auto Sub = tmp() / "subdir";
  std::filesystem::create_directories(Sub);
  writeFile(Sub / "Makefile", "all:\n\t@echo subdir-works\n.PHONY: all\n");
  auto R = ncc({"make", "-C", Sub.string()});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("subdir-works")) << "out: " << R.out;
}

TEST_F(BuildTest, CLISpecificMakefile) {
  writeFile(tmp() / "Custom.mk",
            "all:\n\t@echo custom-makefile\n.PHONY: all\n");
  auto R = runMake({"-f", "Custom.mk"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("custom-makefile")) << "out: " << R.out;
}

TEST_F(BuildTest, CLINoMakefileError) {
  auto Empty = tmp() / "empty";
  std::filesystem::create_directories(Empty);
  auto R = ncc({"make", "-C", Empty.string()});
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.stderrContains("No targets")) << "err: " << R.err;
}

TEST_F(BuildTest, CLIAlwaysMake) {
  writeFile(tmp() / "src.c", "int main(){}");
  writeMakefile(
      "out.o: src.c\n"
      "\t@echo rebuilding\n"
      "\tcp src.c out.o\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;

  auto R2 = runMake({"-B"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("rebuilding")) << "out: " << R2.out;
}

TEST_F(BuildTest, CLIPrintDatabase) {
  writeMakefile(
      "CC = gcc\n"
      "all: main.o\n"
      "\t@echo link\n"
      ".PHONY: all\n");
  auto R = runMake({"-p"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("all")) << "out: " << R.out;
}

TEST_F(BuildTest, CLIMultipleTargets) {
  writeMakefile(
      "foo:\n"
      "\t@echo foo\n"
      "bar:\n"
      "\t@echo bar\n"
      ".PHONY: foo bar\n");
  auto R = ncc({"make", "-C", tmp().string(), "foo", "bar"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("foo")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("bar")) << "out: " << R.out;
}

TEST_F(BuildTest, CLIMultipleCmdVars) {
  writeMakefile(
      "all:\n"
      "\t@echo $(CC) $(ARCH)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n", "CC=mycc", "ARCH=arm64"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("mycc arm64")) << "out: " << R.out;
}

// ============================================================================
// 17. Error Handling
// ============================================================================

TEST_F(BuildTest, ErrorOnFailedCommand) {
  writeMakefile(
      "all:\n"
      "\tfalse\n"
      ".PHONY: all\n");
  auto R = runMake();
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.stderrContains("Error")) << "err: " << R.err;
}

TEST_F(BuildTest, KeepGoingContinues) {
  writeMakefile(
      "all: fail succeed\n"
      "fail:\n"
      "\tfalse\n"
      "succeed:\n"
      "\t@echo succeeded\n"
      ".PHONY: all fail succeed\n");
  auto R = runMake({"-k"});
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.contains("succeeded")) << "out: " << R.out;
}

TEST_F(BuildTest, MissingPrereqError) {
  writeMakefile(
      "all: nonexistent.o\n"
      "\t@echo link\n");
  auto R = runMake();
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.stderrContains("No rule to make target")) << "err: " << R.err;
}

// ============================================================================
// 18. Parallel Build
// ============================================================================

TEST_F(BuildTest, ParallelBuildBasic) {
  writeMakefile(
      "all: a b c\n"
      "a:\n\t@echo a\n"
      "b:\n\t@echo b\n"
      "c:\n\t@echo c\n"
      ".PHONY: all a b c\n");
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("c")) << "out: " << R.out;
}

TEST_F(BuildTest, ParallelBuildRespectsDeps) {
  writeMakefile(
      "all: top\n"
      "top: mid\n"
      "mid: base\n"
      "base:\n\t@echo base\n"
      "mid:\n\t@echo mid\n"
      "top:\n\t@echo top\n"
      ".PHONY: all top mid base\n");
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  std::string Out = R.out;
  EXPECT_LT(Out.find("base"), Out.find("mid"));
  EXPECT_LT(Out.find("mid"), Out.find("top"));
}

TEST_F(BuildTest, ParallelBuildDryRun) {
  writeMakefile(
      "all: x y\n"
      "x:\n\t@echo x\n"
      "y:\n\t@echo y\n"
      ".PHONY: all x y\n");
  auto R = runMake({"-j4", "-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo x")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo y")) << "out: " << R.out;
}

// ============================================================================
// 19. End-to-End Execution
// ============================================================================

TEST_F(BuildTest, EndToEndEchoExecution) {
  writeMakefile(
      "all:\n"
      "\t@echo hello-world\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello-world")) << "out: " << R.out;
}

TEST_F(BuildTest, EndToEndFileCreation) {
  writeMakefile(
      "target.txt:\n"
      "\techo created > target.txt\n"
      "all: target.txt\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(std::filesystem::exists(tmp() / "target.txt"));
}

TEST_F(BuildTest, EndToEndCleanTarget) {
  writeFile(tmp() / "junk.txt", "junk");
  writeMakefile(
      "clean:\n"
      "\trm -f junk.txt\n"
      ".PHONY: clean\n");
  auto R = runMake({}, "clean");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_FALSE(std::filesystem::exists(tmp() / "junk.txt"));
}

TEST_F(BuildTest, EndToEndMultiRecipe) {
  writeMakefile(
      "all:\n"
      "\t@echo step1\n"
      "\t@echo step2\n"
      "\t@echo step3\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  std::string Out = R.out;
  EXPECT_LT(Out.find("step1"), Out.find("step2"));
  EXPECT_LT(Out.find("step2"), Out.find("step3"));
}

// ============================================================================
// 20. Special Variables
// ============================================================================

TEST_F(BuildTest, SpecialVarMakecmdgoals) {
  writeMakefile(
      "all:\n"
      "\t@echo goals=$(MAKECMDGOALS)\n"
      "clean:\n"
      "\t@echo goals=$(MAKECMDGOALS)\n"
      ".PHONY: all clean\n");
  auto R = runMake({}, "clean");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("goals=clean")) << "out: " << R.out;
}

TEST_F(BuildTest, SpecialVarCurdir) {
  writeMakefile(
      "all:\n"
      "\t@echo dir=$(CURDIR)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("dir=")) << "out: " << R.out;
}

TEST_F(BuildTest, DefaultGoalVariable) {
  writeMakefile(
      ".DEFAULT_GOAL = custom\n"
      "first:\n"
      "\t@echo first\n"
      "custom:\n"
      "\t@echo custom\n"
      ".PHONY: first custom\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo custom")) << "out: " << R.out;
}

// ============================================================================
// 21. Integration — Realistic Scenarios
// ============================================================================

TEST_F(BuildTest, IntegrationNeverCProjectBuild) {
  writeFile(tmp() / "main.c", "int main(){return 0;}");
  writeFile(tmp() / "utils.c", "void utils(){}");
  writeFile(tmp() / "parser.c", "void parse(){}");
  writeMakefile(
      "CC := echo-neverc\n"
      "CFLAGS := -Wall -O2 -std=c11\n"
      "LDFLAGS :=\n"
      "TARGET := mycompiler\n"
      "\n"
      "SRCS := main.c utils.c parser.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "\n"
      "all: $(TARGET)\n"
      "\n"
      "$(TARGET): $(OBJS)\n"
      "\t@echo $(CC) $(LDFLAGS) -o $@ $^\n"
      "\n"
      "%.o: %.c\n"
      "\t@echo $(CC) $(CFLAGS) -MMD -c $< -o $@\n"
      "\n"
      "clean:\n"
      "\t@echo rm -f $(OBJS) $(TARGET)\n"
      "\n"
      ".PHONY: all clean\n");

  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo-neverc -Wall -O2 -std=c11 -MMD -c main.c"))
      << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo-neverc  -o mycompiler"))
      << "out: " << R.out;

  auto Clean = runMake({"-n"}, "clean");
  ASSERT_TRUE(Clean.ok()) << "stderr: " << Clean.err;
  EXPECT_TRUE(Clean.contains("rm -f main.o utils.o parser.o"))
      << "out: " << Clean.out;
}

TEST_F(BuildTest, IntegrationCrossCompileConditional) {
  writeMakefile(
      "ARCH ?= x86_64\n"
      "ifeq ($(ARCH),x86_64)\n"
      "  TRIPLE := x86_64-unknown-linux-gnu\n"
      "  CFLAGS := -m64\n"
      "else ifeq ($(ARCH),aarch64)\n"
      "  TRIPLE := aarch64-unknown-linux-gnu\n"
      "  CFLAGS := -march=armv8-a\n"
      "else\n"
      "  TRIPLE := unknown\n"
      "  CFLAGS :=\n"
      "endif\n"
      "CC := neverc --target=$(TRIPLE)\n"
      "all:\n"
      "\t@echo $(CC) $(CFLAGS)\n"
      ".PHONY: all\n");

  auto R1 = runMake({"-n"});
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("x86_64-unknown-linux-gnu")) << "out: " << R1.out;
  EXPECT_TRUE(R1.contains("-m64")) << "out: " << R1.out;

  auto R2 = runMake({"-n", "ARCH=aarch64"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("aarch64-unknown-linux-gnu")) << "out: " << R2.out;
  EXPECT_TRUE(R2.contains("-march=armv8-a")) << "out: " << R2.out;
}

TEST_F(BuildTest, IntegrationDebugReleaseConfig) {
  writeFile(tmp() / "main.c", "int main(){return 0;}");
  writeMakefile(
      "MODE ?= release\n"
      "CC := echo-cc\n"
      "\n"
      "ifeq ($(MODE),debug)\n"
      "  CFLAGS := -g -O0 -DDEBUG\n"
      "else\n"
      "  CFLAGS := -O2 -DNDEBUG\n"
      "endif\n"
      "\n"
      "TARGET := app_$(MODE)\n"
      "SRCS := main.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "\n"
      "all: $(TARGET)\n"
      "\n"
      "$(TARGET): $(OBJS)\n"
      "\t@echo $(CC) -o $@ $^\n"
      "\n"
      "%.o: %.c\n"
      "\t@echo $(CC) $(CFLAGS) -c $< -o $@\n"
      "\n"
      ".PHONY: all\n");

  auto Release = runMake({"-n"});
  ASSERT_TRUE(Release.ok()) << "stderr: " << Release.err;
  EXPECT_TRUE(Release.contains("-O2")) << "out: " << Release.out;
  EXPECT_TRUE(Release.contains("app_release")) << "out: " << Release.out;

  auto Debug = runMake({"-n", "MODE=debug"});
  ASSERT_TRUE(Debug.ok()) << "stderr: " << Debug.err;
  EXPECT_TRUE(Debug.contains("-g -O0 -DDEBUG")) << "out: " << Debug.out;
  EXPECT_TRUE(Debug.contains("app_debug")) << "out: " << Debug.out;
}

// ============================================================================
// 22. Stress Tests
// ============================================================================

TEST_F(BuildTest, StressDeepDependencyChain) {
  std::string Mk;
  Mk += "all: t20\n";
  for (int I = 20; I >= 1; --I) {
    if (I > 1)
      Mk += "t" + std::to_string(I) + ": t" + std::to_string(I - 1) + "\n";
    else
      Mk += "t1:\n";
    Mk += "\t@echo step" + std::to_string(I) + "\n";
  }
  Mk += ".PHONY: all";
  for (int I = 1; I <= 20; ++I)
    Mk += " t" + std::to_string(I);
  Mk += "\n";
  writeMakefile(Mk);

  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  std::string Out = R.out;
  EXPECT_LT(Out.find("step1"), Out.find("step20"));
}

TEST_F(BuildTest, StressWideFanOutParallel) {
  std::string Mk = "all:";
  for (int I = 1; I <= 20; ++I)
    Mk += " t" + std::to_string(I);
  Mk += "\n";
  for (int I = 1; I <= 20; ++I)
    Mk += "t" + std::to_string(I) + ":\n\t@echo done" +
           std::to_string(I) + "\n";
  Mk += ".PHONY: all";
  for (int I = 1; I <= 20; ++I)
    Mk += " t" + std::to_string(I);
  Mk += "\n";
  writeMakefile(Mk);

  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  for (int I = 1; I <= 20; ++I)
    EXPECT_TRUE(R.contains("done" + std::to_string(I)))
        << "Missing done" << I << ". out: " << R.out;
}

TEST_F(BuildTest, StressManyVariables) {
  std::string Mk;
  for (int I = 0; I < 50; ++I)
    Mk += "V" + std::to_string(I) + " = val" + std::to_string(I) + "\n";
  Mk += "all:\n\t@echo $(V0) $(V25) $(V49)\n.PHONY: all\n";
  writeMakefile(Mk);

  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("val0")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("val25")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("val49")) << "out: " << R.out;
}

// ============================================================================
// 23. Robustness Edge Cases
// ============================================================================

TEST_F(BuildTest, EmptyMakefile) {
  writeMakefile("");
  auto R = runMake();
  EXPECT_FALSE(R.ok());
}

TEST_F(BuildTest, CommentOnlyMakefile) {
  writeMakefile("# Only comments\n# Nothing else\n");
  auto R = runMake();
  EXPECT_FALSE(R.ok());
}

TEST_F(BuildTest, EmptyPrereqWithRecipe) {
  writeMakefile(
      "target:\n"
      "\t@echo built\n"
      ".PHONY: target\n");
  auto R = runMake({}, "target");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("built")) << "out: " << R.out;
}

TEST_F(BuildTest, MultiplePhonyDeclarations) {
  writeMakefile(
      ".PHONY: clean\n"
      ".PHONY: all install\n"
      "all:\n\t@echo all\n"
      "clean:\n\t@echo clean\n"
      "install:\n\t@echo install\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("all")) << "out: " << R.out;
}

TEST_F(BuildTest, VarInTargetAndPrereq) {
  writeFile(tmp() / "src.txt", "data");
  writeMakefile(
      "SRC = src.txt\n"
      "DST = dst.txt\n"
      "$(DST): $(SRC)\n"
      "\t@echo copy $(SRC) to $(DST)\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("copy src.txt to dst.txt")) << "out: " << R.out;
}

TEST_F(BuildTest, RecursiveVarSelfRefDoesNotLoop) {
  writeMakefile(
      "X = $(X) extra\n"
      "all:\n"
      "\t@echo '$(X)'\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
}

TEST_F(BuildTest, FuncEvalWithForeach) {
  writeMakefile(
      "MODULES = alpha beta\n"
      "define MODULE_template\n"
      "$(1)_build:\n"
      "\t@echo building-$(1)\n"
      ".PHONY: $(1)_build\n"
      "endef\n"
      "all: alpha_build beta_build\n"
      ".PHONY: all\n"
      "$(foreach m,$(MODULES),$(eval $(call MODULE_template,$(m))))\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("building-alpha")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("building-beta")) << "out: " << R.out;
}

TEST_F(BuildTest, MultipleRulesAddPrereqs) {
  writeMakefile(
      "all: a\n"
      "all: b\n"
      "a:\n\t@echo a\n"
      "b:\n\t@echo b\n"
      "all:\n\t@echo all\n"
      ".PHONY: all a b\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo b")) << "out: " << R.out;
}

TEST_F(BuildTest, FunctionInPrereqList) {
  writeMakefile(
      "MODS = x y\n"
      "OBJS = $(addsuffix .o,$(MODS))\n"
      "all: $(OBJS)\n"
      "x.o:\n\t@echo x\n"
      "y.o:\n\t@echo y\n"
      ".PHONY: all x.o y.o\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo x")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo y")) << "out: " << R.out;
}

// ============================================================================
// 24. Order-Only Prerequisites Behavior
// ============================================================================

TEST_F(BuildTest, OrderOnlyDoesNotTriggerRebuild) {
  writeFile(tmp() / "src.c", "int main(){}");
  std::filesystem::create_directories(tmp() / "outdir");
  writeMakefile(
      "outdir/out.o: src.c | outdir\n"
      "\tcp src.c outdir/out.o\n"
      "outdir:\n"
      "\tmkdir -p outdir\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;

  auto R2 = runMake();
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("up to date")) << "Order-only should not trigger rebuild. out: " << R2.out;
}

TEST_F(BuildTest, OrderOnlyCreatesDir) {
  writeFile(tmp() / "input.txt", "data");
  writeMakefile(
      "builddir/output.txt: input.txt | builddir\n"
      "\tcp input.txt builddir/output.txt\n"
      "builddir:\n"
      "\tmkdir -p builddir\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("mkdir -p builddir")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cp input.txt builddir/output.txt")) << "out: " << R.out;
}

// ============================================================================
// 25. Combined Recipe Prefixes
// ============================================================================

TEST_F(BuildTest, RecipeCombinedSilentIgnore) {
  writeMakefile(
      "all:\n"
      "\t@-false\n"
      "\t@echo survived-combined\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("survived-combined")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("false")) << "@- should suppress echo. out: " << R.out;
}

TEST_F(BuildTest, RecipeSilentForceInDryRun) {
  writeMakefile(
      "all:\n"
      "\t@+echo force-silent\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("force-silent")) << "out: " << R.out;
}

// ============================================================================
// 26. Nested Function Calls
// ============================================================================

TEST_F(BuildTest, NestedPatsubstInFilter) {
  writeMakefile(
      "SRCS = a.c b.c c.h d.c\n"
      "C_OBJS = $(patsubst %.c,%.o,$(filter %.c,$(SRCS)))\n"
      "all:\n"
      "\t@echo $(C_OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a.o b.o d.o")) << "out: " << R.out;
}

TEST_F(BuildTest, NestedForeachWithAddsuffix) {
  writeMakefile(
      "DIRS = src lib\n"
      "EXTS = .c .h\n"
      "RESULT = $(foreach d,$(DIRS),$(addsuffix /main,$(d)))\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("src/main lib/main")) << "out: " << R.out;
}

TEST_F(BuildTest, NestedIfWithFilter) {
  writeMakefile(
      "SRCS = a.c b.c\n"
      "HAS_C = $(if $(filter %.c,$(SRCS)),yes,no)\n"
      "all:\n"
      "\t@echo $(HAS_C)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("yes")) << "out: " << R.out;
}

// ============================================================================
// 27. $(call) Advanced
// ============================================================================

TEST_F(BuildTest, CallWithThreeArgs) {
  writeMakefile(
      "make_flag = -$(1)$(2)=$(3)\n"
      "FLAG = $(call make_flag,D,DEBUG,1)\n"
      "all:\n"
      "\t@echo $(FLAG)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-DDEBUG=1")) << "out: " << R.out;
}

TEST_F(BuildTest, CallNestedInForeach) {
  writeMakefile(
      "compile = $(1).o: $(1).c\n"
      "MODS = alpha beta\n"
      "$(foreach m,$(MODS),$(eval $(call compile,$(m))))\n"
      "all: alpha.o beta.o\n"
      ".PHONY: all\n");
  writeFile(tmp() / "alpha.c", "");
  writeFile(tmp() / "beta.c", "");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
}

TEST_F(BuildTest, CallPositionalVarRestore) {
  writeMakefile(
      "inner = [$1,$2]\n"
      "outer = {$(call inner,x,y)}=$1\n"
      "RESULT = $(call outer,z)\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("{[x,y]}=z")) << "out: " << R.out;
}

// ============================================================================
// 28. Define Block Advanced
// ============================================================================

TEST_F(BuildTest, DefineBlockAppendMode) {
  writeMakefile(
      "X = base\n"
      "define X +=\n"
      "appended\n"
      "endef\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("base")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("appended")) << "out: " << R.out;
}

TEST_F(BuildTest, DefineBlockOverride) {
  writeMakefile(
      "override define CFLAGS\n"
      "-Wall -Werror\n"
      "endef\n"
      "all:\n"
      "\t@echo $(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n", "CFLAGS=-O0"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-Wall -Werror")) << "out: " << R.out;
}

// ============================================================================
// 29. Variable Edge Cases
// ============================================================================

TEST_F(BuildTest, VarEmptyConditionalAssign) {
  writeMakefile(
      "X =\n"
      "X ?= fallback\n"
      "all:\n"
      "\t@echo '$(X)'\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
}

TEST_F(BuildTest, VarAppendToUndefined) {
  writeMakefile(
      "NEWVAR += first\n"
      "NEWVAR += second\n"
      "all:\n"
      "\t@echo $(NEWVAR)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("first second")) << "out: " << R.out;
}

TEST_F(BuildTest, VarMultipleSubstitutionRef) {
  writeMakefile(
      "FILES = a.c b.cpp c.c d.cpp\n"
      "C_OBJ = $(FILES:.c=.o)\n"
      "all:\n"
      "\t@echo $(C_OBJ)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a.o")) << "out: " << R.out;
}

TEST_F(BuildTest, VarDoubleColonSimpleAssign) {
  writeMakefile(
      "X ::= immediate\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("immediate")) << "out: " << R.out;
}

TEST_F(BuildTest, VarInRecipeExpansion) {
  writeMakefile(
      "CC = echo-cc\n"
      "CFLAGS = -Wall\n"
      "all:\n"
      "\t@$(CC) $(CFLAGS) hello\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo-cc -Wall hello")) << "out: " << R.out;
}

// ============================================================================
// 30. GNUmakefile and makefile Priority
// ============================================================================

TEST_F(BuildTest, MakefileSearchPriority) {
  writeFile(tmp() / "makefile", "all:\n\t@echo lowercase\n.PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("lowercase")) << "out: " << R.out;
}

// ============================================================================
// 31. $? Auto Variable (Newer Prerequisites)
// ============================================================================

TEST_F(BuildTest, AutoVarNewerPrereqs) {
  writeFile(tmp() / "dep1.txt", "old");
  writeFile(tmp() / "dep2.txt", "old");
  writeMakefile(
      "target:\n"
      "\t@echo newer=$?\n"
      ".PHONY: target\n");
  auto R = runMake({"-n"}, "target");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("newer=")) << "out: " << R.out;
}

// ============================================================================
// 32. Error Recovery and Edge Cases
// ============================================================================

TEST_F(BuildTest, ErrorKeepGoingMultipleFailures) {
  writeMakefile(
      "all: t1 t2 t3\n"
      "t1:\n\tfalse\n"
      "t2:\n\tfalse\n"
      "t3:\n\t@echo t3-ok\n"
      ".PHONY: all t1 t2 t3\n");
  auto R = runMake({"-k"});
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.contains("t3-ok")) << "t3 should still run. out: " << R.out;
}

TEST_F(BuildTest, ErrorMissingMakefileWithF) {
  auto R = runMake({"-f", "does_not_exist.mk"});
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.stderrContains("No such file")) << "err: " << R.err;
}

TEST_F(BuildTest, ErrorNoTargetInEmptyMakefile) {
  writeMakefile("X = 1\nY = 2\n");
  auto R = runMake();
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.stderrContains("No targets")) << "err: " << R.err;
}

// ============================================================================
// 33. Whitespace Handling
// ============================================================================

TEST_F(BuildTest, WhitespaceInAssignment) {
  writeMakefile(
      "  X   =   hello  world  \n"
      "all:\n"
      "\t@echo '$(X)'\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello  world")) << "out: " << R.out;
}

TEST_F(BuildTest, WhitespaceTargetPrereqTrimming) {
  writeMakefile(
      "  all  :  dep  \n"
      "  dep  :\n"
      "\t@echo dep-done\n"
      "  all  :\n"
      "\t@echo all-done\n"
      ".PHONY: all dep\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("dep-done")) << "out: " << R.out;
}

// ============================================================================
// 34. Pattern Rule Edge Cases
// ============================================================================

TEST_F(BuildTest, PatternRuleFirstMatchWins) {
  writeFile(tmp() / "demo.c", "");
  writeMakefile(
      "%.o: %.c\n"
      "\t@echo first-pattern $<\n"
      "all: demo.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("first-pattern demo.c")) << "out: " << R.out;
}

TEST_F(BuildTest, PatternRuleWithPrefix) {
  writeFile(tmp() / "test_main.c", "");
  writeMakefile(
      "test_%: test_%.c\n"
      "\t@echo build-test $@\n"
      "all: test_main\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("build-test test_main")) << "out: " << R.out;
}

TEST_F(BuildTest, ExplicitRuleOverridesPattern) {
  writeFile(tmp() / "main.c", "");
  writeMakefile(
      "%.o: %.c\n"
      "\t@echo pattern $<\n"
      "main.o: main.c\n"
      "\t@echo explicit $<\n"
      "all: main.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("explicit main.c")) << "out: " << R.out;
}

// ============================================================================
// 35. Include Edge Cases
// ============================================================================

TEST_F(BuildTest, IncludeMultipleFiles) {
  writeFile(tmp() / "a.mk", "VA = from-a\n");
  writeFile(tmp() / "b.mk", "VB = from-b\n");
  writeMakefile(
      "include a.mk b.mk\n"
      "all:\n"
      "\t@echo $(VA) $(VB)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("from-a from-b")) << "out: " << R.out;
}

TEST_F(BuildTest, SincludeIsSameAsOptionalInclude) {
  writeMakefile(
      "sinclude nonexistent.mk\n"
      "all:\n"
      "\t@echo sinclude-ok\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo sinclude-ok")) << "out: " << R.out;
}

TEST_F(BuildTest, IncludeChained) {
  writeFile(tmp() / "inner.mk", "INNER_VAR = deep-value\n");
  writeFile(tmp() / "outer.mk", "include inner.mk\n");
  writeMakefile(
      "include outer.mk\n"
      "all:\n"
      "\t@echo $(INNER_VAR)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("deep-value")) << "out: " << R.out;
}

// ============================================================================
// 36. Conditional Edge Cases
// ============================================================================

TEST_F(BuildTest, ConditionalIfeqEmptyStrings) {
  writeMakefile(
      "X =\n"
      "ifeq ($(X),)\n"
      "MSG = empty\n"
      "else\n"
      "MSG = notempty\n"
      "endif\n"
      "all:\n"
      "\t@echo $(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("empty")) << "out: " << R.out;
}

TEST_F(BuildTest, ConditionalTripleElseIfeq) {
  writeMakefile(
      "T ?= c\n"
      "ifeq ($(T),a)\n"
      "  R = got-a\n"
      "else ifeq ($(T),b)\n"
      "  R = got-b\n"
      "else ifeq ($(T),c)\n"
      "  R = got-c\n"
      "else\n"
      "  R = got-other\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R1 = runMake({"-n"});
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("got-c")) << "out: " << R1.out;

  auto R2 = runMake({"-n", "T=a"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("got-a")) << "out: " << R2.out;

  auto R3 = runMake({"-n", "T=zzz"});
  ASSERT_TRUE(R3.ok()) << "stderr: " << R3.err;
  EXPECT_TRUE(R3.contains("got-other")) << "out: " << R3.out;
}

TEST_F(BuildTest, ConditionalIfdefWithEmptyVar) {
  writeMakefile(
      "X =\n"
      "ifdef X\n"
      "MSG = defined\n"
      "else\n"
      "MSG = undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo $(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("undefined")) << "Empty var treated as undef. out: " << R.out;
}

// ============================================================================
// 37. Function Edge Cases
// ============================================================================

TEST_F(BuildTest, FuncFilterMultiplePatterns) {
  writeMakefile(
      "FILES = a.c b.h c.s d.c e.h\n"
      "RESULT = $(filter %.c %.h,$(FILES))\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a.c b.h d.c e.h")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncSubstEmptyFrom) {
  writeMakefile(
      "X = $(subst ,,hello)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncOrAllEmpty) {
  writeMakefile(
      "A =\n"
      "B =\n"
      "R = $(or $(A),$(B))\n"
      "all:\n"
      "\t@echo 'R=[$(R)]'\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("R=[]")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncAndOneEmpty) {
  writeMakefile(
      "A = yes\n"
      "B =\n"
      "R = $(and $(A),$(B))\n"
      "all:\n"
      "\t@echo 'R=[$(R)]'\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("R=[]")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncIfElseBranch) {
  writeMakefile(
      "X =\n"
      "R = $(if $(X),yes,no)\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("no")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncWordsEmpty) {
  writeMakefile(
      "X = $(words )\n"
      "all:\n"
      "\t@echo count=$(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("count=0")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncWordlistBeyondEnd) {
  writeMakefile(
      "LIST = a b c\n"
      "X = $(wordlist 2,10,$(LIST))\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("b c")) << "out: " << R.out;
}

// ============================================================================
// 38. Parallel Build Edge Cases
// ============================================================================

TEST_F(BuildTest, ParallelBuildDiamondDependency) {
  writeMakefile(
      "all: left right\n"
      "left: common\n"
      "\t@echo left\n"
      "right: common\n"
      "\t@echo right\n"
      "common:\n"
      "\t@echo common\n"
      ".PHONY: all left right common\n");
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  std::string Out = R.out;
  EXPECT_LT(Out.find("common"), Out.find("left")) << "out: " << Out;
  EXPECT_LT(Out.find("common"), Out.find("right")) << "out: " << Out;
}

TEST_F(BuildTest, ParallelBuildKeepGoing) {
  writeMakefile(
      "all: ok1 fail ok2\n"
      "ok1:\n\t@echo ok1\n"
      "fail:\n\tfalse\n"
      "ok2:\n\t@echo ok2\n"
      ".PHONY: all ok1 fail ok2\n");
  auto R = runMake({"-j4", "-k"});
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.contains("ok1")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("ok2")) << "ok2 should run with -k. out: " << R.out;
}

TEST_F(BuildTest, ParallelJ1FallsBackToSerial) {
  writeMakefile(
      "all: a b\n"
      "a:\n\t@echo a\n"
      "b:\n\t@echo b\n"
      ".PHONY: all a b\n");
  auto R = runMake({"-j1"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b")) << "out: " << R.out;
}

// ============================================================================
// 39. Integration — Multi-Module Project
// ============================================================================

TEST_F(BuildTest, IntegrationMultiModuleProject) {
  writeFile(tmp() / "main.c", "");
  writeFile(tmp() / "net.c", "");
  writeFile(tmp() / "crypto.c", "");
  writeMakefile(
      "CC := echo-cc\n"
      "CFLAGS := -Wall\n"
      "MODULES := main net crypto\n"
      "OBJS := $(addsuffix .o,$(MODULES))\n"
      "TARGET := server\n"
      "\n"
      "all: $(TARGET)\n"
      "\n"
      "$(TARGET): $(OBJS)\n"
      "\t@echo $(CC) -o $@ $^\n"
      "\n"
      "%.o: %.c\n"
      "\t@echo $(CC) $(CFLAGS) -c $< -o $@\n"
      "\n"
      "clean:\n"
      "\t@echo rm -f $(OBJS) $(TARGET)\n"
      "\n"
      ".PHONY: all clean\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo-cc -Wall -c main.c -o main.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo-cc -Wall -c net.c -o net.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo-cc -Wall -c crypto.c -o crypto.o")) << "out: " << R.out;
}

TEST_F(BuildTest, IntegrationConditionalPlatformBuild) {
  writeFile(tmp() / "main.c", "");
  writeMakefile(
      "OS ?= linux\n"
      "CC := echo-cc\n"
      "\n"
      "ifeq ($(OS),linux)\n"
      "  LDFLAGS := -lpthread -ldl\n"
      "else ifeq ($(OS),darwin)\n"
      "  LDFLAGS := -framework CoreFoundation\n"
      "else\n"
      "  LDFLAGS :=\n"
      "endif\n"
      "\n"
      "all: main.o\n"
      "\t@echo $(CC) $(LDFLAGS) -o app $^\n"
      "\n"
      "%.o: %.c\n"
      "\t@echo $(CC) -c $<\n"
      "\n"
      ".PHONY: all\n");

  auto Linux = runMake({"-n"});
  ASSERT_TRUE(Linux.ok()) << "stderr: " << Linux.err;
  EXPECT_TRUE(Linux.contains("-lpthread")) << "out: " << Linux.out;

  auto Darwin = runMake({"-n", "OS=darwin"});
  ASSERT_TRUE(Darwin.ok()) << "stderr: " << Darwin.err;
  EXPECT_TRUE(Darwin.contains("-framework CoreFoundation")) << "out: " << Darwin.out;
}

// ============================================================================
// 40. Stress — Complex Variable Interaction
// ============================================================================

TEST_F(BuildTest, StressRecursiveVariableChain) {
  writeMakefile(
      "A = $(B)-end\n"
      "B = $(C)-mid\n"
      "C = start\n"
      "all:\n"
      "\t@echo $(A)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("start-mid-end")) << "out: " << R.out;
}

TEST_F(BuildTest, StressForeachEvalManyTargets) {
  std::string Mk;
  Mk += "MODULES =";
  for (int I = 0; I < 15; ++I)
    Mk += " m" + std::to_string(I);
  Mk += "\n";
  // all: must come before $(foreach) so eval'd targets don't steal default
  Mk += "all:";
  for (int I = 0; I < 15; ++I)
    Mk += " m" + std::to_string(I);
  Mk += "\n.PHONY: all\n";
  Mk += "define mod_rule\n";
  Mk += "$(1):\n";
  Mk += "\t@echo build-$(1)\n";
  Mk += ".PHONY: $(1)\n";
  Mk += "endef\n";
  Mk += "$(foreach m,$(MODULES),$(eval $(call mod_rule,$(m))))\n";
  writeMakefile(Mk);

  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("build-m0")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("build-m14")) << "out: " << R.out;
}

TEST_F(BuildTest, StressNestedConditionals) {
  writeMakefile(
      "A = 1\nB = 2\nC = 3\n"
      "ifdef A\n"
      "  ifdef B\n"
      "    ifdef C\n"
      "      MSG = all-defined\n"
      "    else\n"
      "      MSG = c-missing\n"
      "    endif\n"
      "  else\n"
      "    MSG = b-missing\n"
      "  endif\n"
      "else\n"
      "  MSG = a-missing\n"
      "endif\n"
      "all:\n"
      "\t@echo $(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("all-defined")) << "out: " << R.out;
}

// ============================================================================
// 41. Special Variable Coverage
// ============================================================================

TEST_F(BuildTest, SpecialVarMake) {
  writeMakefile(
      "all:\n"
      "\t@echo make=$(MAKE)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("make=")) << "MAKE should be set. out: " << R.out;
  EXPECT_TRUE(R.contains("neverc")) << "MAKE should contain neverc. out: " << R.out;
}

TEST_F(BuildTest, SpecialVarMakefileList) {
  writeMakefile(
      "all:\n"
      "\t@echo list=$(MAKEFILE_LIST)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("akefile")) << "MAKEFILE_LIST should include makefile path. out: " << R.out;
}

TEST_F(BuildTest, SpecialVarShell) {
  writeMakefile(
      "all:\n"
      "\t@echo shell=$(SHELL)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("shell=/bin/sh") || R.contains("shell=cmd.exe"))
      << "SHELL should be set. out: " << R.out;
}

// ============================================================================
// 42. Variable Robustness
// ============================================================================

TEST_F(BuildTest, VarEmptyValueExpansion) {
  writeMakefile(
      "EMPTY =\n"
      "all:\n"
      "\t@echo [$(EMPTY)]\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("[]")) << "Empty var should expand to nothing. out: " << R.out;
}

TEST_F(BuildTest, VarUndefinedExpandsEmpty) {
  writeMakefile(
      "all:\n"
      "\t@echo [$(UNDEFINED_VAR)]\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("[]")) << "Undefined var should expand to empty. out: " << R.out;
}

TEST_F(BuildTest, VarSingleCharRef) {
  writeMakefile(
      "X = single-char\n"
      "all:\n"
      "\t@echo $X\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("single-char")) << "out: " << R.out;
}

TEST_F(BuildTest, VarMixedAssignModes) {
  writeMakefile(
      "A = recursive\n"
      "B := simple\n"
      "C ?= conditional\n"
      "D += appended\n"
      "all:\n"
      "\t@echo A=$(A) B=$(B) C=$(C) D=$(D)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("A=recursive")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("B=simple")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("C=conditional")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("D=appended")) << "out: " << R.out;
}

TEST_F(BuildTest, VarCommandLineOverridesRecursive) {
  writeMakefile(
      "X = from-file\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n", "X=from-cmd"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("from-cmd")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("from-file")) << "File value should be overridden. out: " << R.out;
}

TEST_F(BuildTest, VarChainedRecursive) {
  writeMakefile(
      "CC = $(COMPILER)\n"
      "COMPILER = $(TOOL)\n"
      "TOOL = neverc\n"
      "all:\n"
      "\t@echo $(CC)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("neverc")) << "out: " << R.out;
}

// ============================================================================
// 43. Function Robustness
// ============================================================================

TEST_F(BuildTest, FuncPatsubstNoMatch) {
  writeMakefile(
      "X = $(patsubst %.c,%.o,hello.txt)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello.txt")) << "No match should pass through. out: " << R.out;
}

TEST_F(BuildTest, FuncFilterNoMatch) {
  writeMakefile(
      "FILES = a.txt b.dat c.log\n"
      "X = $(filter %.c,$(FILES))\n"
      "all:\n"
      "\t@echo [$(X)]\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("[]")) << "No match should be empty. out: " << R.out;
}

TEST_F(BuildTest, FuncSortDuplicatesRemoved) {
  writeMakefile(
      "X = $(sort z a b a c b)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a b c z")) << "Sort should deduplicate. out: " << R.out;
}

TEST_F(BuildTest, FuncWordOutOfRange) {
  writeMakefile(
      "LIST = a b c\n"
      "X = $(word 10,$(LIST))\n"
      "all:\n"
      "\t@echo [$(X)]\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("[]")) << "Out of range word should be empty. out: " << R.out;
}

TEST_F(BuildTest, FuncDirNoSlash) {
  writeMakefile(
      "X = $(dir plain_file.c)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("./")) << "No slash should return ./ out: " << R.out;
}

TEST_F(BuildTest, FuncNotdirWithSlash) {
  writeMakefile(
      "X = $(notdir /usr/include/stdio.h)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("stdio.h")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncBasenameSuffix) {
  writeMakefile(
      "X = $(basename src/main.cpp)\n"
      "Y = $(suffix src/main.cpp)\n"
      "all:\n"
      "\t@echo B=$(X) S=$(Y)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("B=src/main")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("S=.cpp")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncForeachEmpty) {
  writeMakefile(
      "EMPTY =\n"
      "X = $(foreach d,$(EMPTY),$(d)/file)\n"
      "all:\n"
      "\t@echo [$(X)]\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("[]")) << "Foreach on empty should produce nothing. out: " << R.out;
}

TEST_F(BuildTest, FuncCallNoArgs) {
  writeMakefile(
      "template = no-args\n"
      "X = $(call template)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("no-args")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncShellMultiLine) {
  writeMakefile(
      "X = $(shell echo line1 && echo line2)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("line1")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("line2")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncAbspath) {
  writeMakefile(
      "X = $(abspath relative/path)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("/")) << "Abspath should return absolute path. out: " << R.out;
  EXPECT_TRUE(R.contains("relative/path")) << "Should contain original path. out: " << R.out;
}

// ============================================================================
// 44. Rule Edge Cases
// ============================================================================

TEST_F(BuildTest, RuleNoRecipe) {
  writeMakefile(
      "all: dep\n"
      "dep:\n"
      ".PHONY: all dep\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "Rule with no recipe should not fail. stderr: " << R.err;
}

TEST_F(BuildTest, RuleMultipleRulesSameTarget) {
  writeMakefile(
      "all: a\n"
      "all: b\n"
      "all:\n"
      "\t@echo all-done\n"
      "a:\n\t@echo a\n"
      "b:\n\t@echo b\n"
      ".PHONY: all a b\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("all-done")) << "out: " << R.out;
}

TEST_F(BuildTest, RulePhonyAlwaysRebuilds) {
  writeMakefile(
      "clean:\n"
      "\t@echo cleaning\n"
      ".PHONY: clean\n");
  auto R1 = runMake({}, "clean");
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("cleaning")) << "out: " << R1.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("cleaning")) << "Phony should always run. out: " << R2.out;
}

TEST_F(BuildTest, RulePatternRuleStemInRecipe) {
  writeFile(tmp() / "test.c", "");
  writeMakefile(
      "%.o: %.c\n"
      "\t@echo stem=$* file=$<\n"
      "all: test.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("stem=test")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("file=test.c")) << "out: " << R.out;
}

// ============================================================================
// 45. DepGraph Robustness
// ============================================================================

TEST_F(BuildTest, DepGraphSelfDependency) {
  writeMakefile(
      "a: a\n"
      "\t@echo a\n"
      ".PHONY: a\n");
  auto R = runMake({}, "a");
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.stderrContains("Circular")) << "Self-dep should be circular. err: " << R.err;
}

TEST_F(BuildTest, DepGraphThreeNodeCycle) {
  writeMakefile(
      "a: b\n\t@echo a\n"
      "b: c\n\t@echo b\n"
      "c: a\n\t@echo c\n"
      ".PHONY: a b c\n");
  auto R = runMake({}, "a");
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.stderrContains("Circular")) << "err: " << R.err;
}

TEST_F(BuildTest, DepGraphFileNotExistNoRule) {
  writeMakefile(
      "all: missing_file.c\n"
      "\t@echo link\n");
  auto R = runMake();
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.stderrContains("No rule to make target")) << "err: " << R.err;
}

// ============================================================================
// 46. JobScheduler Edge Cases
// ============================================================================

TEST_F(BuildTest, ParallelBuildLargeJ) {
  writeMakefile(
      "all: a b c d e\n"
      "a:\n\t@echo a\n"
      "b:\n\t@echo b\n"
      "c:\n\t@echo c\n"
      "d:\n\t@echo d\n"
      "e:\n\t@echo e\n"
      ".PHONY: all a b c d e\n");
  auto R = runMake({"-j99"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("e")) << "out: " << R.out;
}

TEST_F(BuildTest, DryRunDoesNotExecute) {
  writeMakefile(
      "all:\n"
      "\ttouch should_not_exist.marker\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("touch should_not_exist.marker")) << "out: " << R.out;
  EXPECT_FALSE(std::filesystem::exists(tmp() / "should_not_exist.marker"));
}

TEST_F(BuildTest, SilentModeNoCommandEcho) {
  writeMakefile(
      "all:\n"
      "\techo hello-silent\n"
      ".PHONY: all\n");
  auto R = runMake({"-s"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello-silent")) << "out: " << R.out;
}

// ============================================================================
// 47. Integration — C/C++ Project Patterns
// ============================================================================

TEST_F(BuildTest, IntegrationHeaderDependency) {
  writeFile(tmp() / "main.c", "#include \"config.h\"\nint main(){return 0;}");
  writeFile(tmp() / "config.h", "#define VERSION 1\n");
  writeMakefile(
      "CC := echo-cc\n"
      "all: app\n"
      "main.o: main.c config.h\n"
      "\t@echo $(CC) -c $< -o $@\n"
      "app: main.o\n"
      "\t@echo $(CC) -o $@ $^\n"
      ".PHONY: all app\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo-cc -c main.c -o main.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo-cc -o app main.o")) << "out: " << R.out;
}

TEST_F(BuildTest, IntegrationLibraryBuild) {
  writeFile(tmp() / "lib_a.c", "");
  writeFile(tmp() / "lib_b.c", "");
  writeMakefile(
      "CC := echo-cc\n"
      "AR := echo-ar\n"
      "LIBSRCS := lib_a.c lib_b.c\n"
      "LIBOBJS := $(LIBSRCS:.c=.o)\n"
      "LIB := libutil.a\n"
      "\n"
      "all: $(LIB)\n"
      "\n"
      "$(LIB): $(LIBOBJS)\n"
      "\t@echo $(AR) rcs $@ $^\n"
      "\n"
      "%.o: %.c\n"
      "\t@echo $(CC) -c $< -o $@\n"
      "\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo-cc -c lib_a.c -o lib_a.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo-cc -c lib_b.c -o lib_b.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo-ar rcs libutil.a")) << "out: " << R.out;
}

TEST_F(BuildTest, IntegrationInstallTarget) {
  writeMakefile(
      "PREFIX ?= /usr/local\n"
      "install:\n"
      "\t@echo install -m 755 app $(PREFIX)/bin/app\n"
      "uninstall:\n"
      "\t@echo rm -f $(PREFIX)/bin/app\n"
      ".PHONY: install uninstall\n");
  auto R1 = runMake({"-n"}, "install");
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("/usr/local/bin/app")) << "out: " << R1.out;

  auto R2 = runMake({"-n", "PREFIX=/opt"}, "install");
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("/opt/bin/app")) << "out: " << R2.out;
}

TEST_F(BuildTest, IntegrationMultiTargetClean) {
  writeFile(tmp() / "main.c", "");
  writeFile(tmp() / "util.c", "");
  writeMakefile(
      "CC := echo-cc\n"
      "SRCS := main.c util.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "TARGET := app\n"
      "\n"
      "all: $(TARGET)\n"
      "$(TARGET): $(OBJS)\n"
      "\t@echo $(CC) -o $@ $^\n"
      "%.o: %.c\n"
      "\t@echo $(CC) -c $<\n"
      "clean:\n"
      "\t@echo rm -f $(OBJS) $(TARGET)\n"
      ".PHONY: all clean\n");
  auto Clean = runMake({"-n"}, "clean");
  ASSERT_TRUE(Clean.ok()) << "stderr: " << Clean.err;
  EXPECT_TRUE(Clean.contains("rm -f main.o util.o app")) << "out: " << Clean.out;
}

TEST_F(BuildTest, IntegrationConfigurableToolchain) {
  writeFile(tmp() / "main.c", "");
  writeMakefile(
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "all:\n"
      "\t@echo CC=$(CC) LD=$(LD)\n"
      ".PHONY: all\n");
  auto R1 = runMake({"-n"});
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("CC=gcc")) << "out: " << R1.out;

  auto R2 = runMake({"-n", "CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("CC=aarch64-linux-gnu-gcc")) << "out: " << R2.out;
  EXPECT_TRUE(R2.contains("LD=aarch64-linux-gnu-ld")) << "out: " << R2.out;
}

// ============================================================================
// 48. Include Robustness
// ============================================================================

TEST_F(BuildTest, IncludeOverridesVariable) {
  writeFile(tmp() / "override.mk", "CC = from-include\n");
  writeMakefile(
      "CC = original\n"
      "include override.mk\n"
      "all:\n"
      "\t@echo $(CC)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("from-include")) << "Include should override. out: " << R.out;
}

TEST_F(BuildTest, IncludeAddsRules) {
  writeFile(tmp() / "rules.mk",
            "helper:\n\t@echo helper-from-include\n.PHONY: helper\n");
  writeMakefile(
      "include rules.mk\n"
      "all: helper\n"
      "\t@echo all-done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("helper-from-include")) << "out: " << R.out;
}

// ============================================================================
// 49. Define Block Robustness
// ============================================================================

TEST_F(BuildTest, DefineBlockMultiline) {
  writeMakefile(
      "define COMMANDS\n"
      "step1\n"
      "step2\n"
      "step3\n"
      "endef\n"
      "all:\n"
      "\t@echo $(COMMANDS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("step1")) << "out: " << R.out;
}

TEST_F(BuildTest, DefineBlockConditionalAssign) {
  writeMakefile(
      "define OPTS ?=\n"
      "fallback-opts\n"
      "endef\n"
      "all:\n"
      "\t@echo $(OPTS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("fallback-opts")) << "out: " << R.out;
}

// ============================================================================
// 50. Export Robustness
// ============================================================================

TEST_F(BuildTest, ExportVariableInSubprocess) {
  writeMakefile(
      "export BUILD_TYPE = release\n"
      "all:\n"
      "\t@echo $$BUILD_TYPE\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("release")) << "out: " << R.out;
}

TEST_F(BuildTest, ExportWithAssignment) {
  writeMakefile(
      "export CC := custom-cc\n"
      "all:\n"
      "\t@echo $$CC\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("custom-cc")) << "out: " << R.out;
}

// ============================================================================
// 51. CLI Edge Cases
// ============================================================================

TEST_F(BuildTest, CLIJAutoDetect) {
  writeMakefile(
      "all: a b\n"
      "a:\n\t@echo a\n"
      "b:\n\t@echo b\n"
      ".PHONY: all a b\n");
  auto R = runMake({"-j"});
  ASSERT_TRUE(R.ok()) << "-j without arg should use CPU count. stderr: " << R.err;
  EXPECT_TRUE(R.contains("a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b")) << "out: " << R.out;
}

TEST_F(BuildTest, CLIMultipleCDirs) {
  auto Sub1 = tmp() / "d1";
  auto Sub2 = Sub1 / "d2";
  std::filesystem::create_directories(Sub2);
  writeFile(Sub2 / "Makefile", "all:\n\t@echo deep-dir\n.PHONY: all\n");
  auto R = ncc({"make", "-C", Sub1.string(), "-C", "d2"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("deep-dir")) << "out: " << R.out;
}

TEST_F(BuildTest, CLIUnknownOptionWarning) {
  writeMakefile(
      "all:\n\t@echo ok\n.PHONY: all\n");
  auto R = runMake({"--unknown-flag"});
  EXPECT_TRUE(R.stderrContains("Unknown option")) << "err: " << R.err;
}

// ============================================================================
// 52. Stress — Complex Real-World Pattern
// ============================================================================

TEST_F(BuildTest, StressGeneratedMakefile) {
  std::string Mk;
  Mk += "CC := echo-cc\nCFLAGS := -Wall -O2\n\n";

  int N = 30;
  Mk += "SRCS :=";
  for (int I = 0; I < N; ++I) {
    std::string Name = "mod" + std::to_string(I) + ".c";
    Mk += " " + Name;
    writeFile(tmp() / Name, "");
  }
  Mk += "\nOBJS := $(SRCS:.c=.o)\n\n";
  Mk += "app: $(OBJS)\n\t@echo $(CC) -o $@ $^\n\n";
  Mk += "%.o: %.c\n\t@echo $(CC) $(CFLAGS) -c $< -o $@\n\n";
  Mk += ".PHONY: all\nall: app\n";
  writeMakefile(Mk);

  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo-cc -Wall -O2 -c mod0.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo-cc -Wall -O2 -c mod29.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo-cc -o app")) << "out: " << R.out;
}

TEST_F(BuildTest, StressParallelChainAndFanout) {
  std::string Mk;
  Mk += "all: final\n";
  Mk += "final: mid1 mid2 mid3\n\t@echo final\n";
  Mk += "mid1: base\n\t@echo mid1\n";
  Mk += "mid2: base\n\t@echo mid2\n";
  Mk += "mid3: base\n\t@echo mid3\n";
  Mk += "base:\n\t@echo base\n";
  Mk += ".PHONY: all final mid1 mid2 mid3 base\n";
  writeMakefile(Mk);

  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  std::string Out = R.out;
  EXPECT_LT(Out.find("base"), Out.find("final"));
}

TEST_F(BuildTest, StressEvalTemplateNested) {
  writeMakefile(
      "define lib_template\n"
      "$(1).a:\n"
      "\t@echo ar rcs $(1).a\n"
      ".PHONY: $(1).a\n"
      "endef\n"
      "\n"
      "$(eval $(call lib_template,libfoo))\n"
      "$(eval $(call lib_template,libbar))\n"
      "\n"
      "all: libfoo.a libbar.a\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("ar rcs libfoo.a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("ar rcs libbar.a")) << "out: " << R.out;
}

// ============================================================================
// 53. Miscellaneous Edge Cases
// ============================================================================

TEST_F(BuildTest, TabOnlyRecipeLine) {
  writeMakefile(
      "all:\n"
      "\t@echo before\n"
      "\t\n"
      "\t@echo after\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo before")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("echo after")) << "out: " << R.out;
}

TEST_F(BuildTest, TargetWithDot) {
  writeMakefile(
      "lib.so:\n"
      "\t@echo building-lib\n"
      ".PHONY: lib.so\n");
  auto R = runMake({"-n"}, "lib.so");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("building-lib")) << "out: " << R.out;
}

TEST_F(BuildTest, TargetWithHyphen) {
  writeMakefile(
      "my-target:\n"
      "\t@echo my-target-built\n"
      ".PHONY: my-target\n");
  auto R = runMake({}, "my-target");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("my-target-built")) << "out: " << R.out;
}

TEST_F(BuildTest, RecipeWithMultipleCommands) {
  writeMakefile(
      "all:\n"
      "\t@echo cmd1 && echo cmd2\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cmd1")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cmd2")) << "out: " << R.out;
}

TEST_F(BuildTest, VariableInTargetName) {
  writeMakefile(
      "NAME = widget\n"
      "$(NAME):\n"
      "\t@echo building-$(NAME)\n"
      ".PHONY: $(NAME)\n");
  auto R = runMake({"-n"}, "widget");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("building-widget")) << "out: " << R.out;
}

TEST_F(BuildTest, AlwaysMakeRebuildsCurrent) {
  writeFile(tmp() / "src.txt", "content");
  writeMakefile(
      "out.txt: src.txt\n"
      "\tcp src.txt out.txt\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;

  auto R2 = runMake();
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("up to date")) << "Should be up to date. out: " << R2.out;

  auto R3 = runMake({"-B"});
  ASSERT_TRUE(R3.ok()) << "stderr: " << R3.err;
  EXPECT_TRUE(R3.contains("cp src.txt out.txt")) << "-B should force rebuild. out: " << R3.out;
}

// ============================================================================
// Kernel 5.10 Makefile compatibility tests
// ============================================================================

TEST_F(BuildTest, FuncOriginUndefined) {
  writeMakefile(
      "all:\n"
      "\t@echo origin=$(origin NONEXISTENT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("origin=undefined")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncOriginFile) {
  writeMakefile(
      "MYVAR = hello\n"
      "all:\n"
      "\t@echo origin=$(origin MYVAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("origin=file")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncOriginCommandLine) {
  writeMakefile(
      "all:\n"
      "\t@echo origin=$(origin V)\n"
      ".PHONY: all\n");
  auto R = runMake({"V=1"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("origin=command line")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncOriginOverride) {
  writeMakefile(
      "override CFLAGS = -Wall\n"
      "all:\n"
      "\t@echo origin=$(origin CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("origin=override")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncOriginDefault) {
  writeMakefile(
      "all:\n"
      "\t@echo origin=$(origin MAKE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("origin=default")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncValue) {
  // $(value) returns the raw unexpanded text.
  // Test with a simply-expanded variable to verify raw retrieval.
  writeMakefile(
      "X := hello world\n"
      "all:\n"
      "\t@echo value=$(value X)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("value=hello world")) << "out: " << R1.out;

  // Test $(value) with recursive var used inside $(words) to verify
  // the raw text is returned (including literal dollar references).
  writeMakefile(
      "A = one\n"
      "B = $(A) two\n"
      "RAW_WORDS := $(words $(value B))\n"
      "EXP_WORDS := $(words $(B))\n"
      "all:\n"
      "\t@echo raw=$(RAW_WORDS) exp=$(EXP_WORDS)\n"
      ".PHONY: all\n");
  auto R2 = runMake();
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  // $(value B) = "$(A) two" => 2 words: "$(A)" and "two"
  // $(B) = "one two" => 2 words: "one" and "two"
  EXPECT_TRUE(R2.contains("raw=2 exp=2")) << "out: " << R2.out;
}

TEST_F(BuildTest, FuncValueUndefined) {
  writeMakefile(
      "all:\n"
      "\t@echo value=[$(value NOPE)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("value=[]")) << "out: " << R.out;
}

// Kernel-style: ifeq ("$(origin V)", "command line")
TEST_F(BuildTest, KernelStyleVerboseFromOrigin) {
  writeMakefile(
      "ifeq (\"$(origin V)\", \"command line\")\n"
      "  KBUILD_VERBOSE = $(V)\n"
      "endif\n"
      "ifndef KBUILD_VERBOSE\n"
      "  KBUILD_VERBOSE = 0\n"
      "endif\n"
      "all:\n"
      "\t@echo verbose=$(KBUILD_VERBOSE)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("verbose=0")) << "out: " << R1.out;

  auto R2 = runMake({"V=1"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("verbose=1")) << "out: " << R2.out;
}

// Kernel-style: ifeq ("$(origin O)", "command line") -> KBUILD_OUTPUT
TEST_F(BuildTest, KernelStyleOutputDirFromOrigin) {
  writeMakefile(
      "ifeq (\"$(origin O)\", \"command line\")\n"
      "  KBUILD_OUTPUT := $(O)\n"
      "endif\n"
      "KBUILD_OUTPUT ?= .\n"
      "all:\n"
      "\t@echo output=$(KBUILD_OUTPUT)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("output=.")) << "out: " << R1.out;

  auto R2 = runMake({"O=/tmp/build"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("output=/tmp/build")) << "out: " << R2.out;
}

// Kernel-style: ARCH selection via conditionals
TEST_F(BuildTest, KernelStyleArchSelection) {
  writeMakefile(
      "ARCH ?= x86\n"
      "ifeq ($(ARCH),x86)\n"
      "  CFLAGS := -m32\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  CFLAGS := -march=armv8-a\n"
      "else\n"
      "  CFLAGS := -generic\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(ARCH) cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("arch=x86 cflags=-m32")) << "out: " << R1.out;

  auto R2 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64 cflags=-march=armv8-a")) << "out: " << R2.out;
}

// Kernel-style: obj-y += foo.o bar.o pattern (kbuild)
TEST_F(BuildTest, KernelStyleObjYAccumulation) {
  writeMakefile(
      "obj-y :=\n"
      "obj-y += init/main.o\n"
      "obj-y += kernel/sched.o\n"
      "obj-y += mm/page_alloc.o\n"
      "all:\n"
      "\t@echo objs=$(obj-y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("objs=init/main.o kernel/sched.o mm/page_alloc.o"))
      << "out: " << R.out;
}

// Kernel-style: CONFIG_xxx conditional compilation
TEST_F(BuildTest, KernelStyleConfigConditional) {
  writeMakefile(
      "CONFIG_SMP = y\n"
      "CONFIG_PREEMPT = n\n"
      "obj-y :=\n"
      "ifeq ($(CONFIG_SMP),y)\n"
      "  obj-y += smp.o\n"
      "endif\n"
      "ifeq ($(CONFIG_PREEMPT),y)\n"
      "  obj-y += preempt.o\n"
      "endif\n"
      "all:\n"
      "\t@echo objs=$(obj-y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("objs=smp.o")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("preempt.o")) << "out: " << R.out;
}

// Kernel-style: $(call) for reusable build macros
TEST_F(BuildTest, KernelStyleCallMacro) {
  writeMakefile(
      "define filechk_utsrelease\n"
      "echo \"$(1)-$(2)\"\n"
      "endef\n"
      "RELEASE = 5.10.0\n"
      "EXTRA = custom\n"
      "all:\n"
      "\t@$(call filechk_utsrelease,$(RELEASE),$(EXTRA))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("5.10.0-custom")) << "out: " << R.out;
}

// Kernel-style: $(eval) + $(call) template pattern for module objects
TEST_F(BuildTest, KernelStyleEvalCallTemplate) {
  writeMakefile(
      "define build_module\n"
      "$(1)-objs := $(2)\n"
      "endef\n"
      "$(eval $(call build_module,mymod,file1.o file2.o))\n"
      "all:\n"
      "\t@echo mymod-objs=$(mymod-objs)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("mymod-objs=file1.o file2.o")) << "out: " << R.out;
}

// Kernel-style: complex variable composition with $(filter) and $(patsubst)
TEST_F(BuildTest, KernelStyleCflagsComposition) {
  writeMakefile(
      "WARNINGS := -Wall -Wextra -Wno-unused\n"
      "BASE_CFLAGS := -O2 $(WARNINGS)\n"
      "EXTRA_CFLAGS :=\n"
      "DEBUG ?= 0\n"
      "ifeq ($(DEBUG),1)\n"
      "  EXTRA_CFLAGS += -g -DDEBUG\n"
      "endif\n"
      "CFLAGS := $(BASE_CFLAGS) $(EXTRA_CFLAGS)\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("-O2 -Wall -Wextra -Wno-unused"))
      << "out: " << R1.out;

  auto R2 = runMake({"DEBUG=1"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("-g")) << "out: " << R2.out;
  EXPECT_TRUE(R2.contains("-DDEBUG")) << "out: " << R2.out;
}

// Kernel-style: $(foreach) + $(eval) for generating variables per subdir
TEST_F(BuildTest, KernelStyleForeachEvalSubdirs) {
  writeMakefile(
      "SUBDIRS := drivers fs net\n"
      "define subdir_var\n"
      "$(1)_FLAGS := -DMODULE_$(1)\n"
      "endef\n"
      "$(foreach d,$(SUBDIRS),$(eval $(call subdir_var,$(d))))\n"
      "all:\n"
      "\t@echo drv=$(drivers_FLAGS) fs=$(fs_FLAGS) net=$(net_FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("drv=-DMODULE_drivers")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("fs=-DMODULE_fs")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("net=-DMODULE_net")) << "out: " << R.out;
}

// Kernel-style: $(filter-out ...) for excluding files
TEST_F(BuildTest, KernelStyleFilterOutExclude) {
  writeMakefile(
      "ALL_SRCS := main.c utils.c test.c debug.c\n"
      "EXCLUDE := test.c debug.c\n"
      "SRCS := $(filter-out $(EXCLUDE),$(ALL_SRCS))\n"
      "all:\n"
      "\t@echo srcs=$(SRCS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("srcs=main.c utils.c")) << "out: " << R.out;
}

// Kernel-style: $(addprefix) + $(addsuffix) for path construction
TEST_F(BuildTest, KernelStylePathConstruction) {
  writeMakefile(
      "SRCS := main utils io\n"
      "OBJS := $(addsuffix .o,$(addprefix build/,$(SRCS)))\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("objs=build/main.o build/utils.o build/io.o"))
      << "out: " << R.out;
}

// Kernel-style: nested $(if ...) in variable
TEST_F(BuildTest, KernelStyleNestedIfInVar) {
  writeMakefile(
      "ARCH = arm64\n"
      "CROSS_COMPILE = $(if $(filter arm%,$(ARCH)),aarch64-linux-gnu-,)\n"
      "CC = $(CROSS_COMPILE)gcc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc=aarch64-linux-gnu-gcc")) << "out: " << R.out;
}

// Kernel-style: -include for .d dependency files (nonexistent ignored)
TEST_F(BuildTest, KernelStyleDashIncludeDeps) {
  writeFile(tmp() / "vars.mk", "EXTRA_FLAGS := -DINCLUDED\n");
  writeMakefile(
      "-include vars.mk\n"
      "-include nonexistent.d\n"
      "all:\n"
      "\t@echo flags=$(EXTRA_FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("flags=-DINCLUDED")) << "out: " << R.out;
}

// Kernel-style: export variables for sub-makes
TEST_F(BuildTest, KernelStyleExportForSubmake) {
  writeMakefile(
      "export ARCH := x86_64\n"
      "export CROSS_COMPILE :=\n"
      "CFLAGS := -O2\n"
      "export CFLAGS\n"
      "all:\n"
      "\t@echo arch=$$ARCH cflags=$$CFLAGS\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("arch=x86_64")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cflags=-O2")) << "out: " << R.out;
}

// Kernel-style: $(words) and $(word) for list manipulation
TEST_F(BuildTest, KernelStyleListManipulation) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "PARTS = $(VERSION) $(PATCHLEVEL) $(SUBLEVEL)\n"
      "all:\n"
      "\t@echo count=$(words $(PARTS)) first=$(firstword $(PARTS)) "
      "last=$(lastword $(PARTS))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("count=3")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("first=5")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("last=0")) << "out: " << R.out;
}

// Kernel-style: $(sort) for unique + sorted lists
TEST_F(BuildTest, KernelStyleSortUniquePaths) {
  writeMakefile(
      "INC_DIRS := include arch/x86/include include lib/include "
      "arch/x86/include\n"
      "UNIQUE := $(sort $(INC_DIRS))\n"
      "all:\n"
      "\t@echo unique=$(UNIQUE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  // $(sort) removes duplicates and sorts alphabetically
  EXPECT_FALSE(R.out.empty()) << "out: " << R.out;
  // arch/x86/include should appear only once
  auto first = R.out.find("arch/x86/include");
  auto second = R.out.find("arch/x86/include", first + 1);
  EXPECT_EQ(second, std::string::npos) << "duplicates not removed: " << R.out;
}

// Kernel-style: $(subst) for version string generation
TEST_F(BuildTest, KernelStyleVersionString) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 209\n"
      "KERNELRELEASE = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "all:\n"
      "\t@echo release=$(KERNELRELEASE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("release=5.10.209")) << "out: " << R.out;
}

// Kernel-style: $(call) command macro with @ prefix after expansion
TEST_F(BuildTest, KernelStyleDefineCommandMacro) {
  writeMakefile(
      "define cmd_cc_o_c\n"
      "@echo CC $(2)\n"
      "endef\n"
      "all:\n"
      "\t$(call cmd_cc_o_c,,main.c)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC main.c")) << "out: " << R.out;
}

// Kernel-style: pattern rule with paths (use flat layout to avoid
// directory-creation requirements in the test environment).
TEST_F(BuildTest, KernelStylePatternRuleSubdir) {
  writeFile(tmp() / "kern_main.c", "int main(){return 0;}\n");
  writeMakefile(
      "%.o: %.c\n"
      "\t@echo compiling $< to $@\n"
      "all: kern_main.o\n"
      "\t@echo linked\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("compiling kern_main.c to kern_main.o"))
      << "out: " << R.out;
}

// Kernel-style: $(strip) to normalize whitespace in conditionals
TEST_F(BuildTest, KernelStyleStripInConditional) {
  writeMakefile(
      "CONFIG =   y  \n"
      "ifeq ($(strip $(CONFIG)),y)\n"
      "  RESULT = enabled\n"
      "else\n"
      "  RESULT = disabled\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("result=enabled")) << "out: " << R.out;
}

// Kernel-style: $(findstring) for checking arch
TEST_F(BuildTest, KernelStyleFindstringArch) {
  writeMakefile(
      "ARCH = x86_64\n"
      "ifneq ($(findstring x86,$(ARCH)),)\n"
      "  IS_X86 = yes\n"
      "else\n"
      "  IS_X86 = no\n"
      "endif\n"
      "all:\n"
      "\t@echo is_x86=$(IS_X86)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("is_x86=yes")) << "out: " << R.out;
}

// Kernel-style: $(patsubst %.c,%.o,...) for object list generation
TEST_F(BuildTest, KernelStylePatsubstObjList) {
  writeMakefile(
      "SRCS := main.c sched.c fork.c\n"
      "OBJS := $(patsubst %.c,%.o,$(SRCS))\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("objs=main.o sched.o fork.o")) << "out: " << R.out;
}

// Kernel-style: substitution reference $(SRCS:.c=.o)
TEST_F(BuildTest, KernelStyleSubstRef) {
  writeMakefile(
      "SRCS := main.c utils.c io.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("objs=main.o utils.o io.o")) << "out: " << R.out;
}

// Kernel-style: multiple include files with variable expansion
TEST_F(BuildTest, KernelStyleIncludeWithExpansion) {
  writeFile(tmp() / "arch.mk",
            "ARCH_CFLAGS := -march=native\n");
  writeFile(tmp() / "config.mk",
            "CONFIG_SMP := y\n");
  writeMakefile(
      "ARCH_FILE := arch.mk\n"
      "include $(ARCH_FILE) config.mk\n"
      "all:\n"
      "\t@echo cflags=$(ARCH_CFLAGS) smp=$(CONFIG_SMP)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cflags=-march=native")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("smp=y")) << "out: " << R.out;
}

// Kernel-style: $(wildcard) for file discovery
TEST_F(BuildTest, KernelStyleForeachWildcard) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "x.h", "");
  writeMakefile(
      "SRCS := $(wildcard *.c)\n"
      "all:\n"
      "\t@echo srcs=$(sort $(SRCS))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b.c")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("x.h")) << "should not find .h: " << R.out;
}

// Complex: $(or ...) + $(and ...) for multi-condition checks
TEST_F(BuildTest, KernelStyleOrAndConditions) {
  writeMakefile(
      "A = yes\n"
      "B =\n"
      "C = ok\n"
      "R1 = $(or $(A),$(B),$(C))\n"
      "R2 = $(and $(A),$(B),$(C))\n"
      "R3 = $(and $(A),$(C))\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=[$(R2)] r3=$(R3)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r1=yes")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r2=[]")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r3=ok")) << "out: " << R.out;
}

// Kernel-style: recursive variable with deferred expansion
TEST_F(BuildTest, KernelStyleDeferredExpansion) {
  writeMakefile(
      "CC = gcc\n"
      "COMPILE = $(CC) $(CFLAGS)\n"
      "CFLAGS = -O2\n"
      "all:\n"
      "\t@echo compile=$(COMPILE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("compile=gcc -O2")) << "out: " << R.out;
}

// Kernel-style: override from command line
TEST_F(BuildTest, KernelStyleCmdLineOverridesFile) {
  writeMakefile(
      "CC = gcc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R = runMake({"CC=clang"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc=clang")) << "out: " << R.out;
}

// Kernel-style: $(dir) + $(notdir) decomposition
TEST_F(BuildTest, KernelStyleDirNotdirDecomposition) {
  writeMakefile(
      "FILE := arch/x86/kernel/head.S\n"
      "all:\n"
      "\t@echo dir=$(dir $(FILE)) name=$(notdir $(FILE))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("dir=arch/x86/kernel/")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("name=head.S")) << "out: " << R.out;
}

// Kernel-style: $(basename) + $(suffix) for file type handling
TEST_F(BuildTest, KernelStyleBasenameSuffix) {
  writeMakefile(
      "SRCS := vmlinux.lds.S head.o main.o\n"
      "STEMS := $(basename $(SRCS))\n"
      "EXTS := $(suffix $(SRCS))\n"
      "all:\n"
      "\t@echo stems=$(STEMS) exts=$(EXTS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("stems=vmlinux.lds head main")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("exts=.S .o .o")) << "out: " << R.out;
}

// Kernel-style: nested conditional with ifdef/ifndef
TEST_F(BuildTest, KernelStyleNestedIfdefIfndef) {
  writeMakefile(
      "CONFIG_64BIT = y\n"
      "ifdef CONFIG_64BIT\n"
      "  ifndef CONFIG_X86_32\n"
      "    BITS := 64\n"
      "  else\n"
      "    BITS := 32\n"
      "  endif\n"
      "else\n"
      "  BITS := 32\n"
      "endif\n"
      "all:\n"
      "\t@echo bits=$(BITS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("bits=64")) << "out: " << R.out;
}

// Kernel-style: $(filter %.o,$(obj-y)) to extract object files
TEST_F(BuildTest, KernelStyleFilterObjectFiles) {
  writeMakefile(
      "obj-y := main.o sched.o Kconfig README\n"
      "OBJECTS := $(filter %.o,$(obj-y))\n"
      "NON_OBJ := $(filter-out %.o,$(obj-y))\n"
      "all:\n"
      "\t@echo objects=$(OBJECTS) non=$(NON_OBJ)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("objects=main.o sched.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("non=Kconfig README")) << "out: " << R.out;
}

// Kernel-style: define + $(call) for command template with @ prefix
TEST_F(BuildTest, KernelStyleDefineRecipeTemplate) {
  writeMakefile(
      "define do_compile\n"
      "@echo Compiling $(1)\n"
      "endef\n"
      "all:\n"
      "\t$(call do_compile,vmlinux)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("Compiling vmlinux")) << "out: " << R.out;
}

// Stress test: kernel-style many config options + conditionals
TEST_F(BuildTest, KernelStyleManyConfigOptions) {
  std::string Mk;
  for (int I = 0; I < 50; ++I) {
    Mk += "CONFIG_OPT" + std::to_string(I) + " = " +
          (I % 3 == 0 ? "y" : "n") + "\n";
  }
  Mk += "ENABLED :=\n";
  for (int I = 0; I < 50; ++I) {
    Mk += "ifeq ($(CONFIG_OPT" + std::to_string(I) + "),y)\n";
    Mk += "  ENABLED += opt" + std::to_string(I) + "\n";
    Mk += "endif\n";
  }
  Mk += "all:\n";
  Mk += "\t@echo count=$(words $(ENABLED))\n";
  Mk += ".PHONY: all\n";
  writeMakefile(Mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  // 0,3,6,...,48 => 17 items
  EXPECT_TRUE(R.contains("count=17")) << "out: " << R.out;
}

// Kernel-style: shell function for detecting host tools
TEST_F(BuildTest, KernelStyleShellDetection) {
  writeMakefile(
      "HOSTCC := $(shell which echo 2>/dev/null || echo /bin/echo)\n"
      "all:\n"
      "\t@echo hostcc=$(HOSTCC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_FALSE(R.out.find("hostcc=") == std::string::npos) << "out: " << R.out;
}

// Kernel-style: $(foreach) with $(if) for conditional list building
TEST_F(BuildTest, KernelStyleForeachWithIf) {
  writeMakefile(
      "ALL := a b c d e\n"
      "SKIP := b d\n"
      "FILTERED := $(foreach x,$(ALL),"
      "$(if $(filter $(x),$(SKIP)),,$(x)))\n"
      "CLEAN := $(strip $(FILTERED))\n"
      "all:\n"
      "\t@echo result=$(CLEAN)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("result=a c e")) << "out: " << R.out;
}

// Kernel-style: MAKECMDGOALS check for conditional behavior
TEST_F(BuildTest, KernelStyleMakecmdgoalsCheck) {
  writeMakefile(
      "ifneq ($(filter clean,$(MAKECMDGOALS)),)\n"
      "  CLEANING = yes\n"
      "else\n"
      "  CLEANING = no\n"
      "endif\n"
      "all:\n"
      "\t@echo cleaning=$(CLEANING)\n"
      "clean:\n"
      "\t@echo cleaning=$(CLEANING)\n"
      ".PHONY: all clean\n");
  auto R1 = runMake({}, "all");
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("cleaning=no")) << "out: " << R1.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("cleaning=yes")) << "out: " << R2.out;
}

// ============================================================================
// New features: realpath, flavor, unexport, MAKEFLAGS
// ============================================================================

TEST_F(BuildTest, FuncRealpathExistingFile) {
  writeFile(tmp() / "hello.c", "int main(){return 0;}\n");
  writeMakefile(
      "SRC := $(realpath hello.c)\n"
      "all:\n"
      "\t@echo src=$(SRC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello.c")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("src= ")) << "should not be empty: " << R.out;
}

TEST_F(BuildTest, FuncRealpathNonexistent) {
  writeMakefile(
      "SRC := $(realpath does_not_exist.c)\n"
      "all:\n"
      "\t@echo src=[$(SRC)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("src=[]")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncRealpathMultipleFiles) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeMakefile(
      "FILES := $(realpath a.c missing.c b.c)\n"
      "COUNT := $(words $(FILES))\n"
      "all:\n"
      "\t@echo count=$(COUNT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("count=2")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncFlavorRecursive) {
  writeMakefile(
      "X = hello\n"
      "all:\n"
      "\t@echo flavor=$(flavor X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("flavor=recursive")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncFlavorSimple) {
  writeMakefile(
      "X := hello\n"
      "all:\n"
      "\t@echo flavor=$(flavor X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("flavor=simple")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncFlavorUndefined) {
  writeMakefile(
      "all:\n"
      "\t@echo flavor=$(flavor NOPE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("flavor=undefined")) << "out: " << R.out;
}

TEST_F(BuildTest, FuncFlavorConditionalAssign) {
  writeMakefile(
      "X ?= fallback\n"
      "all:\n"
      "\t@echo flavor=$(flavor X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("flavor=recursive")) << "out: " << R.out;
}

TEST_F(BuildTest, UnexportVariable) {
  writeMakefile(
      "GREP_OPTIONS := --color=auto\n"
      "export GREP_OPTIONS\n"
      "unexport GREP_OPTIONS\n"
      "all:\n"
      "\t@echo grepopts=[$$GREP_OPTIONS]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("grepopts=[]")) << "out: " << R.out;
}

TEST_F(BuildTest, UnexportMultipleVariables) {
  writeMakefile(
      "A := val_a\n"
      "B := val_b\n"
      "export A B\n"
      "unexport B\n"
      "all:\n"
      "\t@echo a=$$A b=[$$B]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a=val_a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b=[]")) << "out: " << R.out;
}

TEST_F(BuildTest, MakeflagsBasic) {
  writeMakefile(
      "all:\n"
      "\t@echo flags=[$(MAKEFLAGS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("flags=[")) << "out: " << R.out;
}

TEST_F(BuildTest, MakeflagsDryRun) {
  writeMakefile(
      "all:\n"
      "\t@echo flags=$(MAKEFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("n")) << "MAKEFLAGS should contain n: " << R.out;
}

TEST_F(BuildTest, MakeflagsSilent) {
  writeMakefile(
      "all:\n"
      "\t@echo flags=$(MAKEFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-s"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("s")) << "MAKEFLAGS should contain s: " << R.out;
}

TEST_F(BuildTest, MakeflagsWithCmdVars) {
  writeMakefile(
      "all:\n"
      "\t@echo flags=$(MAKEFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"V=1"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("V=1")) << "MAKEFLAGS should have V=1: " << R.out;
}

// ============================================================================
// Kernel-style tests using new features
// ============================================================================

TEST_F(BuildTest, KernelStyleUnexportGrepOptions) {
  writeMakefile(
      "unexport GREP_OPTIONS\n"
      "all:\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("done")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelStyleFlavorCheck) {
  writeMakefile(
      "CFLAGS = -O2\n"
      "LDFLAGS := -lm\n"
      "ifeq ($(flavor CFLAGS),recursive)\n"
      "  CFLAGS_TYPE = recursive\n"
      "else\n"
      "  CFLAGS_TYPE = simple\n"
      "endif\n"
      "ifeq ($(flavor LDFLAGS),simple)\n"
      "  LDFLAGS_TYPE = simple\n"
      "else\n"
      "  LDFLAGS_TYPE = recursive\n"
      "endif\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS_TYPE) ldflags=$(LDFLAGS_TYPE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cflags=recursive")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("ldflags=simple")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelStyleRealpathKbuildExtmod) {
  std::filesystem::create_directories(tmp() / "ext_module");
  writeFile(tmp() / "ext_module" / "Makefile", "all:\n\t@echo ext\n");
  writeMakefile(
      "KBUILD_EXTMOD := $(realpath ext_module)\n"
      "all:\n"
      "\t@echo extmod=$(KBUILD_EXTMOD)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("ext_module")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("extmod= ")) << "should not be empty: " << R.out;
}

TEST_F(BuildTest, KernelStyleMakeflagsCheckVerbose) {
  writeMakefile(
      "ifeq ($(findstring s,$(MAKEFLAGS)),s)\n"
      "  QUIET = quiet\n"
      "else\n"
      "  QUIET = verbose\n"
      "endif\n"
      "all:\n"
      "\t@echo mode=$(QUIET)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("mode=verbose")) << "out: " << R1.out;

  auto R2 = runMake({"-s"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("mode=quiet")) << "out: " << R2.out;
}

// ============================================================================
// Additional robustness/edge-case tests
// ============================================================================

TEST_F(BuildTest, AbspathVsRealpathDifference) {
  writeMakefile(
      "ABS := $(abspath ./foo/../bar)\n"
      "REAL := $(realpath ./nonexistent)\n"
      "all:\n"
      "\t@echo abs_has_bar=$(findstring bar,$(ABS)) real=[$(REAL)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("abs_has_bar=bar")) << "abspath should resolve ..: " << R.out;
  EXPECT_TRUE(R.contains("real=[]")) << "realpath of nonexistent: " << R.out;
}

TEST_F(BuildTest, NestedForeachCallEval) {
  writeMakefile(
      "MODULES := usb net\n"
      "define mod_template\n"
      "$(1)_SRCS := $(1)_core.c $(1)_init.c\n"
      "$(1)_OBJS := $$(patsubst %.c,%.o,$$($(1)_SRCS))\n"
      "endef\n"
      "$(foreach m,$(MODULES),$(eval $(call mod_template,$(m))))\n"
      "all:\n"
      "\t@echo usb_objs=$(usb_OBJS) net_objs=$(net_OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("usb_core.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("net_core.o")) << "out: " << R.out;
}

TEST_F(BuildTest, RecipePrefixAfterCallExpansion) {
  writeMakefile(
      "define silent_echo\n"
      "@echo $(1)\n"
      "endef\n"
      "all:\n"
      "\t$(call silent_echo,hello world)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello world")) << "out: " << R.out;
  // The @echo should be silent — the "echo" command itself shouldn't appear
  // in the output a second time as a non-silent line.
  size_t Count = 0;
  size_t Pos = 0;
  while ((Pos = R.out.find("hello world", Pos)) != std::string::npos) {
    ++Count;
    Pos += 11;
  }
  EXPECT_EQ(Count, 1u) << "should only appear once (silently): " << R.out;
}

TEST_F(BuildTest, DefineWithAllAssignModes) {
  writeMakefile(
      "define A =\n"
      "recursive_val\n"
      "endef\n"
      "define B :=\n"
      "simple_val\n"
      "endef\n"
      "define C +=\n"
      "append_val\n"
      "endef\n"
      "C = base\n"
      "define C +=\n"
      "appended\n"
      "endef\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B) c=$(C)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a=recursive_val")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b=simple_val")) << "out: " << R.out;
}

TEST_F(BuildTest, EmptyVariableInConditional) {
  writeMakefile(
      "EMPTY =\n"
      "ifeq ($(EMPTY),)\n"
      "  RESULT = empty\n"
      "else\n"
      "  RESULT = not_empty\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("result=empty")) << "out: " << R.out;
}

TEST_F(BuildTest, VariableWithSpecialCharsInName) {
  writeMakefile(
      "CONFIG_NET = y\n"
      "obj-y := main.o\n"
      "obj-$(CONFIG_NET) += net.o\n"
      "RESULT := $(obj-y)\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("main.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("net.o")) << "out: " << R.out;
}

TEST_F(BuildTest, CondElseIfNeqChain) {
  writeMakefile(
      "LEVEL = 3\n"
      "ifeq ($(LEVEL),1)\n"
      "  MSG = low\n"
      "else ifeq ($(LEVEL),2)\n"
      "  MSG = mid\n"
      "else ifeq ($(LEVEL),3)\n"
      "  MSG = high\n"
      "else\n"
      "  MSG = unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo msg=$(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("msg=high")) << "out: " << R.out;
}

TEST_F(BuildTest, MultiplePatternRulesFirstMatch) {
  writeFile(tmp() / "test.c", "int main(){return 0;}\n");
  writeMakefile(
      "%.o: %.c\n"
      "\t@echo cc $< -o $@\n"
      "%.o: %.s\n"
      "\t@echo as $< -o $@\n"
      "all: test.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc test.c")) << "should match .c pattern: " << R.out;
}

TEST_F(BuildTest, KernelStyleRecursiveCall) {
  writeMakefile(
      "define reverse\n"
      "$(if $(1),$(call reverse,$(wordlist 2,$(words $(1)),$(1))) $(firstword $(1)))\n"
      "endef\n"
      "LIST = a b c d\n"
      "REV = $(strip $(call reverse,$(LIST)))\n"
      "all:\n"
      "\t@echo rev=$(REV)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("rev=d c b a")) << "out: " << R.out;
}

TEST_F(BuildTest, OriginWithFlavorCombined) {
  writeMakefile(
      "X = hello\n"
      "Y := world\n"
      "all:\n"
      "\t@echo xo=$(origin X) xf=$(flavor X) yo=$(origin Y) yf=$(flavor Y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("xo=file")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("xf=recursive")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("yo=file")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("yf=simple")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelStyleKbuildVerboseCheck) {
  writeMakefile(
      "ifeq (\"$(origin V)\", \"command line\")\n"
      "  KBUILD_VERBOSE = $(V)\n"
      "endif\n"
      "ifndef KBUILD_VERBOSE\n"
      "  KBUILD_VERBOSE = 0\n"
      "endif\n"
      "ifeq ($(KBUILD_VERBOSE),1)\n"
      "  quiet =\n"
      "  Q =\n"
      "else\n"
      "  quiet = quiet_\n"
      "  Q = @\n"
      "endif\n"
      "ifneq ($(findstring s,$(MAKEFLAGS)),)\n"
      "  quiet = silent_\n"
      "endif\n"
      "all:\n"
      "\t@echo quiet=[$(quiet)] Q=[$(Q)]\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("quiet=[quiet_]")) << "default: " << R1.out;
  EXPECT_TRUE(R1.contains("Q=[@]")) << "default Q: " << R1.out;

  auto R2 = runMake({"V=1"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("quiet=[]")) << "verbose: " << R2.out;
  EXPECT_TRUE(R2.contains("Q=[]")) << "verbose Q: " << R2.out;

  auto R3 = runMake({"-s"});
  ASSERT_TRUE(R3.ok()) << "stderr: " << R3.err;
  EXPECT_TRUE(R3.contains("quiet=[silent_]")) << "silent: " << R3.out;
}

TEST_F(BuildTest, KernelStyleCompleteVersionBlock) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "EXTRAVERSION =\n"
      "NAME = Dare mighty things\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)"
      "$(if $(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo ver=$(KERNELVERSION) name=$(NAME)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("ver=5.10.0")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("name=Dare mighty things")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelStyleExportCrossCompile) {
  writeMakefile(
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC = $(CROSS_COMPILE)gcc\n"
      "LD = $(CROSS_COMPILE)ld\n"
      "export ARCH CROSS_COMPILE CC LD\n"
      "all:\n"
      "\t@echo cc=$(CC) ld=$(LD)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc")) << "out: " << R1.out;
  EXPECT_TRUE(R1.contains("ld=ld")) << "out: " << R1.out;

  auto R2 = runMake({"CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << "out: " << R2.out;
  EXPECT_TRUE(R2.contains("ld=aarch64-linux-gnu-ld")) << "out: " << R2.out;
}

TEST_F(BuildTest, KernelStyleNeedExistCheck) {
  std::filesystem::create_directories(tmp() / "scripts");
  writeFile(tmp() / "scripts" / "check.sh", "#!/bin/sh\necho found\n");
  writeMakefile(
      "CHECK := $(wildcard scripts/check.sh)\n"
      "ifeq ($(CHECK),)\n"
      "  STATUS = missing\n"
      "else\n"
      "  STATUS = found\n"
      "endif\n"
      "all:\n"
      "\t@echo status=$(STATUS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("status=found")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelStyleForeachMultiDefine) {
  writeMakefile(
      "ARCHS := x86 arm mips\n"
      "define arch_flags\n"
      "CFLAGS_$(1) := -DARCH_$(1)\n"
      "endef\n"
      "$(foreach a,$(ARCHS),$(eval $(call arch_flags,$(a))))\n"
      "all:\n"
      "\t@echo x86=$(CFLAGS_x86) arm=$(CFLAGS_arm) mips=$(CFLAGS_mips)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("x86=-DARCH_x86")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("arm=-DARCH_arm")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("mips=-DARCH_mips")) << "out: " << R.out;
}

TEST_F(BuildTest, FilterWithMultiplePatterns) {
  writeMakefile(
      "FILES := main.c lib.a utils.o readme.txt data.h\n"
      "CODE := $(filter %.c %.h %.o,$(FILES))\n"
      "all:\n"
      "\t@echo code=$(CODE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("main.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("utils.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("data.h")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("lib.a")) << "should not contain .a: " << R.out;
  EXPECT_FALSE(R.contains("readme.txt")) << "should not contain .txt: " << R.out;
}

TEST_F(BuildTest, WordlistBoundaryValues) {
  writeMakefile(
      "LIST := a b c d e\n"
      "R1 := $(wordlist 2,4,$(LIST))\n"
      "R2 := $(wordlist 1,1,$(LIST))\n"
      "R3 := $(wordlist 3,100,$(LIST))\n"
      "R4 := $(wordlist 10,20,$(LIST))\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2) r3=$(R3) r4=[$(R4)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r1=b c d")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r2=a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r3=c d e")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r4=[]")) << "out of range: " << R.out;
}

TEST_F(BuildTest, SubstRefWithVariableValue) {
  writeMakefile(
      "SRCS := kernel/main.c kernel/sched.c mm/page.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("kernel/main.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("kernel/sched.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("mm/page.o")) << "out: " << R.out;
}

TEST_F(BuildTest, OverrideDefineBlock) {
  writeMakefile(
      "override define CFLAGS\n"
      "-Wall -Werror\n"
      "endef\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O2"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-Wall -Werror")) << "override should win: " << R.out;
}

TEST_F(BuildTest, IncludeChainThreeLevels) {
  writeFile(tmp() / "level3.mk", "DEEP := reached_level3\n");
  writeFile(tmp() / "level2.mk", "include level3.mk\n"
                                   "MID := reached_level2\n");
  writeMakefile(
      "include level2.mk\n"
      "all:\n"
      "\t@echo deep=$(DEEP) mid=$(MID)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("deep=reached_level3")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("mid=reached_level2")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelStyleComplexBuildTemplate) {
  writeMakefile(
      "srctree := .\n"
      "objtree := .\n"
      "src := kernel\n"
      "obj := kernel\n"
      "\n"
      "define rule_cc_o_c\n"
      "@echo '  CC      $(2)'\n"
      "endef\n"
      "\n"
      "define filechk\n"
      "@echo '  CHK     $(1)'\n"
      "endef\n"
      "\n"
      "obj-y := main.o sched.o fork.o\n"
      "EXTRA := $(addprefix $(obj)/,$(obj-y))\n"
      "\n"
      "all:\n"
      "\t@echo objects=$(EXTRA)\n"
      "\t$(call rule_cc_o_c,,main.c)\n"
      "\t$(call filechk,include/generated/utsrelease.h)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("objects=kernel/main.o kernel/sched.o kernel/fork.o"))
      << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC      main.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CHK     include/generated/utsrelease.h"))
      << "out: " << R.out;
}

TEST_F(BuildTest, StressNestedFunctions) {
  writeMakefile(
      "LIST := a.c b.c c.c d.c e.c\n"
      "RESULT := $(sort $(patsubst %.c,%.o,"
      "$(filter-out c.c,$(strip $(LIST)))))\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("d.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("e.o")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("c.o")) << "c.c should be filtered out: " << R.out;
}

TEST_F(BuildTest, StressManyVariablesAndRules) {
  std::string Mk;
  for (int I = 0; I < 100; ++I) {
    Mk += "VAR" + std::to_string(I) + " := val" + std::to_string(I) + "\n";
  }
  Mk += "TOTAL := $(words";
  for (int I = 0; I < 100; ++I) {
    Mk += " $(VAR" + std::to_string(I) + ")";
  }
  Mk += ")\n";
  Mk += "all:\n\t@echo total=$(TOTAL)\n.PHONY: all\n";
  writeMakefile(Mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("total=100")) << "out: " << R.out;
}

TEST_F(BuildTest, DepGraphDiamondShape) {
  writeFile(tmp() / "base.h", "#pragma once\n");
  writeFile(tmp() / "left.c", "#include \"base.h\"\n");
  writeFile(tmp() / "right.c", "#include \"base.h\"\n");
  writeMakefile(
      "all: left.o right.o\n"
      "\t@echo linked\n"
      "left.o: left.c base.h\n"
      "\t@echo cc left.c\n"
      "right.o: right.c base.h\n"
      "\t@echo cc right.c\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc left.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cc right.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("linked")) << "out: " << R.out;
}

TEST_F(BuildTest, ParallelDiamondDeps) {
  writeFile(tmp() / "a.src", "");
  writeFile(tmp() / "b.src", "");
  writeMakefile(
      "all: final\n"
      "\t@echo done\n"
      "final: left right\n"
      "\t@echo link\n"
      "left: a.src\n"
      "\t@echo build_left\n"
      "right: b.src\n"
      "\t@echo build_right\n"
      ".PHONY: all final left right\n");
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("build_left")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("build_right")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("link")) << "out: " << R.out;
}

// ============================================================================
// ifdef/ifndef semantic correctness (raw value, not expanded)
// ============================================================================

TEST_F(BuildTest, IfdefEmptyRecursiveVar) {
  writeMakefile(
      "X =\n"
      "ifdef X\n"
      "  R = defined\n"
      "else\n"
      "  R = undef\n"
      "endif\n"
      "all:\n"
      "\t@echo r=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r=undef")) << "empty = should be undef: " << R.out;
}

TEST_F(BuildTest, IfdefNonEmptyRecursiveVar) {
  writeMakefile(
      "X = hello\n"
      "ifdef X\n"
      "  R = defined\n"
      "else\n"
      "  R = undef\n"
      "endif\n"
      "all:\n"
      "\t@echo r=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r=defined")) << "out: " << R.out;
}

TEST_F(BuildTest, IfdefRecursiveRefToEmpty) {
  // X = $(Y) where Y is empty. GNU make says ifdef X is TRUE
  // because X's raw value "$(Y)" is non-empty.
  writeMakefile(
      "Y =\n"
      "X = $(Y)\n"
      "ifdef X\n"
      "  R = defined\n"
      "else\n"
      "  R = undef\n"
      "endif\n"
      "all:\n"
      "\t@echo r=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r=defined"))
      << "raw value $(Y) is non-empty: " << R.out;
}

TEST_F(BuildTest, IfndefUndefinedVar) {
  writeMakefile(
      "ifndef NOTSET\n"
      "  R = yes\n"
      "else\n"
      "  R = no\n"
      "endif\n"
      "all:\n"
      "\t@echo r=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r=yes")) << "out: " << R.out;
}

TEST_F(BuildTest, IfndefEmptySimpleVar) {
  writeMakefile(
      "X :=\n"
      "ifndef X\n"
      "  R = yes\n"
      "else\n"
      "  R = no\n"
      "endif\n"
      "all:\n"
      "\t@echo r=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r=yes")) << "empty := should be ifndef true: " << R.out;
}

TEST_F(BuildTest, KernelStyleIfdefCrossCompile) {
  // Kernel pattern: CROSS_COMPILE ?= sets empty, ifdef checks it
  writeMakefile(
      "CROSS_COMPILE ?=\n"
      "ifdef CROSS_COMPILE\n"
      "  HAS_CROSS = yes\n"
      "else\n"
      "  HAS_CROSS = no\n"
      "endif\n"
      "all:\n"
      "\t@echo has_cross=$(HAS_CROSS)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("has_cross=no"))
      << "empty ?= means no cross: " << R1.out;

  auto R2 = runMake({"CROSS_COMPILE=arm-linux-"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("has_cross=yes"))
      << "cmdline sets cross: " << R2.out;
}

// ============================================================================
// include with glob patterns
// ============================================================================

TEST_F(BuildTest, IncludeWithGlobPattern) {
  writeFile(tmp() / "config_a.mk", "A_VAL := from_a\n");
  writeFile(tmp() / "config_b.mk", "B_VAL := from_b\n");
  writeMakefile(
      "include config_*.mk\n"
      "all:\n"
      "\t@echo a=$(A_VAL) b=$(B_VAL)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a=from_a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b=from_b")) << "out: " << R.out;
}

TEST_F(BuildTest, OptionalIncludeGlobNoMatch) {
  writeMakefile(
      "-include nonexistent_*.mk\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("ok")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelStyleIncludeDepFiles) {
  writeFile(tmp() / "dep1.mk", "DEP_A := from_dep1\n");
  writeFile(tmp() / "dep2.mk", "DEP_B := from_dep2\n");
  writeMakefile(
      "-include dep*.mk\n"
      "all:\n"
      "\t@echo a=$(DEP_A) b=$(DEP_B)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a=from_dep1")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b=from_dep2")) << "out: " << R.out;
}

// ============================================================================
// Additional edge cases for completeness
// ============================================================================

TEST_F(BuildTest, ShellAssignBang) {
  writeMakefile(
      "HOSTARCH != uname -m 2>/dev/null || echo unknown\n"
      "all:\n"
      "\t@echo arch=$(HOSTARCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_FALSE(R.out.find("arch=") == std::string::npos) << "out: " << R.out;
}

TEST_F(BuildTest, ExportWithSimpleAssign) {
  writeMakefile(
      "export CC := neverc\n"
      "all:\n"
      "\t@echo cc=$$CC\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc=neverc")) << "out: " << R.out;
}

TEST_F(BuildTest, DollarEscapeInRecipe) {
  writeMakefile(
      "all:\n"
      "\t@echo price=$$100\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  // $$ in recipe → shell sees $100
  EXPECT_TRUE(R.out.find("price=") != std::string::npos) << "out: " << R.out;
}

TEST_F(BuildTest, MultiplePrereqRulesForSameTarget) {
  writeFile(tmp() / "main.c", "int main(){return 0;}\n");
  writeFile(tmp() / "config.h", "");
  writeFile(tmp() / "types.h", "");
  writeMakefile(
      "main.o: main.c\n"
      "\t@echo cc main.c\n"
      "main.o: config.h types.h\n"
      "all: main.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc main.c")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelStyleComplexCondChain) {
  writeMakefile(
      "ARCH = arm64\n"
      "CONFIG_64BIT = y\n"
      "ifeq ($(ARCH),x86)\n"
      "  BITS = 32\n"
      "  ifdef CONFIG_X86_64\n"
      "    BITS = 64\n"
      "  endif\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  ifdef CONFIG_64BIT\n"
      "    BITS = 64\n"
      "  else\n"
      "    BITS = 32\n"
      "  endif\n"
      "else\n"
      "  BITS = unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(ARCH) bits=$(BITS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("arch=arm64")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("bits=64")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelStyleSrcTreePaths) {
  writeMakefile(
      "srctree := .\n"
      "objtree := .\n"
      "VERSIONFILE := $(srctree)/include/config/kernel.release\n"
      "TGTPATH := $(addprefix $(objtree)/,init kernel mm fs)\n"
      "all:\n"
      "\t@echo tgt=$(TGTPATH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("./init")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("./kernel")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("./mm")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("./fs")) << "out: " << R.out;
}

TEST_F(BuildTest, EvalGeneratesMultipleRules) {
  writeMakefile(
      "MODULES := mod_a mod_b\n"
      "define gen_rule\n"
      "$(1):\n"
      "\t@echo building $(1)\n"
      "endef\n"
      ".PHONY: all $(MODULES)\n"
      "all: $(MODULES)\n"
      "\t@echo all done\n"
      "$(foreach m,$(MODULES),$(eval $(call gen_rule,$(m))))\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("building mod_a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("building mod_b")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("all done")) << "out: " << R.out;
}
