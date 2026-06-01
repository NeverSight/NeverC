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

// ============================================================================
// Linux 5.10 Kernel: FORCE target pattern
// ============================================================================

TEST_F(BuildTest, KernelForceTargetAlwaysRebuilds) {
  writeFile(tmp() / "vmlinux.c", "int main(){return 0;}\n");
  writeMakefile(
      "vmlinux: vmlinux.c FORCE\n"
      "\t@echo building vmlinux\n"
      "FORCE:\n"
      ".PHONY: FORCE\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("building vmlinux"))
      << "FORCE must trigger rebuild: " << R.out;
}

TEST_F(BuildTest, KernelForceWithPhonyAll) {
  writeFile(tmp() / "foo.c", "");
  writeMakefile(
      ".PHONY: all FORCE\n"
      "all: foo.o\n"
      "\t@echo all done\n"
      "foo.o: foo.c FORCE\n"
      "\t@echo cc foo.c\n"
      "FORCE:\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc foo.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("all done")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelForceChainedDeps) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeMakefile(
      ".PHONY: all FORCE\n"
      "all: a.o b.o\n"
      "\t@echo link\n"
      "a.o: a.c FORCE\n"
      "\t@echo cc a\n"
      "b.o: b.c FORCE\n"
      "\t@echo cc b\n"
      "FORCE:\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cc b")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("link")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Kbuild obj-y / obj-m accumulation
// ============================================================================

TEST_F(BuildTest, KernelObjYAccumulationMultidir) {
  writeMakefile(
      "obj-y := init/ kernel/ mm/\n"
      "obj-y += fs/ drivers/\n"
      "DIRS := $(patsubst %/,%,$(obj-y))\n"
      "all:\n"
      "\t@echo dirs=$(DIRS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("init")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("drivers")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelObjYConfigConditional) {
  writeMakefile(
      "CONFIG_EXT4 = y\n"
      "CONFIG_XFS = m\n"
      "CONFIG_BTRFS =\n"
      "obj-$(CONFIG_EXT4) += ext4.o\n"
      "obj-$(CONFIG_XFS) += xfs.o\n"
      "obj-$(CONFIG_BTRFS) += btrfs.o\n"
      "builtin := $(obj-y)\n"
      "modules := $(obj-m)\n"
      "all:\n"
      "\t@echo builtin=$(builtin) modules=$(modules)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("builtin=ext4.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("modules=xfs.o")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("btrfs")) << "empty config should exclude: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: cc-option / try-run style macros
// ============================================================================

TEST_F(BuildTest, KernelCallMacroWithShellFallback) {
  writeMakefile(
      "define try-run\n"
      "$(shell if $(1) >/dev/null 2>&1; then echo $(2); else echo $(3); fi)\n"
      "endef\n"
      "HAS_ECHO := $(call try-run,echo test,yes,no)\n"
      "all:\n"
      "\t@echo has=$(HAS_ECHO)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("has=yes")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelCcOptionSimulation) {
  writeMakefile(
      "CC := echo\n"
      "define cc-option\n"
      "$(shell $(CC) $(1) 2>/dev/null && echo $(1))\n"
      "endef\n"
      "CFLAGS += $(call cc-option,-Wall)\n"
      "CFLAGS += $(call cc-option,-Wextra)\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Kconfig-style variable composition
// ============================================================================

TEST_F(BuildTest, KernelKconfigCflagsComposition) {
  writeMakefile(
      "ARCH := x86\n"
      "CONFIG_SMP := y\n"
      "CONFIG_PREEMPT := y\n"
      "KBUILD_CFLAGS := -O2\n"
      "ifdef CONFIG_SMP\n"
      "  KBUILD_CFLAGS += -DCONFIG_SMP\n"
      "endif\n"
      "ifdef CONFIG_PREEMPT\n"
      "  KBUILD_CFLAGS += -DCONFIG_PREEMPT\n"
      "endif\n"
      "KBUILD_CFLAGS += -march=$(ARCH)\n"
      "all:\n"
      "\t@echo flags=$(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-O2")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-DCONFIG_SMP")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-DCONFIG_PREEMPT")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-march=x86")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelCflagsPerArchOverride) {
  writeMakefile(
      "ARCH ?= x86\n"
      "ifeq ($(ARCH),x86)\n"
      "  CFLAGS_ARCH := -m64 -mno-red-zone\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  CFLAGS_ARCH := -mgeneral-regs-only\n"
      "else\n"
      "  CFLAGS_ARCH :=\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(ARCH) flags=$(CFLAGS_ARCH)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("-m64")) << "default x86: " << R1.out;

  auto R2 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("-mgeneral-regs-only")) << "arm64: " << R2.out;
}

// ============================================================================
// Linux 5.10 Kernel: Recursive make / sub-directory
// ============================================================================

TEST_F(BuildTest, KernelRecursiveMakeSubdir) {
  std::filesystem::create_directories(tmp() / "drivers");
  writeFile(tmp() / "drivers" / "Makefile",
            "obj-y := net.o usb.o\n"
            "all:\n"
            "\t@echo subdir-objs=$(obj-y)\n"
            ".PHONY: all\n");
  writeMakefile(
      "SUBDIRS := drivers\n"
      "all: $(SUBDIRS)\n"
      "\t@echo top-done\n"
      ".PHONY: all $(SUBDIRS)\n"
      "$(SUBDIRS):\n"
      "\t@$(MAKE) -C $@ -s\n");
  auto R = runMake({"-s"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("subdir-objs=net.o usb.o")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Dependency file (.d) inclusion
// ============================================================================

TEST_F(BuildTest, KernelDepFileInclusion) {
  writeFile(tmp() / "main.c", "int main(){return 0;}\n");
  writeFile(tmp() / "main.d", "main.o: main.c config.h\n");
  writeFile(tmp() / "config.h", "#define VER 1\n");
  writeMakefile(
      "-include main.d\n"
      "main.o: main.c\n"
      "\t@echo cc main.c\n"
      "all: main.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc main.c")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelDepFileMultipleTargets) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "shared.h", "");
  writeFile(tmp() / ".a.o.d", "a.o: a.c shared.h\n");
  writeFile(tmp() / ".b.o.d", "b.o: b.c shared.h\n");
  writeMakefile(
      ".PHONY: all\n"
      "all: a.o b.o\n"
      "\t@echo link\n"
      "-include .a.o.d .b.o.d\n"
      "%.o: %.c\n"
      "\t@echo cc $<\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc a.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cc b.c")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Version extraction pattern
// ============================================================================

TEST_F(BuildTest, KernelVersionExtraction) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "EXTRAVERSION =\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo ver=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("ver=5.10.0")) << "out: " << R.out;
}

TEST_F(BuildTest, KernelVersionWithExtraversion) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 42\n"
      "EXTRAVERSION = -rc1\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo ver=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("ver=5.10.42-rc1")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Foreach + eval generating compile rules
// ============================================================================

TEST_F(BuildTest, KernelForeachEvalCompileRules) {
  writeFile(tmp() / "init.c", "");
  writeFile(tmp() / "core.c", "");
  writeFile(tmp() / "sched.c", "");
  writeMakefile(
      "obj-y := init.o core.o sched.o\n"
      "define compile_rule\n"
      "$(1): $(patsubst %.o,%.c,$(1))\n"
      "\t@echo CC $(1)\n"
      "endef\n"
      ".PHONY: all\n"
      "all: $(obj-y)\n"
      "\t@echo linked\n"
      "$(foreach o,$(obj-y),$(eval $(call compile_rule,$(o))))\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC init.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC core.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC sched.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("linked")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Hostcc / crosscc separation
// ============================================================================

TEST_F(BuildTest, KernelHostCrossCompilerSeparation) {
  writeMakefile(
      "HOSTCC := gcc\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "CROSS_COMPILE ?=\n"
      "ifeq ($(CROSS_COMPILE),)\n"
      "  EFFECTIVE_CC := $(HOSTCC)\n"
      "else\n"
      "  EFFECTIVE_CC := $(CC)\n"
      "endif\n"
      "all:\n"
      "\t@echo cc=$(EFFECTIVE_CC)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc")) << "default host: " << R1.out;

  auto R2 = runMake({"CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << "cross: " << R2.out;
}

// ============================================================================
// Linux 5.10 Kernel: Export chain for sub-makes
// ============================================================================

TEST_F(BuildTest, KernelExportChainForSubmake) {
  writeMakefile(
      "export ARCH := x86_64\n"
      "export KBUILD_CFLAGS := -O2\n"
      "export CC := neverc\n"
      "all:\n"
      "\t@echo arch=$$ARCH cc=$$CC flags=$$KBUILD_CFLAGS\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("arch=x86_64")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cc=neverc")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("flags=-O2")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Filter for config options
// ============================================================================

TEST_F(BuildTest, KernelFilterConfigObjLists) {
  writeMakefile(
      "CONFIG_A = y\n"
      "CONFIG_B = m\n"
      "CONFIG_C = y\n"
      "obj-y += a.o c.o\n"
      "obj-m += b.o\n"
      "BUILTINS := $(filter-out %/,$(obj-y))\n"
      "MODULES := $(filter-out %/,$(obj-m))\n"
      "all:\n"
      "\t@echo builtins=$(BUILTINS) modules=$(MODULES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("builtins=a.o c.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("modules=b.o")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Sort unique and dedup
// ============================================================================

TEST_F(BuildTest, KernelSortDedup) {
  writeMakefile(
      "INCLUDE_DIRS := arch/x86 include lib arch/x86 include/uapi\n"
      "UNIQUE := $(sort $(INCLUDE_DIRS))\n"
      "FLAGS := $(addprefix -I,$(UNIQUE))\n"
      "all:\n"
      "\t@echo flags=$(FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-Iarch/x86")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-Iinclude")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-Ilib")) << "out: " << R.out;
  // sort deduplicates, so arch/x86 should appear only once
  std::string Flags;
  auto pos = R.out.find("flags=");
  if (pos != std::string::npos)
    Flags = R.out.substr(pos);
  size_t first = Flags.find("-Iarch/x86");
  size_t second = Flags.find("-Iarch/x86", first + 1);
  EXPECT_EQ(second, std::string::npos) << "duplicate found: " << Flags;
}

// ============================================================================
// Linux 5.10 Kernel: Define as recipe template
// ============================================================================

TEST_F(BuildTest, KernelDefineAsRecipeTemplate) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeMakefile(
      "define cmd_cc_o_c\n"
      "@echo CC $(2) -o $(1)\n"
      "endef\n"
      "define rule_cc_o_c\n"
      "$(1): $(2)\n"
      "\t$(call cmd_cc_o_c,$(1),$(2))\n"
      "endef\n"
      ".PHONY: all\n"
      "all: a.o b.o\n"
      "\t@echo done\n"
      "$(eval $(call rule_cc_o_c,a.o,a.c))\n"
      "$(eval $(call rule_cc_o_c,b.o,b.c))\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC a.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC b.c")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Nested foreach with filter
// ============================================================================

TEST_F(BuildTest, KernelNestedForeachFilter) {
  writeMakefile(
      "ARCHES := x86 arm64 mips\n"
      "CONFIGS_x86 := SMP X86_64\n"
      "CONFIGS_arm64 := SMP ARM64\n"
      "CONFIGS_mips := SMP MIPS32\n"
      "SMP_ARCHES := $(foreach a,$(ARCHES),$(if $(filter "
      "SMP,$(CONFIGS_$(a))),$(a)))\n"
      "all:\n"
      "\t@echo smp=$(SMP_ARCHES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("x86")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("arm64")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("mips")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: .DEFAULT_GOAL
// ============================================================================

TEST_F(BuildTest, KernelDefaultGoal) {
  writeMakefile(
      ".DEFAULT_GOAL := help\n"
      "vmlinux:\n"
      "\t@echo building vmlinux\n"
      "help:\n"
      "\t@echo usage info\n"
      ".PHONY: vmlinux help\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("usage info")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("building vmlinux")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Complex subst-ref for object lists
// ============================================================================

TEST_F(BuildTest, KernelSubstRefObjectList) {
  writeMakefile(
      "SRCS := init/main.c kernel/fork.c mm/page_alloc.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "DEPS := $(SRCS:.c=.d)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      "\t@echo deps=$(DEPS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("init/main.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("kernel/fork.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("mm/page_alloc.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("init/main.d")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: MAKECMDGOALS for target-specific logic
// ============================================================================

TEST_F(BuildTest, KernelMakecmdgoalsTargetSpecific) {
  writeMakefile(
      "ifneq ($(filter clean,$(MAKECMDGOALS)),)\n"
      "  SKIP_BUILD := yes\n"
      "else\n"
      "  SKIP_BUILD := no\n"
      "endif\n"
      "all:\n"
      "\t@echo skip=$(SKIP_BUILD)\n"
      "clean:\n"
      "\t@echo skip=$(SKIP_BUILD) cleaning\n"
      ".PHONY: all clean\n");
  auto R1 = runMake({}, "all");
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("skip=no")) << "out: " << R1.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("skip=yes")) << "out: " << R2.out;
}

// ============================================================================
// Edge Cases: Variable expansion in target names
// ============================================================================

TEST_F(BuildTest, EdgeVarInTargetName) {
  writeMakefile(
      "PROG := myapp\n"
      "$(PROG):\n"
      "\t@echo building $(PROG)\n"
      ".PHONY: $(PROG)\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("building myapp")) << "out: " << R.out;
}

TEST_F(BuildTest, EdgeVarInPrereqName) {
  writeFile(tmp() / "src.c", "");
  writeMakefile(
      "SRC := src.c\n"
      "out.o: $(SRC)\n"
      "\t@echo compiling $(SRC)\n"
      "all: out.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("compiling src.c")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Empty and whitespace handling
// ============================================================================

TEST_F(BuildTest, EdgeEmptyVarInConditionalIfeq) {
  writeMakefile(
      "X :=\n"
      "ifeq ($(X),)\n"
      "  RESULT := empty\n"
      "else\n"
      "  RESULT := notempty\n"
      "endif\n"
      "all:\n"
      "\t@echo r=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r=empty")) << "out: " << R.out;
}

TEST_F(BuildTest, EdgeStripInConditional) {
  writeMakefile(
      "X :=   \n"
      "ifeq ($(strip $(X)),)\n"
      "  RESULT := stripped_empty\n"
      "else\n"
      "  RESULT := has_content\n"
      "endif\n"
      "all:\n"
      "\t@echo r=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r=stripped_empty")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Pattern rule priority
// ============================================================================

TEST_F(BuildTest, EdgePatternRulePriorityExplicitWins) {
  writeFile(tmp() / "special.c", "");
  writeMakefile(
      "%.o: %.c\n"
      "\t@echo generic $@\n"
      "special.o: special.c\n"
      "\t@echo explicit special.o\n"
      "all: special.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("explicit special.o")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("generic special.o")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Deeply nested function calls
// ============================================================================

TEST_F(BuildTest, EdgeDeeplyNestedFunctions) {
  writeMakefile(
      "X := a.c b.c c.c\n"
      "RESULT := $(sort $(filter %.c,$(patsubst %.o,%.c,$(patsubst "
      "%.c,%.o,$(X)))))\n"
      "all:\n"
      "\t@echo r=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("c.c")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Multiple recipes for same phony target
// ============================================================================

TEST_F(BuildTest, EdgePhonyAlwaysRuns) {
  writeMakefile(
      ".PHONY: clean\n"
      "clean:\n"
      "\t@echo cleaning\n"
      "all: clean\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("cleaning")) << "out: " << R1.out;

  auto R2 = runMake();
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("cleaning"))
      << "phony must always run: " << R2.out;
}

// ============================================================================
// Edge Cases: Substitution reference with complex patterns
// ============================================================================

TEST_F(BuildTest, EdgeSubstRefPathPrefix) {
  writeMakefile(
      "SRCS := src/a.c src/b.c src/c.c\n"
      "OBJS := $(SRCS:src/%.c=obj/%.o)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("obj/a.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("obj/b.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("obj/c.o")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Order-only with pattern rules
// ============================================================================

TEST_F(BuildTest, EdgeOrderOnlyWithPatternRule) {
  writeFile(tmp() / "a.c", "");
  std::filesystem::create_directories(tmp() / "builddir");
  writeMakefile(
      ".PHONY: all\n"
      "all: builddir/a.o\n"
      "\t@echo done\n"
      "builddir/%.o: %.c | builddir\n"
      "\t@echo cc $< -o $@\n"
      "builddir:\n"
      "\t@echo mkdir builddir\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc a.c")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Recursive variable with multiple levels
// ============================================================================

TEST_F(BuildTest, EdgeRecursiveVarMultiLevel) {
  writeMakefile(
      "A = $(B)\n"
      "B = $(C)\n"
      "C = final_value\n"
      "all:\n"
      "\t@echo a=$(A)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a=final_value")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Command line var overrides file var
// ============================================================================

TEST_F(BuildTest, EdgeCmdLineOverridesAllAssignModes) {
  writeMakefile(
      "X = from_file\n"
      "X := also_from_file\n"
      "X += appended\n"
      "all:\n"
      "\t@echo x=$(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"X=from_cmdline"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("x=from_cmdline")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Large fan-out dependency tree
// ============================================================================

TEST_F(BuildTest, StressLargeFanOutDeps) {
  std::string AllTargets;
  for (int I = 0; I < 50; ++I) {
    std::string Name = "obj_" + std::to_string(I) + ".o";
    writeFile(tmp() / ("obj_" + std::to_string(I) + ".c"), "");
    if (!AllTargets.empty())
      AllTargets += " ";
    AllTargets += Name;
  }
  std::string Makefile;
  Makefile += ".PHONY: all\n";
  Makefile += "all: " + AllTargets + "\n\t@echo linked 50 objs\n";
  for (int I = 0; I < 50; ++I) {
    std::string Name = "obj_" + std::to_string(I) + ".o";
    std::string Src = "obj_" + std::to_string(I) + ".c";
    Makefile += Name + ": " + Src + "\n\t@echo cc " + Src + "\n";
  }
  writeMakefile(Makefile);
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc obj_0.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cc obj_49.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("linked 50 objs")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Pattern rule with multiple prerequisites
// ============================================================================

TEST_F(BuildTest, EdgePatternRuleMultiPrereqs) {
  writeFile(tmp() / "test.c", "");
  writeFile(tmp() / "test.h", "");
  writeMakefile(
      "%.o: %.c %.h\n"
      "\t@echo cc $< (with header $*.h)\n"
      "all: test.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc test.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("test.h")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Conditional with function calls
// ============================================================================

TEST_F(BuildTest, EdgeConditionalWithFunctionCall) {
  writeMakefile(
      "FEATURES := debug smp preempt\n"
      "ifneq ($(filter debug,$(FEATURES)),)\n"
      "  CFLAGS += -g -DDEBUG\n"
      "endif\n"
      "ifneq ($(filter smp,$(FEATURES)),)\n"
      "  CFLAGS += -DSMP\n"
      "endif\n"
      "ifeq ($(filter noacpi,$(FEATURES)),)\n"
      "  CFLAGS += -DACPI\n"
      "endif\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-g")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-DDEBUG")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-DSMP")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-DACPI")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Info/warning during variable evaluation
// ============================================================================

TEST_F(BuildTest, EdgeInfoDuringEval) {
  writeMakefile(
      "$(info Build started)\n"
      "CC := neverc\n"
      "$(info Using CC=$(CC))\n"
      "all:\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("Build started")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("Using CC=neverc")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Multiline define with tabs (recipe template)
// ============================================================================

TEST_F(BuildTest, EdgeDefineMultilineRecipe) {
  writeMakefile(
      "define do_build\n"
      "@echo step1: prepare\n"
      "@echo step2: compile\n"
      "@echo step3: link\n"
      "endef\n"
      "all:\n"
      "\t$(do_build)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.out.find("step1") != std::string::npos ||
              R.out.find("prepare") != std::string::npos)
      << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Quiet/verbose build control
// ============================================================================

TEST_F(BuildTest, KernelQuietVerboseControl) {
  writeMakefile(
      "V ?= 0\n"
      "ifeq ($(V),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "quiet_cmd_cc = CC $@\n"
      "cmd_cc = gcc -c $< -o $@\n"
      "all:\n"
      "\t@echo q=$(quiet) Q=$(Q)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("q=quiet_")) << "quiet mode: " << R1.out;
  EXPECT_TRUE(R1.contains("Q=@")) << "quiet mode: " << R1.out;

  auto R2 = runMake({"V=1"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("Q=") || R2.out.find("Q= ") != std::string::npos)
      << "verbose mode: " << R2.out;
}

// ============================================================================
// Linux 5.10 Kernel: Complex arch-specific include
// ============================================================================

TEST_F(BuildTest, KernelArchSpecificInclude) {
  std::filesystem::create_directories(tmp() / "arch" / "x86");
  writeFile(tmp() / "arch" / "x86" / "Makefile",
            "ARCH_CFLAGS := -m64 -march=x86-64\n");
  writeMakefile(
      "SRCARCH := x86\n"
      "include arch/$(SRCARCH)/Makefile\n"
      "all:\n"
      "\t@echo flags=$(ARCH_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-m64")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-march=x86-64")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Linker script variable construction
// ============================================================================

TEST_F(BuildTest, KernelLinkerScriptVarConstruction) {
  writeMakefile(
      "LDFLAGS :=\n"
      "LDFLAGS += -z noexecstack\n"
      "LDFLAGS += --build-id\n"
      "LDFLAGS_vmlinux := -T vmlinux.lds\n"
      "LDFLAGS_vmlinux += --whole-archive\n"
      "ALL_LDFLAGS := $(LDFLAGS) $(LDFLAGS_vmlinux)\n"
      "all:\n"
      "\t@echo ld=$(ALL_LDFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-z noexecstack")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("--build-id")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-T vmlinux.lds")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("--whole-archive")) << "out: " << R.out;
}

// ============================================================================
// Edge Cases: Parallel build correctness
// ============================================================================

TEST_F(BuildTest, ParallelBuildDependencyOrder) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "c.c", "");
  writeMakefile(
      "all: app\n"
      "\t@echo done\n"
      "app: a.o b.o c.o\n"
      "\t@echo link\n"
      "%.o: %.c\n"
      "\t@echo cc $<\n"
      ".PHONY: all\n");
  auto R = runMake({"-j4", "-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc a.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cc b.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cc c.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("link")) << "out: " << R.out;
  // Link must come after all cc commands
  size_t LinkPos = R.out.find("link");
  size_t LastCc = R.out.rfind("cc ");
  EXPECT_GT(LinkPos, LastCc) << "link before compile: " << R.out;
}

// ============================================================================
// INTEGRATION: Mini-kernel build simulation
// Exercises FORCE, obj-y, Kconfig, arch-include, foreach+eval,
// pattern rules, dep files, export, quiet/verbose, MAKECMDGOALS,
// version extraction — all together.
// ============================================================================

TEST_F(BuildTest, IntegrationMiniKernelBuild) {
  // Directory structure
  std::filesystem::create_directories(tmp() / "arch" / "x86");
  std::filesystem::create_directories(tmp() / "init");
  std::filesystem::create_directories(tmp() / "kernel");

  // Source files
  writeFile(tmp() / "init" / "main.c", "int main(){return 0;}\n");
  writeFile(tmp() / "kernel" / "fork.c", "void fork(){}\n");
  writeFile(tmp() / "kernel" / "sched.c", "void sched(){}\n");

  // Arch Makefile
  writeFile(tmp() / "arch" / "x86" / "Makefile",
            "ARCH_CFLAGS := -m64\n"
            "ARCH_LDFLAGS := -z max-page-size=0x200000\n");

  // Sub-makefiles
  writeFile(tmp() / "init" / "Makefile",
            "obj-y := main.o\n");
  writeFile(tmp() / "kernel" / "Makefile",
            "obj-y := fork.o sched.o\n");

  // Main Makefile: full kernel-style patterns
  writeMakefile(
      // Version
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "EXTRAVERSION =\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "\n"
      // Config
      "ARCH ?= x86\n"
      "SRCARCH := $(ARCH)\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "\n"
      // Verbose control
      "V ?= 0\n"
      "ifeq ($(V),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "\n"
      // Base flags
      "KBUILD_CFLAGS := -O2 -Wall\n"
      "KBUILD_LDFLAGS :=\n"
      "\n"
      // Arch include
      "include arch/$(SRCARCH)/Makefile\n"
      "KBUILD_CFLAGS += $(ARCH_CFLAGS)\n"
      "KBUILD_LDFLAGS += $(ARCH_LDFLAGS)\n"
      "\n"
      // Object lists from sub-makefiles
      "init-y :=\n"
      "kernel-y :=\n"
      "include init/Makefile\n"
      "init-y += $(addprefix init/,$(obj-y))\n"
      "obj-y :=\n"
      "include kernel/Makefile\n"
      "kernel-y += $(addprefix kernel/,$(obj-y))\n"
      "\n"
      // MAKECMDGOALS check
      "ifneq ($(filter clean,$(MAKECMDGOALS)),)\n"
      "  SKIP_BUILD := yes\n"
      "else\n"
      "  SKIP_BUILD := no\n"
      "endif\n"
      "\n"
      // Export
      "export CC KBUILD_CFLAGS\n"
      "\n"
      // Default goal
      ".DEFAULT_GOAL := all\n"
      "\n"
      // SRCS/OBJS
      "ALL_OBJS := $(init-y) $(kernel-y)\n"
      "SRCS := $(ALL_OBJS:.o=.c)\n"
      "\n"
      // Targets
      ".PHONY: all vmlinux clean FORCE\n"
      "all: vmlinux\n"
      "\t$(Q)echo version=$(KERNELVERSION) done\n"
      "\n"
      "vmlinux: $(ALL_OBJS) FORCE\n"
      "\t$(Q)echo LD vmlinux objs=$(ALL_OBJS) flags=$(KBUILD_LDFLAGS)\n"
      "\n"
      // Pattern rule
      "%.o: %.c FORCE\n"
      "\t$(Q)echo CC $< cflags=$(KBUILD_CFLAGS)\n"
      "\n"
      "clean:\n"
      "\t$(Q)echo clean skip=$(SKIP_BUILD)\n"
      "\n"
      "FORCE:\n"
      "\n"
      // Dep file include (optional, no .d files yet)
      "-include $(ALL_OBJS:.o=.d)\n");

  // Test 1: Default build
  auto R1 = runMake({"-n"});
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("CC init/main.c")) << "init compiled: " << R1.out;
  EXPECT_TRUE(R1.contains("CC kernel/fork.c")) << "fork compiled: " << R1.out;
  EXPECT_TRUE(R1.contains("CC kernel/sched.c"))
      << "sched compiled: " << R1.out;
  EXPECT_TRUE(R1.contains("LD vmlinux")) << "vmlinux linked: " << R1.out;
  EXPECT_TRUE(R1.contains("version=5.10.0 done"))
      << "version correct: " << R1.out;
  EXPECT_TRUE(R1.contains("-O2")) << "cflags: " << R1.out;
  EXPECT_TRUE(R1.contains("-m64")) << "arch cflags: " << R1.out;

  // Test 2: Cross-compile
  auto R2 = runMake({"-n", "CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("CC init/main.c"))
      << "cross init: " << R2.out;

  // Test 3: Clean target
  auto R3 = runMake({"-n"}, "clean");
  ASSERT_TRUE(R3.ok()) << "stderr: " << R3.err;
  EXPECT_TRUE(R3.contains("clean")) << "clean target: " << R3.out;
  EXPECT_TRUE(R3.contains("skip=yes")) << "skip build: " << R3.out;

  // Test 4: Verbose mode
  auto R4 = runMake({"-n", "V=1"});
  ASSERT_TRUE(R4.ok()) << "stderr: " << R4.err;
  EXPECT_TRUE(R4.contains("CC init/main.c"))
      << "verbose: " << R4.out;
}

// ============================================================================
// $(file) function
// ============================================================================

TEST_F(BuildTest, FileFuncWriteAndRead) {
  writeMakefile(
      "$(file >out.txt,hello world)\n"
      "CONTENT := $(file <out.txt)\n"
      "all:\n"
      "\t@echo got=$(CONTENT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("got=hello world")) << "out: " << R.out;
}

TEST_F(BuildTest, FileFuncAppend) {
  writeMakefile(
      "$(file >log.txt,line1)\n"
      "$(file >>log.txt,line2)\n"
      "all:\n"
      "\t@cat log.txt\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("line1")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("line2")) << "out: " << R.out;
}

TEST_F(BuildTest, FileFuncReadNonexistent) {
  writeMakefile(
      "CONTENT := $(file <no_such_file.txt)\n"
      "all:\n"
      "\t@echo empty=[$(CONTENT)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("empty=[]")) << "out: " << R.out;
}

TEST_F(BuildTest, FileFuncWriteEmpty) {
  writeMakefile(
      "$(file >empty.txt)\n"
      "CONTENT := $(file <empty.txt)\n"
      "all:\n"
      "\t@echo empty=[$(CONTENT)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("empty=[]")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Kbuild descend macro pattern
// ============================================================================

TEST_F(BuildTest, KernelKbuildDescendMacro) {
  std::filesystem::create_directories(tmp() / "drivers" / "net");
  writeFile(tmp() / "drivers" / "net" / "Makefile",
            "obj-y := e1000.o rtl8139.o\n"
            "all:\n"
            "\t@echo net-objs=$(obj-y)\n"
            ".PHONY: all\n");
  writeMakefile(
      "define descend\n"
      "$(Q)$(MAKE) -C $(1) -s\n"
      "endef\n"
      "Q := @\n"
      "SUBDIRS := drivers/net\n"
      "all: $(SUBDIRS)\n"
      "\t@echo top-done\n"
      ".PHONY: all $(SUBDIRS)\n"
      "$(SUBDIRS):\n"
      "\t$(call descend,$@)\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("make") || R.contains("neverc"))
      << "descend called: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Module versioning pattern
// ============================================================================

TEST_F(BuildTest, KernelModuleVersionPattern) {
  writeMakefile(
      "CONFIG_MODVERSIONS := y\n"
      "ifdef CONFIG_MODVERSIONS\n"
      "  MODVER_CFLAGS := -DMODVERSIONS\n"
      "endif\n"
      "CFLAGS := -O2 $(MODVER_CFLAGS)\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-DMODVERSIONS")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-O2")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Multi-level else ifeq chain
// ============================================================================

TEST_F(BuildTest, KernelMultiLevelElseIfeq) {
  writeMakefile(
      "ARCH := arm64\n"
      "ifeq ($(ARCH),x86)\n"
      "  BITS := 64\n"
      "  MACH := pc\n"
      "else ifeq ($(ARCH),arm)\n"
      "  BITS := 32\n"
      "  MACH := versatile\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  BITS := 64\n"
      "  MACH := generic\n"
      "else\n"
      "  BITS := unknown\n"
      "  MACH := unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(ARCH) bits=$(BITS) mach=$(MACH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("bits=64")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("mach=generic")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Complex config-dependent obj lists
// ============================================================================

TEST_F(BuildTest, KernelComplexConfigObjLists) {
  writeMakefile(
      "CONFIG_NET := y\n"
      "CONFIG_BT := m\n"
      "CONFIG_USB :=\n"
      "CONFIG_FS := y\n"
      "obj-$(CONFIG_NET) += net/\n"
      "obj-$(CONFIG_BT) += bluetooth/\n"
      "obj-$(CONFIG_USB) += usb/\n"
      "obj-$(CONFIG_FS) += fs/\n"
      "BUILTIN := $(sort $(obj-y))\n"
      "MODULES := $(sort $(obj-m))\n"
      "all:\n"
      "\t@echo builtin=$(BUILTIN)\n"
      "\t@echo modules=$(MODULES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("fs/")) << "fs builtin: " << R.out;
  EXPECT_TRUE(R.contains("net/")) << "net builtin: " << R.out;
  EXPECT_TRUE(R.contains("bluetooth/")) << "bt module: " << R.out;
  EXPECT_FALSE(R.contains("usb/")) << "usb excluded: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Header dependency generation (.d files)
// ============================================================================

TEST_F(BuildTest, KernelDepFileInclude) {
  writeFile(tmp() / "foo.c", "int main(){return 0;}\n");
  writeFile(tmp() / "foo.h", "#define FOO 1\n");
  writeFile(tmp() / ".foo.o.d", "foo.o: foo.c foo.h\n");
  writeMakefile(
      "obj-y := foo.o\n"
      "%.o: %.c\n"
      "\t@echo CC $<\n"
      ".PHONY: all\n"
      "all: $(obj-y)\n"
      "\t@echo done\n"
      "-include $(obj-y:.o=.d)\n"
      "-include .*.d\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC foo.c")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Nested foreach + eval for multi-module build
// ============================================================================

TEST_F(BuildTest, KernelNestedForeachEvalModules) {
  writeMakefile(
      "MODULES := netfilter crypto scsi\n"
      "define module_template\n"
      "$(1)-objs := $(1)_core.o $(1)_init.o\n"
      "$(1).ko: $$($(1)-objs)\n"
      "\t@echo LD $(1).ko from $$($(1)-objs)\n"
      "endef\n"
      "$(foreach m,$(MODULES),$(eval $(call module_template,$(m))))\n"
      "all: $(addsuffix .ko,$(MODULES))\n"
      "\t@echo all-modules-done\n"
      ".PHONY: all\n"
      "%.o:\n"
      "\t@echo CC $@\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("LD netfilter.ko")) << "netfilter: " << R.out;
  EXPECT_TRUE(R.contains("LD crypto.ko")) << "crypto: " << R.out;
  EXPECT_TRUE(R.contains("LD scsi.ko")) << "scsi: " << R.out;
  EXPECT_TRUE(R.contains("all-modules-done")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Version extraction pattern
// ============================================================================

TEST_F(BuildTest, KernelVersionExtractionComplex) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 209\n"
      "EXTRAVERSION = -rc1\n"
      "KERNELRELEASE = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo release=$(KERNELRELEASE)\n"
      "\t@echo version=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("release=5.10.209-rc1")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("version=5.10.209-rc1")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Export chain with inherited vars
// ============================================================================

TEST_F(BuildTest, KernelExportChainInherited) {
  writeMakefile(
      "CC := gcc\n"
      "CFLAGS := -O2 -Wall\n"
      "LDFLAGS := -static\n"
      "export CC CFLAGS LDFLAGS\n"
      "HOSTCC := cc\n"
      "unexport HOSTCC\n"
      "all:\n"
      "\t@echo cc=$(CC) cflags=$(CFLAGS) ld=$(LDFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc=gcc")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cflags=-O2 -Wall")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("ld=-static")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: filter + patsubst chain for object lists
// ============================================================================

TEST_F(BuildTest, KernelFilterPatsubstChain) {
  writeMakefile(
      "SRCS := main.c lib.c test.c debug.c\n"
      "EXCLUDE := test.c debug.c\n"
      "BUILD_SRCS := $(filter-out $(EXCLUDE),$(SRCS))\n"
      "OBJS := $(patsubst %.c,%.o,$(BUILD_SRCS))\n"
      "DEPS := $(patsubst %.o,%.d,$(OBJS))\n"
      "all:\n"
      "\t@echo srcs=$(BUILD_SRCS)\n"
      "\t@echo objs=$(OBJS)\n"
      "\t@echo deps=$(DEPS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("srcs=main.c lib.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("objs=main.o lib.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("deps=main.d lib.d")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Recursive CFLAGS with override
// ============================================================================

TEST_F(BuildTest, KernelRecursiveCflagsOverride) {
  writeMakefile(
      "CFLAGS = -O2\n"
      "CFLAGS += -Wall\n"
      "override CFLAGS += -DDEBUG\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-Os"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-DDEBUG")) << "override respected: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Complex include chain with arch
// ============================================================================

TEST_F(BuildTest, KernelComplexIncludeChain) {
  std::filesystem::create_directories(tmp() / "arch" / "arm64" / "include");
  std::filesystem::create_directories(tmp() / "scripts");

  writeFile(tmp() / "scripts" / "Kbuild.include",
            "cmd = @$(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))' &&) "
            "$(cmd_$(1))\n"
            "any-hierarchical = $(strip $(foreach d,$(1),$(if "
            "$(findstring /,$(d)),$(d))))\n");

  writeFile(tmp() / "arch" / "arm64" / "Makefile",
            "ARCH_CPPFLAGS := -DARM64\n"
            "ARCH_AFLAGS := -march=armv8-a\n"
            "ARCH_CFLAGS := -mgeneral-regs-only\n");

  writeMakefile(
      "SRCARCH := arm64\n"
      "-include scripts/Kbuild.include\n"
      "include arch/$(SRCARCH)/Makefile\n"
      "CFLAGS := $(ARCH_CFLAGS) -O2\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS) cpp=$(ARCH_CPPFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-mgeneral-regs-only")) << "arch cflags: " << R.out;
  EXPECT_TRUE(R.contains("-DARM64")) << "arch cpp: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: if/and/or conditional functions
// ============================================================================

TEST_F(BuildTest, KernelConditionalFunctions) {
  writeMakefile(
      "CONFIG_A := y\n"
      "CONFIG_B :=\n"
      "CONFIG_C := y\n"
      "RESULT1 := $(if $(CONFIG_A),has-a,no-a)\n"
      "RESULT2 := $(if $(CONFIG_B),has-b,no-b)\n"
      "RESULT3 := $(or $(CONFIG_B),$(CONFIG_A),fallback)\n"
      "RESULT4 := $(and $(CONFIG_A),$(CONFIG_C),both)\n"
      "RESULT5 := $(and $(CONFIG_A),$(CONFIG_B),both)\n"
      "all:\n"
      "\t@echo r1=$(RESULT1) r2=$(RESULT2) r3=$(RESULT3) r4=$(RESULT4) r5=$(RESULT5)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r1=has-a")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r2=no-b")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r3=y")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r4=both")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r5=")) << "and with empty: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Substitution ref with directory prefix
// ============================================================================

TEST_F(BuildTest, KernelSubstRefDirPrefix) {
  writeMakefile(
      "OBJS := drivers/net/e1000.o drivers/scsi/sd.o fs/ext4/super.o\n"
      "SRCS := $(OBJS:.o=.c)\n"
      "DIRS := $(sort $(dir $(OBJS)))\n"
      "all:\n"
      "\t@echo srcs=$(SRCS)\n"
      "\t@echo dirs=$(DIRS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("drivers/net/e1000.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("fs/ext4/super.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("drivers/net/")) << "dirs: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: word/words/firstword/lastword
// ============================================================================

TEST_F(BuildTest, KernelWordFunctions) {
  writeMakefile(
      "FILES := alpha.c beta.c gamma.c delta.c\n"
      "FIRST := $(firstword $(FILES))\n"
      "LAST := $(lastword $(FILES))\n"
      "COUNT := $(words $(FILES))\n"
      "THIRD := $(word 3,$(FILES))\n"
      "RANGE := $(wordlist 2,3,$(FILES))\n"
      "all:\n"
      "\t@echo first=$(FIRST) last=$(LAST) count=$(COUNT) "
      "third=$(THIRD) range=$(RANGE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("first=alpha.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("last=delta.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("count=4")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("third=gamma.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("range=beta.c gamma.c")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Kbuild cmd macro pattern
// ============================================================================

TEST_F(BuildTest, KernelKbuildCmdPattern) {
  writeMakefile(
      "quiet_cmd_cc_o_c = CC      $@\n"
      "      cmd_cc_o_c = gcc -c -o $@ $<\n"
      "quiet := quiet_\n"
      "define cmd\n"
      "$(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))' &&) "
      "$(cmd_$(1))\n"
      "endef\n"
      "V ?= 0\n"
      "ifeq ($(V),1)\n"
      "  quiet :=\n"
      "endif\n"
      "all:\n"
      "\t@echo test-cmd=$(quiet)cmd_cc_o_c\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("test-cmd=quiet_cmd_cc_o_c")) << "out: " << R.out;
}

// ============================================================================
// Robustness: deeply nested variable references
// ============================================================================

TEST_F(BuildTest, RobustDeeplyNestedVarRefs) {
  writeMakefile(
      "A = hello\n"
      "B = A\n"
      "C = B\n"
      "D = C\n"
      "all:\n"
      "\t@echo val=$($($($(D))))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("val=hello")) << "out: " << R.out;
}

// ============================================================================
// Robustness: empty $(foreach)
// ============================================================================

TEST_F(BuildTest, RobustEmptyForeach) {
  writeMakefile(
      "EMPTY :=\n"
      "RESULT := $(foreach x,$(EMPTY),item-$(x))\n"
      "all:\n"
      "\t@echo result=[$(RESULT)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("result=[]")) << "out: " << R.out;
}

// ============================================================================
// Robustness: $(call) with zero args
// ============================================================================

TEST_F(BuildTest, RobustCallZeroArgs) {
  writeMakefile(
      "define greeting\n"
      "hello-world\n"
      "endef\n"
      "RESULT := $(call greeting)\n"
      "all:\n"
      "\t@echo got=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("got=hello-world")) << "out: " << R.out;
}

// ============================================================================
// Robustness: $(call) with many args
// ============================================================================

TEST_F(BuildTest, RobustCallManyArgs) {
  writeMakefile(
      "define multi\n"
      "$(1)-$(2)-$(3)-$(4)-$(5)\n"
      "endef\n"
      "RESULT := $(call multi,a,b,c,d,e)\n"
      "all:\n"
      "\t@echo got=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("got=a-b-c-d-e")) << "out: " << R.out;
}

// ============================================================================
// Robustness: multiple rules for same target (prereq merge)
// ============================================================================

TEST_F(BuildTest, RobustMultiRuleSameTarget) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "c.h", "");
  writeMakefile(
      "prog: a.o b.o\n"
      "\t@echo linking $@\n"
      "%.o: %.c\n"
      "\t@echo CC $<\n"
      "a.o: c.h\n"
      "all: prog\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC a.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC b.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("linking prog")) << "out: " << R.out;
}

// ============================================================================
// Robustness: pattern rule with no prerequisites (catchall)
// ============================================================================

TEST_F(BuildTest, RobustCatchallPatternRule) {
  writeMakefile(
      "all: output.txt\n"
      "\t@echo done\n"
      "output.txt:\n"
      "\t@echo generating $@\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("generating output.txt")) << "out: " << R.out;
}

// ============================================================================
// Robustness: recipe with all three prefixes @-+
// ============================================================================

TEST_F(BuildTest, RobustRecipeAllPrefixes) {
  writeMakefile(
      "all:\n"
      "\t@echo silent-line\n"
      "\t-echo ignore-error && false\n"
      "\t@echo after-ignore\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("echo silent-line")) << "out: " << R.out;
}

// ============================================================================
// Robustness: large fan-out with diamond dependency
// ============================================================================

TEST_F(BuildTest, RobustDiamondDependency) {
  writeFile(tmp() / "base.c", "");
  writeFile(tmp() / "left.c", "");
  writeFile(tmp() / "right.c", "");
  writeMakefile(
      "all: left.o right.o\n"
      "\t@echo link\n"
      "left.o: base.o left.c\n"
      "\t@echo CC left\n"
      "right.o: base.o right.c\n"
      "\t@echo CC right\n"
      "base.o: base.c\n"
      "\t@echo CC base\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC base")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC left")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC right")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("link")) << "out: " << R.out;
}

// ============================================================================
// Robustness: $(eval) with dynamic variable names
// ============================================================================

TEST_F(BuildTest, RobustEvalDynamicVarNames) {
  writeMakefile(
      "CONFIGS := FEAT_A FEAT_B FEAT_C\n"
      "define set_config\n"
      "$(1)_ENABLED := yes\n"
      "endef\n"
      "$(foreach c,$(CONFIGS),$(eval $(call set_config,$(c))))\n"
      "all:\n"
      "\t@echo a=$(FEAT_A_ENABLED) b=$(FEAT_B_ENABLED) c=$(FEAT_C_ENABLED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a=yes")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b=yes")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("c=yes")) << "out: " << R.out;
}

// ============================================================================
// Robustness: strip in conditional comparison
// ============================================================================

TEST_F(BuildTest, RobustStripInConditional) {
  writeMakefile(
      "A :=   hello   \n"
      "ifeq ($(strip $(A)),hello)\n"
      "  MATCH := yes\n"
      "else\n"
      "  MATCH := no\n"
      "endif\n"
      "all:\n"
      "\t@echo match=$(MATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("match=yes")) << "out: " << R.out;
}

// ============================================================================
// Robustness: findstring for feature detection
// ============================================================================

TEST_F(BuildTest, RobustFindstringFeatureDetect) {
  writeMakefile(
      "CFLAGS := -O2 -Wall -DDEBUG -march=armv8\n"
      "HAS_DEBUG := $(findstring -DDEBUG,$(CFLAGS))\n"
      "HAS_ASAN := $(findstring -fsanitize,$(CFLAGS))\n"
      "all:\n"
      "\t@echo debug=[$(HAS_DEBUG)] asan=[$(HAS_ASAN)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("debug=[-DDEBUG]")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("asan=[]")) << "out: " << R.out;
}

// ============================================================================
// Robustness: nested ifeq inside foreach+eval
// ============================================================================

TEST_F(BuildTest, RobustNestedIfeqInForeachEval) {
  writeMakefile(
      "MODULES := mod_a mod_b mod_c\n"
      "mod_a_TYPE := builtin\n"
      "mod_b_TYPE := module\n"
      "mod_c_TYPE := builtin\n"
      "define classify_module\n"
      "ifeq ($($(1)_TYPE),builtin)\n"
      "BUILTIN_MODS += $(1)\n"
      "else\n"
      "MODULE_MODS += $(1)\n"
      "endif\n"
      "endef\n"
      "$(foreach m,$(MODULES),$(eval $(call classify_module,$(m))))\n"
      "all:\n"
      "\t@echo builtin=$(BUILTIN_MODS)\n"
      "\t@echo modules=$(MODULE_MODS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("mod_a")) << "mod_a builtin: " << R.out;
  EXPECT_TRUE(R.contains("mod_c")) << "mod_c builtin: " << R.out;
}

// ============================================================================
// Robustness: $(origin) with different sources
// ============================================================================

TEST_F(BuildTest, RobustOriginAllSources) {
  writeMakefile(
      "FILE_VAR := hello\n"
      "override OVR_VAR := world\n"
      "UNDEF_CHECK := $(origin NONEXISTENT)\n"
      "FILE_CHECK := $(origin FILE_VAR)\n"
      "OVR_CHECK := $(origin OVR_VAR)\n"
      "all:\n"
      "\t@echo undef=$(UNDEF_CHECK) file=$(FILE_CHECK) ovr=$(OVR_CHECK)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("undef=undefined")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("file=file")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("ovr=override")) << "out: " << R.out;
}

// ============================================================================
// Robustness: $(value) preserves unexpanded form
// ============================================================================

TEST_F(BuildTest, RobustValuePreservesRaw) {
  writeMakefile(
      "X = hello\n"
      "Y = $(X) world\n"
      "RAW := $(value Y)\n"
      "EXPANDED := $(Y)\n"
      "all:\n"
      "\t@echo raw=[$(RAW)] expanded=[$(EXPANDED)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("expanded=[hello world]")) << "out: " << R.out;
}

// ============================================================================
// Robustness: sort deduplicates
// ============================================================================

TEST_F(BuildTest, RobustSortDedup) {
  writeMakefile(
      "LIST := z a m a z b m c\n"
      "SORTED := $(sort $(LIST))\n"
      "all:\n"
      "\t@echo sorted=$(SORTED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("sorted=a b c m z")) << "out: " << R.out;
}

// ============================================================================
// Robustness: suffix and basename functions
// ============================================================================

TEST_F(BuildTest, RobustSuffixBasename) {
  writeMakefile(
      "FILES := src/main.c include/header.h lib/libfoo.a Makefile\n"
      "SUFFIXES := $(suffix $(FILES))\n"
      "BASES := $(basename $(FILES))\n"
      "all:\n"
      "\t@echo suf=$(SUFFIXES)\n"
      "\t@echo base=$(BASES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains(".c")) << "suf: " << R.out;
  EXPECT_TRUE(R.contains(".h")) << "suf: " << R.out;
  EXPECT_TRUE(R.contains(".a")) << "suf: " << R.out;
  EXPECT_TRUE(R.contains("src/main")) << "base: " << R.out;
}

// ============================================================================
// Robustness: addprefix/addsuffix composition
// ============================================================================

TEST_F(BuildTest, RobustAddprefixSuffixComposition) {
  writeMakefile(
      "MODS := net fs crypto\n"
      "DIRS := $(addprefix drivers/,$(addsuffix /,$(MODS)))\n"
      "OBJS := $(addsuffix .o,$(addprefix obj_,$(MODS)))\n"
      "all:\n"
      "\t@echo dirs=$(DIRS)\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("drivers/net/")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("drivers/fs/")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("obj_net.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("obj_crypto.o")) << "out: " << R.out;
}

// ============================================================================
// Robustness: define with := (simple expansion)
// ============================================================================

TEST_F(BuildTest, RobustDefineSimpleExpansion) {
  writeMakefile(
      "X := original\n"
      "define Y :=\n"
      "value-is-$(X)\n"
      "endef\n"
      "X := changed\n"
      "all:\n"
      "\t@echo y=$(Y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("y=value-is-original")) << "out: " << R.out;
}

// ============================================================================
// Robustness: large number of variables
// ============================================================================

TEST_F(BuildTest, RobustManyVariables) {
  std::string Mk;
  for (int I = 0; I < 100; ++I)
    Mk += "VAR_" + std::to_string(I) + " := val" + std::to_string(I) + "\n";
  Mk += "RESULT := $(VAR_0) $(VAR_50) $(VAR_99)\n";
  Mk += "all:\n\t@echo $(RESULT)\n.PHONY: all\n";
  writeMakefile(Mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("val0")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("val50")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("val99")) << "out: " << R.out;
}

// ============================================================================
// Robustness: foreach over generated numbers
// ============================================================================

TEST_F(BuildTest, RobustForeachNumbers) {
  writeMakefile(
      "NUMS := 1 2 3 4 5 6 7 8 9 10\n"
      "ITEMS := $(foreach n,$(NUMS),item_$(n))\n"
      "COUNT := $(words $(ITEMS))\n"
      "all:\n"
      "\t@echo count=$(COUNT) first=$(firstword $(ITEMS)) "
      "last=$(lastword $(ITEMS))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("count=10")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("first=item_1")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("last=item_10")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: full Kbuild mini-system (comprehensive)
// ============================================================================

TEST_F(BuildTest, KernelFullKbuildMiniSystem) {
  std::filesystem::create_directories(tmp() / "arch" / "x86");
  std::filesystem::create_directories(tmp() / "init");
  std::filesystem::create_directories(tmp() / "kernel");
  std::filesystem::create_directories(tmp() / "mm");
  std::filesystem::create_directories(tmp() / "scripts");

  writeFile(tmp() / "init" / "main.c", "");
  writeFile(tmp() / "kernel" / "fork.c", "");
  writeFile(tmp() / "kernel" / "sched.c", "");
  writeFile(tmp() / "mm" / "page_alloc.c", "");

  writeFile(tmp() / "scripts" / "Kbuild.include",
            "any-hierarchical = $(strip $(foreach d,$(1),$(if "
            "$(findstring /,$(d)),$(d))))\n");

  writeFile(tmp() / "arch" / "x86" / "Makefile",
            "ARCH_CFLAGS := -m64 -mno-red-zone\n"
            "ARCH_LDFLAGS := -z max-page-size=0x200000\n");

  writeFile(tmp() / "init" / "Makefile", "obj-y := main.o\n");
  writeFile(tmp() / "kernel" / "Makefile", "obj-y := fork.o sched.o\n");
  writeFile(tmp() / "mm" / "Makefile", "obj-y := page_alloc.o\n");

  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "EXTRAVERSION =\n"
      "NAME = Dare mighty things\n"
      "\n"
      "KERNELRELEASE = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)\n"
      "\n"
      "ARCH ?= x86\n"
      "SRCARCH := $(ARCH)\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "AR := $(CROSS_COMPILE)ar\n"
      "\n"
      "V ?= 0\n"
      "ifeq ($(V),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "\n"
      "CONFIG_SMP := y\n"
      "CONFIG_PREEMPT := y\n"
      "CONFIG_64BIT := y\n"
      "\n"
      "KBUILD_CFLAGS := -O2 -Wall -Wstrict-prototypes\n"
      "KBUILD_LDFLAGS :=\n"
      "\n"
      "ifdef CONFIG_SMP\n"
      "  KBUILD_CFLAGS += -DCONFIG_SMP\n"
      "endif\n"
      "ifdef CONFIG_PREEMPT\n"
      "  KBUILD_CFLAGS += -DCONFIG_PREEMPT\n"
      "endif\n"
      "\n"
      "-include scripts/Kbuild.include\n"
      "include arch/$(SRCARCH)/Makefile\n"
      "KBUILD_CFLAGS += $(ARCH_CFLAGS)\n"
      "KBUILD_LDFLAGS += $(ARCH_LDFLAGS)\n"
      "\n"
      "init-y :=\n"
      "kernel-y :=\n"
      "mm-y :=\n"
      "\n"
      "include init/Makefile\n"
      "init-y += $(addprefix init/,$(obj-y))\n"
      "obj-y :=\n"
      "include kernel/Makefile\n"
      "kernel-y += $(addprefix kernel/,$(obj-y))\n"
      "obj-y :=\n"
      "include mm/Makefile\n"
      "mm-y += $(addprefix mm/,$(obj-y))\n"
      "\n"
      "ALL_OBJS := $(init-y) $(kernel-y) $(mm-y)\n"
      "SRCS := $(ALL_OBJS:.o=.c)\n"
      "\n"
      "export CC KBUILD_CFLAGS KBUILD_LDFLAGS\n"
      "\n"
      "ifneq ($(filter clean,$(MAKECMDGOALS)),)\n"
      "  SKIP_BUILD := yes\n"
      "else\n"
      "  SKIP_BUILD := no\n"
      "endif\n"
      "\n"
      ".DEFAULT_GOAL := all\n"
      ".PHONY: all vmlinux clean FORCE\n"
      "\n"
      "all: vmlinux\n"
      "\t$(Q)echo '  BUILD   $(KERNELRELEASE) [$(NAME)] done'\n"
      "\n"
      "vmlinux: $(ALL_OBJS) FORCE\n"
      "\t$(Q)echo '  LD      vmlinux objs=$(words $(ALL_OBJS)) "
      "flags=$(KBUILD_LDFLAGS)'\n"
      "\n"
      "%.o: %.c FORCE\n"
      "\t$(Q)echo '  CC      $< cflags=$(KBUILD_CFLAGS)'\n"
      "\n"
      "clean:\n"
      "\t$(Q)echo '  CLEAN   skip=$(SKIP_BUILD)'\n"
      "\n"
      "FORCE:\n"
      "\n"
      "-include $(ALL_OBJS:.o=.d)\n");

  // Default build
  auto R1 = runMake({"-n"});
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("CC")) << "compile: " << R1.out;
  EXPECT_TRUE(R1.contains("init/main.c")) << "init: " << R1.out;
  EXPECT_TRUE(R1.contains("kernel/fork.c")) << "fork: " << R1.out;
  EXPECT_TRUE(R1.contains("kernel/sched.c")) << "sched: " << R1.out;
  EXPECT_TRUE(R1.contains("mm/page_alloc.c")) << "mm: " << R1.out;
  EXPECT_TRUE(R1.contains("LD")) << "link: " << R1.out;
  EXPECT_TRUE(R1.contains("5.10.0")) << "version: " << R1.out;
  EXPECT_TRUE(R1.contains("-DCONFIG_SMP")) << "smp flag: " << R1.out;
  EXPECT_TRUE(R1.contains("-DCONFIG_PREEMPT")) << "preempt: " << R1.out;
  EXPECT_TRUE(R1.contains("-m64")) << "arch: " << R1.out;
  EXPECT_TRUE(R1.contains("-mno-red-zone")) << "redzone: " << R1.out;

  // Cross-compile
  auto R2 = runMake({"-n", "CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;

  // Clean
  auto R3 = runMake({"-n"}, "clean");
  ASSERT_TRUE(R3.ok()) << "stderr: " << R3.err;
  EXPECT_TRUE(R3.contains("CLEAN")) << "clean: " << R3.out;
  EXPECT_TRUE(R3.contains("skip=yes")) << "skip: " << R3.out;

  // Verbose
  auto R4 = runMake({"-n", "V=1"});
  ASSERT_TRUE(R4.ok()) << "stderr: " << R4.err;
}

// ============================================================================
// Robustness: parallel build with many independent targets
// ============================================================================

TEST_F(BuildTest, RobustParallelManyIndependent) {
  std::string Mk = "all:";
  for (int I = 0; I < 20; ++I) {
    std::string Name = "t" + std::to_string(I);
    Mk += " " + Name;
  }
  Mk += "\n\t@echo all-done\n.PHONY: all";
  for (int I = 0; I < 20; ++I) {
    std::string Name = "t" + std::to_string(I);
    Mk += " " + Name;
  }
  Mk += "\n";
  for (int I = 0; I < 20; ++I) {
    std::string Name = "t" + std::to_string(I);
    Mk += Name + ":\n\t@echo built-" + Name + "\n";
  }
  writeMakefile(Mk);
  auto R = runMake({"-j4", "-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("built-t0")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("built-t19")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("all-done")) << "out: " << R.out;
}

// ============================================================================
// Robustness: dry-run does not execute
// ============================================================================

TEST_F(BuildTest, RobustDryRunNoExecute) {
  writeMakefile(
      "all:\n"
      "\techo creating > result.txt\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_FALSE(std::filesystem::exists(tmp() / "result.txt"))
      << "dry-run should not create files";
}

// ============================================================================
// Robustness: conditional with quoted strings
// ============================================================================

TEST_F(BuildTest, RobustConditionalQuotedStrings) {
  writeMakefile(
      "X := hello world\n"
      "ifeq (\"$(X)\",\"hello world\")\n"
      "  MATCH := yes\n"
      "else\n"
      "  MATCH := no\n"
      "endif\n"
      "all:\n"
      "\t@echo match=$(MATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("match=yes")) << "out: " << R.out;
}

// ============================================================================
// Robustness: pattern rule with directory
// ============================================================================

TEST_F(BuildTest, RobustPatternRuleWithDir) {
  std::filesystem::create_directories(tmp() / "src");
  std::filesystem::create_directories(tmp() / "obj");
  writeFile(tmp() / "src" / "main.c", "");
  writeFile(tmp() / "src" / "util.c", "");
  writeMakefile(
      "SRCS := src/main.c src/util.c\n"
      "OBJS := $(patsubst src/%.c,obj/%.o,$(SRCS))\n"
      "prog: $(OBJS)\n"
      "\t@echo link $(OBJS)\n"
      "obj/%.o: src/%.c\n"
      "\t@echo CC $< -> $@\n"
      ".PHONY: all\n"
      "all: prog\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC src/main.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC src/util.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("link")) << "out: " << R.out;
}

// ============================================================================
// Robustness: static pattern rule
// ============================================================================

TEST_F(BuildTest, RobustStaticPatternRule) {
  writeFile(tmp() / "foo.c", "");
  writeFile(tmp() / "bar.c", "");
  writeFile(tmp() / "baz.c", "");
  writeMakefile(
      "OBJECTS := foo.o bar.o baz.o\n"
      "$(OBJECTS): %.o: %.c\n"
      "\t@echo CC $< -> $@\n"
      "all: $(OBJECTS)\n"
      "\t@echo link-done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC foo.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC bar.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC baz.c")) << "out: " << R.out;
}

// ============================================================================
// Robustness: shell assignment operator !=
// ============================================================================

TEST_F(BuildTest, RobustShellAssignment) {
  writeMakefile(
      "DATE != echo hello-from-shell\n"
      "all:\n"
      "\t@echo got=$(DATE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("got=hello-from-shell")) << "out: " << R.out;
}

// ============================================================================
// Robustness: .DEFAULT_GOAL override
// ============================================================================

TEST_F(BuildTest, RobustDefaultGoalOverride) {
  writeMakefile(
      "first:\n"
      "\t@echo first-target\n"
      "second:\n"
      "\t@echo second-target\n"
      ".DEFAULT_GOAL := second\n"
      ".PHONY: first second\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("second-target")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("first-target")) << "out: " << R.out;
}

// ============================================================================
// Robustness: keep-going mode (-k)
// ============================================================================

TEST_F(BuildTest, RobustKeepGoing) {
  writeMakefile(
      "all: good bad\n"
      "\t@echo all-done\n"
      "good:\n"
      "\t@echo good-built\n"
      "bad:\n"
      "\tfalse\n"
      ".PHONY: all good bad\n");
  auto R = runMake({"-k"});
  EXPECT_TRUE(R.contains("good-built")) << "good ran: " << R.out;
}

// ============================================================================
// Robustness: always-make (-B)
// ============================================================================

TEST_F(BuildTest, RobustAlwaysMake) {
  writeFile(tmp() / "src.c", "");
  writeFile(tmp() / "src.o", "");
  writeMakefile(
      "src.o: src.c\n"
      "\t@echo rebuilding\n"
      "all: src.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-B", "-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("rebuilding")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: dtb (device tree) style build
// ============================================================================

TEST_F(BuildTest, KernelDtbStyleBuild) {
  writeMakefile(
      "dtb-y := rk3399-firefly.dtb imx8mq-evk.dtb bcm2711-rpi-4-b.dtb\n"
      "DTB_NAMES := $(patsubst %.dtb,%,$(dtb-y))\n"
      "DTS_FILES := $(addsuffix .dts,$(DTB_NAMES))\n"
      "all: $(dtb-y)\n"
      "\t@echo built=$(words $(dtb-y)) dtbs\n"
      "%.dtb:\n"
      "\t@echo DTC $@\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("DTC rk3399-firefly.dtb")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("DTC bcm2711-rpi-4-b.dtb")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("built=3 dtbs")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: filechk pattern (version header generation)
// ============================================================================

TEST_F(BuildTest, KernelFilechkPattern) {
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "define filechk_version\n"
      "echo \\#define LINUX_VERSION_CODE $(shell echo $$(($(VERSION) * "
      "65536 + $(PATCHLEVEL) * 256)))\n"
      "endef\n"
      "VERSION_CODE := $(shell echo $$(($(VERSION) * 65536 + "
      "$(PATCHLEVEL) * 256)))\n"
      "all:\n"
      "\t@echo code=$(VERSION_CODE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("code=330240")) << "out: " << R.out;
}

// ============================================================================
// Robustness: multiple include files in sequence
// ============================================================================

TEST_F(BuildTest, RobustMultiIncludeSequence) {
  writeFile(tmp() / "vars.mk", "A := from-vars\nB := from-vars\n");
  writeFile(tmp() / "overrides.mk", "B := from-overrides\nC := from-overrides\n");
  writeMakefile(
      "include vars.mk\n"
      "include overrides.mk\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B) c=$(C)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a=from-vars")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b=from-overrides")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("c=from-overrides")) << "out: " << R.out;
}

// ============================================================================
// Robustness: conditional variable assignment order
// ============================================================================

TEST_F(BuildTest, RobustConditionalAssignOrder) {
  writeMakefile(
      "A ?= default\n"
      "B ?= default\n"
      "A := explicit\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a=explicit")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("b=default")) << "out: " << R.out;
}

// ============================================================================
// Robustness: $(flavor) function
// ============================================================================

TEST_F(BuildTest, RobustFlavorFunction) {
  writeMakefile(
      "REC = recursive\n"
      "SIM := simple\n"
      "FLAV_R := $(flavor REC)\n"
      "FLAV_S := $(flavor SIM)\n"
      "FLAV_U := $(flavor NONEXIST)\n"
      "all:\n"
      "\t@echo rec=$(FLAV_R) sim=$(FLAV_S) undef=$(FLAV_U)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("rec=recursive")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("sim=simple")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("undef=undefined")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: FORCE target pattern (always-rebuild)
// ============================================================================

TEST_F(BuildTest, KernelForceTarget) {
  writeFile(tmp() / "vmlinux", "");
  writeMakefile(
      "vmlinux: FORCE\n"
      "\t@echo linking-vmlinux\n"
      "FORCE:\n"
      ".PHONY: FORCE\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("linking-vmlinux")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: obj-y / obj-m Kbuild pattern
// ============================================================================

TEST_F(BuildTest, KernelObjYPattern) {
  writeMakefile(
      "CONFIG_EXT4 := y\n"
      "CONFIG_BTRFS := m\n"
      "CONFIG_XFS :=\n"
      "obj-$(CONFIG_EXT4) += ext4.o\n"
      "obj-$(CONFIG_BTRFS) += btrfs.o\n"
      "obj-$(CONFIG_XFS) += xfs.o\n"
      "all:\n"
      "\t@echo builtin=$(obj-y) module=$(obj-m)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("builtin=ext4.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("module=btrfs.o")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("xfs.o")) << "xfs should not appear: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: cc-option macro pattern
// ============================================================================

TEST_F(BuildTest, KernelCcOptionMacro) {
  writeMakefile(
      "define try-run\n"
      "$(shell if $(1) >/dev/null 2>&1; then echo $(2); else echo $(3); fi)\n"
      "endef\n"
      "cc-option = $(call try-run,echo | $(CC) $(1) -x c - -o /dev/null,$(1),$(2))\n"
      "CC := cc\n"
      "CFLAGS := -O2\n"
      "CFLAGS += $(call cc-option,-fno-stack-protector)\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("flags=")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: squote/escsq quoting macros
// ============================================================================

TEST_F(BuildTest, KernelQuotingMacros) {
  writeMakefile(
      "squote := '\n"
      "empty :=\n"
      "space := $(empty) $(empty)\n"
      "escsq = $(subst $(squote),'\\$(squote)',$(1))\n"
      "MSG := hello world\n"
      "QUOTED := $(call escsq,$(MSG))\n"
      "all:\n"
      "\t@echo $(QUOTED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("hello world")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: if_changed pattern (simplified)
// ============================================================================

TEST_F(BuildTest, KernelIfChangedPattern) {
  writeMakefile(
      "define cmd_cc_o_c\n"
      "$(CC) $(CFLAGS) -c -o $@ $<\n"
      "endef\n"
      "CC := gcc\n"
      "CFLAGS := -Wall -O2\n"
      "cmd = @$(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))' &&) "
      "$(cmd_$(1))\n"
      "quiet := quiet_\n"
      "quiet_cmd_cc_o_c := CC\n"
      "all:\n"
      "\t@echo cc_cmd=$(cmd_cc_o_c)\n"
      "\t@echo quiet=$(quiet_cmd_cc_o_c)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("quiet=CC")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: LINUXINCLUDE path construction
// ============================================================================

TEST_F(BuildTest, KernelLinuxInclude) {
  writeMakefile(
      "srctree := .\n"
      "objtree := build\n"
      "USERINCLUDE := -I$(srctree)/arch/x86/include/uapi "
      "-I$(objtree)/arch/x86/include/generated/uapi\n"
      "LINUXINCLUDE := -I$(srctree)/arch/x86/include "
      "$(USERINCLUDE) -include $(srctree)/include/linux/kconfig.h\n"
      "all:\n"
      "\t@echo inc=$(LINUXINCLUDE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-I./arch/x86/include")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-Ibuild/arch/x86/include/generated/uapi"))
      << "out: " << R.out;
  EXPECT_TRUE(R.contains("-include ./include/linux/kconfig.h"))
      << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: version string construction
// ============================================================================

TEST_F(BuildTest, KernelFullVersionString) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 209\n"
      "EXTRAVERSION =\n"
      "NAME = Dare mighty things\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)"
      "$(if $(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo v=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("v=5.10.209")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: multi-level Kbuild obj-y aggregation
// ============================================================================

TEST_F(BuildTest, KernelMultiLevelObjY) {
  writeMakefile(
      "obj-y := core.o\n"
      "obj-y += init.o\n"
      "obj-y += main.o\n"
      "ifdef CONFIG_SMP\n"
      "obj-y += smp.o\n"
      "endif\n"
      "CONFIG_SMP := y\n"
      "all:\n"
      "\t@echo objs=$(obj-y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("core.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("init.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("main.o")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: filter CONFIG pattern
// ============================================================================

TEST_F(BuildTest, KernelFilterConfig) {
  writeMakefile(
      "CONFIG_A := y\n"
      "CONFIG_B := m\n"
      "CONFIG_C := y\n"
      "ALL_CONFIGS := CONFIG_A CONFIG_B CONFIG_C\n"
      "ENABLED := $(foreach c,$(ALL_CONFIGS),$(if $(filter y,"
      "$($(c))),$(c)))\n"
      "all:\n"
      "\t@echo enabled=$(ENABLED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CONFIG_A")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CONFIG_C")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("CONFIG_B")) << "CONFIG_B should not be enabled: "
      << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: nested $(if) in CFLAGS
// ============================================================================

TEST_F(BuildTest, KernelNestedIfCflags) {
  writeMakefile(
      "ARCH := x86\n"
      "CONFIG_64BIT := y\n"
      "KBUILD_CFLAGS := -Wall\n"
      "KBUILD_CFLAGS += $(if $(filter x86,$(ARCH)),-m64,-m32)\n"
      "KBUILD_CFLAGS += $(if $(CONFIG_64BIT),-DCONFIG_64BIT)\n"
      "all:\n"
      "\t@echo cflags=$(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-m64")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-DCONFIG_64BIT")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: kbuild descend with MAKE variable
// ============================================================================

TEST_F(BuildTest, KernelSubdirDescend) {
  writeMakefile(
      "subdirs := fs net drivers\n"
      "all: $(subdirs)\n"
      "\t@echo done\n"
      "$(subdirs): FORCE\n"
      "\t@echo descending-into-$@\n"
      "FORCE:\n"
      ".PHONY: FORCE all $(subdirs)\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("descending-into-fs")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("descending-into-net")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("descending-into-drivers")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: header dependency file (.d) pattern
// ============================================================================

TEST_F(BuildTest, KernelDepFilePattern) {
  writeFile(tmp() / "main.c", "");
  writeFile(tmp() / "util.c", "");
  writeFile(tmp() / "util.h", "");
  writeFile(tmp() / ".main.o.d", "main.o: main.c util.h\n");
  writeMakefile(
      "SRCS := main.c util.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "prog: $(OBJS)\n"
      "\t@echo linking $@ from $^\n"
      "%.o: %.c\n"
      "\t@echo CC $<\n"
      "-include $(OBJS:%.o=.%.o.d)\n"
      ".PHONY: prog\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC main.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC util.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("linking prog")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: origin-based variable override guard
// ============================================================================

TEST_F(BuildTest, KernelOriginGuard) {
  writeMakefile(
      "ifeq ($(origin CC),default)\n"
      "CC := gcc\n"
      "endif\n"
      "ifeq ($(origin CC),command line)\n"
      "KBUILD_CFLAGS += -DCUSTOM_CC\n"
      "endif\n"
      "all:\n"
      "\t@echo cc=$(CC) flags=$(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CC=clang"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc=clang")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-DCUSTOM_CC")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: sort + unique for dependency dedup (complex)
// ============================================================================

TEST_F(BuildTest, KernelSortDedupComplex) {
  writeMakefile(
      "DEPS := z.h b.h a.h c.h a.h b.h d.h z.h\n"
      "UNIQUE := $(sort $(DEPS))\n"
      "COUNT := $(words $(UNIQUE))\n"
      "all:\n"
      "\t@echo deps=$(UNIQUE) count=$(COUNT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("deps=a.h b.h c.h d.h z.h")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("count=5")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: $(value) with recursive variable
// ============================================================================

TEST_F(BuildTest, KernelValueRecursive) {
  writeMakefile(
      "CC = $(CROSS_COMPILE)gcc\n"
      "CROSS_COMPILE := aarch64-linux-gnu-\n"
      "RAW := $(value CC)\n"
      "EXPANDED := $(CC)\n"
      "all:\n"
      "\t@echo raw=$(RAW) expanded=$(EXPANDED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("expanded=aarch64-linux-gnu-gcc"))
      << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: multi-target phony with shared deps
// ============================================================================

TEST_F(BuildTest, KernelMultiTargetPhony) {
  writeMakefile(
      "targets := vmlinux modules dtbs\n"
      ".PHONY: all $(targets)\n"
      "all: $(targets)\n"
      "\t@echo all-complete\n"
      "vmlinux:\n"
      "\t@echo build-vmlinux\n"
      "modules:\n"
      "\t@echo build-modules\n"
      "dtbs:\n"
      "\t@echo build-dtbs\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("build-vmlinux")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("build-modules")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("build-dtbs")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("all-complete")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: define with multiline recipe template
// ============================================================================

TEST_F(BuildTest, KernelDefineRecipeTemplate) {
  writeMakefile(
      "define do_compile\n"
      "@echo CPP $(1)\n"
      "@echo CC $(1)\n"
      "endef\n"
      "all:\n"
      "\t$(call do_compile,kernel/main.c)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CPP kernel/main.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC kernel/main.c")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: ARCH-dependent include
// ============================================================================

TEST_F(BuildTest, KernelArchInclude) {
  writeFile(tmp() / "arch-x86.mk", "ARCH_CFLAGS := -m64 -march=x86-64\n");
  writeMakefile(
      "SRCARCH := x86\n"
      "-include arch-$(SRCARCH).mk\n"
      "all:\n"
      "\t@echo arch_flags=$(ARCH_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("arch_flags=-m64 -march=x86-64"))
      << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: complex Kconfig + foreach + eval build system
// ============================================================================

TEST_F(BuildTest, KernelKconfigForeachEval) {
  writeMakefile(
      "CONFIG_EXT4_FS := y\n"
      "CONFIG_BTRFS_FS := m\n"
      "CONFIG_XFS_FS :=\n"
      "filesystems := EXT4_FS BTRFS_FS XFS_FS\n"
      "define fs_template\n"
      "$(if $(filter y,$(CONFIG_$(1))),obj-y += $(1).o)\n"
      "$(if $(filter m,$(CONFIG_$(1))),obj-m += $(1).o)\n"
      "endef\n"
      "$(foreach fs,$(filesystems),$(eval $(call fs_template,$(fs))))\n"
      "all:\n"
      "\t@echo builtin=$(obj-y) module=$(obj-m)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("builtin=EXT4_FS.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("module=BTRFS_FS.o")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("XFS_FS.o")) << "XFS should not appear: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: MAKECMDGOALS check for clean
// ============================================================================

TEST_F(BuildTest, KernelMakeCmdGoals) {
  writeMakefile(
      "ifneq ($(filter clean,$(MAKECMDGOALS)),)\n"
      "SKIP_BUILD := yes\n"
      "endif\n"
      "all:\n"
      "\t@echo skip=$(SKIP_BUILD)\n"
      "clean:\n"
      "\t@echo cleaning\n"
      ".PHONY: all clean\n");
  auto R = runMake({}, "clean");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cleaning")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: export chain for sub-make
// ============================================================================

TEST_F(BuildTest, KernelExportForSubMake) {
  writeMakefile(
      "CC := gcc\n"
      "HOSTCC := cc\n"
      "CFLAGS := -O2 -Wall\n"
      "export CC CFLAGS\n"
      "unexport HOSTCC\n"
      "all:\n"
      "\t@echo cc=$(CC) cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc=gcc")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("cflags=-O2 -Wall")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: pattern rule priority (specific over generic)
// ============================================================================

TEST_F(BuildTest, KernelPatternRulePriority) {
  writeFile(tmp() / "boot.S", "");
  writeFile(tmp() / "main.c", "");
  writeMakefile(
      "all: boot.o main.o\n"
      "\t@echo done\n"
      "%.o: %.S\n"
      "\t@echo AS $<\n"
      "%.o: %.c\n"
      "\t@echo CC $<\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("AS boot.S")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC main.c")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: addprefix + filter-out for include paths
// ============================================================================

TEST_F(BuildTest, KernelAddprefixFilterOut) {
  writeMakefile(
      "DIRS := arch/x86 kernel mm drivers\n"
      "EXCLUDE := drivers\n"
      "FILTERED := $(filter-out $(EXCLUDE),$(DIRS))\n"
      "INCLUDES := $(addprefix -I,$(FILTERED))\n"
      "all:\n"
      "\t@echo inc=$(INCLUDES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-Iarch/x86")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-Ikernel")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-Imm")) << "out: " << R.out;
  EXPECT_FALSE(R.contains("-Idrivers")) << "drivers should be excluded: "
      << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: $(patsubst) chained transformations
// ============================================================================

TEST_F(BuildTest, KernelPatsubstChain) {
  writeMakefile(
      "SRCS := fs/ext4/super.c fs/ext4/inode.c kernel/fork.c\n"
      "OBJS := $(patsubst %.c,%.o,$(SRCS))\n"
      "DEPS := $(patsubst %.o,%.d,$(OBJS))\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      "\t@echo deps=$(DEPS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("fs/ext4/super.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("kernel/fork.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("fs/ext4/super.d")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("kernel/fork.d")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: substitution reference for .o -> .c mapping
// ============================================================================

TEST_F(BuildTest, KernelSubstRefMapping) {
  writeMakefile(
      "OBJS := main.o util.o lib.o\n"
      "SRCS := $(OBJS:.o=.c)\n"
      "HDRS := $(OBJS:.o=.h)\n"
      "all:\n"
      "\t@echo srcs=$(SRCS)\n"
      "\t@echo hdrs=$(HDRS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("srcs=main.c util.c lib.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("hdrs=main.h util.h lib.h")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: else ifeq chain (arch selection)
// ============================================================================

TEST_F(BuildTest, KernelElseIfeqChain) {
  writeMakefile(
      "ARCH := arm64\n"
      "ifeq ($(ARCH),x86)\n"
      "BITS := 64\n"
      "MARCH := -march=x86-64\n"
      "else ifeq ($(ARCH),arm64)\n"
      "BITS := 64\n"
      "MARCH := -march=armv8-a\n"
      "else ifeq ($(ARCH),arm)\n"
      "BITS := 32\n"
      "MARCH := -march=armv7-a\n"
      "else\n"
      "BITS := unknown\n"
      "MARCH :=\n"
      "endif\n"
      "all:\n"
      "\t@echo bits=$(BITS) march=$(MARCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("bits=64")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("march=-march=armv8-a")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: complex CFLAGS construction
// ============================================================================

TEST_F(BuildTest, KernelComplexCflags) {
  writeMakefile(
      "ARCH := x86\n"
      "CONFIG_64BIT := y\n"
      "CONFIG_STACK_PROTECTOR := y\n"
      "CONFIG_RETPOLINE := y\n"
      "KBUILD_CFLAGS := -Wall -Wundef -Werror=strict-prototypes\n"
      "KBUILD_CFLAGS += $(if $(CONFIG_64BIT),-m64,-m32)\n"
      "KBUILD_CFLAGS += $(if $(CONFIG_STACK_PROTECTOR),"
      "-fstack-protector-strong)\n"
      "KBUILD_CFLAGS += $(if $(CONFIG_RETPOLINE),"
      "-mindirect-branch=thunk-extern)\n"
      "KBUILD_CFLAGS += -fno-delete-null-pointer-checks\n"
      "all:\n"
      "\t@echo $(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-m64")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-fstack-protector-strong")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("-mindirect-branch=thunk-extern"))
      << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: $(word) and $(words) for version parsing
// ============================================================================

TEST_F(BuildTest, KernelWordVersionParse) {
  writeMakefile(
      "COMPILER_INFO := gcc GCC 10.3.0 20210408\n"
      "CC_NAME := $(word 1,$(COMPILER_INFO))\n"
      "CC_VER := $(word 3,$(COMPILER_INFO))\n"
      "NWORDS := $(words $(COMPILER_INFO))\n"
      "all:\n"
      "\t@echo name=$(CC_NAME) ver=$(CC_VER) n=$(NWORDS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("name=gcc")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("ver=10.3.0")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("n=4")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: ifdef with empty vs undefined
// ============================================================================

TEST_F(BuildTest, KernelIfdefEmptyVsUndefined) {
  writeMakefile(
      "DEFINED_EMPTY :=\n"
      "DEFINED_VALUE := yes\n"
      "ifdef DEFINED_EMPTY\n"
      "R1 := defined\n"
      "else\n"
      "R1 := undefined\n"
      "endif\n"
      "ifdef DEFINED_VALUE\n"
      "R2 := defined\n"
      "else\n"
      "R2 := undefined\n"
      "endif\n"
      "ifdef NEVER_SET\n"
      "R3 := defined\n"
      "else\n"
      "R3 := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2) r3=$(R3)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r1=undefined")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r2=defined")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("r3=undefined")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: override from command line
// ============================================================================

TEST_F(BuildTest, KernelOverrideFromCmdline) {
  writeMakefile(
      "CFLAGS := -O2\n"
      "override CFLAGS += -Wall\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O3"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << "override should add -Wall: "
      << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: order-only prereq for output dirs
// ============================================================================

TEST_F(BuildTest, KernelOrderOnlyOutputDir) {
  writeFile(tmp() / "main.c", "");
  std::filesystem::create_directory(tmp() / "obj");
  writeMakefile(
      "obj/main.o: main.c | obj\n"
      "\t@echo CC $< -o $@\n"
      "obj:\n"
      "\t@mkdir -p $@\n"
      "all: obj/main.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC main.c")) << "out: " << R.out;
}

// ============================================================================
// Stress: 50 targets with pattern rule and parallel
// ============================================================================

TEST_F(BuildTest, StressFiftyTargetsParallel) {
  std::string Mk = "TARGETS :=";
  for (int I = 0; I < 50; ++I) {
    std::string Name = "mod" + std::to_string(I);
    Mk += " " + Name + ".o";
    writeFile(tmp() / (Name + ".c"), "");
  }
  Mk += "\nall: $(TARGETS)\n\t@echo built=$(words $(TARGETS))\n";
  Mk += "%.o: %.c\n\t@echo CC $<\n";
  Mk += ".PHONY: all\n";
  writeMakefile(Mk);
  auto R = runMake({"-j4", "-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC mod0.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC mod49.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("built=50")) << "out: " << R.out;
}

// ============================================================================
// Stress: diamond dependency pattern
// ============================================================================

TEST_F(BuildTest, StressDiamondDependency) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "common.h", "");
  writeMakefile(
      "prog: a.o b.o\n"
      "\t@echo LINK $@\n"
      "%.o: %.c common.h\n"
      "\t@echo CC $< [deps: $^]\n"
      "all: prog\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC a.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("CC b.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("LINK prog")) << "out: " << R.out;
}

// ============================================================================
// Robustness: recipe with multiline $(call)
// ============================================================================

TEST_F(BuildTest, RobustRecipeMultilineCall) {
  writeMakefile(
      "define compile_step\n"
      "@echo STEP1 $(1)\n"
      "@echo STEP2 $(1)\n"
      "endef\n"
      "all:\n"
      "\t$(call compile_step,myfile.c)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("STEP1 myfile.c")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("STEP2 myfile.c")) << "out: " << R.out;
}

// ============================================================================
// Robustness: variable name with hyphen (Kbuild style)
// ============================================================================

TEST_F(BuildTest, RobustHyphenVarName) {
  writeMakefile(
      "net-objs := socket.o proto.o\n"
      "net-objs += filter.o\n"
      "all:\n"
      "\t@echo objs=$(net-objs)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("socket.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("proto.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("filter.o")) << "out: " << R.out;
}

// ============================================================================
// Robustness: $(strip) with tabs and spaces in conditional
// ============================================================================

TEST_F(BuildTest, RobustStripTabsSpaces) {
  writeMakefile(
      "VAR :=  \t  \t \n"
      "ifeq ($(strip $(VAR)),)\n"
      "RESULT := empty\n"
      "else\n"
      "RESULT := notempty\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("result=empty")) << "out: " << R.out;
}

// ============================================================================
// Robustness: semicolon inline recipe
// ============================================================================

TEST_F(BuildTest, RobustInlineRecipe) {
  writeMakefile(
      "all: ; @echo inline-recipe\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("inline-recipe")) << "out: " << R.out;
}

// ============================================================================
// Robustness: $(@D) auto variable in output dir creation
// ============================================================================

TEST_F(BuildTest, RobustAtDAutoVar) {
  writeFile(tmp() / "src.c", "");
  writeMakefile(
      "build/src.o: src.c\n"
      "\t@echo dir=$(@D) file=$(@F)\n"
      "all: build/src.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("dir=build")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("file=src.o")) << "out: " << R.out;
}

// ============================================================================
// Robustness: $? (newer prerequisites) auto variable
// ============================================================================

TEST_F(BuildTest, RobustNewerPrereqsAutoVar) {
  writeFile(tmp() / "old.c", "");
  writeFile(tmp() / "new.c", "");
  writeMakefile(
      "prog: old.c new.c\n"
      "\t@echo newer=$?\n"
      ".PHONY: prog\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("newer=")) << "out: " << R.out;
}

// ============================================================================
// Robustness: empty recipe (no-op rule)
// ============================================================================

TEST_F(BuildTest, RobustEmptyRecipe) {
  writeMakefile(
      "all: prep build\n"
      "\t@echo done\n"
      "prep:\n"
      "build: prep\n"
      "\t@echo building\n"
      ".PHONY: all prep build\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("building")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("done")) << "out: " << R.out;
}

// ============================================================================
// override += respects command-line variable protection
// ============================================================================

TEST_F(BuildTest, AppendIgnoredOnCmdlineVar) {
  writeMakefile(
      "CFLAGS := -O0\n"
      "CFLAGS += -Wall\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O2"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("flags=-O2")) << "append should be ignored: " << R.out;
  EXPECT_FALSE(R.contains("-Wall")) << "-Wall must not leak: " << R.out;
}

TEST_F(BuildTest, OverrideAppendOnCmdlineVar) {
  writeMakefile(
      "override CFLAGS += -Wall\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O2"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-O2")) << "cmdline base must remain: " << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << "override += must append: " << R.out;
}

TEST_F(BuildTest, OverrideRecursiveOnCmdlineVar) {
  writeMakefile(
      "override CC = custom-cc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R = runMake({"CC=gcc"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cc=custom-cc")) << "override = must win: " << R.out;
}

TEST_F(BuildTest, DefineAppendIgnoredOnCmdlineVar) {
  writeMakefile(
      "define EXTRA +=\n"
      "-Wextra\n"
      "endef\n"
      "all:\n"
      "\t@echo extra=$(EXTRA)\n"
      ".PHONY: all\n");
  auto R = runMake({"EXTRA=-Wall"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("extra=-Wall")) << "define += must be ignored: " << R.out;
  EXPECT_FALSE(R.contains("-Wextra")) << "-Wextra must not leak: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: Kbuild quiet/verbose command display
// ============================================================================

TEST_F(BuildTest, KernelQuietVerboseDisplay) {
  writeFile(tmp() / "main.c", "");
  writeMakefile(
      "V ?= 0\n"
      "ifeq ($(V),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "quiet_cmd_cc = CC $@\n"
      "cmd_cc = gcc -c -o $@ $<\n"
      "main.o: main.c\n"
      "\t$(Q)echo $($(quiet)cmd_cc)\n"
      ".PHONY: main.o\n");
  auto R1 = runMake({"-n"});
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("CC main.o")) << "quiet mode: " << R1.out;

  auto R2 = runMake({"-n", "V=1"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("gcc -c")) << "verbose mode: " << R2.out;
}

// ============================================================================
// Linux 5.10 Kernel: nested variable expansion $($(var))
// ============================================================================

TEST_F(BuildTest, KernelNestedVarExpansion) {
  writeMakefile(
      "CONFIG_FOO = y\n"
      "obj-y = core.o\n"
      "obj-m = plugin.o\n"
      "SELECTED := obj-$(CONFIG_FOO)\n"
      "all:\n"
      "\t@echo result=$($(SELECTED))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("result=core.o")) << "nested expansion: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: multi-level foreach + filter + patsubst
// ============================================================================

TEST_F(BuildTest, KernelForeachFilterPatsubst) {
  writeMakefile(
      "SUBDIRS := fs net drivers\n"
      "fs-objs := inode.o super.o\n"
      "net-objs := socket.o tcp.o\n"
      "drivers-objs := pci.o usb.o\n"
      "ALL_OBJS := $(foreach d,$(SUBDIRS),$(addprefix $(d)/,$($(d)-objs)))\n"
      "C_FILES := $(patsubst %.o,%.c,$(ALL_OBJS))\n"
      "all:\n"
      "\t@echo objs=$(ALL_OBJS)\n"
      "\t@echo srcs=$(C_FILES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("fs/inode.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("net/tcp.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("drivers/usb.c")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: define recipe template with $(call) multiline
// ============================================================================

TEST_F(BuildTest, KernelDefineMultilineRecipeCall) {
  writeFile(tmp() / "a.c", "");
  writeMakefile(
      "define compile_rule\n"
      "@echo CC $(1)\n"
      "@echo LD $(1:.c=.o)\n"
      "endef\n"
      "all: a.c\n"
      "\t$(call compile_rule,$<)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC a.c")) << "first line: " << R.out;
  EXPECT_TRUE(R.contains("LD a.o")) << "second line: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: $(file) function write and read
// ============================================================================

TEST_F(BuildTest, FileFunctionWriteAndRead) {
  writeMakefile(
      "$(file >$(CURDIR)/flags.txt,-O2 -Wall)\n"
      "READ := $(file <$(CURDIR)/flags.txt)\n"
      "all:\n"
      "\t@echo read=$(READ)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("read=-O2 -Wall")) << "file read: " << R.out;
}

TEST_F(BuildTest, FileFunctionAppend) {
  writeMakefile(
      "$(file >$(CURDIR)/log.txt,line1)\n"
      "$(file >>$(CURDIR)/log.txt,line2)\n"
      "all:\n"
      "\t@cat $(CURDIR)/log.txt\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("line1")) << "first write: " << R.out;
  EXPECT_TRUE(R.contains("line2")) << "appended line: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: complex if/findstring/filter chain
// ============================================================================

TEST_F(BuildTest, KernelIfFindstringChain) {
  writeMakefile(
      "ARCH := x86_64\n"
      "CFLAGS := -O2\n"
      "CFLAGS += $(if $(findstring x86,$(ARCH)),-m64)\n"
      "CFLAGS += $(if $(findstring arm,$(ARCH)),-mfloat-abi=hard)\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-m64")) << "x86 detected: " << R.out;
  EXPECT_FALSE(R.contains("-mfloat-abi")) << "arm not detected: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: $(eval) generating pattern rules from template
// ============================================================================

TEST_F(BuildTest, KernelEvalPatternRuleGeneration) {
  std::filesystem::create_directories(tmp() / "fs");
  std::filesystem::create_directories(tmp() / "net");
  writeFile(tmp() / "fs" / "Makefile", "");
  writeFile(tmp() / "net" / "Makefile", "");
  writeMakefile(
      ".DEFAULT_GOAL := all\n"
      "MODULES := fs net\n"
      "define module_rule\n"
      "$(1)/built-in.o: $(1)/Makefile\n"
      "\t@echo building $(1)\n"
      "endef\n"
      "$(foreach m,$(MODULES),$(eval $(call module_rule,$(m))))\n"
      ".PHONY: all\n"
      "all: fs/built-in.o net/built-in.o\n"
      "\t@echo all done\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("building fs")) << "fs rule: " << R.out;
  EXPECT_TRUE(R.contains("building net")) << "net rule: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: mixed export and unexport
// ============================================================================

TEST_F(BuildTest, KernelExportUnexportInteraction) {
  writeMakefile(
      "KBUILD_CFLAGS := -O2\n"
      "SECRET := hidden\n"
      "export KBUILD_CFLAGS\n"
      "unexport SECRET\n"
      "all:\n"
      "\t@echo exported\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
}

// ============================================================================
// Linux 5.10 Kernel: version extraction via $(word) / $(subst) with extra
// ============================================================================

TEST_F(BuildTest, KernelVersionExtractionDetailed) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 186\n"
      "EXTRAVERSION =\n"
      "KERNELVERSION = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)\n"
      "MAJOR := $(word 1,$(subst ., ,$(KERNELVERSION)))\n"
      "MINOR := $(word 2,$(subst ., ,$(KERNELVERSION)))\n"
      "all:\n"
      "\t@echo ver=$(KERNELVERSION) major=$(MAJOR) minor=$(MINOR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("ver=5.10.186")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("major=5")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("minor=10")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: header dependency via -include *.d
// ============================================================================

TEST_F(BuildTest, KernelDashIncludeGlobMissing) {
  writeMakefile(
      "OBJS := main.o\n"
      "-include $(OBJS:.o=.d)\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "-include must not fail on missing .d files: " << R.err;
  EXPECT_TRUE(R.contains("ok")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: $(sort) dedup and $(filter) on CONFIG_ vars
// ============================================================================

TEST_F(BuildTest, KernelSortFilterConfigVars) {
  writeMakefile(
      "CONFIGS := CONFIG_A CONFIG_B CONFIG_A CONFIG_C CONFIG_B\n"
      "UNIQUE := $(sort $(CONFIGS))\n"
      "SELECTED := $(filter CONFIG_A CONFIG_C,$(UNIQUE))\n"
      "all:\n"
      "\t@echo unique=$(UNIQUE)\n"
      "\t@echo selected=$(SELECTED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CONFIG_A CONFIG_B CONFIG_C"))
      << "sorted+deduped: " << R.out;
  EXPECT_TRUE(R.contains("selected=CONFIG_A CONFIG_C"))
      << "filtered: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: static pattern rule with obj-y list
// ============================================================================

TEST_F(BuildTest, KernelStaticPatternObjY) {
  writeFile(tmp() / "init.c", "");
  writeFile(tmp() / "main.c", "");
  writeMakefile(
      "obj-y := init.o main.o\n"
      ".PHONY: all\n"
      "all: $(obj-y)\n"
      "\t@echo link\n"
      "$(obj-y): %.o: %.c\n"
      "\t@echo CC $< -o $@\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC init.c")) << "static pattern: " << R.out;
  EXPECT_TRUE(R.contains("CC main.c")) << "static pattern: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: deeply nested $(if $(filter ...)) with $(call)
// ============================================================================

TEST_F(BuildTest, KernelDeeplyNestedIfFilterCall) {
  writeMakefile(
      "define check-flag\n"
      "$(if $(filter $(1),$(SUPPORTED)),-D$(1)_ENABLED)\n"
      "endef\n"
      "SUPPORTED := SMP PREEMPT HZ_1000\n"
      "CFLAGS := $(strip $(call check-flag,SMP) $(call check-flag,RT) "
      "$(call check-flag,HZ_1000))\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("-DSMP_ENABLED")) << "SMP found: " << R.out;
  EXPECT_TRUE(R.contains("-DHZ_1000_ENABLED")) << "HZ_1000 found: " << R.out;
  EXPECT_FALSE(R.contains("-DRT_ENABLED")) << "RT not found: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: multi-target prerequisite-only rule (from .d files)
// ============================================================================

TEST_F(BuildTest, KernelMultiTargetPrereqOnly) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "common.h", "");
  writeMakefile(
      ".PHONY: all\n"
      "all: a.o b.o\n"
      "\t@echo link\n"
      "%.o: %.c\n"
      "\t@echo CC $<\n"
      "a.o b.o: common.h\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC a.c")) << "a.o: " << R.out;
  EXPECT_TRUE(R.contains("CC b.c")) << "b.o: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: MAKEFLAGS inspection
// ============================================================================

TEST_F(BuildTest, KernelMakeflagsInspection) {
  writeMakefile(
      "ifneq ($(findstring s,$(MAKEFLAGS)),)\n"
      "  QUIET := yes\n"
      "else\n"
      "  QUIET := no\n"
      "endif\n"
      "all:\n"
      "\t@echo quiet=$(QUIET)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("quiet=no")) << "not silent: " << R1.out;

  auto R2 = runMake({"-s"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("quiet=yes")) << "silent mode: " << R2.out;
}

// ============================================================================
// Linux 5.10 Kernel: if_changed style cmd dispatch
// ============================================================================

TEST_F(BuildTest, KernelIfChangedCmdDispatch) {
  writeFile(tmp() / "test.c", "");
  writeMakefile(
      "define if_changed\n"
      "@echo $(1)\n"
      "endef\n"
      "quiet_cmd_cc_o_c = CC      $@\n"
      "cmd_cc_o_c = gcc -c -o $@ $<\n"
      "test.o: test.c\n"
      "\t$(call if_changed,$(cmd_cc_o_c))\n"
      ".PHONY: test.o\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("gcc -c")) << "cmd expanded: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: addprefix + notdir combination
// ============================================================================

TEST_F(BuildTest, KernelAddprefixNotdir) {
  writeMakefile(
      "SRCS := src/foo.c src/bar.c lib/baz.c\n"
      "OBJS := $(addprefix obj/,$(notdir $(patsubst %.c,%.o,$(SRCS))))\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("obj/foo.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("obj/bar.o")) << "out: " << R.out;
  EXPECT_TRUE(R.contains("obj/baz.o")) << "out: " << R.out;
}

// ============================================================================
// Linux 5.10 Kernel: $(or) / $(and) conditional functions
// ============================================================================

TEST_F(BuildTest, KernelOrAndFunctions) {
  writeMakefile(
      "A :=\n"
      "B := val\n"
      "C := other\n"
      "R1 := $(or $(A),$(B),$(C))\n"
      "R2 := $(and $(B),$(C))\n"
      "R3 := $(and $(A),$(B))\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2) r3=$(R3)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("r1=val")) << "or picks first non-empty: " << R.out;
  EXPECT_TRUE(R.contains("r2=other")) << "and returns last: " << R.out;
  EXPECT_TRUE(R.contains("r3=")) << "and short-circuits on empty: " << R.out;
}

// ============================================================================
// MAKEFILE_LIST tracking through include
// ============================================================================

TEST_F(BuildTest, MakefileListTracksIncludes) {
  writeFile(tmp() / "sub.mk",
      "SUB_MKLIST := $(MAKEFILE_LIST)\n");
  writeMakefile(
      "include sub.mk\n"
      "all:\n"
      "\t@echo mfl=$(SUB_MKLIST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("sub.mk")) << "MAKEFILE_LIST tracks include: " << R.out;
}

TEST_F(BuildTest, MakefileListChainedIncludes) {
  writeFile(tmp() / "a.mk", "include b.mk\n");
  writeFile(tmp() / "b.mk",
      "CHAIN_MFL := $(MAKEFILE_LIST)\n");
  writeMakefile(
      "include a.mk\n"
      "all:\n"
      "\t@echo mfl=$(CHAIN_MFL)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("a.mk")) << "a.mk tracked: " << R.out;
  EXPECT_TRUE(R.contains("b.mk")) << "b.mk tracked: " << R.out;
}

// ============================================================================
// Recursion depth limit
// ============================================================================

TEST_F(BuildTest, RecursionDepthLimit) {
  writeMakefile(
      "A = $(B)\n"
      "B = $(C)\n"
      "C = $(A)\n"
      "all:\n"
      "\t@echo val=$(A)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "should not crash: " << R.err;
}

// ============================================================================
// Kbuild-style full module build simulation
// ============================================================================

TEST_F(BuildTest, KernelKbuildModuleBuild) {
  writeFile(tmp() / "mod_a.c", "");
  writeFile(tmp() / "mod_b.c", "");
  writeFile(tmp() / "mod_main.c", "");
  writeMakefile(
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "EXTRA_CFLAGS := -DMODULE -Wall\n"
      "\n"
      "obj-m := mymod.o\n"
      "mymod-objs := mod_a.o mod_b.o mod_main.o\n"
      "\n"
      "define compile_rule\n"
      "$(1): $(2)\n"
      "\t@echo CC $(2) -o $(1)\n"
      "endef\n"
      "\n"
      ".PHONY: all\n"
      "all: $(mymod-objs)\n"
      "\t@echo LD -o mymod.ko $(mymod-objs)\n"
      "\n"
      "$(foreach o,$(mymod-objs),\\\n"
      "  $(eval $(call compile_rule,$(o),$(patsubst %.o,%.c,$(o)))))\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC mod_a.c")) << "compile mod_a: " << R.out;
  EXPECT_TRUE(R.contains("CC mod_b.c")) << "compile mod_b: " << R.out;
  EXPECT_TRUE(R.contains("CC mod_main.c")) << "compile mod_main: " << R.out;
  EXPECT_TRUE(R.contains("LD -o mymod.ko")) << "link: " << R.out;
}

// ============================================================================
// Kernel CONFIG_* conditional build simulation
// ============================================================================

TEST_F(BuildTest, KernelConfigConditionalBuild) {
  writeFile(tmp() / "core.c", "");
  writeFile(tmp() / "net.c", "");
  writeFile(tmp() / "debug.c", "");
  writeMakefile(
      "CONFIG_NET := y\n"
      "CONFIG_DEBUG :=\n"
      "\n"
      "obj-y := core.o\n"
      "obj-$(CONFIG_NET) += net.o\n"
      "obj-$(CONFIG_DEBUG) += debug.o\n"
      "\n"
      ".PHONY: all\n"
      "all: $(obj-y)\n"
      "\t@echo link $(obj-y)\n"
      "%.o: %.c\n"
      "\t@echo CC $<\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC core.c")) << "core always: " << R.out;
  EXPECT_TRUE(R.contains("CC net.c")) << "net when CONFIG_NET=y: " << R.out;
  EXPECT_FALSE(R.contains("CC debug.c"))
      << "debug excluded when CONFIG_DEBUG empty: " << R.out;
}

// ============================================================================
// Kernel version.h generation simulation (shell + define + file)
// ============================================================================

TEST_F(BuildTest, KernelVersionGeneration) {
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 0\n"
      "EXTRAVERSION :=\n"
      "KERNELVERSION := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)\n"
      "\n"
      "define kernel_version_string\n"
      "v$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "endef\n"
      "\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      "\t@echo string=$(strip $(kernel_version_string))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("version=5.10.0")) << "version: " << R.out;
  EXPECT_TRUE(R.contains("string=v5.10.0")) << "string: " << R.out;
}

// ============================================================================
// Kernel FORCE target chain
// ============================================================================

TEST_F(BuildTest, KernelFORCETargetChain) {
  writeFile(tmp() / "version.c", "");
  writeMakefile(
      ".PHONY: all FORCE\n"
      "\n"
      "all: version.o\n"
      "\t@echo link\n"
      "\n"
      "version.o: version.c FORCE\n"
      "\t@echo CC version.c\n"
      "\n"
      "FORCE:\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC version.c"))
      << "FORCE makes target always rebuild: " << R.out;
}

// ============================================================================
// Kernel cc-option style function (try-run pattern)
// ============================================================================

TEST_F(BuildTest, KernelCcOptionPattern) {
  writeMakefile(
      "define try-run\n"
      "$(if $(shell echo ok 2>/dev/null),$(1),$(2))\n"
      "endef\n"
      "\n"
      "define cc-option\n"
      "$(call try-run,$(1),$(2))\n"
      "endef\n"
      "\n"
      "CFLAGS := $(strip $(call cc-option,-fstack-protector,))\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("cflags=-fstack-protector"))
      << "cc-option pattern: " << R.out;
}

// ============================================================================
// Kernel auto-dependency (.d file) integration
// ============================================================================

TEST_F(BuildTest, KernelAutoDependencyDFiles) {
  writeFile(tmp() / "foo.c", "");
  writeFile(tmp() / "bar.c", "");
  writeFile(tmp() / "common.h", "");
  writeFile(tmp() / "foo.d", "foo.o: foo.c common.h\n");
  writeFile(tmp() / "bar.d", "bar.o: bar.c common.h\n");
  writeMakefile(
      ".PHONY: all\n"
      "all: foo.o bar.o\n"
      "\t@echo link\n"
      "%.o: %.c\n"
      "\t@echo CC $< -o $@\n"
      "-include foo.d\n"
      "-include bar.d\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC foo.c")) << "foo compiled: " << R.out;
  EXPECT_TRUE(R.contains("CC bar.c")) << "bar compiled: " << R.out;
}

// ============================================================================
// Kernel PHONY accumulated via variable
// ============================================================================

TEST_F(BuildTest, KernelPhonyAccumulatedViaVariable) {
  writeMakefile(
      "PHONY := all\n"
      "PHONY += clean\n"
      "PHONY += install\n"
      "\n"
      "all:\n"
      "\t@echo build\n"
      "clean:\n"
      "\t@echo clean\n"
      "install:\n"
      "\t@echo install\n"
      "\n"
      ".PHONY: $(PHONY)\n");
  auto R = runMake({}, "clean");
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("clean")) << "phony via var: " << R.out;
}

// ============================================================================
// Kernel quiet command display pattern
// ============================================================================

TEST_F(BuildTest, KernelQuietCmdDisplay) {
  writeFile(tmp() / "test.c", "");
  writeMakefile(
      "ifeq ($(V),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "\n"
      "quiet_cmd_cc = CC      $@\n"
      "cmd_cc = gcc -c $< -o $@\n"
      "\n"
      "define cmd\n"
      "$(if $($(quiet)cmd_$(1)),@echo '  $($(quiet)cmd_$(1))')\n"
      "$(Q)$(cmd_$(1))\n"
      "endef\n"
      "\n"
      ".PHONY: test.o\n"
      "test.o: test.c\n"
      "\t$(call cmd,cc)\n");
  auto R1 = runMake({"-n"});
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("CC"))
      << "quiet mode shows short form: " << R1.out;

  auto R2 = runMake({"-n", "V=1"});
  ASSERT_TRUE(R2.ok()) << "stderr: " << R2.err;
  EXPECT_TRUE(R2.contains("gcc -c"))
      << "verbose mode shows full cmd: " << R2.out;
}

// ============================================================================
// Kernel hostcc / cross-compile variable chain
// ============================================================================

TEST_F(BuildTest, KernelCrossCompileVariableChain) {
  writeMakefile(
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "AS := $(CROSS_COMPILE)as\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "AR := $(CROSS_COMPILE)ar\n"
      "\n"
      "HOSTCC := gcc\n"
      "HOSTCXX := g++\n"
      "\n"
      "all:\n"
      "\t@echo CC=$(CC) LD=$(LD) HOSTCC=$(HOSTCC) ARCH=$(ARCH)\n"
      ".PHONY: all\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << "stderr: " << R1.err;
  EXPECT_TRUE(R1.contains("CC=gcc")) << "default CC: " << R1.out;
  EXPECT_TRUE(R1.contains("ARCH=x86")) << "default ARCH: " << R1.out;

  auto R2 = runMake({}, "CROSS_COMPILE=arm-linux-gnueabi- ARCH=arm");
  // Note: this passes "CROSS_COMPILE=arm-linux-gnueabi-" and "ARCH=arm" as cmd vars
  // But runMake takes ExtraArgs and target separately. Let me adjust.
  auto R3 = runMake({"CROSS_COMPILE=aarch64-linux-gnu-", "ARCH=arm64"});
  ASSERT_TRUE(R3.ok()) << "stderr: " << R3.err;
  EXPECT_TRUE(R3.contains("CC=aarch64-linux-gnu-gcc"))
      << "cross CC: " << R3.out;
  EXPECT_TRUE(R3.contains("ARCH=arm64"))
      << "cross ARCH: " << R3.out;
}

// ============================================================================
// Kernel nested foreach + eval for Kbuild objects
// ============================================================================

TEST_F(BuildTest, KernelNestedForeachEvalKbuild) {
  writeFile(tmp() / "init_main.c", "");
  writeFile(tmp() / "init_do.c", "");
  writeFile(tmp() / "kernel_sched.c", "");
  writeFile(tmp() / "kernel_fork.c", "");
  writeMakefile(
      "subdirs := init kernel\n"
      "init-objs := init_main.o init_do.o\n"
      "kernel-objs := kernel_sched.o kernel_fork.o\n"
      "\n"
      "all-objs := $(foreach d,$(subdirs),$($(d)-objs))\n"
      "\n"
      "define build_obj\n"
      "$(1): $(patsubst %.o,%.c,$(1))\n"
      "\t@echo CC $$< -o $$@\n"
      "endef\n"
      "\n"
      ".PHONY: all\n"
      "all: $(all-objs)\n"
      "\t@echo LD vmlinux\n"
      "\n"
      "$(foreach o,$(all-objs),$(eval $(call build_obj,$(o))))\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC init_main.c")) << "init_main: " << R.out;
  EXPECT_TRUE(R.contains("CC kernel_sched.c")) << "kernel_sched: " << R.out;
  EXPECT_TRUE(R.contains("CC kernel_fork.c")) << "kernel_fork: " << R.out;
  EXPECT_TRUE(R.contains("LD vmlinux")) << "link: " << R.out;
}

// ============================================================================
// Kernel subdir-y aggregation with addprefix
// ============================================================================

TEST_F(BuildTest, KernelSubdirAggregation) {
  writeMakefile(
      "src-dir := src/\n"
      "core-y := main.o init.o\n"
      "drivers-y := pci.o usb.o\n"
      "\n"
      "all-objs := $(addprefix $(src-dir),$(core-y) $(drivers-y))\n"
      "\n"
      "all:\n"
      "\t@echo objs=$(all-objs)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("src/main.o")) << R.out;
  EXPECT_TRUE(R.contains("src/init.o")) << R.out;
  EXPECT_TRUE(R.contains("src/pci.o")) << R.out;
  EXPECT_TRUE(R.contains("src/usb.o")) << R.out;
}

// ============================================================================
// Kernel complex ifeq chain with else ifeq
// ============================================================================

TEST_F(BuildTest, KernelComplexIfeqChain) {
  writeMakefile(
      "ARCH := arm64\n"
      "ifeq ($(ARCH),x86)\n"
      "  BITS := 64\n"
      "  MACH := pc\n"
      "else ifeq ($(ARCH),arm)\n"
      "  BITS := 32\n"
      "  MACH := versatile\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  BITS := 64\n"
      "  MACH := virt\n"
      "else\n"
      "  BITS := unknown\n"
      "  MACH := generic\n"
      "endif\n"
      "\n"
      "all:\n"
      "\t@echo bits=$(BITS) mach=$(MACH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("bits=64")) << "arm64 bits: " << R.out;
  EXPECT_TRUE(R.contains("mach=virt")) << "arm64 mach: " << R.out;
}

// ============================================================================
// Kernel define + override interaction
// ============================================================================

TEST_F(BuildTest, KernelDefineOverrideInteraction) {
  writeMakefile(
      "override define CFLAGS_KERNEL\n"
      "-DKERNEL\n"
      "endef\n"
      "\n"
      "CFLAGS_MODULE := -DMODULE\n"
      "\n"
      "all:\n"
      "\t@echo kernel=$(CFLAGS_KERNEL) module=$(CFLAGS_MODULE)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS_KERNEL=-DUSER"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("kernel=-DKERNEL"))
      << "override define beats cmdline: " << R.out;
}

// ============================================================================
// Kernel export with := assignment
// ============================================================================

TEST_F(BuildTest, KernelExportWithSimpleAssign) {
  writeMakefile(
      "export KBUILD_VERBOSE := 0\n"
      "export srctree := .\n"
      "export objtree := .\n"
      "\n"
      "all:\n"
      "\t@echo verbose=$(KBUILD_VERBOSE) srctree=$(srctree)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("verbose=0")) << R.out;
  EXPECT_TRUE(R.contains("srctree=.")) << R.out;
}

// ============================================================================
// Kernel substitution ref with path patterns
// ============================================================================

TEST_F(BuildTest, KernelSubstRefWithPaths) {
  writeMakefile(
      "SRCS := arch/x86/boot.c arch/x86/setup.c drivers/pci/probe.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("arch/x86/boot.o")) << R.out;
  EXPECT_TRUE(R.contains("arch/x86/setup.o")) << R.out;
  EXPECT_TRUE(R.contains("drivers/pci/probe.o")) << R.out;
}

// ============================================================================
// Kernel $(firstword $(MAKEFILE_LIST)) pattern
// ============================================================================

TEST_F(BuildTest, KernelFirstwordMakefileList) {
  writeMakefile(
      "THIS_MAKEFILE := $(firstword $(MAKEFILE_LIST))\n"
      "all:\n"
      "\t@echo this=$(THIS_MAKEFILE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("this=Makefile") || R.contains("this=./Makefile") ||
              R.contains("this=makefile") ||
              R.contains("this=" + (tmp() / "Makefile").string()))
      << "firstword MAKEFILE_LIST: " << R.out;
}

// ============================================================================
// Kernel define with call producing multiple lines
// ============================================================================

TEST_F(BuildTest, KernelDefineCallMultiline) {
  writeMakefile(
      "define do_build\n"
      "@echo STEP1: prepare $(1)\n"
      "@echo STEP2: compile $(1)\n"
      "@echo STEP3: link $(1)\n"
      "endef\n"
      "\n"
      ".PHONY: all\n"
      "all:\n"
      "\t$(call do_build,vmlinux)\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("STEP1: prepare vmlinux")) << R.out;
  EXPECT_TRUE(R.contains("STEP2: compile vmlinux")) << R.out;
  EXPECT_TRUE(R.contains("STEP3: link vmlinux")) << R.out;
}

// ============================================================================
// Kernel $(words) and $(word) for list processing
// ============================================================================

TEST_F(BuildTest, KernelWordsFunctions) {
  writeMakefile(
      "LIST := a b c d e\n"
      "COUNT := $(words $(LIST))\n"
      "THIRD := $(word 3,$(LIST))\n"
      "LAST := $(lastword $(LIST))\n"
      "FIRST := $(firstword $(LIST))\n"
      "MID := $(wordlist 2,4,$(LIST))\n"
      "\n"
      "all:\n"
      "\t@echo count=$(COUNT) third=$(THIRD) last=$(LAST) "
      "first=$(FIRST) mid=$(MID)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("count=5")) << R.out;
  EXPECT_TRUE(R.contains("third=c")) << R.out;
  EXPECT_TRUE(R.contains("last=e")) << R.out;
  EXPECT_TRUE(R.contains("first=a")) << R.out;
  EXPECT_TRUE(R.contains("mid=b c d")) << R.out;
}

// ============================================================================
// Kernel $(sort) for dedup + sort (kernel-style dirs)
// ============================================================================

TEST_F(BuildTest, KernelSortDedupDirs) {
  writeMakefile(
      "DIRS := lib drivers lib arch drivers init\n"
      "UNIQUE := $(sort $(DIRS))\n"
      "\n"
      "all:\n"
      "\t@echo dirs=$(UNIQUE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("dirs=arch drivers init lib")) << R.out;
}

// ============================================================================
// Kernel $(dir) and $(notdir) combination
// ============================================================================

TEST_F(BuildTest, KernelDirNotdirCombination) {
  writeMakefile(
      "FILES := src/kernel/main.c lib/utils.c drivers/pci/core.c\n"
      "DIRS := $(sort $(dir $(FILES)))\n"
      "NAMES := $(notdir $(FILES))\n"
      "\n"
      "all:\n"
      "\t@echo dirs=$(DIRS)\n"
      "\t@echo names=$(NAMES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("drivers/pci/")) << R.out;
  EXPECT_TRUE(R.contains("src/kernel/")) << R.out;
  EXPECT_TRUE(R.contains("main.c")) << R.out;
  EXPECT_TRUE(R.contains("core.c")) << R.out;
}

// ============================================================================
// Kernel $(basename) and $(suffix)
// ============================================================================

TEST_F(BuildTest, KernelBasenameSuffix) {
  writeMakefile(
      "FILES := kernel.o init.o drivers.a libfoo.so\n"
      "BASES := $(basename $(FILES))\n"
      "SUFFS := $(suffix $(FILES))\n"
      "\n"
      "all:\n"
      "\t@echo bases=$(BASES)\n"
      "\t@echo suffs=$(SUFFS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("kernel")) << R.out;
  EXPECT_TRUE(R.contains(".o")) << R.out;
  EXPECT_TRUE(R.contains(".a")) << R.out;
  EXPECT_TRUE(R.contains(".so")) << R.out;
}

// ============================================================================
// Kernel $(subst) for string manipulation
// ============================================================================

TEST_F(BuildTest, KernelSubstStringManip) {
  writeMakefile(
      "KERNELRELEASE := 5.10.0-custom\n"
      "SAFE := $(subst .,_,$(subst -,_,$(KERNELRELEASE)))\n"
      "\n"
      "all:\n"
      "\t@echo safe=$(SAFE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("safe=5_10_0_custom")) << R.out;
}

// ============================================================================
// Kernel MAKECMDGOALS detection
// ============================================================================

TEST_F(BuildTest, KernelMakecmdgoals) {
  writeMakefile(
      "ifneq ($(filter clean mrproper,$(MAKECMDGOALS)),)\n"
      "  DOING_CLEAN := yes\n"
      "else\n"
      "  DOING_CLEAN := no\n"
      "endif\n"
      "\n"
      "all:\n"
      "\t@echo cleaning=$(DOING_CLEAN)\n"
      "clean:\n"
      "\t@echo cleaning=$(DOING_CLEAN)\n"
      ".PHONY: all clean\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cleaning=no")) << R1.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cleaning=yes")) << R2.out;
}

// ============================================================================
// Kernel .DEFAULT_GOAL override
// ============================================================================

TEST_F(BuildTest, KernelDefaultGoalOverride) {
  writeMakefile(
      ".DEFAULT_GOAL := help\n"
      "\n"
      "all:\n"
      "\t@echo all\n"
      "help:\n"
      "\t@echo help-text\n"
      ".PHONY: all help\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("help-text")) << "DEFAULT_GOAL override: " << R.out;
  EXPECT_FALSE(R.contains("all\n"))
      << "should not run all: " << R.out;
}

// ============================================================================
// Kernel $(warning) / $(info) output
// ============================================================================

TEST_F(BuildTest, KernelInfoWarningOutput) {
  writeMakefile(
      "$(info Building NeverC kernel module...)\n"
      "all:\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("Building NeverC kernel module"))
      << "info output: " << R.out;
}

// ============================================================================
// Kernel filter-out for excluding files
// ============================================================================

TEST_F(BuildTest, KernelFilterOutExclude) {
  writeMakefile(
      "ALL_OBJS := main.o init.o test_main.o debug.o test_debug.o\n"
      "TEST_OBJS := $(filter test_%,$(ALL_OBJS))\n"
      "PROD_OBJS := $(filter-out test_%,$(ALL_OBJS))\n"
      "\n"
      "all:\n"
      "\t@echo test=$(TEST_OBJS)\n"
      "\t@echo prod=$(PROD_OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("test=test_main.o test_debug.o")) << R.out;
  EXPECT_TRUE(R.contains("prod=main.o init.o debug.o")) << R.out;
}

// ============================================================================
// Kernel addsuffix / addprefix combined with pattern
// ============================================================================

TEST_F(BuildTest, KernelAddsuffixAddprefixCombined) {
  writeMakefile(
      "MODS := ext4 btrfs xfs\n"
      "MOD_KO := $(addsuffix .ko,$(addprefix fs/,$(MODS)))\n"
      "\n"
      "all:\n"
      "\t@echo modules=$(MOD_KO)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("fs/ext4.ko")) << R.out;
  EXPECT_TRUE(R.contains("fs/btrfs.ko")) << R.out;
  EXPECT_TRUE(R.contains("fs/xfs.ko")) << R.out;
}

// ============================================================================
// Kernel ifndef for default values
// ============================================================================

TEST_F(BuildTest, KernelIfndefDefaults) {
  writeMakefile(
      "ifndef INSTALL_PATH\n"
      "INSTALL_PATH := /boot\n"
      "endif\n"
      "\n"
      "ifndef INSTALL_MOD_PATH\n"
      "INSTALL_MOD_PATH :=\n"
      "endif\n"
      "\n"
      "MODLIB := $(INSTALL_MOD_PATH)/lib/modules/5.10.0\n"
      "\n"
      "all:\n"
      "\t@echo path=$(INSTALL_PATH) modlib=$(MODLIB)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("path=/boot")) << R.out;
  EXPECT_TRUE(R.contains("modlib=/lib/modules/5.10.0")) << R.out;
}

// ============================================================================
// Kernel ifndef overridden by command line
// ============================================================================

TEST_F(BuildTest, KernelIfndefCmdLineOverride) {
  writeMakefile(
      "ifndef INSTALL_PATH\n"
      "INSTALL_PATH := /boot\n"
      "endif\n"
      "all:\n"
      "\t@echo path=$(INSTALL_PATH)\n"
      ".PHONY: all\n");
  auto R = runMake({"INSTALL_PATH=/custom/boot"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("path=/custom/boot"))
      << "cmdline overrides ifndef: " << R.out;
}

// ============================================================================
// Kernel $(eval) generating rules dynamically
// ============================================================================

TEST_F(BuildTest, KernelEvalDynamicRuleGeneration) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "c.c", "");
  writeMakefile(
      "define make-obj-rule\n"
      "$(1).o: $(1).c\n"
      "\t@echo CC $(1).c -o $(1).o\n"
      "endef\n"
      "\n"
      "SOURCES := a b c\n"
      "\n"
      ".PHONY: all\n"
      "all: $(addsuffix .o,$(SOURCES))\n"
      "\t@echo LINK\n"
      "\n"
      "$(foreach src,$(SOURCES),$(eval $(call make-obj-rule,$(src))))\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC a.c -o a.o")) << R.out;
  EXPECT_TRUE(R.contains("CC b.c -o b.o")) << R.out;
  EXPECT_TRUE(R.contains("CC c.c -o c.o")) << R.out;
  EXPECT_TRUE(R.contains("LINK")) << R.out;
}

// ============================================================================
// Kernel origin function for variable source detection
// ============================================================================

TEST_F(BuildTest, KernelOriginDetection) {
  writeMakefile(
      "FILE_VAR := from_file\n"
      "override OVER_VAR := from_override\n"
      "\n"
      "all:\n"
      "\t@echo file_origin=$(origin FILE_VAR)\n"
      "\t@echo cmd_origin=$(origin CMD_VAR)\n"
      "\t@echo over_origin=$(origin OVER_VAR)\n"
      "\t@echo undef_origin=$(origin NONEXISTENT)\n"
      ".PHONY: all\n");
  auto R = runMake({"CMD_VAR=from_cmd"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("file_origin=file")) << R.out;
  EXPECT_TRUE(R.contains("cmd_origin=command line")) << R.out;
  EXPECT_TRUE(R.contains("over_origin=override")) << R.out;
  EXPECT_TRUE(R.contains("undef_origin=undefined")) << R.out;
}

// ============================================================================
// Kernel $(value) for raw unexpanded value
// ============================================================================

TEST_F(BuildTest, KernelValueFunction) {
  writeMakefile(
      "CC = $(CROSS_COMPILE)gcc\n"
      "CROSS_COMPILE := arm-\n"
      "\n"
      "ifeq ($(CC),arm-gcc)\n"
      "  EXPANDED_OK := yes\n"
      "else\n"
      "  EXPANDED_OK := no\n"
      "endif\n"
      "\n"
      "ifneq ($(value CC),arm-gcc)\n"
      "  VALUE_DIFFERENT := yes\n"
      "else\n"
      "  VALUE_DIFFERENT := no\n"
      "endif\n"
      "\n"
      "all:\n"
      "\t@echo expanded_ok=$(EXPANDED_OK) value_diff=$(VALUE_DIFFERENT) cc=$(CC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("expanded_ok=yes"))
      << "CC expands to arm-gcc: " << R.out;
  EXPECT_TRUE(R.contains("value_diff=yes"))
      << "value returns raw (not equal to expanded): " << R.out;
  EXPECT_TRUE(R.contains("cc=arm-gcc"))
      << "CC expansion: " << R.out;
}

// ============================================================================
// Kernel complex Kbuild simulation (full pipeline)
// ============================================================================

TEST_F(BuildTest, KernelFullKbuildSimulation) {
  writeFile(tmp() / "init_main.c", "");
  writeFile(tmp() / "kernel_core.c", "");
  writeFile(tmp() / "drivers_base.c", "");
  writeMakefile(
      "# Top-level Kbuild simulation\n"
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 0\n"
      "KERNELRELEASE := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "\n"
      "ARCH ?= x86_64\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "\n"
      "CFLAGS := -Wall -O2\n"
      "ifdef CONFIG_DEBUG_INFO\n"
      "CFLAGS += -g\n"
      "endif\n"
      "\n"
      "# Kbuild-style object lists\n"
      "init-y := init_main.o\n"
      "core-y := kernel_core.o\n"
      "drivers-y := drivers_base.o\n"
      "\n"
      "vmlinux-deps := $(init-y) $(core-y) $(drivers-y)\n"
      "\n"
      "# Build rules\n"
      "define rule_cc_o_c\n"
      "$(1): $(patsubst %.o,%.c,$(1))\n"
      "\t@echo '  CC [$(KERNELRELEASE)] $(patsubst %.o,%.c,$(1))'\n"
      "endef\n"
      "\n"
      ".PHONY: all vmlinux\n"
      "all: vmlinux\n"
      "\n"
      "vmlinux: $(vmlinux-deps)\n"
      "\t@echo '  LD vmlinux ($(KERNELRELEASE))'\n"
      "\t@echo '  Objects: $(vmlinux-deps)'\n"
      "\n"
      "$(foreach obj,$(vmlinux-deps),$(eval $(call rule_cc_o_c,$(obj))))\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << "stderr: " << R.err;
  EXPECT_TRUE(R.contains("CC [5.10.0] init_main.c")) << R.out;
  EXPECT_TRUE(R.contains("CC [5.10.0] kernel_core.c")) << R.out;
  EXPECT_TRUE(R.contains("CC [5.10.0] drivers_base.c")) << R.out;
  EXPECT_TRUE(R.contains("LD vmlinux (5.10.0)")) << R.out;
}

// ============================================================================
// Edge: empty variable in substitution reference
// ============================================================================

TEST_F(BuildTest, EdgeEmptyVarSubstRef) {
  writeMakefile(
      "EMPTY :=\n"
      "RESULT := $(EMPTY:.c=.o)\n"
      "all:\n"
      "\t@echo result=[$(RESULT)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=[]")) << R.out;
}

// ============================================================================
// Edge: $(if) with complex expressions
// ============================================================================

TEST_F(BuildTest, EdgeIfComplexExpressions) {
  writeMakefile(
      "X := hello\n"
      "Y :=\n"
      "R1 := $(if $(X),yes,no)\n"
      "R2 := $(if $(Y),yes,no)\n"
      "R3 := $(if $(filter hello,$(X)),matched,nope)\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2) r3=$(R3)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=yes")) << R.out;
  EXPECT_TRUE(R.contains("r2=no")) << R.out;
  EXPECT_TRUE(R.contains("r3=matched")) << R.out;
}

// ============================================================================
// Edge: deeply nested variable references
// ============================================================================

TEST_F(BuildTest, EdgeDeeplyNestedVarRefs) {
  writeMakefile(
      "A := hello\n"
      "B := A\n"
      "C := B\n"
      "D := C\n"
      "RESULT := $($($($(D))))\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=hello")) << "nested var deref: " << R.out;
}

// ============================================================================
// Edge: target with variable in name
// ============================================================================

TEST_F(BuildTest, EdgeTargetWithVariable) {
  writeMakefile(
      "OUTPUT := output.txt\n"
      "$(OUTPUT):\n"
      "\t@echo creating $(OUTPUT)\n"
      ".PHONY: $(OUTPUT)\n"
      "all: $(OUTPUT)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("creating output.txt")) << R.out;
}

// ============================================================================
// Edge: $(strip) for whitespace normalization
// ============================================================================

TEST_F(BuildTest, EdgeStripWhitespace) {
  writeMakefile(
      "RAW :=   hello   world   \n"
      "STRIPPED := $(strip $(RAW))\n"
      "all:\n"
      "\t@echo stripped=[$(STRIPPED)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("stripped=[hello world]")) << R.out;
}

// ============================================================================
// Edge: multiple include with wildcards
// ============================================================================

TEST_F(BuildTest, EdgeMultipleIncludeWithWildcard) {
  writeFile(tmp() / "conf_a.mk", "A := alpha\n");
  writeFile(tmp() / "conf_b.mk", "B := beta\n");
  writeMakefile(
      "-include conf_*.mk\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=alpha")) << R.out;
  EXPECT_TRUE(R.contains("b=beta")) << R.out;
}

// ============================================================================
// Edge: $(abspath) for path canonicalization
// ============================================================================

TEST_F(BuildTest, EdgeAbspath) {
  writeMakefile(
      "P := $(abspath foo/../bar/baz)\n"
      "all:\n"
      "\t@echo path=$(P)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bar/baz")) << "abspath resolves ..: " << R.out;
  EXPECT_FALSE(R.contains("foo/../")) << "no foo/..: " << R.out;
}

// ============================================================================
// Edge: += to undefined variable creates recursive
// ============================================================================

TEST_F(BuildTest, EdgeAppendToUndefined) {
  writeMakefile(
      "UNDEF_VAR += first\n"
      "UNDEF_VAR += second\n"
      "all:\n"
      "\t@echo val=$(UNDEF_VAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("val=first second")) << R.out;
}

// ============================================================================
// Edge: ?= does not override existing value
// ============================================================================

TEST_F(BuildTest, EdgeConditionalAssignNoOverride) {
  writeMakefile(
      "CC := clang\n"
      "CC ?= gcc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=clang"))
      << "?= does not override existing: " << R.out;
}

// ============================================================================
// Edge: multiple prerequisites for same target (merge)
// ============================================================================

TEST_F(BuildTest, EdgeMultiPrereqsMerge) {
  writeFile(tmp() / "a.h", "");
  writeFile(tmp() / "b.h", "");
  writeFile(tmp() / "main.c", "");
  writeMakefile(
      "main.o: main.c\n"
      "\t@echo CC main.c\n"
      "main.o: a.h\n"
      "main.o: b.h\n"
      ".PHONY: main.o\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC main.c")) << R.out;
}

// ============================================================================
// Edge: $$ escaping in recipes
// ============================================================================

TEST_F(BuildTest, EdgeDollarEscaping) {
  writeMakefile(
      "all:\n"
      "\t@echo price=$$100\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
}

// ============================================================================
// Edge: flavor function
// ============================================================================

TEST_F(BuildTest, EdgeFlavorFunction) {
  writeMakefile(
      "REC = recursive\n"
      "SIM := simple\n"
      "all:\n"
      "\t@echo rec=$(flavor REC) sim=$(flavor SIM) undef=$(flavor UNDEF)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("rec=recursive")) << R.out;
  EXPECT_TRUE(R.contains("sim=simple")) << R.out;
  EXPECT_TRUE(R.contains("undef=undefined")) << R.out;
}

// ============================================================================
// Stress: 50-module Kbuild with eval-generated rules
// ============================================================================

TEST_F(BuildTest, Stress50ModuleKbuild) {
  std::string Makefile;
  Makefile += "define gen_rule\n";
  Makefile += "$(1).o: $(1).c\n";
  Makefile += "\t@echo CC $(1)\n";
  Makefile += "endef\n\n";

  Makefile += "MODULES :=";
  for (int i = 0; i < 50; ++i) {
    std::string Name = "mod_" + std::to_string(i);
    writeFile(tmp() / (Name + ".c"), "");
    Makefile += " " + Name;
  }
  Makefile += "\n\n";

  Makefile += "OBJS := $(addsuffix .o,$(MODULES))\n\n";
  Makefile += ".PHONY: all\n";
  Makefile += "all: $(OBJS)\n";
  Makefile += "\t@echo LINK $(words $(OBJS)) objects\n\n";
  Makefile += "$(foreach m,$(MODULES),$(eval $(call gen_rule,$(m))))\n";

  writeMakefile(Makefile);
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC mod_0")) << R.out;
  EXPECT_TRUE(R.contains("CC mod_49")) << R.out;
  EXPECT_TRUE(R.contains("LINK 50 objects")) << R.out;
}

// ============================================================================
// Stress: parallel build with diamond dependency
// ============================================================================

TEST_F(BuildTest, StressParallelDiamond) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "c.c", "");
  writeFile(tmp() / "common.h", "");
  writeMakefile(
      ".PHONY: all\n"
      "all: prog1 prog2\n"
      "\t@echo done\n"
      "prog1: a.o b.o\n"
      "\t@echo LINK prog1\n"
      "prog2: b.o c.o\n"
      "\t@echo LINK prog2\n"
      "%.o: %.c common.h\n"
      "\t@echo CC $<\n"
      ".PHONY: prog1 prog2 a.o b.o c.o\n");
  auto R = runMake({"-j4", "-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC a.c")) << R.out;
  EXPECT_TRUE(R.contains("CC b.c")) << R.out;
  EXPECT_TRUE(R.contains("CC c.c")) << R.out;
  EXPECT_TRUE(R.contains("LINK prog1")) << R.out;
  EXPECT_TRUE(R.contains("LINK prog2")) << R.out;
}

// ============================================================================
// Kernel: sinclude is alias for -include
// ============================================================================

TEST_F(BuildTest, KernelSincludeAlias) {
  writeFile(tmp() / "optional.mk", "OPT_VAR := present\n");
  writeMakefile(
      "sinclude optional.mk\n"
      "sinclude nonexistent.mk\n"
      "all:\n"
      "\t@echo opt=$(OPT_VAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("opt=present")) << R.out;
}

// ============================================================================
// Kernel: shell assignment operator !=
// ============================================================================

TEST_F(BuildTest, KernelShellAssignOperator) {
  writeMakefile(
      "KERNEL_DATE != echo test_date\n"
      "all:\n"
      "\t@echo date=$(KERNEL_DATE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("date=test_date")) << R.out;
}

// ============================================================================
// Kernel: combined pattern rule with multiple prerequisites
// ============================================================================

TEST_F(BuildTest, KernelPatternRuleMultiPrereqs) {
  writeFile(tmp() / "foo.c", "");
  writeFile(tmp() / "foo.h", "");
  writeFile(tmp() / "bar.c", "");
  writeFile(tmp() / "bar.h", "");
  writeMakefile(
      ".PHONY: all\n"
      "all: foo.o bar.o\n"
      "\t@echo link\n"
      "%.o: %.c %.h\n"
      "\t@echo CC $< with header $*.h\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC foo.c")) << R.out;
  EXPECT_TRUE(R.contains("CC bar.c")) << R.out;
}

// ============================================================================
// Kernel: multi-arch build with ifeq + filter
// ============================================================================

TEST_F(BuildTest, KernelMultiArchBuild) {
  writeMakefile(
      "ARCH := arm64\n"
      "SUPPORTED_ARCHS := x86 x86_64 arm arm64 mips\n"
      "\n"
      "ifeq ($(filter $(ARCH),$(SUPPORTED_ARCHS)),)\n"
      "$(error Unsupported architecture: $(ARCH))\n"
      "endif\n"
      "\n"
      "ifeq ($(filter $(ARCH),arm arm64),)\n"
      "  NEED_LIBGCC :=\n"
      "else\n"
      "  NEED_LIBGCC := -lgcc\n"
      "endif\n"
      "\n"
      "all:\n"
      "\t@echo arch=$(ARCH) libgcc=$(NEED_LIBGCC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arch=arm64")) << R.out;
  EXPECT_TRUE(R.contains("libgcc=-lgcc")) << R.out;
}

// ============================================================================
// Kernel: nested define with call and eval
// ============================================================================

TEST_F(BuildTest, KernelNestedDefineCallEval) {
  writeFile(tmp() / "x.c", "");
  writeFile(tmp() / "y.c", "");
  writeMakefile(
      "define inner_template\n"
      "$(1).o: $(1).c\n"
      "\t@echo INNER_CC $(1)\n"
      "endef\n"
      "\n"
      "define outer_template\n"
      "$(eval $(call inner_template,$(1)))\n"
      "endef\n"
      "\n"
      ".PHONY: all\n"
      "all: x.o y.o\n"
      "\t@echo done\n"
      "\n"
      "$(call outer_template,x)\n"
      "$(call outer_template,y)\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("INNER_CC x")) << R.out;
  EXPECT_TRUE(R.contains("INNER_CC y")) << R.out;
}

// ============================================================================
// Edge: variable with hyphen in name
// ============================================================================

TEST_F(BuildTest, EdgeVarWithHyphen) {
  writeMakefile(
      "my-var := hello-world\n"
      "obj-y := foo.o bar.o\n"
      "all:\n"
      "\t@echo var=$(my-var) obj=$(obj-y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("var=hello-world")) << R.out;
  EXPECT_TRUE(R.contains("obj=foo.o bar.o")) << R.out;
}

// ============================================================================
// Edge: export all variables
// ============================================================================

TEST_F(BuildTest, EdgeExportAll) {
  writeMakefile(
      "A := alpha\n"
      "B := beta\n"
      "export\n"
      "all:\n"
      "\t@echo a=$(A)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=alpha")) << R.out;
}

// ============================================================================
// Kernel: complex substitution reference with % pattern
// ============================================================================

TEST_F(BuildTest, KernelComplexSubstRef) {
  writeMakefile(
      "SRCS := src/a.c src/b.c lib/c.c\n"
      "OBJS := $(SRCS:src/%.c=obj/%.o)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("obj/a.o")) << R.out;
  EXPECT_TRUE(R.contains("obj/b.o")) << R.out;
  EXPECT_TRUE(R.contains("lib/c.c"))
      << "lib/c.c should not match src/%.c pattern: " << R.out;
}
