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
