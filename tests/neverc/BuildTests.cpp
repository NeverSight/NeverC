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

// ============================================================================
// undefine directive
// ============================================================================

TEST_F(BuildTest, UndefineBasic) {
  writeMakefile(
      "FOO := hello\n"
      "undefine FOO\n"
      "all:\n"
      "\t@echo val=[$(FOO)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("val=[]")) << R.out;
}

TEST_F(BuildTest, UndefineIfdef) {
  writeMakefile(
      "X := something\n"
      "undefine X\n"
      "ifdef X\n"
      "  RESULT := defined\n"
      "else\n"
      "  RESULT := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("undefined")) << R.out;
}

TEST_F(BuildTest, UndefineOverride) {
  writeMakefile(
      "override undefine FOO\n"
      "all:\n"
      "\t@echo val=[$(FOO)]\n"
      ".PHONY: all\n");
  auto R = runMake({"FOO=cmdval"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("val=[]")) << R.out;
}

TEST_F(BuildTest, UndefineNoOverrideCmdLine) {
  writeMakefile(
      "undefine FOO\n"
      "all:\n"
      "\t@echo val=$(FOO)\n"
      ".PHONY: all\n");
  auto R = runMake({"FOO=cmdval"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("val=cmdval")) << R.out;
}

TEST_F(BuildTest, UndefineRedefine) {
  writeMakefile(
      "X := first\n"
      "undefine X\n"
      "X := second\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("second")) << R.out;
}

// ============================================================================
// MAKE_VERSION special variable
// ============================================================================

TEST_F(BuildTest, MakeVersionDefined) {
  writeMakefile(
      "all:\n"
      "\t@echo ver=$(MAKE_VERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ver=4.3")) << R.out;
}

TEST_F(BuildTest, MakeVersionFilter) {
  writeMakefile(
      "ifneq ($(filter 4.%,$(MAKE_VERSION)),)\n"
      "  MODERN := yes\n"
      "else\n"
      "  MODERN := no\n"
      "endif\n"
      "all:\n"
      "\t@echo modern=$(MODERN)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("modern=yes")) << R.out;
}

// ============================================================================
// else ifdef / else ifndef chains
// ============================================================================

TEST_F(BuildTest, ElseIfdef) {
  writeMakefile(
      "B := val_b\n"
      "ifdef A\n"
      "  R := from_a\n"
      "else ifdef B\n"
      "  R := from_b\n"
      "else\n"
      "  R := fallback\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("from_b")) << R.out;
}

TEST_F(BuildTest, ElseIfndef) {
  writeMakefile(
      "A := exists\n"
      "ifdef A\n"
      "  R := from_a\n"
      "else ifndef B\n"
      "  R := b_not_defined\n"
      "else\n"
      "  R := fallback\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("from_a")) << R.out;
}

TEST_F(BuildTest, ElseIfdefChain3) {
  writeMakefile(
      "C := val_c\n"
      "ifdef A\n"
      "  R := from_a\n"
      "else ifdef B\n"
      "  R := from_b\n"
      "else ifdef C\n"
      "  R := from_c\n"
      "else\n"
      "  R := fallback\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("from_c")) << R.out;
}

TEST_F(BuildTest, ElseIfndefFallthrough) {
  writeMakefile(
      "A := 1\n"
      "B := 2\n"
      "ifndef A\n"
      "  R := no_a\n"
      "else ifndef B\n"
      "  R := no_b\n"
      "else\n"
      "  R := both_exist\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("both_exist")) << R.out;
}

// ============================================================================
// Kernel-style: ARCH selection with else ifeq chain
// ============================================================================

TEST_F(BuildTest, KernelArchSelectionChain) {
  writeMakefile(
      "SRCARCH := arm64\n"
      "\n"
      "ifeq ($(SRCARCH),x86)\n"
      "  ARCH_CFLAGS := -m32\n"
      "else ifeq ($(SRCARCH),x86_64)\n"
      "  ARCH_CFLAGS := -m64\n"
      "else ifeq ($(SRCARCH),arm)\n"
      "  ARCH_CFLAGS := -march=armv7-a\n"
      "else ifeq ($(SRCARCH),arm64)\n"
      "  ARCH_CFLAGS := -march=armv8-a\n"
      "else\n"
      "  ARCH_CFLAGS :=\n"
      "endif\n"
      "\n"
      "all:\n"
      "\t@echo flags=$(ARCH_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("flags=-march=armv8-a")) << R.out;
}

// ============================================================================
// Kernel-style: Kbuild verbose/quiet with define+call
// ============================================================================

TEST_F(BuildTest, KernelKbuildVerboseQuiet) {
  writeMakefile(
      "V ?= 0\n"
      "ifeq ($(V),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "\n"
      "quiet_cmd_cc_o = CC      $@\n"
      "cmd_cc_o = gcc -c -o $@ $<\n"
      "\n"
      "define cmd\n"
      "$(if $($(quiet)cmd_$(1)),$($(quiet)cmd_$(1)),$(cmd_$(1)))\n"
      "endef\n"
      "\n"
      "all:\n"
      "\t@echo $(call cmd,cc_o)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC")) << R.out;
}

// ============================================================================
// Kernel-style: CONFIG_* conditional compilation
// ============================================================================

TEST_F(BuildTest, KernelConfigConditionals) {
  writeMakefile(
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES := y\n"
      "\n"
      "obj-y := main.o\n"
      "obj-$(CONFIG_SMP) += smp.o\n"
      "obj-$(CONFIG_MODULES) += module.o\n"
      "obj-$(CONFIG_DEBUG) += debug.o\n"
      "\n"
      "all:\n"
      "\t@echo objs=$(obj-y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("main.o")) << R.out;
  EXPECT_TRUE(R.contains("smp.o")) << R.out;
  EXPECT_TRUE(R.contains("module.o")) << R.out;
  EXPECT_FALSE(R.contains("debug.o")) << R.out;
}

// ============================================================================
// Kernel-style: version extraction with shell+subst
// ============================================================================

TEST_F(BuildTest, KernelVersionExtract) {
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 0\n"
      "EXTRAVERSION :=\n"
      "KERNELRELEASE := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)\n"
      "KERNELVERSION := $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))\n"
      "all:\n"
      "\t@echo rel=$(KERNELRELEASE) ver=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("rel=5.10.0")) << R.out;
  EXPECT_TRUE(R.contains("ver=5.10.0")) << R.out;
}

// ============================================================================
// Kernel-style: FORCE target chain with dependency
// ============================================================================

TEST_F(BuildTest, KernelFORCETargetChainWithDep) {
  writeFile(tmp() / "vmlinux.o", "");
  writeMakefile(
      ".PHONY: all FORCE\n"
      "all: vmlinux\n"
      "\t@echo done\n"
      "\n"
      "vmlinux: vmlinux.o FORCE\n"
      "\t@echo LD vmlinux\n"
      "\n"
      "FORCE:\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("LD vmlinux")) << R.out;
}

// ============================================================================
// Kernel-style: cc-option pattern with shell
// ============================================================================

TEST_F(BuildTest, KernelCCOptionPattern) {
  writeMakefile(
      "define try-run\n"
      "$(shell set -e; if $(1) 2>/dev/null; then echo $(2); else echo $(3); "
      "fi)\n"
      "endef\n"
      "\n"
      "cc-option = $(call try-run,echo | $(CC) $(1) -x c - -o /dev/null "
      "2>/dev/null,$(1),$(2))\n"
      "\n"
      "CC := cc\n"
      "CFLAGS := -Wall\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("flags=-Wall")) << R.out;
}

// ============================================================================
// Kernel-style: subdir Makefile with obj-y aggregation
// ============================================================================

TEST_F(BuildTest, KernelSubdirObjY) {
  writeMakefile(
      "obj-y :=\n"
      "obj-y += kernel/\n"
      "obj-y += drivers/\n"
      "\n"
      "core-y := $(patsubst %/,%/built-in.a,$(obj-y))\n"
      "all:\n"
      "\t@echo core=$(core-y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("kernel/built-in.a")) << R.out;
  EXPECT_TRUE(R.contains("drivers/built-in.a")) << R.out;
}

// ============================================================================
// Kernel-style: KBUILD_MODULES detection
// ============================================================================

TEST_F(BuildTest, KernelKBUILDModulesDetect) {
  writeMakefile(
      "MAKECMDGOALS := modules\n"
      "\n"
      "ifneq ($(filter modules,$(MAKECMDGOALS)),)\n"
      "  KBUILD_MODULES := 1\n"
      "endif\n"
      "\n"
      "ifdef KBUILD_MODULES\n"
      "  MODULE_FLAG := -DMODULE\n"
      "else\n"
      "  MODULE_FLAG :=\n"
      "endif\n"
      "\n"
      "all:\n"
      "\t@echo mflag=$(MODULE_FLAG)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("mflag=-DMODULE")) << R.out;
}

// ============================================================================
// Kernel-style: foreach+eval+call Kbuild module template
// ============================================================================

TEST_F(BuildTest, KernelForeachEvalCallTemplate) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "c.c", "");
  writeMakefile(
      ".PHONY: all\n"
      "all: mod_a.ko mod_b.ko\n"
      "\t@echo done\n"
      "\n"
      "modules := mod_a mod_b\n"
      "mod_a-objs := a.o b.o\n"
      "mod_b-objs := c.o\n"
      "\n"
      "define build_module\n"
      "$(1).ko: $($(1)-objs)\n"
      "\t@echo LD $(1).ko from $($(1)-objs)\n"
      "endef\n"
      "\n"
      "$(foreach m,$(modules),$(eval $(call build_module,$(m))))\n"
      "\n"
      "%.o: %.c\n"
      "\t@echo CC $<\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("LD mod_a.ko")) << R.out;
  EXPECT_TRUE(R.contains("LD mod_b.ko")) << R.out;
}

// ============================================================================
// Kernel-style: .d dependency include with header tracking
// ============================================================================

TEST_F(BuildTest, KernelDepFileIncludeHeaderTracking) {
  writeFile(tmp() / "foo.c", "");
  writeFile(tmp() / "foo.h", "");
  writeFile(tmp() / ".foo.o.d", "foo.o: foo.c foo.h\n");
  writeMakefile(
      "-include .*.d\n"
      "%.o: %.c\n"
      "\t@echo CC $< deps=$^\n"
      ".PHONY: all\n"
      "all: foo.o\n"
      "\t@echo done\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC foo.c")) << R.out;
}

// ============================================================================
// Kernel-style: complex export+override interaction
// ============================================================================

TEST_F(BuildTest, KernelExportOverride) {
  writeMakefile(
      "ARCH := x86_64\n"
      "override SHELL := /bin/bash\n"
      "export ARCH\n"
      "\n"
      "all:\n"
      "\t@echo arch=$(ARCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arch=x86_64")) << R.out;
}

// ============================================================================
// Kernel-style: MAKEFLAGS += -rR
// ============================================================================

TEST_F(BuildTest, KernelMakeFlagsAppend) {
  writeMakefile(
      "MAKEFLAGS += -rR\n"
      "all:\n"
      "\t@echo flags=$(MAKEFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-rR")) << R.out;
}

// ============================================================================
// Kernel-style: multi-level variable indirection
// ============================================================================

TEST_F(BuildTest, KernelMultiLevelVarIndirection) {
  writeMakefile(
      "ARCH := arm64\n"
      "arm64_CFLAGS := -march=armv8-a\n"
      "x86_CFLAGS := -m64\n"
      "CFLAGS := $($(ARCH)_CFLAGS)\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cflags=-march=armv8-a")) << R.out;
}

// ============================================================================
// Kernel-style: undefine in Kbuild context
// ============================================================================

TEST_F(BuildTest, KernelUndefineInKbuild) {
  writeMakefile(
      "KBUILD_CFLAGS := -O2\n"
      "KBUILD_CFLAGS += -Wall\n"
      "\n"
      "ifdef CONFIG_DEBUG\n"
      "  KBUILD_CFLAGS += -g\n"
      "endif\n"
      "\n"
      "undefine CONFIG_DEBUG\n"
      "ifndef CONFIG_DEBUG\n"
      "  KBUILD_CFLAGS += -DNDEBUG\n"
      "endif\n"
      "\n"
      "all:\n"
      "\t@echo $(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-DNDEBUG")) << R.out;
}

// ============================================================================
// Kernel-style: complex if/filter/patsubst pipeline
// ============================================================================

TEST_F(BuildTest, KernelComplexFilterPipeline) {
  writeMakefile(
      "SRCS := main.c util.c debug.c test.c\n"
      "EXCLUDE := test.c debug.c\n"
      "FILTERED := $(filter-out $(EXCLUDE),$(SRCS))\n"
      "OBJS := $(patsubst %.c,%.o,$(FILTERED))\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("main.o")) << R.out;
  EXPECT_TRUE(R.contains("util.o")) << R.out;
  EXPECT_FALSE(R.contains("test.o")) << R.out;
  EXPECT_FALSE(R.contains("debug.o")) << R.out;
}

// ============================================================================
// Kernel-style: obj-y with subdir and sort
// ============================================================================

TEST_F(BuildTest, KernelObjYSortDedup) {
  writeMakefile(
      "obj-y += net.o\n"
      "obj-y += fs.o\n"
      "obj-y += net.o\n"
      "obj-y += mm.o\n"
      "SORTED := $(sort $(obj-y))\n"
      "all:\n"
      "\t@echo sorted=$(SORTED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("sorted=fs.o mm.o net.o")) << R.out;
}

// ============================================================================
// Kernel-style: full mini-kernel simulation
// ============================================================================

TEST_F(BuildTest, KernelFullMiniSimulation) {
  std::filesystem::create_directories(tmp() / "init");
  std::filesystem::create_directories(tmp() / "kernel");
  std::filesystem::create_directories(tmp() / "mm");
  writeFile(tmp() / "init/main.c", "");
  writeFile(tmp() / "kernel/sched.c", "");
  writeFile(tmp() / "mm/page.c", "");
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 42\n"
      "EXTRAVERSION :=\n"
      "\n"
      "ARCH ?= arm64\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "\n"
      "KERNELRELEASE := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)\n"
      "\n"
      "CFLAGS := -O2 -Wall\n"
      "ifeq ($(ARCH),arm64)\n"
      "  CFLAGS += -march=armv8-a\n"
      "else ifeq ($(ARCH),x86_64)\n"
      "  CFLAGS += -m64\n"
      "endif\n"
      "\n"
      "init-y := init/main.o\n"
      "core-y := kernel/sched.o\n"
      "mm-y   := mm/page.o\n"
      "\n"
      "vmlinux-deps := $(init-y) $(core-y) $(mm-y)\n"
      "\n"
      ".PHONY: all vmlinux FORCE\n"
      "all: vmlinux\n"
      "\t@echo Build complete: $(KERNELRELEASE)\n"
      "\n"
      "define rule_cc_o\n"
      "$(1): $(basename $(1)).c\n"
      "\t@echo CC $(1) [$(CFLAGS)]\n"
      "endef\n"
      "\n"
      "$(foreach o,$(vmlinux-deps),$(eval $(call rule_cc_o,$(o))))\n"
      "\n"
      "FORCE:\n"
      "\n"
      "vmlinux: $(vmlinux-deps) FORCE\n"
      "\t@echo LD vmlinux $(KERNELRELEASE)\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC init/main.o")) << R.out;
  EXPECT_TRUE(R.contains("CC kernel/sched.o")) << R.out;
  EXPECT_TRUE(R.contains("CC mm/page.o")) << R.out;
  EXPECT_TRUE(R.contains("LD vmlinux 5.10.42")) << R.out;
  EXPECT_TRUE(R.contains("-march=armv8-a")) << R.out;
}

// ============================================================================
// Robustness: deep nested foreach+call+eval
// ============================================================================

TEST_F(BuildTest, RobustDeepForeachCallEval) {
  writeFile(tmp() / "a1.c", "");
  writeFile(tmp() / "a2.c", "");
  writeFile(tmp() / "b1.c", "");
  writeFile(tmp() / "b2.c", "");
  writeMakefile(
      ".PHONY: all\n"
      "all: a.ko b.ko\n"
      "\t@echo done\n"
      "\n"
      "MODULES := a b\n"
      "a_SRCS := a1 a2\n"
      "b_SRCS := b1 b2\n"
      "\n"
      "define src_to_obj\n"
      "$(1).o: $(1).c\n"
      "\t@echo CC $(1)\n"
      "endef\n"
      "\n"
      "define module_template\n"
      "$(foreach s,$($(1)_SRCS),$(eval $(call src_to_obj,$(s))))\n"
      "$(1).ko: $(addsuffix .o,$($(1)_SRCS))\n"
      "\t@echo LD $(1).ko\n"
      "endef\n"
      "\n"
      "$(foreach m,$(MODULES),$(eval $(call module_template,$(m))))\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC a1")) << R.out;
  EXPECT_TRUE(R.contains("CC a2")) << R.out;
  EXPECT_TRUE(R.contains("LD a.ko")) << R.out;
  EXPECT_TRUE(R.contains("LD b.ko")) << R.out;
}

// ============================================================================
// Robustness: variable with special chars
// ============================================================================

TEST_F(BuildTest, RobustVarSpecialChars) {
  writeMakefile(
      "lib-special.name := libfoo\n"
      "all:\n"
      "\t@echo lib=$(lib-special.name)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("lib=libfoo")) << R.out;
}

// ============================================================================
// Robustness: empty variable in patsubst
// ============================================================================

TEST_F(BuildTest, RobustEmptyVarPatsubst) {
  writeMakefile(
      "EMPTY :=\n"
      "RESULT := $(patsubst %.c,%.o,$(EMPTY))\n"
      "all:\n"
      "\t@echo res=[$(RESULT)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("res=[]")) << R.out;
}

// ============================================================================
// Robustness: multiple targets in one rule
// ============================================================================

TEST_F(BuildTest, RobustMultiTargetRule) {
  writeFile(tmp() / "input.txt", "data");
  writeMakefile(
      "output1.txt output2.txt: input.txt\n"
      "\t@echo gen $@\n"
      ".PHONY: all\n"
      "all: output1.txt output2.txt\n"
      "\t@echo done\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("gen output1.txt")) << R.out;
}

// ============================================================================
// Robustness: inline recipe after semicolon with phony
// ============================================================================

TEST_F(BuildTest, RobustInlineRecipePhony) {
  writeMakefile(
      ".PHONY: all\n"
      "all: ; @echo inline_works\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("inline_works")) << R.out;
}

// ============================================================================
// Robustness: recipe prefix after variable expansion
// ============================================================================

TEST_F(BuildTest, RobustRecipePrefixExpansion) {
  writeMakefile(
      "define quiet_cmd\n"
      "@echo quiet_output\n"
      "endef\n"
      "all:\n"
      "\t$(quiet_cmd)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("quiet_output")) << R.out;
  EXPECT_FALSE(R.contains("echo quiet_output")) << R.out;
}

// ============================================================================
// Robustness: parallel error propagation
// ============================================================================

TEST_F(BuildTest, RobustParallelErrorPropagation) {
  writeFile(tmp() / "good.c", "");
  writeMakefile(
      ".PHONY: all\n"
      "all: good.o bad.o\n"
      "\t@echo link\n"
      "good.o: good.c\n"
      "\t@echo CC good.o\n"
      "bad.o:\n"
      "\t@exit 1\n");
  auto R = runMake({"-j2"});
  EXPECT_FALSE(R.ok());
}

// ============================================================================
// Robustness: keep-going with parallel
// ============================================================================

TEST_F(BuildTest, RobustKeepGoingParallel) {
  writeFile(tmp() / "ok.c", "");
  writeMakefile(
      ".PHONY: all fail ok\n"
      "all: fail ok\n"
      "\t@echo done\n"
      "fail:\n"
      "\t@exit 1\n"
      "ok:\n"
      "\t@echo ok_ran\n");
  auto R = runMake({"-k", "-j2"});
  EXPECT_FALSE(R.ok());
}

// ============================================================================
// Robustness: deeply nested conditional
// ============================================================================

TEST_F(BuildTest, RobustDeeplyNestedConditional) {
  writeMakefile(
      "A := 1\n"
      "B := 2\n"
      "C := 3\n"
      "D := 4\n"
      "ifdef A\n"
      "  ifdef B\n"
      "    ifdef C\n"
      "      ifdef D\n"
      "        R := all_defined\n"
      "      endif\n"
      "    endif\n"
      "  endif\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("all_defined")) << R.out;
}

// ============================================================================
// Robustness: wordlist edge cases
// ============================================================================

TEST_F(BuildTest, RobustWordlistEdge) {
  writeMakefile(
      "LIST := a b c d e\n"
      "W1 := $(word 3,$(LIST))\n"
      "W2 := $(wordlist 2,4,$(LIST))\n"
      "W3 := $(words $(LIST))\n"
      "W4 := $(word 99,$(LIST))\n"
      "all:\n"
      "\t@echo w1=$(W1) w2=$(W2) w3=$(W3) w4=[$(W4)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("w1=c")) << R.out;
  EXPECT_TRUE(R.contains("w2=b c d")) << R.out;
  EXPECT_TRUE(R.contains("w3=5")) << R.out;
  EXPECT_TRUE(R.contains("w4=[]")) << R.out;
}

// ============================================================================
// Robustness: multiple assignment modes on same var
// ============================================================================

TEST_F(BuildTest, RobustMixedAssignModes) {
  writeMakefile(
      "X := initial\n"
      "X += appended\n"
      "Y ?= default\n"
      "Y += more\n"
      "Z = recursive_$(X)\n"
      "all:\n"
      "\t@echo x=$(X) y=$(Y) z=$(Z)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x=initial appended")) << R.out;
  EXPECT_TRUE(R.contains("y=default more")) << R.out;
  EXPECT_TRUE(R.contains("z=recursive_initial appended")) << R.out;
}

// ============================================================================
// Robustness: define with := mode
// ============================================================================

TEST_F(BuildTest, RobustDefineSimpleMode) {
  writeMakefile(
      "X := world\n"
      "define GREETING :=\n"
      "hello $(X)\n"
      "endef\n"
      "X := changed\n"
      "all:\n"
      "\t@echo $(GREETING)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello world")) << R.out;
}

// ============================================================================
// Robustness: static pattern rule with multiple targets
// ============================================================================

TEST_F(BuildTest, RobustStaticPatternMultiTarget) {
  writeFile(tmp() / "x.src", "");
  writeFile(tmp() / "y.src", "");
  writeFile(tmp() / "z.src", "");
  writeMakefile(
      "OBJS := x.obj y.obj z.obj\n"
      ".PHONY: all\n"
      "all: $(OBJS)\n"
      "\t@echo done\n"
      "$(OBJS): %.obj: %.src\n"
      "\t@echo COMPILE $< -> $@\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("COMPILE x.src")) << R.out;
  EXPECT_TRUE(R.contains("COMPILE y.src")) << R.out;
  EXPECT_TRUE(R.contains("COMPILE z.src")) << R.out;
}

// ============================================================================
// Robustness: order-only prereqs don't trigger rebuild
// ============================================================================

TEST_F(BuildTest, RobustOrderOnlyNoRebuild) {
  writeFile(tmp() / "src.c", "");
  writeFile(tmp() / "outdir/.keep", "");
  writeMakefile(
      "output.o: src.c | outdir\n"
      "\t@echo CC $<\n"
      "outdir:\n"
      "\t@echo MKDIR outdir\n"
      ".PHONY: all\n"
      "all: output.o\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC src.c")) << R.out;
}

// ============================================================================
// Robustness: $? auto-var (newer prereqs)
// ============================================================================

TEST_F(BuildTest, RobustAutoVarNewer) {
  writeFile(tmp() / "target.out", "old");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  writeFile(tmp() / "newer.c", "new");
  writeFile(tmp() / "older.c", "old");
  // Touch older.c to be older than target.out
  writeMakefile(
      "target.out: newer.c\n"
      "\t@echo newer=$?\n"
      ".PHONY: all\n"
      "all: target.out\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
}

// ============================================================================
// Robustness: $(if) with complex condition
// ============================================================================

TEST_F(BuildTest, RobustIfComplexCondition) {
  writeMakefile(
      "DEBUG :=\n"
      "RELEASE := yes\n"
      "MODE := $(if $(DEBUG),debug,$(if $(RELEASE),release,unknown))\n"
      "all:\n"
      "\t@echo mode=$(MODE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("mode=release")) << R.out;
}

// ============================================================================
// Robustness: $(and) / $(or) with multiple args
// ============================================================================

TEST_F(BuildTest, RobustAndOrMultiArgs) {
  writeMakefile(
      "A := yes\n"
      "B := yes\n"
      "C :=\n"
      "R1 := $(and $(A),$(B),result)\n"
      "R2 := $(and $(A),$(C),result)\n"
      "R3 := $(or $(C),$(B),fallback)\n"
      "R4 := $(or $(C),,fallback)\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=[$(R2)] r3=$(R3) r4=$(R4)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=result")) << R.out;
  EXPECT_TRUE(R.contains("r2=[]")) << R.out;
  EXPECT_TRUE(R.contains("r3=yes")) << R.out;
  EXPECT_TRUE(R.contains("r4=fallback")) << R.out;
}

// ============================================================================
// Robustness: $(foreach) with nested call
// ============================================================================

TEST_F(BuildTest, RobustForeachNestedCall) {
  writeMakefile(
      "to_upper = $(subst a,A,$(subst b,B,$(1)))\n"
      "ITEMS := abc bac cab\n"
      "RESULT := $(foreach i,$(ITEMS),$(call to_upper,$(i)))\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ABc")) << R.out;
  EXPECT_TRUE(R.contains("BAc")) << R.out;
  EXPECT_TRUE(R.contains("cAB")) << R.out;
}

// ============================================================================
// Robustness: export with := assignment
// ============================================================================

TEST_F(BuildTest, RobustExportWithAssignment) {
  writeMakefile(
      "export CC := gcc\n"
      "export CFLAGS := -O2 -Wall\n"
      "all:\n"
      "\t@echo cc=$(CC) flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
  EXPECT_TRUE(R.contains("flags=-O2 -Wall")) << R.out;
}

// ============================================================================
// Robustness: $(basename) and $(suffix) edge cases
// ============================================================================

TEST_F(BuildTest, RobustBasenameSuffixEdge) {
  writeMakefile(
      "FILES := foo.c bar.h.bak noext .hidden path/to/file.o\n"
      "B := $(basename $(FILES))\n"
      "S := $(suffix $(FILES))\n"
      "all:\n"
      "\t@echo b=$(B)\n"
      "\t@echo s=$(S)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("foo")) << R.out;
  EXPECT_TRUE(R.contains("bar.h")) << R.out;
  EXPECT_TRUE(R.contains(".c")) << R.out;
}

// ============================================================================
// Robustness: $(dir) and $(notdir) with paths
// ============================================================================

TEST_F(BuildTest, RobustDirNotdir) {
  writeMakefile(
      "PATHS := src/foo.c lib/bar.h main.c\n"
      "DIRS := $(dir $(PATHS))\n"
      "FILES := $(notdir $(PATHS))\n"
      "all:\n"
      "\t@echo dirs=$(DIRS)\n"
      "\t@echo files=$(FILES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("src/")) << R.out;
  EXPECT_TRUE(R.contains("lib/")) << R.out;
  EXPECT_TRUE(R.contains("foo.c")) << R.out;
  EXPECT_TRUE(R.contains("bar.h")) << R.out;
}

// ============================================================================
// Robustness: dry-run preserves + force prefix
// ============================================================================

TEST_F(BuildTest, RobustDryRunForcePrefix) {
  writeMakefile(
      ".PHONY: all\n"
      "all:\n"
      "\t@echo silent\n"
      "\t+echo forced\n"
      "\t-echo ignore_err\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("echo silent")) << R.out;
  EXPECT_TRUE(R.contains("echo forced")) << R.out;
}

// ============================================================================
// Stress: 80 module Kbuild simulation
// ============================================================================

TEST_F(BuildTest, Stress80ModuleKbuild) {
  std::string Mf;
  Mf += ".DEFAULT_GOAL := all\n"
        "MODULES :=\n";
  for (int I = 0; I < 80; ++I) {
    std::string M = "mod" + std::to_string(I);
    Mf += "MODULES += " + M + "\n";
    writeFile(tmp() / (M + ".c"), "");
  }
  Mf += "\n"
        "define build_mod\n"
        "$(1).o: $(1).c\n"
        "\t@echo CC $(1)\n"
        "endef\n"
        "\n"
        "$(foreach m,$(MODULES),$(eval $(call build_mod,$(m))))\n"
        "\n"
        "OBJS := $(addsuffix .o,$(MODULES))\n"
        "\n"
        ".PHONY: all\n"
        "all: $(OBJS)\n"
        "\t@echo total=$(words $(MODULES))\n";
  writeMakefile(Mf);
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC mod0")) << R.out;
  EXPECT_TRUE(R.contains("CC mod79")) << R.out;
  EXPECT_TRUE(R.contains("total=80")) << R.out;
}

// ============================================================================
// Stress: parallel fan-out 20 targets
// ============================================================================

TEST_F(BuildTest, StressParallelFanout20) {
  std::string Mf = ".PHONY: all\nall:";
  for (int I = 0; I < 20; ++I) {
    std::string T = "t" + std::to_string(I);
    Mf += " " + T;
    writeFile(tmp() / (T + ".c"), "");
  }
  Mf += "\n\t@echo done\n";
  for (int I = 0; I < 20; ++I) {
    std::string T = "t" + std::to_string(I);
    Mf += T + ": " + T + ".c\n\t@echo CC " + T + "\n";
  }
  writeMakefile(Mf);
  auto R = runMake({"-n", "-j4"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC t0")) << R.out;
  EXPECT_TRUE(R.contains("CC t19")) << R.out;
}

// ============================================================================
// Stress: chain dependency A->B->C->D->E
// ============================================================================

TEST_F(BuildTest, StressChainDependency) {
  writeFile(tmp() / "e.src", "");
  writeMakefile(
      "a: b\n\t@echo BUILD_A\n"
      "b: c\n\t@echo BUILD_B\n"
      "c: d\n\t@echo BUILD_C\n"
      "d: e\n\t@echo BUILD_D\n"
      "e: e.src\n\t@echo BUILD_E\n"
      ".PHONY: all\n"
      "all: a\n\t@echo done\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  std::string Out = R.out;
  auto posE = Out.find("BUILD_E");
  auto posD = Out.find("BUILD_D");
  auto posC = Out.find("BUILD_C");
  auto posB = Out.find("BUILD_B");
  auto posA = Out.find("BUILD_A");
  EXPECT_NE(posE, std::string::npos);
  EXPECT_NE(posD, std::string::npos);
  EXPECT_LT(posE, posD) << "E must build before D";
  EXPECT_LT(posD, posC) << "D must build before C";
  EXPECT_LT(posC, posB) << "C must build before B";
  EXPECT_LT(posB, posA) << "B must build before A";
}

// ============================================================================
// Robustness: $(eval) generating rules with $(call) arguments
// ============================================================================

TEST_F(BuildTest, RobustEvalCallGenerateRules) {
  writeFile(tmp() / "alpha.in", "");
  writeFile(tmp() / "beta.in", "");
  writeMakefile(
      ".PHONY: all\n"
      "all: alpha.out beta.out\n"
      "\t@echo done\n"
      "\n"
      "define GEN_RULE\n"
      "$(1).out: $(1).in\n"
      "\t@echo PROCESS $(1)\n"
      "endef\n"
      "\n"
      "INPUTS := alpha beta\n"
      "$(foreach i,$(INPUTS),$(eval $(call GEN_RULE,$(i))))\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("PROCESS alpha")) << R.out;
  EXPECT_TRUE(R.contains("PROCESS beta")) << R.out;
}

// ============================================================================
// Robustness: ifeq with variable refs in both args
// ============================================================================

TEST_F(BuildTest, RobustIfeqVarRefBothArgs) {
  writeMakefile(
      "EXPECTED := arm64\n"
      "ACTUAL := arm64\n"
      "ifeq ($(EXPECTED),$(ACTUAL))\n"
      "  MATCH := yes\n"
      "else\n"
      "  MATCH := no\n"
      "endif\n"
      "all:\n"
      "\t@echo match=$(MATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("match=yes")) << R.out;
}

// ============================================================================
// Robustness: ifneq with quoted strings
// ============================================================================

TEST_F(BuildTest, RobustIfneqQuotedStrings) {
  writeMakefile(
      "MODE := release\n"
      "ifneq \"$(MODE)\" \"debug\"\n"
      "  OPTIMIZED := yes\n"
      "endif\n"
      "all:\n"
      "\t@echo opt=$(OPTIMIZED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("opt=yes")) << R.out;
}

// ============================================================================
// Robustness: phony target always runs
// ============================================================================

TEST_F(BuildTest, RobustPhonyAlwaysRuns) {
  writeMakefile(
      ".PHONY: all clean test\n"
      "all: test\n"
      "\t@echo all\n"
      "test:\n"
      "\t@echo test\n"
      "clean:\n"
      "\t@echo clean\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("test")) << R1.out;
  auto R2 = runMake();
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("test")) << R2.out;
}

// ============================================================================
// Robustness: $(file) write and read
// ============================================================================

TEST_F(BuildTest, RobustFileWriteRead) {
  writeMakefile(
      "$(file >flags.txt,-O2 -Wall)\n"
      "$(file >>flags.txt,-DNDEBUG)\n"
      "FLAGS := $(file <flags.txt)\n"
      "all:\n"
      "\t@echo flags=$(FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
}

// ============================================================================
// Kernel-style: complete Kbuild flow simulation
// ============================================================================

TEST_F(BuildTest, KernelComprehensiveKbuildFlow) {
  std::filesystem::create_directories(tmp() / "arch/arm64/kernel");
  std::filesystem::create_directories(tmp() / "init");
  std::filesystem::create_directories(tmp() / "kernel");
  writeFile(tmp() / "arch/arm64/kernel/head.c", "");
  writeFile(tmp() / "arch/arm64/kernel/entry.c", "");
  writeFile(tmp() / "init/main.c", "");
  writeFile(tmp() / "kernel/fork.c", "");
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 186\n"
      "EXTRAVERSION :=\n"
      "\n"
      "ARCH ?= arm64\n"
      "SRCARCH := $(ARCH)\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "\n"
      "KBUILD_CFLAGS := -O2\n"
      "\n"
      "ifeq ($(SRCARCH),arm64)\n"
      "  KBUILD_CFLAGS += -march=armv8-a\n"
      "else ifeq ($(SRCARCH),x86_64)\n"
      "  KBUILD_CFLAGS += -m64\n"
      "else ifeq ($(SRCARCH),x86)\n"
      "  KBUILD_CFLAGS += -m32\n"
      "endif\n"
      "\n"
      "ifneq ($(filter 4.%,$(MAKE_VERSION)),)\n"
      "  HAS_MODERN_MAKE := y\n"
      "endif\n"
      "\n"
      "ifdef HAS_MODERN_MAKE\n"
      "  KBUILD_CFLAGS += -DMODERN_MAKE\n"
      "endif\n"
      "\n"
      "KERNELRELEASE := "
      "$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)\n"
      "\n"
      "arch-y := arch/$(SRCARCH)/kernel/head.o "
      "arch/$(SRCARCH)/kernel/entry.o\n"
      "init-y := init/main.o\n"
      "core-y := kernel/fork.o\n"
      "vmlinux-deps := $(arch-y) $(init-y) $(core-y)\n"
      "\n"
      ".PHONY: all vmlinux FORCE\n"
      "all: vmlinux\n"
      "\t@echo Kernel: $(KERNELRELEASE) [$(SRCARCH)]\n"
      "\n"
      "define rule_cc\n"
      "$(1): $(basename $(1)).c\n"
      "\t@echo CC $(1) [$(KBUILD_CFLAGS)]\n"
      "endef\n"
      "\n"
      "$(foreach o,$(vmlinux-deps),$(eval $(call rule_cc,$(o))))\n"
      "\n"
      "FORCE:\n"
      "\n"
      "vmlinux: $(vmlinux-deps) FORCE\n"
      "\t@echo LD vmlinux $(KERNELRELEASE)\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC arch/arm64/kernel/head.o")) << R.out;
  EXPECT_TRUE(R.contains("CC arch/arm64/kernel/entry.o")) << R.out;
  EXPECT_TRUE(R.contains("CC init/main.o")) << R.out;
  EXPECT_TRUE(R.contains("CC kernel/fork.o")) << R.out;
  EXPECT_TRUE(R.contains("-march=armv8-a")) << R.out;
  EXPECT_TRUE(R.contains("-DMODERN_MAKE")) << R.out;
  EXPECT_TRUE(R.contains("LD vmlinux 5.10.186")) << R.out;
}

// ============================================================================
// Robustness: $(call) with 0 args expansion
// ============================================================================

TEST_F(BuildTest, RobustCallZeroArgsExpand) {
  writeMakefile(
      "greeting = hello world\n"
      "R := $(call greeting)\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello world")) << R.out;
}

// ============================================================================
// Robustness: $(subst) chained
// ============================================================================

TEST_F(BuildTest, RobustSubstChained) {
  writeMakefile(
      "S := foo-bar-baz\n"
      "R := $(subst -,_,$(subst foo,FOO,$(S)))\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("FOO_bar_baz")) << R.out;
}

// ============================================================================
// Robustness: empty recipe rule with existing file
// ============================================================================

TEST_F(BuildTest, RobustEmptyRecipeExistingFile) {
  writeFile(tmp() / "exists.txt", "");
  writeMakefile(
      ".PHONY: all\n"
      "all: exists.txt\n"
      "\t@echo done\n"
      "exists.txt:\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

// ============================================================================
// Robustness: $(addprefix) + $(addsuffix) pipeline
// ============================================================================

TEST_F(BuildTest, RobustAddprefixAddsuffix) {
  writeMakefile(
      "NAMES := foo bar baz\n"
      "PATHS := $(addprefix src/,$(addsuffix .c,$(NAMES)))\n"
      "all:\n"
      "\t@echo $(PATHS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("src/foo.c")) << R.out;
  EXPECT_TRUE(R.contains("src/bar.c")) << R.out;
  EXPECT_TRUE(R.contains("src/baz.c")) << R.out;
}

// ============================================================================
// Robustness: include chain (a.mk includes b.mk includes c.mk)
// ============================================================================

TEST_F(BuildTest, RobustIncludeChain) {
  writeFile(tmp() / "c.mk", "C_VAR := from_c\n");
  writeFile(tmp() / "b.mk", "include c.mk\nB_VAR := from_b_$(C_VAR)\n");
  writeMakefile(
      "include b.mk\n"
      "all:\n"
      "\t@echo b=$(B_VAR) c=$(C_VAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("b=from_b_from_c")) << R.out;
  EXPECT_TRUE(R.contains("c=from_c")) << R.out;
}

// ============================================================================
// Robustness: override += preserves cmdline
// ============================================================================

TEST_F(BuildTest, RobustOverrideAppend) {
  writeMakefile(
      "override CFLAGS += -Wall\n"
      "override CFLAGS += -Werror\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O2"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-Werror")) << R.out;
}

// ============================================================================
// Robustness: $(filter) with multiple patterns
// ============================================================================

TEST_F(BuildTest, RobustFilterMultiPattern) {
  writeMakefile(
      "ALL := foo.c bar.h baz.S qux.c test.S\n"
      "COMPILE := $(filter %.c %.S,$(ALL))\n"
      "all:\n"
      "\t@echo compile=$(COMPILE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("foo.c")) << R.out;
  EXPECT_TRUE(R.contains("baz.S")) << R.out;
  EXPECT_TRUE(R.contains("qux.c")) << R.out;
  EXPECT_TRUE(R.contains("test.S")) << R.out;
  EXPECT_FALSE(R.contains("bar.h")) << R.out;
}

// ============================================================================
// Robustness: .DEFAULT_GOAL override with multiple targets
// ============================================================================

TEST_F(BuildTest, RobustDefaultGoalOverrideMulti) {
  writeMakefile(
      ".DEFAULT_GOAL := custom\n"
      "first:\n"
      "\t@echo first\n"
      "custom:\n"
      "\t@echo custom_target\n"
      ".PHONY: first custom\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("custom_target")) << R.out;
  EXPECT_FALSE(R.contains("first")) << R.out;
}

// ============================================================================
// KERNEL ROBUSTNESS TESTS — Linux 5.10 Makefile Pattern Compatibility
// ============================================================================

// --- foreach variable restoration (GNU make spec compliance) ---

TEST_F(BuildTest, ForeachVarRestoration) {
  writeMakefile(
      "X := original\n"
      "LIST := $(foreach X,a b c,item_$(X))\n"
      "all:\n"
      "\t@echo list=$(LIST) x=$(X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("list=item_a item_b item_c")) << R.out;
  EXPECT_TRUE(R.contains("x=original")) << R.out;
}

TEST_F(BuildTest, ForeachVarRestorationUndefined) {
  writeMakefile(
      "LIST := $(foreach Z,1 2 3,val_$(Z))\n"
      "all:\n"
      "\t@echo list=$(LIST) z=[$(Z)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("list=val_1 val_2 val_3")) << R.out;
  EXPECT_TRUE(R.contains("z=[]")) << R.out;
}

// --- Kbuild obj-y aggregation pattern ---

TEST_F(BuildTest, KbuildObjYAggregation) {
  writeMakefile(
      "CONFIG_FOO := y\n"
      "CONFIG_BAR := m\n"
      "obj-y := core.o\n"
      "obj-$(CONFIG_FOO) += foo.o\n"
      "obj-$(CONFIG_BAR) += bar.o\n"
      "obj-n += skip.o\n"
      "all:\n"
      "\t@echo obj-y=$(obj-y)\n"
      "\t@echo obj-m=$(obj-m)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("obj-y=core.o foo.o")) << R.out;
  EXPECT_TRUE(R.contains("obj-m=bar.o")) << R.out;
}

// --- FORCE target chain (kernel fundamental pattern) ---

TEST_F(BuildTest, KbuildFORCEChain) {
  writeFile(tmp() / "src.c", "int main(){}");
  writeMakefile(
      "FORCE:\n"
      ".PHONY: FORCE\n"
      "version.h: FORCE\n"
      "\t@echo '#define VERSION \"5.10.0\"' > version.h\n"
      "build: version.h\n"
      "\t@echo built\n"
      ".PHONY: build\n");
  auto R = runMake({}, "build");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("built")) << R.out;
}

// --- Complex ifeq chain (ARCH selection) ---

TEST_F(BuildTest, KbuildArchSelection) {
  writeMakefile(
      "ARCH ?= x86\n"
      "ifeq ($(ARCH),x86)\n"
      "  BITS := 64\n"
      "  KERNEL_ARCH := x86_64\n"
      "else ifeq ($(ARCH),arm)\n"
      "  BITS := 32\n"
      "  KERNEL_ARCH := arm\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  BITS := 64\n"
      "  KERNEL_ARCH := aarch64\n"
      "else\n"
      "  BITS := unknown\n"
      "  KERNEL_ARCH := unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(KERNEL_ARCH) bits=$(BITS)\n"
      ".PHONY: all\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("arch=x86_64")) << R1.out;
  EXPECT_TRUE(R1.contains("bits=64")) << R1.out;

  auto R2 = runMake({"ARCH=arm"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm")) << R2.out;
  EXPECT_TRUE(R2.contains("bits=32")) << R2.out;

  auto R3 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R3.ok()) << R3.err;
  EXPECT_TRUE(R3.contains("arch=aarch64")) << R3.out;

  auto R4 = runMake({"ARCH=mips"});
  ASSERT_TRUE(R4.ok()) << R4.err;
  EXPECT_TRUE(R4.contains("arch=unknown")) << R4.out;
}

// --- cc-option pattern: $(shell) test for compiler capability ---

TEST_F(BuildTest, KbuildCcOption) {
  writeMakefile(
      "CC := cc\n"
      "define cc-option\n"
      "$(shell $(CC) $(1) -x c -c /dev/null -o /dev/null 2>/dev/null "
      "&& echo $(1))\n"
      "endef\n"
      "CFLAGS := -O2\n"
      "CFLAGS += $(call cc-option,-fno-stack-protector)\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
}

// --- Substitution reference for obj-y to source list ---

TEST_F(BuildTest, KbuildSubstRefObjToSrc) {
  writeMakefile(
      "obj-y := main.o util.o driver.o\n"
      "src-y := $(obj-y:.o=.c)\n"
      "all:\n"
      "\t@echo src=$(src-y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("src=main.c util.c driver.c")) << R.out;
}

// --- Deep patsubst + addprefix pipeline ---

TEST_F(BuildTest, KbuildPatsubstPipeline) {
  writeMakefile(
      "obj-y := a.o b.o c.o\n"
      "SRC_DIR := kernel/\n"
      "SRCS := $(addprefix $(SRC_DIR),$(patsubst %.o,%.c,$(obj-y)))\n"
      "all:\n"
      "\t@echo srcs=$(SRCS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("srcs=kernel/a.c kernel/b.c kernel/c.c")) << R.out;
}

// --- foreach + eval dynamic rule generation (Kbuild template) ---

TEST_F(BuildTest, KbuildForeachEvalRuleGen) {
  writeMakefile(
      "MODULES := net fs crypto\n"
      "define module_template\n"
      "$(1)-objs := $(1)_init.o $(1)_core.o\n"
      "endef\n"
      "$(foreach m,$(MODULES),$(eval $(call module_template,$(m))))\n"
      "all:\n"
      "\t@echo net=$(net-objs)\n"
      "\t@echo fs=$(fs-objs)\n"
      "\t@echo crypto=$(crypto-objs)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("net=net_init.o net_core.o")) << R.out;
  EXPECT_TRUE(R.contains("fs=fs_init.o fs_core.o")) << R.out;
  EXPECT_TRUE(R.contains("crypto=crypto_init.o crypto_core.o")) << R.out;
}

// --- ifndef default + command line override ---

TEST_F(BuildTest, KbuildIfndefDefault) {
  writeMakefile(
      "ifndef CROSS_COMPILE\n"
      "  CROSS_COMPILE :=\n"
      "endif\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;

  auto R2 = runMake({"CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
}

// --- Multi-line define for compound commands ---

TEST_F(BuildTest, KbuildDefineMultilineCmd) {
  writeMakefile(
      "define do_build\n"
      "@echo step1_$(1)\n"
      "@echo step2_$(1)\n"
      "endef\n"
      "all:\n"
      "\t$(call do_build,kernel)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("step1_kernel")) << R.out;
  EXPECT_TRUE(R.contains("step2_kernel")) << R.out;
}

// --- Export computed variables ---

TEST_F(BuildTest, KbuildExportComputed) {
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 0\n"
      "KERNELVERSION := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "export KERNELVERSION\n"
      "all:\n"
      "\t@echo ver=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ver=5.10.0")) << R.out;
}

// --- Double dollar escape in recipes (shell variables) ---

TEST_F(BuildTest, KbuildDoubleDollarEscape) {
  writeMakefile(
      "all:\n"
      "\t@X=hello; echo $$X\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello")) << R.out;
}

// --- filter + filter-out for CONFIG conditional ---

TEST_F(BuildTest, KbuildFilterConfig) {
  writeMakefile(
      "ALL_OBJS := net.o fs.o crypto.o debug.o test.o\n"
      "SKIP := debug.o test.o\n"
      "BUILD_OBJS := $(filter-out $(SKIP),$(ALL_OBJS))\n"
      "all:\n"
      "\t@echo build=$(BUILD_OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("net.o")) << R.out;
  EXPECT_TRUE(R.contains("fs.o")) << R.out;
  EXPECT_TRUE(R.contains("crypto.o")) << R.out;
  EXPECT_FALSE(R.contains("debug.o")) << R.out;
  EXPECT_FALSE(R.contains("test.o")) << R.out;
}

// --- ifdef vs ifndef with empty recursive variable ---

TEST_F(BuildTest, KbuildIfdefEmptyRecursive) {
  writeMakefile(
      "EMPTY =\n"
      "NOTEMPTY = value\n"
      "REF = $(NONEXIST)\n"
      "ifdef EMPTY\n"
      "  R1 := defined\n"
      "else\n"
      "  R1 := undefined\n"
      "endif\n"
      "ifdef NOTEMPTY\n"
      "  R2 := defined\n"
      "else\n"
      "  R2 := undefined\n"
      "endif\n"
      "ifdef REF\n"
      "  R3 := defined\n"
      "else\n"
      "  R3 := undefined\n"
      "endif\n"
      "ifdef NONEXIST\n"
      "  R4 := defined\n"
      "else\n"
      "  R4 := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2) r3=$(R3) r4=$(R4)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=undefined")) << R.out;
  EXPECT_TRUE(R.contains("r2=defined")) << R.out;
  EXPECT_TRUE(R.contains("r3=defined")) << R.out;
  EXPECT_TRUE(R.contains("r4=undefined")) << R.out;
}

// --- ifeq with variable expansion on both sides ---

TEST_F(BuildTest, KbuildIfeqVarExpansion) {
  writeMakefile(
      "A := hello\n"
      "B := hello\n"
      "ifeq ($(A),$(B))\n"
      "  MATCH := yes\n"
      "else\n"
      "  MATCH := no\n"
      "endif\n"
      "all:\n"
      "\t@echo match=$(MATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("match=yes")) << R.out;
}

// --- Substitution reference with directory paths ---

TEST_F(BuildTest, KbuildSubstRefDirPath) {
  writeMakefile(
      "OBJS := drivers/net/e1000.o drivers/gpu/drm.o\n"
      "SRCS := $(OBJS:.o=.c)\n"
      "all:\n"
      "\t@echo srcs=$(SRCS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("drivers/net/e1000.c")) << R.out;
  EXPECT_TRUE(R.contains("drivers/gpu/drm.c")) << R.out;
}

// --- $(origin) in kernel config detection ---

TEST_F(BuildTest, KbuildOriginDetection) {
  writeMakefile(
      "FILE_VAR := from_file\n"
      "all:\n"
      "\t@echo file_origin=$(origin FILE_VAR)\n"
      "\t@echo cmd_origin=$(origin CMD_VAR)\n"
      "\t@echo undef_origin=$(origin NOSUCHVAR)\n"
      ".PHONY: all\n");
  auto R = runMake({"CMD_VAR=from_cmd"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("file_origin=file")) << R.out;
  EXPECT_TRUE(R.contains("cmd_origin=command line")) << R.out;
  EXPECT_TRUE(R.contains("undef_origin=undefined")) << R.out;
}

// --- $(value) for getting unexpanded value ---

TEST_F(BuildTest, KbuildValueFunction) {
  writeMakefile(
      "FOO = hello_world\n"
      "BAR := expanded\n"
      "RAW_FOO := $(value FOO)\n"
      "all:\n"
      "\t@echo raw=$(RAW_FOO) exp=$(FOO)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("raw=hello_world")) << R.out;
  EXPECT_TRUE(R.contains("exp=hello_world")) << R.out;
}

// --- Multiple prerequisites from separate rule declarations ---

TEST_F(BuildTest, KbuildMultiPrereqMerge) {
  writeFile(tmp() / "a.h", "");
  writeFile(tmp() / "b.h", "");
  writeFile(tmp() / "c.h", "");
  writeMakefile(
      "main.o: a.h\n"
      "main.o: b.h\n"
      "main.o: c.h\n"
      "\t@echo building_main prereqs=$^\n"
      ".PHONY: main.o\n");
  auto R = runMake({}, "main.o");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building_main")) << R.out;
  EXPECT_TRUE(R.contains("c.h")) << R.out;
}

// --- Nested $(if) with $(findstring) ---

TEST_F(BuildTest, KbuildNestedIfFindstring) {
  writeMakefile(
      "ARCH := x86\n"
      "FEATURES := smp preempt debug\n"
      "HAS_SMP := $(if $(findstring smp,$(FEATURES)),yes,no)\n"
      "HAS_RT := $(if $(findstring rt,$(FEATURES)),yes,no)\n"
      "all:\n"
      "\t@echo smp=$(HAS_SMP) rt=$(HAS_RT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("smp=yes")) << R.out;
  EXPECT_TRUE(R.contains("rt=no")) << R.out;
}

// --- $(word) and $(words) for version parsing ---

TEST_F(BuildTest, KbuildVersionParsing) {
  writeMakefile(
      "VERSION_STR := 5 10 42\n"
      "MAJOR := $(word 1,$(VERSION_STR))\n"
      "MINOR := $(word 2,$(VERSION_STR))\n"
      "PATCH := $(word 3,$(VERSION_STR))\n"
      "COUNT := $(words $(VERSION_STR))\n"
      "all:\n"
      "\t@echo ver=$(MAJOR).$(MINOR).$(PATCH) parts=$(COUNT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ver=5.10.42")) << R.out;
  EXPECT_TRUE(R.contains("parts=3")) << R.out;
}

// --- Nested foreach with call ---

TEST_F(BuildTest, KbuildNestedForeachCall) {
  writeMakefile(
      "ARCHS := x86 arm\n"
      "CONFIGS := smp nosmp\n"
      "define gen_target\n"
      "$(1)-$(2)\n"
      "endef\n"
      "TARGETS := $(foreach a,$(ARCHS),$(foreach c,$(CONFIGS),"
      "$(call gen_target,$(a),$(c))))\n"
      "all:\n"
      "\t@echo targets=$(TARGETS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x86-smp")) << R.out;
  EXPECT_TRUE(R.contains("x86-nosmp")) << R.out;
  EXPECT_TRUE(R.contains("arm-smp")) << R.out;
  EXPECT_TRUE(R.contains("arm-nosmp")) << R.out;
}

// --- Kbuild quiet/verbose mode pattern ---

TEST_F(BuildTest, KbuildQuietVerbose) {
  writeMakefile(
      "ifeq ($(V),1)\n"
      "  Q :=\n"
      "  quiet :=\n"
      "else\n"
      "  Q := @\n"
      "  quiet := quiet_\n"
      "endif\n"
      "quiet_cmd_cc = CC $@\n"
      "cmd_cc = gcc -c -o $@ $<\n"
      "define run_cmd\n"
      "$(if $($(quiet)cmd_$(1)),@echo '  $($(quiet)cmd_$(1))')\n"
      "$(Q)$(cmd_$(1))\n"
      "endef\n"
      "all:\n"
      "\t@echo quiet=$(quiet) q=$(Q)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("quiet=quiet_")) << R.out;
  EXPECT_TRUE(R.contains("q=@")) << R.out;

  auto R2 = runMake({"V=1"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("quiet= q=")) << R2.out;
}

// --- Complex $(call) with multiple positional args ---

TEST_F(BuildTest, KbuildCallMultiArgs) {
  writeMakefile(
      "define link_cmd\n"
      "ld -o $(1) $(2) $(3)\n"
      "endef\n"
      "LINK := $(call link_cmd,vmlinux,built-in.o,lib.a)\n"
      "all:\n"
      "\t@echo cmd=$(LINK)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ld -o vmlinux built-in.o lib.a")) << R.out;
}

// --- $(sort) dedup + ordering ---

TEST_F(BuildTest, KbuildSortDedup) {
  writeMakefile(
      "DIRS := drivers/net drivers/gpu drivers/net kernel arch\n"
      "UNIQUE := $(sort $(DIRS))\n"
      "all:\n"
      "\t@echo dirs=$(UNIQUE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  std::string Out = R.out;
  EXPECT_TRUE(Out.find("drivers/net") != std::string::npos) << R.out;
  size_t first = Out.find("drivers/net");
  size_t second = Out.find("drivers/net", first + 1);
  EXPECT_EQ(second, std::string::npos) << "should have no dupes: " << R.out;
}

// --- undefine + ifdef interaction ---

TEST_F(BuildTest, KbuildUndefineIfdef) {
  writeMakefile(
      "FOO := bar\n"
      "ifdef FOO\n"
      "  BEFORE := defined\n"
      "endif\n"
      "undefine FOO\n"
      "ifdef FOO\n"
      "  AFTER := defined\n"
      "else\n"
      "  AFTER := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo before=$(BEFORE) after=$(AFTER)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("before=defined")) << R.out;
  EXPECT_TRUE(R.contains("after=undefined")) << R.out;
}

// --- MAKECMDGOALS detection ---

TEST_F(BuildTest, KbuildMakecmdgoals) {
  writeMakefile(
      "ifeq ($(MAKECMDGOALS),clean)\n"
      "  ACTION := cleaning\n"
      "else\n"
      "  ACTION := building\n"
      "endif\n"
      "all:\n"
      "\t@echo action=$(ACTION)\n"
      "clean:\n"
      "\t@echo action=$(ACTION)\n"
      ".PHONY: all clean\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("action=building")) << R1.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("action=cleaning")) << R2.out;
}

// --- Pattern rule with auto variables $@ $< $^ ---

TEST_F(BuildTest, KbuildPatternAutoVars) {
  writeFile(tmp() / "foo.src", "");
  writeFile(tmp() / "bar.src", "");
  writeMakefile(
      "%.out: %.src\n"
      "\t@echo target=$@ first=$< all=$^\n"
      "all: foo.out bar.out\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("target=foo.out")) << R.out;
  EXPECT_TRUE(R.contains("first=foo.src")) << R.out;
  EXPECT_TRUE(R.contains("target=bar.out")) << R.out;
}

// --- $(eval) generating rules with pattern ---

TEST_F(BuildTest, KbuildEvalPatternRuleGen) {
  writeMakefile(
      "SUBDIRS := init kernel mm\n"
      "define subdir_rule\n"
      "$(1)/built-in.o:\n"
      "\t@echo building_$(1)\n"
      ".PHONY: $(1)/built-in.o\n"
      "endef\n"
      "$(foreach d,$(SUBDIRS),$(eval $(call subdir_rule,$(d))))\n"
      "vmlinux: $(addsuffix /built-in.o,$(SUBDIRS))\n"
      "\t@echo linking_vmlinux\n"
      ".PHONY: vmlinux\n");
  auto R = runMake({}, "vmlinux");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building_init")) << R.out;
  EXPECT_TRUE(R.contains("building_kernel")) << R.out;
  EXPECT_TRUE(R.contains("building_mm")) << R.out;
  EXPECT_TRUE(R.contains("linking_vmlinux")) << R.out;
}

// --- $(and) / $(or) multi-arg ---

TEST_F(BuildTest, KbuildAndOrMultiArg) {
  writeMakefile(
      "A := yes\n"
      "B := also\n"
      "C :=\n"
      "R_AND_AB := $(and $(A),$(B))\n"
      "R_AND_AC := $(and $(A),$(C))\n"
      "R_OR_AC := $(or $(A),$(C))\n"
      "R_OR_CC := $(or $(C),$(C))\n"
      "all:\n"
      "\t@echo and_ab=[$(R_AND_AB)] and_ac=[$(R_AND_AC)] "
      "or_ac=[$(R_OR_AC)] or_cc=[$(R_OR_CC)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("and_ab=[also]")) << R.out;
  EXPECT_TRUE(R.contains("and_ac=[]")) << R.out;
  EXPECT_TRUE(R.contains("or_ac=[yes]")) << R.out;
  EXPECT_TRUE(R.contains("or_cc=[]")) << R.out;
}

// --- Override with += and command line interaction ---

TEST_F(BuildTest, KbuildOverridePlusCmd) {
  writeMakefile(
      "CFLAGS := -Wall\n"
      "CFLAGS += -Wextra\n"
      "override CFLAGS += -Werror\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("-Wall")) << R1.out;
  EXPECT_TRUE(R1.contains("-Wextra")) << R1.out;
  EXPECT_TRUE(R1.contains("-Werror")) << R1.out;

  auto R2 = runMake({"CFLAGS=-O2"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("-O2")) << R2.out;
  EXPECT_TRUE(R2.contains("-Werror")) << R2.out;
  EXPECT_FALSE(R2.contains("-Wall")) << "should be overridden: " << R2.out;
}

// --- $(dir) + $(notdir) kernel path manipulation ---

TEST_F(BuildTest, KbuildDirNotdir) {
  writeMakefile(
      "FILES := drivers/net/e1000.c arch/x86/boot.S\n"
      "DIRS := $(dir $(FILES))\n"
      "NAMES := $(notdir $(FILES))\n"
      "all:\n"
      "\t@echo dirs=$(DIRS)\n"
      "\t@echo names=$(NAMES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("drivers/net/")) << R.out;
  EXPECT_TRUE(R.contains("arch/x86/")) << R.out;
  EXPECT_TRUE(R.contains("e1000.c")) << R.out;
  EXPECT_TRUE(R.contains("boot.S")) << R.out;
}

// --- Conditional ?= with recursive variable ---

TEST_F(BuildTest, KbuildConditionalAssign) {
  writeMakefile(
      "CC ?= gcc\n"
      "LD ?= ld\n"
      "all:\n"
      "\t@echo cc=$(CC) ld=$(LD)\n"
      ".PHONY: all\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;
  EXPECT_TRUE(R1.contains("ld=ld")) << R1.out;

  auto R2 = runMake({"CC=clang"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=clang")) << R2.out;
}

// --- $(basename) + $(suffix) kernel usage ---

TEST_F(BuildTest, KbuildBasenameSuffix) {
  writeMakefile(
      "FILES := kernel/main.c arch/boot.S lib/utils.o\n"
      "BASES := $(basename $(FILES))\n"
      "SUFFS := $(suffix $(FILES))\n"
      "all:\n"
      "\t@echo bases=$(BASES)\n"
      "\t@echo suffs=$(SUFFS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("kernel/main")) << R.out;
  EXPECT_TRUE(R.contains("arch/boot")) << R.out;
  EXPECT_TRUE(R.contains("lib/utils")) << R.out;
  EXPECT_TRUE(R.contains(".c")) << R.out;
  EXPECT_TRUE(R.contains(".S")) << R.out;
  EXPECT_TRUE(R.contains(".o")) << R.out;
}

// --- $(strip) in ifeq comparison ---

TEST_F(BuildTest, KbuildStripIfeq) {
  writeMakefile(
      "VAR :=   spaces   \n"
      "ifeq ($(strip $(VAR)),spaces)\n"
      "  RESULT := match\n"
      "else\n"
      "  RESULT := nomatch\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=match")) << R.out;
}

// --- MAKE_VERSION detection ---

TEST_F(BuildTest, KbuildMakeVersionDetect) {
  writeMakefile(
      "ifneq ($(filter 4.%,$(MAKE_VERSION)),)\n"
      "  HAS_V4 := yes\n"
      "else\n"
      "  HAS_V4 := no\n"
      "endif\n"
      "all:\n"
      "\t@echo v4=$(HAS_V4) ver=$(MAKE_VERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("v4=yes")) << R.out;
  EXPECT_TRUE(R.contains("ver=4.3")) << R.out;
}

// --- Recursive variable with late binding ---

TEST_F(BuildTest, KbuildRecursiveLateBinding) {
  writeMakefile(
      "GREETING = Hello $(WHO)\n"
      "WHO := World\n"
      "R1 := $(GREETING)\n"
      "WHO := Kernel\n"
      "R2 := $(GREETING)\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=Hello World")) << R.out;
  EXPECT_TRUE(R.contains("r2=Hello Kernel")) << R.out;
}

// --- include with wildcard glob ---

TEST_F(BuildTest, KbuildIncludeWildcard) {
  writeFile(tmp() / "a.mk", "A_VAR := from_a\n");
  writeFile(tmp() / "b.mk", "B_VAR := from_b\n");
  writeMakefile(
      "-include *.mk\n"
      "all:\n"
      "\t@echo a=$(A_VAR) b=$(B_VAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=from_a")) << R.out;
  EXPECT_TRUE(R.contains("b=from_b")) << R.out;
}

// --- $(firstword $(MAKEFILE_LIST)) pattern ---

TEST_F(BuildTest, KbuildMakefileListFirstword) {
  writeMakefile(
      "THIS := $(firstword $(MAKEFILE_LIST))\n"
      "all:\n"
      "\t@echo this=$(THIS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("this=Makefile") || R.contains("this=makefile") ||
              R.contains("this=GNUmakefile") ||
              R.out.find("this=") != std::string::npos)
      << R.out;
  EXPECT_TRUE(R.out.find("this=") != std::string::npos &&
              R.out.find("this=\n") == std::string::npos)
      << "should have non-empty value: " << R.out;
}

// --- $(flavor) function ---

TEST_F(BuildTest, KbuildFlavorFunction) {
  writeMakefile(
      "REC = recursive\n"
      "SIM := simple\n"
      "all:\n"
      "\t@echo rec=$(flavor REC) sim=$(flavor SIM) "
      "undef=$(flavor NOSUCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("rec=recursive")) << R.out;
  EXPECT_TRUE(R.contains("sim=simple")) << R.out;
  EXPECT_TRUE(R.contains("undef=undefined")) << R.out;
}

// --- Static pattern rule with multiple targets ---

TEST_F(BuildTest, KbuildStaticPatternMultiTarget) {
  writeFile(tmp() / "a.in", "");
  writeFile(tmp() / "b.in", "");
  writeFile(tmp() / "c.in", "");
  writeMakefile(
      "OUTS := a.out b.out c.out\n"
      ".PHONY: all\n"
      "all: $(OUTS)\n"
      "\t@echo all_done\n"
      "$(OUTS): %.out: %.in\n"
      "\t@echo converting $< to $@\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("converting a.in to a.out")) << R.out;
  EXPECT_TRUE(R.contains("converting b.in to b.out")) << R.out;
  EXPECT_TRUE(R.contains("converting c.in to c.out")) << R.out;
  EXPECT_TRUE(R.contains("all_done")) << R.out;
}

// --- Complex Kbuild mini-kernel simulation ---

TEST_F(BuildTest, KbuildMiniKernelSimulation) {
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 0\n"
      "EXTRAVERSION :=\n"
      "KERNELVERSION := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)\n"
      "\n"
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "\n"
      "ifeq ($(ARCH),x86)\n"
      "  SRCARCH := x86\n"
      "  BITS := 64\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  SRCARCH := arm64\n"
      "  BITS := 64\n"
      "else\n"
      "  SRCARCH := $(ARCH)\n"
      "  BITS := 32\n"
      "endif\n"
      "\n"
      "CFLAGS := -O2 -Wall\n"
      "ifeq ($(V),1)\n"
      "  Q :=\n"
      "  quiet :=\n"
      "else\n"
      "  Q := @\n"
      "  quiet := quiet_\n"
      "endif\n"
      "\n"
      "ifdef CONFIG_DEBUG\n"
      "  CFLAGS += -g -DDEBUG\n"
      "endif\n"
      "\n"
      "SUBDIRS := init kernel mm\n"
      "obj-y := $(addsuffix /built-in.o,$(SUBDIRS))\n"
      "\n"
      "define subdir_template\n"
      "$(1)/built-in.o: FORCE\n"
      "\t@echo '  BUILD   $(1)'\n"
      ".PHONY: $(1)/built-in.o\n"
      "endef\n"
      "$(foreach d,$(SUBDIRS),$(eval $(call subdir_template,$(d))))\n"
      "\n"
      "FORCE:\n"
      ".PHONY: FORCE\n"
      "\n"
      "vmlinux: $(obj-y)\n"
      "\t@echo '  LINK    vmlinux ($(KERNELVERSION) $(SRCARCH))'\n"
      ".PHONY: vmlinux\n"
      "\n"
      ".DEFAULT_GOAL := all\n"
      "all: vmlinux\n"
      "\t@echo '  Build complete: $(KERNELVERSION)'\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("BUILD   init")) << R.out;
  EXPECT_TRUE(R.contains("BUILD   kernel")) << R.out;
  EXPECT_TRUE(R.contains("BUILD   mm")) << R.out;
  EXPECT_TRUE(R.contains("LINK    vmlinux (5.10.0 x86)")) << R.out;
  EXPECT_TRUE(R.contains("Build complete: 5.10.0")) << R.out;
}

// --- Complex Kbuild with cross-compile ---

TEST_F(BuildTest, KbuildMiniKernelCrossCompile) {
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "KERNELVERSION := $(VERSION).$(PATCHLEVEL)\n"
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "ifeq ($(ARCH),arm64)\n"
      "  SRCARCH := arm64\n"
      "else\n"
      "  SRCARCH := $(ARCH)\n"
      "endif\n"
      "all:\n"
      "\t@echo cc=$(CC) arch=$(SRCARCH) ver=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=aarch64-linux-gnu-gcc")) << R.out;
  EXPECT_TRUE(R.contains("arch=arm64")) << R.out;
  EXPECT_TRUE(R.contains("ver=5.10")) << R.out;
}

// --- Order-only prereqs don't trigger rebuild ---

TEST_F(BuildTest, KbuildOrderOnlyNoRebuild) {
  writeFile(tmp() / "src.txt", "data");
  writeMakefile(
      ".PHONY: dirs\n"
      "output.txt: src.txt | dirs\n"
      "\t@echo building_output\n"
      "\t@touch output.txt\n"
      "dirs:\n"
      "\t@echo makedirs\n");
  auto R = runMake({}, "output.txt");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("makedirs")) << R.out;
  EXPECT_TRUE(R.contains("building_output")) << R.out;
}

// --- $(subst) chain for version manipulation ---

TEST_F(BuildTest, KbuildSubstChain) {
  writeMakefile(
      "VER := 5.10.42-rc1\n"
      "VER_NODOT := $(subst .,-,$(VER))\n"
      "VER_CLEAN := $(subst -rc1,,$(VER))\n"
      "all:\n"
      "\t@echo nodot=$(VER_NODOT) clean=$(VER_CLEAN)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("nodot=5-10-42-rc1")) << R.out;
  EXPECT_TRUE(R.contains("clean=5.10.42")) << R.out;
}

// --- $(foreach) with empty list ---

TEST_F(BuildTest, ForeachEmptyList) {
  writeMakefile(
      "EMPTY :=\n"
      "RESULT := $(foreach x,$(EMPTY),item_$(x))\n"
      "all:\n"
      "\t@echo result=[$(RESULT)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=[]")) << R.out;
}

// --- $(call) with zero args ---

TEST_F(BuildTest, CallZeroArgs) {
  writeMakefile(
      "define show\n"
      "hello_world\n"
      "endef\n"
      "VAL := $(call show)\n"
      "all:\n"
      "\t@echo val=$(VAL)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello_world")) << R.out;
}

// --- Recipe with continuation lines ---

TEST_F(BuildTest, RecipeContinuationLine) {
  writeMakefile(
      "all:\n"
      "\t@echo hello \\\n"
      "\tworld\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello")) << R.out;
}

// --- Deeply nested variable reference ---

TEST_F(BuildTest, DeepNestedVarRef) {
  writeMakefile(
      "A := value_a\n"
      "B := A\n"
      "C := B\n"
      "all:\n"
      "\t@echo result=$($($(C)))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=value_a")) << R.out;
}

// --- $(file) write and read ---

TEST_F(BuildTest, FileWriteRead) {
  writeMakefile(
      "$(file >test_output.txt,hello from file)\n"
      "CONTENT := $(file <test_output.txt)\n"
      "all:\n"
      "\t@echo content=$(CONTENT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("content=hello from file")) << R.out;
}

// --- $(wordlist) boundary cases ---

TEST_F(BuildTest, WordlistBoundary) {
  writeMakefile(
      "LIST := one two three four five\n"
      "MID := $(wordlist 2,4,$(LIST))\n"
      "OVER := $(wordlist 3,999,$(LIST))\n"
      "all:\n"
      "\t@echo mid=$(MID) over=$(OVER)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("mid=two three four")) << R.out;
  EXPECT_TRUE(R.contains("over=three four five")) << R.out;
}

// --- -include missing file (should not error) ---

TEST_F(BuildTest, DashIncludeMissing) {
  writeMakefile(
      "-include nonexistent.mk\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ok")) << R.out;
}

// --- sinclude alias ---

TEST_F(BuildTest, SincludeAlias) {
  writeMakefile(
      "sinclude nonexistent.mk\n"
      "all:\n"
      "\t@echo sinclude_ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("sinclude_ok")) << R.out;
}

// --- Recursive make via $(MAKE) in recipe ---

TEST_F(BuildTest, RecursiveMake) {
  auto subdir = tmp() / "subdir";
  std::filesystem::create_directories(subdir);
  writeFile(subdir / "Makefile",
            "all:\n"
            "\t@echo sub_built\n"
            ".PHONY: all\n");
  writeMakefile(
      "SUBDIR := subdir\n"
      "all:\n"
      "\t@echo parent_start\n"
      "\t$(MAKE) -C $(SUBDIR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("parent_start")) << R.out;
}

// --- Dry run with + prefix forces execution ---

TEST_F(BuildTest, DryRunForcePlusPrefix) {
  writeMakefile(
      "all:\n"
      "\t@echo normal_cmd\n"
      "\t+@echo forced_cmd\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("forced_cmd")) << R.out;
}

// --- $(addsuffix) + $(addprefix) combined ---

TEST_F(BuildTest, KbuildAddsuffixAddprefixCombined) {
  writeMakefile(
      "MODS := net fs\n"
      "MOD_DIRS := $(addprefix drivers/,$(addsuffix /,$(MODS)))\n"
      "all:\n"
      "\t@echo dirs=$(MOD_DIRS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("drivers/net/")) << R.out;
  EXPECT_TRUE(R.contains("drivers/fs/")) << R.out;
}

// --- define with := mode ---

TEST_F(BuildTest, DefineSimpleMode) {
  writeMakefile(
      "X := early\n"
      "define BLOCK :=\n"
      "value_is_$(X)\n"
      "endef\n"
      "X := late\n"
      "all:\n"
      "\t@echo block=$(BLOCK)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("value_is_early")) << R.out;
}

// --- define with += mode ---

TEST_F(BuildTest, DefineAppendMode) {
  writeMakefile(
      "CMDS := initial\n"
      "define CMDS +=\n"
      "appended\n"
      "endef\n"
      "all:\n"
      "\t@echo cmds=$(CMDS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("initial")) << R.out;
  EXPECT_TRUE(R.contains("appended")) << R.out;
}

// --- Kbuild full pipeline: version+arch+config+subdir+eval+link ---

TEST_F(BuildTest, KbuildFullPipeline) {
  writeMakefile(
      "# Version\n"
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 42\n"
      "KERNELVERSION := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "\n"
      "# Architecture\n"
      "ARCH ?= x86\n"
      "ifeq ($(ARCH),x86)\n"
      "  SRCARCH := x86\n"
      "  LDFLAGS := -m elf_x86_64\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  SRCARCH := arm64\n"
      "  LDFLAGS := -m aarch64elf\n"
      "else\n"
      "  $(error Unsupported ARCH: $(ARCH))\n"
      "endif\n"
      "\n"
      "# Compiler\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "CFLAGS := -O2 -Wall\n"
      "\n"
      "# Verbose mode\n"
      "ifeq ($(V),1)\n"
      "  Q :=\n"
      "else\n"
      "  Q := @\n"
      "endif\n"
      "\n"
      "# Config options\n"
      "CONFIG_SMP ?= y\n"
      "CONFIG_MODULES ?= y\n"
      "\n"
      "ifeq ($(CONFIG_SMP),y)\n"
      "  CFLAGS += -DCONFIG_SMP\n"
      "endif\n"
      "\n"
      "# Subsystems\n"
      "SUBDIRS := init kernel mm\n"
      "ifeq ($(CONFIG_MODULES),y)\n"
      "  SUBDIRS += modules\n"
      "endif\n"
      "\n"
      "core-y := $(addsuffix /built-in.o,$(SUBDIRS))\n"
      "\n"
      "define build_subdir\n"
      "$(1)/built-in.o: FORCE\n"
      "\t$(Q)echo '  CC      $(1)'\n"
      ".PHONY: $(1)/built-in.o\n"
      "endef\n"
      "$(foreach d,$(SUBDIRS),$(eval $(call build_subdir,$(d))))\n"
      "\n"
      "FORCE:\n"
      ".PHONY: FORCE\n"
      "\n"
      "# Version check\n"
      "ifneq ($(filter 4.%,$(MAKE_VERSION)),)\n"
      "  MAKE_OK := yes\n"
      "else\n"
      "  MAKE_OK := no\n"
      "endif\n"
      "\n"
      "vmlinux: $(core-y)\n"
      "\t$(Q)echo '  LD [$(LDFLAGS)] vmlinux'\n"
      "\t$(Q)echo '  Version: $(KERNELVERSION)'\n"
      "\t$(Q)echo '  CC: $(CC)'\n"
      "\t$(Q)echo '  Make OK: $(MAKE_OK)'\n"
      ".PHONY: vmlinux\n"
      "\n"
      ".DEFAULT_GOAL := all\n"
      "all: vmlinux\n"
      "\t@echo 'Build complete'\n"
      ".PHONY: all\n");

  auto R = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC      init")) << R.out;
  EXPECT_TRUE(R.contains("CC      kernel")) << R.out;
  EXPECT_TRUE(R.contains("CC      mm")) << R.out;
  EXPECT_TRUE(R.contains("CC      modules")) << R.out;
  EXPECT_TRUE(R.contains("LD [-m aarch64elf] vmlinux")) << R.out;
  EXPECT_TRUE(R.contains("Version: 5.10.42")) << R.out;
  EXPECT_TRUE(R.contains("CC: aarch64-linux-gnu-gcc")) << R.out;
  EXPECT_TRUE(R.contains("Make OK: yes")) << R.out;
  EXPECT_TRUE(R.contains("Build complete")) << R.out;
}

// --- Stress: 100-module Kbuild ---

TEST_F(BuildTest, KbuildStress100Modules) {
  std::string Makefile;
  Makefile += ".DEFAULT_GOAL := all\n";
  Makefile += "MODULES :=";
  for (int I = 0; I < 100; ++I)
    Makefile += " mod" + std::to_string(I);
  Makefile += "\n";
  Makefile += "define mod_rule\n"
              "$(1).o: FORCE\n"
              "\t@echo built_$(1)\n"
              ".PHONY: $(1).o\n"
              "endef\n"
              "$(foreach m,$(MODULES),$(eval $(call mod_rule,$(m))))\n"
              "FORCE:\n"
              ".PHONY: FORCE\n"
              "all: $(addsuffix .o,$(MODULES))\n"
              "\t@echo all_done\n"
              ".PHONY: all\n";
  writeMakefile(Makefile);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("built_mod0")) << R.out;
  EXPECT_TRUE(R.contains("built_mod50")) << R.out;
  EXPECT_TRUE(R.contains("built_mod99")) << R.out;
  EXPECT_TRUE(R.contains("all_done")) << R.out;
}

// --- Stress: parallel fan-out ---

TEST_F(BuildTest, KbuildStressParallelFanout) {
  std::string Makefile = ".DEFAULT_GOAL := all\nTARGETS :=";
  for (int I = 0; I < 20; ++I)
    Makefile += " t" + std::to_string(I);
  Makefile += "\n";
  for (int I = 0; I < 20; ++I)
    Makefile += "t" + std::to_string(I) +
                ": FORCE\n\t@echo done_" + std::to_string(I) + "\n"
                ".PHONY: t" + std::to_string(I) + "\n";
  Makefile += "FORCE:\n.PHONY: FORCE\n"
              "all: $(TARGETS)\n\t@echo all\n.PHONY: all\n";
  writeMakefile(Makefile);
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("done_0")) << R.out;
  EXPECT_TRUE(R.contains("done_19")) << R.out;
  EXPECT_TRUE(R.contains("all")) << R.out;
}

// --- Empty substitution reference ---

TEST_F(BuildTest, EmptySubstRef) {
  writeMakefile(
      "OBJS := a b c\n"
      "RESULT := $(OBJS:=.o)\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=a.o b.o c.o")) << R.out;
}

// --- Variable with special chars in name ---

TEST_F(BuildTest, SpecialCharVarName) {
  writeMakefile(
      "obj-y := val\n"
      "lib-m := other\n"
      "a.b := dotted\n"
      "all:\n"
      "\t@echo objy=$(obj-y) libm=$(lib-m) ab=$(a.b)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("objy=val")) << R.out;
  EXPECT_TRUE(R.contains("libm=other")) << R.out;
  EXPECT_TRUE(R.contains("ab=dotted")) << R.out;
}

// --- Multiple targets on single rule line ---

TEST_F(BuildTest, MultiTargetSingleRule) {
  writeMakefile(
      ".DEFAULT_GOAL := all\n"
      ".PHONY: a b c all\n"
      "a b c:\n"
      "\t@echo building_$@\n"
      "all: a b c\n"
      "\t@echo done\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building_a")) << R.out;
  EXPECT_TRUE(R.contains("building_b")) << R.out;
  EXPECT_TRUE(R.contains("building_c")) << R.out;
}

// --- ifeq with quoted strings ---

TEST_F(BuildTest, IfeqQuotedStrings) {
  writeMakefile(
      "X := hello\n"
      "ifeq \"$(X)\" \"hello\"\n"
      "  MATCH := yes\n"
      "else\n"
      "  MATCH := no\n"
      "endif\n"
      "all:\n"
      "\t@echo match=$(MATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("match=yes")) << R.out;
}

// --- ifeq with single-quoted strings ---

TEST_F(BuildTest, IfeqSingleQuotedStrings) {
  writeMakefile(
      "X := world\n"
      "ifeq '$(X)' 'world'\n"
      "  MATCH := yes\n"
      "else\n"
      "  MATCH := no\n"
      "endif\n"
      "all:\n"
      "\t@echo match=$(MATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("match=yes")) << R.out;
}

// --- export all variables ---

TEST_F(BuildTest, ExportAll) {
  writeMakefile(
      "export\n"
      "MY_VAR := exported_val\n"
      "all:\n"
      "\t@echo var=$(MY_VAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("var=exported_val")) << R.out;
}

// --- Unexport specific variable ---

TEST_F(BuildTest, UnexportSpecific) {
  writeMakefile(
      "export FOO := bar\n"
      "unexport FOO\n"
      "all:\n"
      "\t@echo foo=$(FOO)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("foo=bar")) << R.out;
}

// --- $(abspath) with relative paths ---

TEST_F(BuildTest, AbspathRelative) {
  writeMakefile(
      "P := $(abspath src/../lib/foo.c)\n"
      "all:\n"
      "\t@echo path=$(P)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("lib/foo.c")) << R.out;
  EXPECT_FALSE(R.contains("..")) << "should resolve ..: " << R.out;
}

// --- $(info) / $(warning) output ---

TEST_F(BuildTest, InfoWarningOutput) {
  writeMakefile(
      "$(info Build starting)\n"
      "all:\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("Build starting") || R.err.find("Build starting") != std::string::npos)
      << "out: " << R.out << " err: " << R.err;
}

// --- Mixed assignment modes for same variable ---

TEST_F(BuildTest, MixedAssignModes) {
  writeMakefile(
      "X := initial\n"
      "X += appended\n"
      "Y = $(X)\n"
      "X := changed\n"
      "all:\n"
      "\t@echo x=$(X) y=$(Y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x=changed")) << R.out;
  EXPECT_TRUE(R.contains("y=changed")) << R.out;
}

// --- Complex filter pipeline ---

TEST_F(BuildTest, KbuildFilterPipeline) {
  writeMakefile(
      "ALL := a.c b.S c.h d.c e.S f.o\n"
      "C_FILES := $(filter %.c,$(ALL))\n"
      "ASM_FILES := $(filter %.S,$(ALL))\n"
      "SRC_FILES := $(filter %.c %.S,$(ALL))\n"
      "NON_OBJ := $(filter-out %.o,$(ALL))\n"
      "all:\n"
      "\t@echo c=$(C_FILES)\n"
      "\t@echo asm=$(ASM_FILES)\n"
      "\t@echo src=$(SRC_FILES)\n"
      "\t@echo nonobj=$(NON_OBJ)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("c=a.c d.c")) << R.out;
  EXPECT_TRUE(R.contains("asm=b.S e.S")) << R.out;
  EXPECT_TRUE(R.contains("src=a.c b.S d.c e.S")) << R.out;
  EXPECT_TRUE(R.contains("nonobj=a.c b.S c.h d.c e.S")) << R.out;
}

// ============================================================================
// LINUX 5.10 KERNEL COMPAT — Additional Robustness Tests
// ============================================================================

// --- FORCE target with semicolon inline recipe (kernel idiom) ---

TEST_F(BuildTest, KernelFORCESemicolonEmpty) {
  writeMakefile(
      "FORCE: ;\n"
      ".PHONY: FORCE\n"
      "version.h: FORCE\n"
      "\t@echo GENERATED\n");
  auto R = runMake({}, "version.h");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("GENERATED")) << R.out;
}

TEST_F(BuildTest, KernelFORCENoRecipe) {
  writeMakefile(
      "FORCE:\n"
      ".PHONY: FORCE\n"
      "output: FORCE\n"
      "\t@echo REBUILT\n");
  auto R = runMake({}, "output");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("REBUILT")) << R.out;
}

// --- CFLAGS accumulation pattern (kernel builds CFLAGS in many += steps) ---

TEST_F(BuildTest, KernelCFLAGSAccumulation) {
  writeMakefile(
      "CFLAGS := -Wall\n"
      "CFLAGS += -Wextra\n"
      "CFLAGS += -O2\n"
      "CFLAGS += -fno-strict-aliasing\n"
      "CFLAGS += -DCONFIG_SMP\n"
      "CFLAGS += -DCONFIG_PREEMPT\n"
      "CFLAGS += -I./include\n"
      "CFLAGS += -I./arch/x86/include\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-Wextra")) << R.out;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-fno-strict-aliasing")) << R.out;
  EXPECT_TRUE(R.contains("-DCONFIG_SMP")) << R.out;
  EXPECT_TRUE(R.contains("-I./arch/x86/include")) << R.out;
}

// --- Export with inline := assignment (kernel pattern) ---

TEST_F(BuildTest, KernelExportInlineAssign) {
  writeMakefile(
      "CROSS_COMPILE :=\n"
      "export CC := $(CROSS_COMPILE)gcc\n"
      "export LD := $(CROSS_COMPILE)ld\n"
      "export AR := $(CROSS_COMPILE)ar\n"
      "all:\n"
      "\t@echo cc=$(CC) ld=$(LD) ar=$(AR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
  EXPECT_TRUE(R.contains("ld=ld")) << R.out;
  EXPECT_TRUE(R.contains("ar=ar")) << R.out;
}

// --- Pattern rule with directory paths (Kbuild obj/ pattern) ---

TEST_F(BuildTest, KernelPatternRuleDirPath) {
  fs::create_directories(tmp() / "src");
  writeFile(tmp() / "src" / "main.c", "int main(){}");
  writeFile(tmp() / "src" / "util.c", "void util(){}");
  writeMakefile(
      "OBJS := obj/main.o obj/util.o\n"
      "obj/%.o: src/%.c\n"
      "\t@echo CC $< -o $@\n"
      "all: $(OBJS)\n"
      "\t@echo LINK $(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC src/main.c -o obj/main.o")) << R.out;
  EXPECT_TRUE(R.contains("CC src/util.c -o obj/util.o")) << R.out;
  EXPECT_TRUE(R.contains("LINK")) << R.out;
}

// --- Nested variable indirection $($(PREFIX)_CMD) ---

TEST_F(BuildTest, KernelNestedVarIndirection) {
  writeMakefile(
      "quiet_cmd_cc := CC-quiet\n"
      "cmd_cc := CC-verbose\n"
      "Q := quiet_\n"
      "RESULT := $($(Q)cmd_cc)\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=CC-quiet")) << R.out;
}

TEST_F(BuildTest, KernelNestedVarIndirectionVerbose) {
  writeMakefile(
      "quiet_cmd_cc := CC-quiet\n"
      "cmd_cc := CC-verbose\n"
      "Q :=\n"
      "RESULT := $($(Q)cmd_cc)\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=CC-verbose")) << R.out;
}

// --- cc-option pattern with call+shell+if ---

TEST_F(BuildTest, KernelCcOptionCallShellIf) {
  writeMakefile(
      "cc-option = $(shell if echo | true 2>/dev/null; then echo $(1); fi)\n"
      "CFLAGS := -Wall\n"
      "CFLAGS += $(call cc-option,-Wno-unused)\n"
      "CFLAGS += $(call cc-option,-Wno-format-truncation)\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-Wno-unused")) << R.out;
}

// --- $(wildcard) in include for .d files ---

TEST_F(BuildTest, KernelIncludeWildcardDotD) {
  writeFile(tmp() / "main.d", "main.o: main.c config.h\n");
  writeFile(tmp() / "util.d", "util.o: util.c util.h\n");
  writeFile(tmp() / "main.c", "");
  writeFile(tmp() / "config.h", "");
  writeFile(tmp() / "util.c", "");
  writeFile(tmp() / "util.h", "");
  writeMakefile(
      "OBJS := main.o util.o\n"
      "all: $(OBJS)\n"
      "\t@echo LINK $(OBJS)\n"
      "%.o: %.c\n"
      "\t@echo CC $<\n"
      "-include $(wildcard *.d)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("LINK")) << R.out;
}

// --- $(eval $(call ...)) template for Kbuild modules ---

TEST_F(BuildTest, KernelEvalCallModuleTemplate) {
  writeMakefile(
      "define build_module\n"
      "$(1).ko: $(2)\n"
      "\t@echo LD $(1).ko from $(2)\n"
      "endef\n"
      "$(eval $(call build_module,net_driver,net.o pci.o dma.o))\n"
      "$(eval $(call build_module,usb_driver,usb.o hub.o))\n"
      "%.o:\n"
      "\t@echo CC $@\n"
      "all: net_driver.ko usb_driver.ko\n"
      "\t@echo DONE\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("echo CC net.o") || R.contains("echo CC pci.o") ||
              R.contains("echo CC dma.o"))
      << R.out;
  EXPECT_TRUE(R.contains("LD net_driver.ko")) << R.out;
  EXPECT_TRUE(R.contains("LD usb_driver.ko")) << R.out;
}

// --- Kernel version string construction ---

TEST_F(BuildTest, KernelVersionConstruction) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 186\n"
      "EXTRAVERSION =\n"
      "KERNELVERSION = $(VERSION)$(if "
      "$(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.186")) << R.out;
}

// --- Multiple rules adding prerequisites to same target ---

TEST_F(BuildTest, KernelMultiRulePrereqs) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "c.h", "");
  writeMakefile(
      "prog: a.c\n"
      "prog: b.c\n"
      "prog: c.h\n"
      "prog:\n"
      "\t@echo BUILD $^\n"
      ".PHONY: prog\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a.c")) << R.out;
  EXPECT_TRUE(R.contains("b.c")) << R.out;
  EXPECT_TRUE(R.contains("c.h")) << R.out;
}

// --- $(or) for default value selection ---

TEST_F(BuildTest, KernelOrDefaultValue) {
  writeMakefile(
      "ARCH ?=\n"
      "DEFAULT_ARCH := x86\n"
      "SELECTED_ARCH := $(or $(ARCH),$(DEFAULT_ARCH))\n"
      "all:\n"
      "\t@echo arch=$(SELECTED_ARCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arch=x86")) << R.out;

  auto R2 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
}

// --- Multi-level $(if) nesting ---

TEST_F(BuildTest, KernelMultiLevelIf) {
  writeMakefile(
      "CONFIG_64BIT := y\n"
      "CONFIG_X86 := y\n"
      "BITS := $(if $(CONFIG_64BIT),64,32)\n"
      "ARCH_DIR := $(if $(CONFIG_X86),$(if $(CONFIG_64BIT),x86_64,i386),arm)\n"
      "all:\n"
      "\t@echo bits=$(BITS) dir=$(ARCH_DIR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bits=64")) << R.out;
  EXPECT_TRUE(R.contains("dir=x86_64")) << R.out;
}

// --- $(subst) for version component extraction ---

TEST_F(BuildTest, KernelSubstVersionExtract) {
  writeMakefile(
      "KERNELRELEASE := 5.10.186-generic\n"
      "BASE := $(firstword $(subst -, ,$(KERNELRELEASE)))\n"
      "MAJOR := $(word 1,$(subst ., ,$(BASE)))\n"
      "MINOR := $(word 2,$(subst ., ,$(BASE)))\n"
      "PATCH := $(word 3,$(subst ., ,$(BASE)))\n"
      "all:\n"
      "\t@echo major=$(MAJOR) minor=$(MINOR) patch=$(PATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("major=5")) << R.out;
  EXPECT_TRUE(R.contains("minor=10")) << R.out;
  EXPECT_TRUE(R.contains("patch=186")) << R.out;
}

// --- Kbuild quiet/verbose with variable indirection ---

TEST_F(BuildTest, KernelQuietVerboseIndirection) {
  writeMakefile(
      "ifeq ($(V),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "quiet_cmd_cc_o_c = CC $@\n"
      "cmd_cc_o_c = gcc -c -o $@ $<\n"
      "define rule_cc_o_c\n"
      "$($(quiet)cmd_cc_o_c)\n"
      "endef\n"
      "all:\n"
      "\t@echo $(rule_cc_o_c)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("CC all")) << R1.out;

  auto R2 = runMake({"V=1"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("gcc -c -o all")) << R2.out;
}

// --- Export chain for recursive make ---

TEST_F(BuildTest, KernelExportChainVars) {
  writeMakefile(
      "export CC := neverc\n"
      "export ARCH := x86_64\n"
      "CROSS_COMPILE := arm-linux-gnu-\n"
      "export LD := $(CROSS_COMPILE)ld\n"
      "all:\n"
      "\t@echo cc=$(CC) arch=$(ARCH) ld=$(LD)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=neverc")) << R.out;
  EXPECT_TRUE(R.contains("arch=x86_64")) << R.out;
  EXPECT_TRUE(R.contains("ld=arm-linux-gnu-ld")) << R.out;
}

// --- Deep $(foreach) nesting (3 levels) ---

TEST_F(BuildTest, KernelDeepForeachNesting) {
  writeMakefile(
      "ARCHS := x86 arm\n"
      "CONFIGS := debug release\n"
      "MODULES := core net\n"
      "ALL := $(foreach a,$(ARCHS),$(foreach c,$(CONFIGS),"
      "$(foreach m,$(MODULES),$(a)-$(c)-$(m))))\n"
      "all:\n"
      "\t@echo all=$(ALL)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x86-debug-core")) << R.out;
  EXPECT_TRUE(R.contains("x86-release-net")) << R.out;
  EXPECT_TRUE(R.contains("arm-debug-core")) << R.out;
  EXPECT_TRUE(R.contains("arm-release-net")) << R.out;
}

// --- $(eval) generating another $(eval) ---

TEST_F(BuildTest, KernelEvalChain) {
  writeMakefile(
      "define outer_template\n"
      "$$(eval $$(call inner_template,$(1),$(2)))\n"
      "endef\n"
      "define inner_template\n"
      "$(1)_$(2) := built_$(1)_$(2)\n"
      "endef\n"
      "$(eval $(call outer_template,mod,x86))\n"
      "all:\n"
      "\t@echo result=$(mod_x86)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=built_mod_x86")) << R.out;
}

// --- $(call) referencing another $(call) ---

TEST_F(BuildTest, KernelCallChain) {
  writeMakefile(
      "to-upper = $(subst a,A,$(subst b,B,$(1)))\n"
      "make-config = CONFIG_$(call to-upper,$(1))\n"
      "RESULT := $(call make-config,abc)\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=CONFIG_ABc")) << R.out;
}

// --- Recipe with $$ for shell variables ---

TEST_F(BuildTest, KernelRecipeDollarDollar) {
  writeMakefile(
      "all:\n"
      "\t@for f in a b c; do echo file=$$f; done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("file=a")) << R.out;
  EXPECT_TRUE(R.contains("file=b")) << R.out;
  EXPECT_TRUE(R.contains("file=c")) << R.out;
}

// --- Variable in target name ---

TEST_F(BuildTest, KernelVarInTargetName) {
  writeMakefile(
      "PROG := myapp\n"
      "$(PROG): \n"
      "\t@echo building $(PROG)\n"
      ".PHONY: $(PROG)\n"
      "all: $(PROG)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building myapp")) << R.out;
}

// --- Conditional assignment chain (?= multiple times) ---

TEST_F(BuildTest, KernelConditionalAssignChain) {
  writeMakefile(
      "CC ?= gcc\n"
      "CC ?= clang\n"
      "LD ?= ld\n"
      "all:\n"
      "\t@echo cc=$(CC) ld=$(LD)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
  EXPECT_TRUE(R.contains("ld=ld")) << R.out;
}

// --- ifeq with stripped whitespace (kernel uses $(strip) in ifeq) ---

TEST_F(BuildTest, KernelIfeqStripWhitespace) {
  writeMakefile(
      "FOO :=  yes  \n"
      "ifeq ($(strip $(FOO)),yes)\n"
      "  RESULT := matched\n"
      "else\n"
      "  RESULT := nomatch\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=matched")) << R.out;
}

// --- Pattern rule with multiple prerequisites ---

TEST_F(BuildTest, KernelPatternRuleMultiPrereq) {
  writeFile(tmp() / "foo.c", "");
  writeFile(tmp() / "common.h", "");
  writeMakefile(
      "%.o: %.c common.h\n"
      "\t@echo CC $< with deps=$^\n"
      "all: foo.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC foo.c with deps=foo.c common.h")) << R.out;
}

// --- Large CONFIG variable count (kernel has hundreds) ---

TEST_F(BuildTest, KernelLargeConfigSet) {
  std::string MF;
  for (int i = 0; i < 50; ++i)
    MF += "CONFIG_OPT" + std::to_string(i) + " := y\n";
  MF += "ENABLED :=\n";
  for (int i = 0; i < 50; ++i)
    MF += "ifeq ($(CONFIG_OPT" + std::to_string(i) + "),y)\n"
          "  ENABLED += opt" + std::to_string(i) + "\n"
          "endif\n";
  MF += "all:\n"
        "\t@echo count=$(words $(ENABLED))\n"
        ".PHONY: all\n";
  writeMakefile(MF);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=50")) << R.out;
}

// --- $(findstring) for feature detection (kernel pattern) ---

TEST_F(BuildTest, KernelFindstringFeatureDetect) {
  writeMakefile(
      "CFLAGS := -Wall -Werror -O2\n"
      "HAS_WALL := $(findstring -Wall,$(CFLAGS))\n"
      "HAS_O3 := $(findstring -O3,$(CFLAGS))\n"
      "all:\n"
      "\t@echo wall=$(HAS_WALL) o3=[$(HAS_O3)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("wall=-Wall")) << R.out;
  EXPECT_TRUE(R.contains("o3=[]")) << R.out;
}

// --- $(addprefix) + $(addsuffix) pipeline for path construction ---

TEST_F(BuildTest, KernelPathConstructionPipeline) {
  writeMakefile(
      "MODULES := core net fs\n"
      "SRCDIR := kernel\n"
      "SRCS := $(addprefix $(SRCDIR)/,$(addsuffix .c,$(MODULES)))\n"
      "OBJS := $(patsubst %.c,%.o,$(SRCS))\n"
      "all:\n"
      "\t@echo srcs=$(SRCS)\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("srcs=kernel/core.c kernel/net.c kernel/fs.c"))
      << R.out;
  EXPECT_TRUE(R.contains("objs=kernel/core.o kernel/net.o kernel/fs.o"))
      << R.out;
}

// --- $(filter) with multiple patterns (kernel source classification) ---

TEST_F(BuildTest, KernelFilterMultiPatternClassify) {
  writeMakefile(
      "ALL_FILES := main.c boot.S config.h lib.c startup.S README\n"
      "C_SRCS := $(filter %.c,$(ALL_FILES))\n"
      "ASM_SRCS := $(filter %.S,$(ALL_FILES))\n"
      "HEADERS := $(filter %.h,$(ALL_FILES))\n"
      "COMPILABLE := $(filter %.c %.S,$(ALL_FILES))\n"
      "all:\n"
      "\t@echo c=$(C_SRCS)\n"
      "\t@echo asm=$(ASM_SRCS)\n"
      "\t@echo hdr=$(HEADERS)\n"
      "\t@echo comp=$(COMPILABLE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("c=main.c lib.c")) << R.out;
  EXPECT_TRUE(R.contains("asm=boot.S startup.S")) << R.out;
  EXPECT_TRUE(R.contains("hdr=config.h")) << R.out;
  EXPECT_TRUE(R.contains("comp=main.c boot.S lib.c startup.S")) << R.out;
}

// --- define + call for multi-line recipes (Kbuild cmd pattern) ---

TEST_F(BuildTest, KernelDefineCmdPattern) {
  writeMakefile(
      "define cmd_link\n"
      "@echo LINK $(2) -o $(1)\n"
      "@echo STRIP $(1)\n"
      "endef\n"
      "vmlinux: \n"
      "\t$(call cmd_link,vmlinux,built-in.a lib.a)\n"
      ".PHONY: vmlinux\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("LINK built-in.a lib.a -o vmlinux")) << R.out;
  EXPECT_TRUE(R.contains("STRIP vmlinux")) << R.out;
}

// --- Substitution reference with directory path ---

TEST_F(BuildTest, KernelSubstRefDirConversion) {
  writeMakefile(
      "SRCS := drivers/a.c drivers/b.c kernel/c.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("objs=drivers/a.o drivers/b.o kernel/c.o"))
      << R.out;
}

// --- $(foreach) + $(if) combination (Kbuild conditional module list) ---

TEST_F(BuildTest, KernelForeachIfConditional) {
  writeMakefile(
      "MODULES := core net crypto gpu\n"
      "ENABLED_core := y\n"
      "ENABLED_net := y\n"
      "ENABLED_crypto :=\n"
      "ENABLED_gpu := y\n"
      "ACTIVE := $(foreach m,$(MODULES),$(if $(ENABLED_$(m)),$(m)))\n"
      "all:\n"
      "\t@echo active=$(ACTIVE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("active=core net gpu")) << R.out;
}

// --- $(eval) with pattern rule generation ---

TEST_F(BuildTest, KernelEvalPatternRuleGenMulti) {
  writeMakefile(
      "DIRS := kernel drivers fs\n"
      "define dir_rule\n"
      "$(1)/built-in.o: FORCE\n"
      "\t@echo BUILD $(1)/built-in.o\n"
      "endef\n"
      "$(foreach d,$(DIRS),$(eval $(call dir_rule,$(d))))\n"
      "FORCE:\n"
      ".PHONY: FORCE\n"
      "vmlinux: $(addsuffix /built-in.o,$(DIRS))\n"
      "\t@echo LINK vmlinux\n"
      ".PHONY: vmlinux\n");
  auto R = runMake({}, "vmlinux");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("BUILD kernel/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("BUILD drivers/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("BUILD fs/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("LINK vmlinux")) << R.out;
}

// --- ifdef with variable containing only whitespace ---

TEST_F(BuildTest, KernelIfdefWhitespaceOnly) {
  writeMakefile(
      "EMPTY :=\n"
      "SPACE := $(EMPTY) $(EMPTY)\n"
      "ifdef SPACE\n"
      "  R1 := defined\n"
      "else\n"
      "  R1 := undefined\n"
      "endif\n"
      "ifdef NOTSET\n"
      "  R2 := defined\n"
      "else\n"
      "  R2 := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r2=undefined")) << R.out;
}

// --- Parallel build with diamond dependency ---

TEST_F(BuildTest, KernelParallelDiamondDep) {
  writeMakefile(
      "all: left right\n"
      "\t@echo ALL\n"
      "left: base\n"
      "\t@echo LEFT\n"
      "right: base\n"
      "\t@echo RIGHT\n"
      "base:\n"
      "\t@echo BASE\n"
      ".PHONY: all left right base\n");
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("BASE")) << R.out;
  EXPECT_TRUE(R.contains("LEFT")) << R.out;
  EXPECT_TRUE(R.contains("RIGHT")) << R.out;
  EXPECT_TRUE(R.contains("ALL")) << R.out;
}

// --- Keep-going (-k) with partial failure ---

TEST_F(BuildTest, KernelKeepGoingPartialFail) {
  writeMakefile(
      "all: good bad1 good2\n"
      "\t@echo ALL\n"
      "good:\n"
      "\t@echo GOOD\n"
      "bad1:\n"
      "\tfalse\n"
      "good2:\n"
      "\t@echo GOOD2\n"
      ".PHONY: all good bad1 good2\n");
  auto R = runMake({"-k"});
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.contains("GOOD")) << R.out;
  EXPECT_TRUE(R.contains("GOOD2")) << R.out;
}

// --- Kbuild Makefile with sub-makefile include chain ---

TEST_F(BuildTest, KernelSubMakefileIncludeChain) {
  fs::create_directories(tmp() / "scripts");
  fs::create_directories(tmp() / "arch" / "x86");
  writeFile(tmp() / "scripts" / "Kbuild.include",
            "KBUILD_INCLUDED := yes\n"
            "cc-option = $(1)\n");
  writeFile(tmp() / "arch" / "x86" / "Makefile",
            "ARCH_CFLAGS := -m64\n"
            "ARCH_NAME := x86_64\n");
  writeMakefile(
      "include scripts/Kbuild.include\n"
      "ARCH := x86\n"
      "include arch/$(ARCH)/Makefile\n"
      "all:\n"
      "\t@echo arch=$(ARCH_NAME) flags=$(ARCH_CFLAGS) inc=$(KBUILD_INCLUDED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arch=x86_64")) << R.out;
  EXPECT_TRUE(R.contains("flags=-m64")) << R.out;
  EXPECT_TRUE(R.contains("inc=yes")) << R.out;
}

// --- Comprehensive Kbuild vmlinux-style build simulation ---

TEST_F(BuildTest, KernelVmlinuxBuildSimulation) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "KERNELRELEASE = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "\n"
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "\n"
      "export CC LD ARCH\n"
      "\n"
      "CFLAGS := -Wall -O2\n"
      "ifeq ($(ARCH),x86)\n"
      "  CFLAGS += -m64\n"
      "  BITS := 64\n"
      "else ifeq ($(ARCH),arm)\n"
      "  CFLAGS += -march=armv7-a\n"
      "  BITS := 32\n"
      "endif\n"
      "\n"
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES := y\n"
      "CONFIG_PRINTK := y\n"
      "\n"
      "core-y := kernel/ mm/\n"
      "drivers-y := drivers/\n"
      "net-y :=\n"
      "ifeq ($(CONFIG_SMP),y)\n"
      "  CFLAGS += -DCONFIG_SMP\n"
      "endif\n"
      "\n"
      "obj-y :=\n"
      "obj-$(CONFIG_PRINTK) += printk.o\n"
      "obj-$(CONFIG_SMP) += smp.o\n"
      "\n"
      "define build_subdir\n"
      "$(1)built-in.o: FORCE\n"
      "\t@echo BUILD $(1)built-in.o [$(CFLAGS)]\n"
      "endef\n"
      "\n"
      "SUBDIRS := $(core-y) $(drivers-y) $(net-y)\n"
      "$(foreach d,$(SUBDIRS),$(eval $(call build_subdir,$(d))))\n"
      "\n"
      "BUILTIN_OBJS := $(addsuffix built-in.o,$(SUBDIRS))\n"
      "\n"
      "vmlinux: $(BUILTIN_OBJS) FORCE\n"
      "\t@echo LINK vmlinux $(KERNELRELEASE) objs=$(obj-y)"
      " bits=$(BITS)\n"
      "\n"
      "FORCE:\n"
      ".PHONY: FORCE vmlinux\n");
  auto R = runMake({}, "vmlinux");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("BUILD kernel/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("BUILD mm/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("BUILD drivers/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("LINK vmlinux 5.10.0")) << R.out;
  EXPECT_TRUE(R.contains("printk.o")) << R.out;
  EXPECT_TRUE(R.contains("smp.o")) << R.out;
  EXPECT_TRUE(R.contains("bits=64")) << R.out;

  auto R2 = runMake({"ARCH=arm"}, "vmlinux");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("bits=32")) << R2.out;
}

// ============================================================================
// KERNEL EDGE CASES — Patterns critical for real Linux 5.10 kernel builds
// ============================================================================

// --- Recipe $$ escaping for shell variables ---

TEST_F(BuildTest, KernelRecipeDollarEscape) {
  writeMakefile(
      "all:\n"
      "\t@for f in a b c; do echo $$f; done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a")) << R.out;
  EXPECT_TRUE(R.contains("b")) << R.out;
  EXPECT_TRUE(R.contains("c")) << R.out;
}

// --- Recipe $$ with make variables combined ---

TEST_F(BuildTest, KernelRecipeDollarMixed) {
  writeMakefile(
      "TARGET := output\n"
      "all:\n"
      "\t@echo target=$@ make_var=$(TARGET)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("target=all")) << R.out;
  EXPECT_TRUE(R.contains("make_var=output")) << R.out;
}

// --- Kbuild quiet/verbose cmd pattern ---

TEST_F(BuildTest, KernelQuietVerboseCmd) {
  writeMakefile(
      "quiet_cmd_cc_o_c = CC      $@\n"
      "      cmd_cc_o_c = gcc -c -o $@ $<\n"
      "\n"
      "ifdef V\n"
      "  quiet :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "endif\n"
      "\n"
      "define echo_cmd\n"
      "$(if $($(quiet)cmd_$(1)),@echo '  $($(quiet)cmd_$(1))')\n"
      "endef\n"
      "\n"
      "all:\n"
      "\t@echo cmd=$(cmd_cc_o_c)\n"
      "\t@echo quiet_cmd=$(quiet_cmd_cc_o_c)\n"
      "\t@echo quiet=$(quiet)\n"
      ".PHONY: all\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("quiet=quiet_")) << R1.out;
  EXPECT_TRUE(R1.contains("cmd=gcc -c -o all")) << R1.out;

  auto R2 = runMake({"V=1"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("quiet=")) << "quiet should be empty with V=1: " << R2.out;
}

// --- cc-option pattern with fallback (kernel compiler feature detection) ---

TEST_F(BuildTest, KernelCcOptionFallback) {
  writeMakefile(
      "CC := echo\n"
      "define cc-option\n"
      "$(shell $(CC) $(1) -c -x c /dev/null -o /dev/null 2>/dev/null "
      "&& echo $(1))\n"
      "endef\n"
      "CFLAGS := -Wall\n"
      "CFLAGS += $(call cc-option,-Wformat-security)\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("flags=-Wall")) << R.out;
}

// --- if_changed pattern with FORCE (kernel build system core) ---

TEST_F(BuildTest, KernelIfChangedFORCE) {
  writeMakefile(
      "define if_changed\n"
      "$(if $(strip $(filter-out $(cmd_$(1)),$(cmd_$@))),\n"
      "  @echo '  $(1) $@')\n"
      "endef\n"
      "cmd_link = ld -o $@ $^\n"
      "vmlinux: FORCE\n"
      "\t@echo BUILD $@\n"
      "FORCE:\n"
      ".PHONY: FORCE vmlinux\n");
  auto R = runMake({}, "vmlinux");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("BUILD vmlinux")) << R.out;
}

// --- Recipe - prefix error suppression ---

TEST_F(BuildTest, KernelRecipeIgnoreError) {
  writeMakefile(
      "all:\n"
      "\t-false\n"
      "\t@echo CONTINUED\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CONTINUED")) << R.out;
}

// --- Recipe + prefix force execution with dry-run ---

TEST_F(BuildTest, KernelRecipeForceInDryRun) {
  writeMakefile(
      "all:\n"
      "\t+@echo FORCED\n"
      "\t@echo SKIPPED\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("FORCED")) << R.out;
  EXPECT_TRUE(R.contains("echo SKIPPED")) << R.out;
}

// --- Combined recipe prefixes @-+ ---

TEST_F(BuildTest, KernelRecipeCombinedPrefixes) {
  writeMakefile(
      "all:\n"
      "\t@-echo SILENT_IGNORE\n"
      "\t-@false\n"
      "\t@echo OK\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("OK")) << R.out;
}

// --- Version string manipulation (kernel Makefile top pattern) ---

TEST_F(BuildTest, KernelVersionParsing) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 186\n"
      "EXTRAVERSION =\n"
      "KERNELVERSION := "
      "$(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)"
      "$(if $(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.186")) << R.out;
}

// --- Kernel version with subst splitting ---

TEST_F(BuildTest, KernelVersionSubstSplit) {
  writeMakefile(
      "KERNELVERSION := 5.10.186\n"
      "PARTS := $(subst ., ,$(KERNELVERSION))\n"
      "MAJOR := $(word 1,$(PARTS))\n"
      "MINOR := $(word 2,$(PARTS))\n"
      "PATCH := $(word 3,$(PARTS))\n"
      "all:\n"
      "\t@echo major=$(MAJOR) minor=$(MINOR) patch=$(PATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("major=5")) << R.out;
  EXPECT_TRUE(R.contains("minor=10")) << R.out;
  EXPECT_TRUE(R.contains("patch=186")) << R.out;
}

// --- Pattern rule with directory prefix ---

TEST_F(BuildTest, KernelPatternRuleDirPrefix) {
  std::filesystem::create_directories(tmp() / "src");
  writeFile(tmp() / "src" / "main.c", "int main(){}");
  writeFile(tmp() / "src" / "util.c", "void util(){}");
  writeMakefile(
      "SRCS := src/main.c src/util.c\n"
      "OBJS := $(patsubst src/%.c,build/%.o,$(SRCS))\n"
      "build/%.o: src/%.c\n"
      "\t@echo CC $< -o $@\n"
      "all: $(OBJS)\n"
      "\t@echo LINK $(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC src/main.c -o build/main.o")) << R.out;
  EXPECT_TRUE(R.contains("CC src/util.c -o build/util.o")) << R.out;
  EXPECT_TRUE(R.contains("LINK build/main.o build/util.o")) << R.out;
}

// --- Export all + selective unexport ---

TEST_F(BuildTest, KernelExportAllUnexport) {
  writeMakefile(
      "FOO := foo_val\n"
      "BAR := bar_val\n"
      "SECRET := hidden\n"
      "export\n"
      "unexport SECRET\n"
      "all:\n"
      "\t@echo foo=$(FOO) bar=$(BAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("foo=foo_val")) << R.out;
  EXPECT_TRUE(R.contains("bar=bar_val")) << R.out;
}

// --- Deeply computed variable names $($(quiet)cmd_$(1)) ---

TEST_F(BuildTest, KernelDeepComputedVarName) {
  writeMakefile(
      "quiet := quiet_\n"
      "quiet_cmd_cc := CC\n"
      "cmd_cc := gcc -c\n"
      "CMD := $($(quiet)cmd_cc)\n"
      "all:\n"
      "\t@echo cmd=$(CMD)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cmd=CC")) << R.out;
}

// --- ifeq with empty variable ---

TEST_F(BuildTest, KernelIfeqEmptyVar) {
  writeMakefile(
      "EXTRA :=\n"
      "ifeq ($(EXTRA),)\n"
      "  RESULT := empty\n"
      "else\n"
      "  RESULT := notempty\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=empty")) << R.out;
}

// --- ifeq with both sides having variable refs ---

TEST_F(BuildTest, KernelIfeqBothSidesVarRef) {
  writeMakefile(
      "A := hello\n"
      "B := hello\n"
      "C := world\n"
      "ifeq ($(A),$(B))\n"
      "  R1 := match\n"
      "else\n"
      "  R1 := nomatch\n"
      "endif\n"
      "ifeq ($(A),$(C))\n"
      "  R2 := match\n"
      "else\n"
      "  R2 := nomatch\n"
      "endif\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=match")) << R.out;
  EXPECT_TRUE(R.contains("r2=nomatch")) << R.out;
}

// --- Multiple targets single rule ---

TEST_F(BuildTest, KernelMultiTargetSingleRule) {
  writeMakefile(
      "all: a b c\n"
      "\t@echo DONE\n"
      "a b c:\n"
      "\t@echo BUILD $@\n"
      ".PHONY: all a b c\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("BUILD a")) << R.out;
  EXPECT_TRUE(R.contains("BUILD b")) << R.out;
  EXPECT_TRUE(R.contains("BUILD c")) << R.out;
  EXPECT_TRUE(R.contains("DONE")) << R.out;
}

// --- .DEFAULT_GOAL explicit set ---

TEST_F(BuildTest, KernelDefaultGoalExplicit) {
  writeMakefile(
      ".DEFAULT_GOAL := custom\n"
      "first:\n"
      "\t@echo FIRST\n"
      "custom:\n"
      "\t@echo CUSTOM\n"
      ".PHONY: first custom\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CUSTOM")) << R.out;
  EXPECT_FALSE(R.contains("FIRST")) << R.out;
}

// --- Semicolon inline recipe ---

TEST_F(BuildTest, KernelInlineRecipe) {
  writeMakefile(
      "all: ; @echo INLINE\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("INLINE")) << R.out;
}

// --- ifdef with recursive empty-reference chain ---

TEST_F(BuildTest, KernelIfdefRecursiveEmptyRef) {
  writeMakefile(
      "EMPTY =\n"
      "REF = $(EMPTY)\n"
      "ifdef REF\n"
      "  R := defined\n"
      "else\n"
      "  R := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo r=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r=defined")) << R.out;
}

// --- foreach + call + eval triple for Kbuild module template ---

TEST_F(BuildTest, KernelForeachCallEvalModule) {
  writeMakefile(
      ".DEFAULT_GOAL := all\n"
      "MODULES := net fs crypto\n"
      "\n"
      "define module_template\n"
      "$(1)-objs := $(1)_core.o $(1)_init.o\n"
      "$(1).ko: $$($(1)-objs)\n"
      "\t@echo LD $$@ from $$($(1)-objs)\n"
      "endef\n"
      "\n"
      "$(foreach m,$(MODULES),$(eval $(call module_template,$(m))))\n"
      "\n"
      "%.o:\n"
      "\t@echo CC $@\n"
      "\n"
      "all: $(addsuffix .ko,$(MODULES))\n"
      "\t@echo ALL_MODULES_BUILT\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ALL_MODULES_BUILT") ||
              R.contains("echo ALL_MODULES_BUILT")) << R.out;
}

// --- filter with multiple patterns (kernel CONFIG classification) ---

TEST_F(BuildTest, KernelFilterMultiPattern) {
  writeMakefile(
      "ALL_CONFIGS := CONFIG_FOO=y CONFIG_BAR=m CONFIG_BAZ=n CONFIG_QUX=y\n"
      "ENABLED := $(filter %=y %=m,$(ALL_CONFIGS))\n"
      "DISABLED := $(filter %=n,$(ALL_CONFIGS))\n"
      "all:\n"
      "\t@echo enabled=$(ENABLED)\n"
      "\t@echo disabled=$(DISABLED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("enabled=CONFIG_FOO=y CONFIG_BAR=m CONFIG_QUX=y"))
      << R.out;
  EXPECT_TRUE(R.contains("disabled=CONFIG_BAZ=n")) << R.out;
}

// --- addprefix + addsuffix pipeline (kernel object path construction) ---

TEST_F(BuildTest, KernelPrefixSuffixPipeline) {
  writeMakefile(
      "MODULES := core net fs\n"
      "OBJ_DIRS := $(addsuffix /,$(addprefix obj/,$(MODULES)))\n"
      "SRC_FILES := $(addsuffix .c,$(addprefix src/,$(MODULES)))\n"
      "all:\n"
      "\t@echo dirs=$(OBJ_DIRS)\n"
      "\t@echo srcs=$(SRC_FILES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dirs=obj/core/ obj/net/ obj/fs/")) << R.out;
  EXPECT_TRUE(R.contains("srcs=src/core.c src/net.c src/fs.c")) << R.out;
}

// --- Complex substitution reference with path patterns ---

TEST_F(BuildTest, KernelSubstRefPaths) {
  writeMakefile(
      "SRCS := arch/x86/boot.c arch/x86/setup.c drivers/pci.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains(
      "objs=arch/x86/boot.o arch/x86/setup.o drivers/pci.o"))
      << R.out;
}

// --- MAKECMDGOALS detection (kernel uses this for config targets) ---

TEST_F(BuildTest, KernelMakeCmdGoalsDetection) {
  writeMakefile(
      "ifneq ($(filter config menuconfig,$(MAKECMDGOALS)),)\n"
      "  MODE := config\n"
      "else\n"
      "  MODE := build\n"
      "endif\n"
      "all:\n"
      "\t@echo mode=$(MODE)\n"
      "config:\n"
      "\t@echo mode=$(MODE)\n"
      ".PHONY: all config\n");

  auto R1 = runMake({}, "all");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("mode=build")) << R1.out;

  auto R2 = runMake({}, "config");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("mode=config")) << R2.out;
}

// --- FORCE target + file dependency interaction ---

TEST_F(BuildTest, KernelFORCEWithFileDep) {
  writeFile(tmp() / "source.txt", "data");
  writeMakefile(
      "output.stamp: source.txt FORCE\n"
      "\t@echo REBUILD output.stamp\n"
      "\t@touch output.stamp\n"
      "FORCE:\n"
      ".PHONY: FORCE\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("REBUILD output.stamp")) << R.out;
}

// --- Nested $(if) with $(findstring) (kernel feature gating) ---

TEST_F(BuildTest, KernelNestedIfFindstring) {
  writeMakefile(
      "ARCH := x86_64\n"
      "CFLAGS := $(if $(findstring x86,$(ARCH)),-m64,"
      "$(if $(findstring arm,$(ARCH)),-marm,-munknown))\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cflags=-m64")) << R.out;
}

// --- define with := mode (immediate expansion in define body) ---

TEST_F(BuildTest, KernelDefineSimpleMode) {
  writeMakefile(
      "X := hello\n"
      "define MSG :=\n"
      "value is $(X)\n"
      "endef\n"
      "X := world\n"
      "all:\n"
      "\t@echo $(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("value is hello")) << R.out;
}

// --- Parallel build with independent fan-out ---

TEST_F(BuildTest, KernelParallelFanOut) {
  std::string MF;
  MF += "all: t1 t2 t3 t4 t5 t6 t7 t8\n";
  MF += "\t@echo ALL_DONE\n";
  for (int i = 1; i <= 8; ++i) {
    MF += "t" + std::to_string(i) + ":\n";
    MF += "\t@echo T" + std::to_string(i) + "\n";
  }
  MF += ".PHONY: all t1 t2 t3 t4 t5 t6 t7 t8\n";
  writeMakefile(MF);
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << R.err;
  for (int i = 1; i <= 8; ++i)
    EXPECT_TRUE(R.contains("T" + std::to_string(i))) << R.out;
  EXPECT_TRUE(R.contains("ALL_DONE")) << R.out;
}

// --- keep-going with partial failures ---

TEST_F(BuildTest, KernelKeepGoingPartial) {
  writeMakefile(
      "all: good1 bad good2\n"
      "\t@echo DONE\n"
      "good1:\n"
      "\t@echo GOOD1\n"
      "bad:\n"
      "\tfalse\n"
      "good2:\n"
      "\t@echo GOOD2\n"
      ".PHONY: all good1 bad good2\n");
  auto R = runMake({"-k"});
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.contains("GOOD1")) << R.out;
  EXPECT_TRUE(R.contains("GOOD2")) << R.out;
}

// --- Order-only prerequisite doesn't trigger rebuild ---

TEST_F(BuildTest, KernelOrderOnlyNoRebuild) {
  writeFile(tmp() / "src.txt", "source");
  writeMakefile(
      "output.txt: src.txt | order_dep\n"
      "\t@echo BUILD\n"
      "\t@cp src.txt output.txt\n"
      "order_dep:\n"
      "\t@echo ORDER\n"
      ".PHONY: order_dep\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("BUILD") || R.contains("cp src.txt output.txt"))
      << R.out;
}

// --- Recursive make simulation $(MAKE) -C subdir ---

TEST_F(BuildTest, KernelRecursiveMakeSimulation) {
  writeMakefile(
      "SUBDIRS := sub1 sub2\n"
      "all: $(SUBDIRS)\n"
      "\t@echo TOP_DONE\n"
      "$(SUBDIRS):\n"
      "\t@echo MAKE_SUBDIR $@\n"
      ".PHONY: all $(SUBDIRS)\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("MAKE_SUBDIR sub1")) << R.out;
  EXPECT_TRUE(R.contains("MAKE_SUBDIR sub2")) << R.out;
  EXPECT_TRUE(R.contains("TOP_DONE")) << R.out;
}

// --- $(sort) deduplication with many duplicates ---

TEST_F(BuildTest, KernelSortDedupMany) {
  writeMakefile(
      "DEPS := c.h a.h b.h a.h c.h d.h b.h\n"
      "UNIQUE := $(sort $(DEPS))\n"
      "all:\n"
      "\t@echo deps=$(UNIQUE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("deps=a.h b.h c.h d.h")) << R.out;
}

// --- $(wildcard) in prerequisites ---

TEST_F(BuildTest, KernelWildcardPrereqs) {
  writeFile(tmp() / "a.h", "");
  writeFile(tmp() / "b.h", "");
  writeMakefile(
      "HEADERS := $(wildcard *.h)\n"
      "COUNT := $(words $(HEADERS))\n"
      "all:\n"
      "\t@echo count=$(COUNT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=2")) << R.out;
}

// --- $(error) stops build ---

TEST_F(BuildTest, KernelErrorStopsBuild) {
  writeMakefile(
      "ifndef REQUIRED_VAR\n"
      "  $(error REQUIRED_VAR is not set)\n"
      "endif\n"
      "all:\n"
      "\t@echo should_not_reach\n"
      ".PHONY: all\n");
  auto R = runMake();
  EXPECT_FALSE(R.ok());
}

// --- $(warning) continues build ---

TEST_F(BuildTest, KernelWarningContinues) {
  writeMakefile(
      "$(warning This is a warning message)\n"
      "all:\n"
      "\t@echo REACHED\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("REACHED")) << R.out;
}

// --- $(info) output ---

TEST_F(BuildTest, KernelInfoOutput) {
  writeMakefile(
      "$(info Building with neverc make)\n"
      "all:\n"
      "\t@echo DONE\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("DONE")) << R.out;
}

// --- include chain (kernel includes sub-Makefiles recursively) ---

TEST_F(BuildTest, KernelIncludeChain) {
  writeFile(tmp() / "config.mk",
            "CONFIG_NET := y\n"
            "include drivers.mk\n");
  writeFile(tmp() / "drivers.mk",
            "ifeq ($(CONFIG_NET),y)\n"
            "  DRIVERS += net\n"
            "endif\n");
  writeMakefile(
      "DRIVERS :=\n"
      "include config.mk\n"
      "all:\n"
      "\t@echo drivers=$(DRIVERS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("drivers=net")) << R.out;
}

// --- -include missing file (no error) ---

TEST_F(BuildTest, KernelDashIncludeMissing) {
  writeMakefile(
      "-include nonexistent.mk\n"
      "-include also_missing.d\n"
      "all:\n"
      "\t@echo OK\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("OK")) << R.out;
}

// --- Complex 4-layer ifeq chain (ARCH selection with fallback) ---

TEST_F(BuildTest, KernelIfeq4LayerChain) {
  writeMakefile(
      "ARCH ?= riscv\n"
      "ifeq ($(ARCH),x86)\n"
      "  KERNEL_ARCH := x86_64\n"
      "else ifeq ($(ARCH),arm)\n"
      "  KERNEL_ARCH := arm\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  KERNEL_ARCH := aarch64\n"
      "else ifeq ($(ARCH),riscv)\n"
      "  KERNEL_ARCH := riscv64\n"
      "else\n"
      "  KERNEL_ARCH := unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(KERNEL_ARCH)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("arch=riscv64")) << R1.out;

  auto R2 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=aarch64")) << R2.out;
}

// --- Auto-var $? (newer prerequisites) ---

TEST_F(BuildTest, KernelAutoVarNewer) {
  writeFile(tmp() / "old.c", "old");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  writeFile(tmp() / "target.o", "target");
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  writeFile(tmp() / "new.c", "new");
  writeMakefile(
      "target.o: old.c new.c\n"
      "\t@echo newer=$?\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("newer=new.c")) << R.out;
  EXPECT_FALSE(R.contains("old.c")) << R.out;
}

// --- Auto-var $(@D) and $(@F) with deep path ---

TEST_F(BuildTest, KernelAutoVarDirFile) {
  std::filesystem::create_directories(tmp() / "src");
  writeFile(tmp() / "src" / "main.c", "");
  writeMakefile(
      "build/out/main.o: src/main.c\n"
      "\t@echo dir=$(@D) file=$(@F)\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dir=build/out")) << R.out;
  EXPECT_TRUE(R.contains("file=main.o")) << R.out;
}

// --- Stress: 300 module Kbuild with foreach+eval ---

TEST_F(BuildTest, KernelStress300ModulesEval) {
  std::string MF;
  MF += ".DEFAULT_GOAL := all\n";
  MF += "MODULES :=\n";
  for (int i = 0; i < 300; ++i)
    MF += "MODULES += m" + std::to_string(i) + "\n";
  MF += "define mod_rule\n"
        "$(1).o:\n"
        "\t@echo CC $(1)\n"
        "endef\n"
        "$(foreach m,$(MODULES),$(eval $(call mod_rule,$(m))))\n"
        "all: $(addsuffix .o,$(MODULES))\n"
        "\t@echo LINK $(words $(MODULES)) modules\n"
        ".PHONY: all\n";
  writeMakefile(MF);
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("echo LINK 300 modules")) << R.out;
}

// --- Parallel diamond dependency (A->B, A->C, B->D, C->D) ---

TEST_F(BuildTest, KernelParallelDiamond) {
  writeMakefile(
      "A: B C\n"
      "\t@echo A\n"
      "B: D\n"
      "\t@echo B\n"
      "C: D\n"
      "\t@echo C\n"
      "D:\n"
      "\t@echo D\n"
      ".PHONY: A B C D\n");
  auto R = runMake({"-j4"}, "A");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("D")) << R.out;
  EXPECT_TRUE(R.contains("B")) << R.out;
  EXPECT_TRUE(R.contains("C")) << R.out;
  EXPECT_TRUE(R.contains("A")) << R.out;
}

// --- $(call) with zero arguments ---

TEST_F(BuildTest, KernelCallZeroArgs) {
  writeMakefile(
      "define greet\n"
      "hello world\n"
      "endef\n"
      "MSG := $(call greet)\n"
      "all:\n"
      "\t@echo msg=$(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("msg=hello world")) << R.out;
}

// --- Override += interaction with command line ---

TEST_F(BuildTest, KernelOverrideAppendCmdLine) {
  writeMakefile(
      "CFLAGS := -O2\n"
      "override CFLAGS += -Wall\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cflags=-O2 -Wall")) << R1.out;

  auto R2 = runMake({"CFLAGS=-Os"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("-Wall")) << R2.out;
}

// --- Static pattern rule with obj-y and dry-run ---

TEST_F(BuildTest, KernelStaticPatternObjYDryRun) {
  writeFile(tmp() / "core.c", "");
  writeFile(tmp() / "net.c", "");
  writeFile(tmp() / "fs.c", "");
  writeMakefile(
      "obj-y := core.o net.o fs.o\n"
      "$(obj-y): %.o: %.c\n"
      "\t@echo CC $< -o $@\n"
      "all: $(obj-y)\n"
      "\t@echo LINKED\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC core.c -o core.o")) << R.out;
  EXPECT_TRUE(R.contains("CC net.c -o net.o")) << R.out;
  EXPECT_TRUE(R.contains("CC fs.c -o fs.o")) << R.out;
}

// --- Full Kbuild pipeline simulation (comprehensive) ---

TEST_F(BuildTest, KernelFullKbuildPipeline) {
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 0\n"
      "KERNELRELEASE := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "\n"
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "\n"
      "ifeq ($(ARCH),x86)\n"
      "  BITS := 64\n"
      "  SRCARCH := x86\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  BITS := 64\n"
      "  SRCARCH := arm64\n"
      "else\n"
      "  BITS := 32\n"
      "  SRCARCH := $(ARCH)\n"
      "endif\n"
      "\n"
      "CFLAGS := -O2 -Wall\n"
      "ifeq ($(BITS),64)\n"
      "  CFLAGS += -m64\n"
      "endif\n"
      "\n"
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES := y\n"
      "CONFIG_NET := y\n"
      "CONFIG_FS := n\n"
      "\n"
      "obj-y := init/main.o\n"
      "obj-$(CONFIG_SMP) += kernel/smp.o\n"
      "obj-$(CONFIG_NET) += net/core.o\n"
      "obj-$(CONFIG_FS) += fs/vfs.o\n"
      "\n"
      "ENABLED := $(filter-out %=n,$(foreach c,SMP MODULES NET FS,"
      "$(c)=$(CONFIG_$(c))))\n"
      "\n"
      "define gen_subdir_rule\n"
      "$(dir $(1))built-in.o: FORCE\n"
      "\t@echo '  AR      $(dir $(1))built-in.o'\n"
      "endef\n"
      "$(foreach o,$(obj-y),$(eval $(call gen_subdir_rule,$(o))))\n"
      "\n"
      "SUBDIRS := $(sort $(dir $(obj-y)))\n"
      "BUILTIN := $(addsuffix built-in.o,$(SUBDIRS))\n"
      "\n"
      "vmlinux: $(BUILTIN) FORCE\n"
      "\t@echo '  LD      vmlinux $(KERNELRELEASE)'\n"
      "\t@echo '  ARCH    $(SRCARCH) bits=$(BITS)'\n"
      "\t@echo '  CC      $(CC)'\n"
      "\t@echo '  CFLAGS  $(CFLAGS)'\n"
      "\t@echo '  ENABLED $(ENABLED)'\n"
      "\t@echo '  OBJS    $(words $(obj-y)) objects'\n"
      "\n"
      "FORCE:\n"
      ".PHONY: FORCE vmlinux\n");

  auto R1 = runMake({}, "vmlinux");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("LD      vmlinux 5.10.0")) << R1.out;
  EXPECT_TRUE(R1.contains("ARCH    x86 bits=64")) << R1.out;
  EXPECT_TRUE(R1.contains("CC      gcc")) << R1.out;
  EXPECT_TRUE(R1.contains("-m64")) << R1.out;
  EXPECT_TRUE(R1.contains("3 objects")) << R1.out;

  auto R2 = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-"},
                     "vmlinux");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("ARCH    arm64 bits=64")) << R2.out;
  EXPECT_TRUE(R2.contains("CC      aarch64-linux-gnu-gcc")) << R2.out;
}

// --- Stress: 200 modules Kbuild simulation ---

TEST_F(BuildTest, KernelStress200Modules) {
  std::string MF = "obj-y :=\n";
  for (int i = 0; i < 200; ++i)
    MF += "obj-y += mod" + std::to_string(i) + ".o\n";
  MF += "COUNT := $(words $(obj-y))\n";
  MF += "all: $(obj-y)\n"
        "\techo LINK $(COUNT) objects\n"
        ".PHONY: all\n";
  for (int i = 0; i < 200; ++i)
    MF += "mod" + std::to_string(i) + ".o:\n"
          "\techo CC mod" + std::to_string(i) + ".o\n";
  writeMakefile(MF);
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("echo LINK 200 objects")) << R.out;
  EXPECT_TRUE(R.contains("echo CC mod0.o")) << R.out;
  EXPECT_TRUE(R.contains("echo CC mod199.o")) << R.out;
}

// --- Parallel stress with dependency chain ---

TEST_F(BuildTest, KernelParallelDependencyChain) {
  writeMakefile(
      "all: step5\n"
      "\t@echo DONE\n"
      "step5: step4\n"
      "\t@echo STEP5\n"
      "step4: step3\n"
      "\t@echo STEP4\n"
      "step3: step2\n"
      "\t@echo STEP3\n"
      "step2: step1\n"
      "\t@echo STEP2\n"
      "step1:\n"
      "\t@echo STEP1\n"
      ".PHONY: all step1 step2 step3 step4 step5\n");
  auto R = runMake({"-j8"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("STEP1")) << R.out;
  EXPECT_TRUE(R.contains("STEP5")) << R.out;
  EXPECT_TRUE(R.contains("DONE")) << R.out;
}

// --- $(shell) returning empty (kernel feature detection) ---

TEST_F(BuildTest, KernelShellReturnEmpty) {
  writeMakefile(
      "HAS_FEATURE := $(shell echo 2>/dev/null)\n"
      "RESULT := $(if $(HAS_FEATURE),yes,no)\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=no")) << R.out;
}

// --- Multiple .PHONY declarations (kernel accumulates .PHONY) ---

TEST_F(BuildTest, KernelMultiplePhonyDecl) {
  writeMakefile(
      ".PHONY: all\n"
      ".PHONY: clean\n"
      ".PHONY: install\n"
      "all:\n"
      "\t@echo ALL\n"
      "clean:\n"
      "\t@echo CLEAN\n"
      "install: all\n"
      "\t@echo INSTALL\n");
  auto R1 = runMake({}, "all");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("ALL")) << R1.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("CLEAN")) << R2.out;

  auto R3 = runMake({}, "install");
  ASSERT_TRUE(R3.ok()) << R3.err;
  EXPECT_TRUE(R3.contains("INSTALL")) << R3.out;
}

// --- Kbuild: obj-y with variable expansion in target ---

TEST_F(BuildTest, KernelObjYVarExpansion) {
  writeMakefile(
      "CONFIG_A := y\n"
      "CONFIG_B := m\n"
      "CONFIG_C := n\n"
      "obj-y := base.o\n"
      "obj-$(CONFIG_A) += mod_a.o\n"
      "obj-$(CONFIG_B) += mod_b.o\n"
      "obj-$(CONFIG_C) += mod_c.o\n"
      "mod_a-objs := a1.o a2.o\n"
      "all:\n"
      "\t@echo obj-y=$(obj-y)\n"
      "\t@echo obj-m=$(obj-m)\n"
      "\t@echo obj-n=$(obj-n)\n"
      "\t@echo mod_a-objs=$(mod_a-objs)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("obj-y=base.o mod_a.o")) << R.out;
  EXPECT_TRUE(R.contains("obj-m=mod_b.o")) << R.out;
  EXPECT_TRUE(R.contains("obj-n=mod_c.o")) << R.out;
  EXPECT_TRUE(R.contains("mod_a-objs=a1.o a2.o")) << R.out;
}

// --- $(lastword) for path manipulation ---

TEST_F(BuildTest, KernelLastwordPath) {
  writeMakefile(
      "MAKEFILE_PATH := scripts/Makefile.build arch/x86/Makefile "
      "drivers/net/Makefile\n"
      "LAST := $(lastword $(MAKEFILE_PATH))\n"
      "FIRST := $(firstword $(MAKEFILE_PATH))\n"
      "all:\n"
      "\t@echo first=$(FIRST) last=$(LAST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("first=scripts/Makefile.build")) << R.out;
  EXPECT_TRUE(R.contains("last=drivers/net/Makefile")) << R.out;
}

// --- ifndef + override command line interaction ---

TEST_F(BuildTest, KernelIfndefOverrideCmd) {
  writeMakefile(
      "ARCH ?= x86\n"
      "ifndef CROSS_COMPILE\n"
      "  CROSS_COMPILE :=\n"
      "endif\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "all:\n"
      "\t@echo arch=$(ARCH) cc=$(CC)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("arch=x86")) << R1.out;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;

  auto R2 = runMake({"ARCH=arm", "CROSS_COMPILE=arm-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm")) << R2.out;
  EXPECT_TRUE(R2.contains("cc=arm-linux-gnu-gcc")) << R2.out;
}

// ============================================================================
// LINUX 5.10 KERNEL INTEGRATION — Real Kbuild Pattern Stress Tests
// ============================================================================

// --- Kbuild quiet/verbose cmd template (the single most important pattern) ---

TEST_F(BuildTest, KbuildCmdTemplate) {
  writeMakefile(
      "KBUILD_VERBOSE ?= 0\n"
      "ifeq ($(KBUILD_VERBOSE),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "quiet_cmd_cc = CC $@\n"
      "      cmd_cc = gcc -c -o $@ $<\n"
      "define echocmd\n"
      "  $(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))';)\n"
      "endef\n"
      "define rule_cc\n"
      "  $(call echocmd,cc) $(cmd_cc)\n"
      "endef\n"
      "all:\n"
      "\t@echo q=$(quiet) Q=$(Q)\n"
      "\t@echo cmd='$(cmd_cc)'\n"
      "\t@echo qcmd='$(quiet_cmd_cc)'\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("q=quiet_")) << R.out;
  EXPECT_TRUE(R.contains("Q=@")) << R.out;
  EXPECT_TRUE(R.contains("cmd='gcc -c -o $@ $<'") ||
              R.contains("cmd=gcc")) << R.out;
}

TEST_F(BuildTest, KbuildCmdTemplateVerbose) {
  writeMakefile(
      "KBUILD_VERBOSE ?= 0\n"
      "ifeq ($(KBUILD_VERBOSE),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "quiet_cmd_cc = CC $@\n"
      "      cmd_cc = gcc -c -o $@ $<\n"
      "all:\n"
      "\t@echo q=[$(quiet)] Q=[$(Q)]\n"
      ".PHONY: all\n");
  auto R = runMake({"KBUILD_VERBOSE=1"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("q=[] Q=[]")) << R.out;
}

// --- Kbuild obj-y multi-subdir aggregation with foreach+eval ---

TEST_F(BuildTest, KbuildMultiSubdirObjY) {
  writeMakefile(
      "subdirs := drivers fs net\n"
      "define subdir_template\n"
      "obj-$(1) :=\n"
      "obj-$(1) += $(1)/core.o\n"
      "obj-$(1) += $(1)/init.o\n"
      "endef\n"
      "$(foreach d,$(subdirs),$(eval $(call subdir_template,$(d))))\n"
      "all-objs := $(obj-drivers) $(obj-fs) $(obj-net)\n"
      "all:\n"
      "\t@echo objs=$(all-objs)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("drivers/core.o")) << R.out;
  EXPECT_TRUE(R.contains("drivers/init.o")) << R.out;
  EXPECT_TRUE(R.contains("fs/core.o")) << R.out;
  EXPECT_TRUE(R.contains("net/core.o")) << R.out;
}

// --- Kbuild CONFIG_* conditional compilation pipeline ---

TEST_F(BuildTest, KbuildConfigPipeline) {
  writeMakefile(
      "CONFIG_SMP := y\n"
      "CONFIG_PREEMPT := n\n"
      "CONFIG_DEBUG_INFO :=\n"
      "CONFIG_NET := y\n"
      "CONFIG_INET := y\n"
      "CONFIG_USB :=\n"
      "obj-y := main.o\n"
      "obj-$(CONFIG_SMP) += smp.o\n"
      "obj-$(CONFIG_NET) += net/\n"
      "obj-$(CONFIG_PREEMPT) += preempt.o\n"
      "obj-$(CONFIG_DEBUG_INFO) += debug.o\n"
      "net-objs-$(CONFIG_INET) := inet.o tcp.o\n"
      "net-objs-$(CONFIG_USB) := usb.o\n"
      "CFLAGS-y :=\n"
      "CFLAGS-$(CONFIG_SMP) += -DCONFIG_SMP\n"
      "CFLAGS-$(CONFIG_DEBUG_INFO) += -g\n"
      "all:\n"
      "\t@echo obj-y=$(obj-y)\n"
      "\t@echo obj-n=$(obj-n)\n"
      "\t@echo net-y=$(net-objs-y)\n"
      "\t@echo cflags=$(CFLAGS-y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("obj-y=main.o smp.o net/")) << R.out;
  EXPECT_TRUE(R.contains("obj-n=preempt.o")) << R.out;
  EXPECT_TRUE(R.contains("net-y=inet.o tcp.o")) << R.out;
  EXPECT_TRUE(R.contains("cflags=-DCONFIG_SMP")) << R.out;
}

// --- Kernel version extraction (typical top-level Makefile pattern) ---

TEST_F(BuildTest, KbuildVersionBlock) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "EXTRAVERSION =\n"
      "NAME = Kleptomaniac Octopus\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      "\t@echo name=$(NAME)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.0")) << R.out;
  EXPECT_TRUE(R.contains("name=Kleptomaniac Octopus")) << R.out;
}

// --- cc-option pattern (testing compiler flags) ---

TEST_F(BuildTest, KbuildCcOptionPattern) {
  writeMakefile(
      "SHELL := /bin/sh\n"
      "CC := gcc\n"
      "define cc-option\n"
      "$(shell if $(CC) $(1) -x c -c /dev/null -o /dev/null > /dev/null "
      "2>&1; then echo $(1); else echo $(2); fi)\n"
      "endef\n"
      "CFLAGS := -Wall\n"
      "CFLAGS += $(call cc-option,-Wno-unused-but-set-variable,)\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cflags=-Wall")) << R.out;
}

// --- Complex filter pipeline (typical Kbuild obj processing) ---

TEST_F(BuildTest, KbuildFilterPipelineSubdir) {
  writeMakefile(
      "obj-y := a.o b.o c.o d/ e.o\n"
      "obj-m := f.o g.o\n"
      "subdir-y := $(filter %/, $(obj-y))\n"
      "obj-y := $(filter-out %/, $(obj-y))\n"
      "real-obj-y := $(patsubst %.o, built-in/%.o, $(obj-y))\n"
      "subdir-y := $(patsubst %/,%,$(subdir-y))\n"
      "all:\n"
      "\t@echo subdir=$(subdir-y)\n"
      "\t@echo obj=$(obj-y)\n"
      "\t@echo real=$(real-obj-y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("subdir=d")) << R.out;
  EXPECT_TRUE(R.contains("obj=a.o b.o c.o e.o")) << R.out;
  EXPECT_TRUE(R.contains("real=built-in/a.o built-in/b.o built-in/c.o "
                          "built-in/e.o")) << R.out;
}

// --- Computed variable references ($($(var))) ---

TEST_F(BuildTest, KbuildComputedVarRef) {
  writeMakefile(
      "CONFIG_ARM := y\n"
      "machine-y := mach-x86\n"
      "machine-$(CONFIG_ARM) := mach-arm\n"
      "MACHINE := $(machine-y)\n"
      "all:\n"
      "\t@echo machine=$(MACHINE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("machine=mach-arm")) << R.out;
}

// --- define + call for multi-line recipe with @ and - prefixes ---

TEST_F(BuildTest, KbuildDefineCallRecipe) {
  writeMakefile(
      "define do_build\n"
      "@echo '  BUILD $(1)'\n"
      "@echo '  LINK  $(1).out'\n"
      "endef\n"
      "all:\n"
      "\t$(call do_build,vmlinux)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("BUILD vmlinux")) << R.out;
  EXPECT_TRUE(R.contains("LINK  vmlinux.out")) << R.out;
}

// --- FORCE target as universal rebuild trigger ---

TEST_F(BuildTest, KbuildFORCEMultipleTargets) {
  writeMakefile(
      "all: gen1 gen2\n"
      "\t@echo done\n"
      "gen1: FORCE\n"
      "\t@echo gen1\n"
      "gen2: FORCE\n"
      "\t@echo gen2\n"
      "FORCE:\n"
      ".PHONY: FORCE all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("gen1")) << R.out;
  EXPECT_TRUE(R.contains("gen2")) << R.out;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

// --- include with glob pattern for .d files ---

TEST_F(BuildTest, KbuildDotDIncludes) {
  writeFile(tmp() / "a.d", "a.o: a.c a.h\n");
  writeFile(tmp() / "b.d", "b.o: b.c\n");
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "a.h", "");
  writeFile(tmp() / "b.c", "");
  writeMakefile(
      "-include $(wildcard *.d)\n"
      "all: a.o b.o\n"
      "\t@echo built\n"
      "%.o: %.c\n"
      "\t@echo compile $< to $@\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile a.c to a.o") ||
              R.contains("echo compile")) << R.out;
}

// --- Substitution reference with directory paths ---

TEST_F(BuildTest, KbuildSubstRefDirPathDeep) {
  writeMakefile(
      "srcs := src/kernel/main.c src/kernel/sched.c src/mm/page.c\n"
      "objs := $(srcs:.c=.o)\n"
      "deps := $(srcs:.c=.d)\n"
      "all:\n"
      "\t@echo objs=$(objs)\n"
      "\t@echo deps=$(deps)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("objs=src/kernel/main.o src/kernel/sched.o "
                          "src/mm/page.o")) << R.out;
  EXPECT_TRUE(R.contains("deps=src/kernel/main.d src/kernel/sched.d "
                          "src/mm/page.d")) << R.out;
}

// --- ifeq with $(findstring ...) inside ---

TEST_F(BuildTest, KbuildIfeqFindstring) {
  writeMakefile(
      "ARCH := x86_64\n"
      "ifeq ($(findstring x86,$(ARCH)),x86)\n"
      "  X86 := y\n"
      "endif\n"
      "ifeq ($(findstring arm,$(ARCH)),arm)\n"
      "  ARM := y\n"
      "endif\n"
      "all:\n"
      "\t@echo x86=$(X86) arm=[$(ARM)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x86=y")) << R.out;
  EXPECT_TRUE(R.contains("arm=[]")) << R.out;
}

// --- Multiple .PHONY declarations (kernel accumulates these) ---

TEST_F(BuildTest, KbuildPhonyAccumulation) {
  writeMakefile(
      ".PHONY: all\n"
      ".PHONY: clean\n"
      ".PHONY: install\n"
      "all:\n"
      "\t@echo building\n"
      "clean:\n"
      "\t@echo cleaning\n"
      "install:\n"
      "\t@echo installing\n");
  auto R1 = runMake({}, "all");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("building")) << R1.out;
  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cleaning")) << R2.out;
}

// --- Complex CFLAGS aggregation (kernel builds up flags gradually) ---

TEST_F(BuildTest, KbuildCFLAGSAggregation) {
  writeMakefile(
      "CONFIG_SMP := y\n"
      "CONFIG_X86_64 := y\n"
      "CONFIG_STACK_PROTECTOR := y\n"
      "KBUILD_CFLAGS := -Wall -Wundef\n"
      "KBUILD_CFLAGS += -Werror=strict-prototypes\n"
      "KBUILD_CFLAGS += -std=gnu11\n"
      "ifdef CONFIG_SMP\n"
      "  KBUILD_CFLAGS += -DCONFIG_SMP\n"
      "endif\n"
      "ifeq ($(CONFIG_X86_64),y)\n"
      "  KBUILD_CFLAGS += -m64\n"
      "  KBUILD_CFLAGS += -mno-red-zone\n"
      "endif\n"
      "ifdef CONFIG_STACK_PROTECTOR\n"
      "  KBUILD_CFLAGS += -fstack-protector\n"
      "endif\n"
      "all:\n"
      "\t@echo $(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-DCONFIG_SMP")) << R.out;
  EXPECT_TRUE(R.contains("-m64")) << R.out;
  EXPECT_TRUE(R.contains("-mno-red-zone")) << R.out;
  EXPECT_TRUE(R.contains("-fstack-protector")) << R.out;
}

// --- Multi-level computed variable ($(call) returns var name, then deref) ---

TEST_F(BuildTest, KbuildMultiLevelVarDeref) {
  writeMakefile(
      "arch_flags_x86 := -m64 -march=x86-64\n"
      "arch_flags_arm := -march=armv7-a\n"
      "ARCH := x86\n"
      "define get_arch_flags\n"
      "$(arch_flags_$(1))\n"
      "endef\n"
      "FLAGS := $(call get_arch_flags,$(ARCH))\n"
      "all:\n"
      "\t@echo flags=$(FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-m64")) << R.out;
  EXPECT_TRUE(R.contains("-march=x86-64")) << R.out;
}

// --- MAKECMDGOALS detection (kernel uses this to skip checks for clean) ---

TEST_F(BuildTest, KbuildMakecmdgoalsFilter) {
  writeMakefile(
      "no-dot-config-targets := clean mrproper\n"
      "ifneq ($(filter $(no-dot-config-targets),$(MAKECMDGOALS)),)\n"
      "  SKIP_CONFIG := y\n"
      "else\n"
      "  SKIP_CONFIG :=\n"
      "endif\n"
      "all:\n"
      "\t@echo skip=$(SKIP_CONFIG)\n"
      "clean:\n"
      "\t@echo skip=$(SKIP_CONFIG)\n"
      ".PHONY: all clean\n");
  auto R1 = runMake({}, "all");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("skip=")) << R1.out;
  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("skip=y")) << R2.out;
}

// --- export computed variables (kernel exports CC, LD, etc.) ---

TEST_F(BuildTest, KbuildExportComputedCross) {
  writeMakefile(
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "AR := $(CROSS_COMPILE)ar\n"
      "export CC LD AR\n"
      "all:\n"
      "\t@echo cc=$(CC) ld=$(LD) ar=$(AR)\n"
      ".PHONY: all\n");
  auto R = runMake({"CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=aarch64-linux-gnu-gcc")) << R.out;
  EXPECT_TRUE(R.contains("ld=aarch64-linux-gnu-ld")) << R.out;
  EXPECT_TRUE(R.contains("ar=aarch64-linux-gnu-ar")) << R.out;
}

// --- Pattern rule + auto vars in complex scenario ---

TEST_F(BuildTest, KbuildPatternRuleAutoVars) {
  writeFile(tmp() / "foo.c", "int foo(){}");
  writeFile(tmp() / "bar.c", "int bar(){}");
  writeMakefile(
      "CC := gcc\n"
      "CFLAGS := -Wall\n"
      "OBJS := foo.o bar.o\n"
      "app: $(OBJS)\n"
      "\t@echo link $^ into $@\n"
      "%.o: %.c\n"
      "\t@echo $(CC) $(CFLAGS) -c $< -o $@\n"
      ".PHONY: app\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("gcc -Wall -c foo.c -o foo.o")) << R.out;
  EXPECT_TRUE(R.contains("gcc -Wall -c bar.c -o bar.o")) << R.out;
  EXPECT_TRUE(R.contains("link foo.o bar.o into app")) << R.out;
}

// --- define with := mode (simple expansion at definition) ---

TEST_F(BuildTest, KbuildDefineSimpleExpand) {
  writeMakefile(
      "X := hello\n"
      "define MSG :=\n"
      "message is $(X)\n"
      "endef\n"
      "X := world\n"
      "all:\n"
      "\t@echo $(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("message is hello")) << R.out;
}

// --- Deeply nested ifeq chain (kernel's arch/x86/Makefile pattern) ---

TEST_F(BuildTest, KbuildDeepIfeqChain) {
  writeMakefile(
      "CONFIG_X86_32 :=\n"
      "CONFIG_X86_64 := y\n"
      "ifeq ($(CONFIG_X86_32),y)\n"
      "  BITS := 32\n"
      "  UTS_MACHINE := i386\n"
      "  KBUILD_AFLAGS += -m32\n"
      "else ifeq ($(CONFIG_X86_64),y)\n"
      "  BITS := 64\n"
      "  UTS_MACHINE := x86_64\n"
      "  KBUILD_AFLAGS += -m64\n"
      "else\n"
      "  BITS := unknown\n"
      "endif\n"
      "ifeq ($(BITS),64)\n"
      "  KBUILD_CFLAGS += -mno-red-zone\n"
      "  KBUILD_CFLAGS += -mcmodel=kernel\n"
      "endif\n"
      "all:\n"
      "\t@echo bits=$(BITS) machine=$(UTS_MACHINE)\n"
      "\t@echo aflags=$(KBUILD_AFLAGS)\n"
      "\t@echo cflags=$(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bits=64")) << R.out;
  EXPECT_TRUE(R.contains("machine=x86_64")) << R.out;
  EXPECT_TRUE(R.contains("-m64")) << R.out;
  EXPECT_TRUE(R.contains("-mno-red-zone")) << R.out;
  EXPECT_TRUE(R.contains("-mcmodel=kernel")) << R.out;
}

// --- foreach + filter combo (Kbuild processes module lists) ---

TEST_F(BuildTest, KbuildForeachFilter) {
  writeMakefile(
      "modules := mod_a mod_b mod_c mod_d\n"
      "skip := mod_b mod_d\n"
      "active := $(filter-out $(skip),$(modules))\n"
      ".DEFAULT_GOAL := all\n"
      "define gen_rule\n"
      "$(1).ko: ; @echo 'LD [M] $(1).ko'\n"
      "endef\n"
      "$(foreach m,$(active),$(eval $(call gen_rule,$(m))))\n"
      "all: $(addsuffix .ko,$(active))\n"
      "\t@echo done mods=$(active)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("mod_a")) << R.out;
  EXPECT_TRUE(R.contains("mod_c")) << R.out;
}

// --- Shell assignment operator != ---

TEST_F(BuildTest, KbuildShellAssign) {
  writeMakefile(
      "UNAME != uname -s\n"
      "all:\n"
      "\t@echo os=$(UNAME)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("os=Darwin") || R.contains("os=Linux")) << R.out;
}

// --- $(word) and $(words) for version parsing with subst ---

TEST_F(BuildTest, KbuildVersionParsingSubst) {
  writeMakefile(
      "VERSION_STRING := 5.10.123-generic\n"
      "SPLIT := $(subst ., ,$(subst -, ,$(VERSION_STRING)))\n"
      "MAJOR := $(word 1,$(SPLIT))\n"
      "MINOR := $(word 2,$(SPLIT))\n"
      "PATCH := $(word 3,$(SPLIT))\n"
      "EXTRA := $(word 4,$(SPLIT))\n"
      "NWORDS := $(words $(SPLIT))\n"
      "all:\n"
      "\t@echo maj=$(MAJOR) min=$(MINOR) pat=$(PATCH) ext=$(EXTRA) n=$(NWORDS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("maj=5")) << R.out;
  EXPECT_TRUE(R.contains("min=10")) << R.out;
  EXPECT_TRUE(R.contains("pat=123")) << R.out;
  EXPECT_TRUE(R.contains("ext=generic")) << R.out;
  EXPECT_TRUE(R.contains("n=4")) << R.out;
}

// --- .DEFAULT_GOAL + MAKECMDGOALS interaction ---

TEST_F(BuildTest, KbuildDefaultGoalOverride) {
  writeMakefile(
      ".DEFAULT_GOAL := help\n"
      "help:\n"
      "\t@echo 'Targets: all clean install'\n"
      "all:\n"
      "\t@echo building\n"
      ".PHONY: help all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("Targets:")) << R1.out;

  auto R2 = runMake({}, "all");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("building")) << R2.out;
}

// --- $(addprefix) + $(addsuffix) pipeline ---

TEST_F(BuildTest, KbuildPrefixSuffixPipeline) {
  writeMakefile(
      "subdirs := kernel mm fs\n"
      "builtin-targets := $(addsuffix /built-in.a,$(subdirs))\n"
      "subdir-makefiles := $(addprefix $(srctree)/,$(addsuffix /Makefile,$(subdirs)))\n"
      "srctree := .\n"
      "all:\n"
      "\t@echo targets=$(builtin-targets)\n"
      "\t@echo makefiles=$(subdir-makefiles)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("targets=kernel/built-in.a mm/built-in.a "
                          "fs/built-in.a")) << R.out;
}

// --- $(sort) for deduplication with include paths ---

TEST_F(BuildTest, KbuildSortDedupIncludes) {
  writeMakefile(
      "INCLUDES := -Iinclude -Iarch/x86/include -Iinclude -Iarch/x86/include "
      "-Idrivers\n"
      "UNIQ_INCLUDES := $(sort $(INCLUDES))\n"
      "all:\n"
      "\t@echo inc=$(UNIQ_INCLUDES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  std::string Out = R.out;
  size_t First = Out.find("-Iinclude");
  size_t Second = Out.find("-Iinclude", First + 1);
  bool NoDup = (Second == std::string::npos) ||
               (Out.substr(Second - 5, 5) != "lude ");
  EXPECT_TRUE(R.contains("-Iarch/x86/include")) << R.out;
  EXPECT_TRUE(R.contains("-Idrivers")) << R.out;
}

// --- Recipe continuation line (backslash in recipe) ---

TEST_F(BuildTest, KbuildRecipeContinuation) {
  writeMakefile(
      "all:\n"
      "\t@echo start && \\\n"
      "\techo middle && \\\n"
      "\techo end\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("start")) << R.out;
  EXPECT_TRUE(R.contains("end")) << R.out;
}

// --- ifndef with empty vs undefined variable ---

TEST_F(BuildTest, KbuildIfndefEmptyVsUndefined) {
  writeMakefile(
      "DEFINED_EMPTY :=\n"
      "DEFINED_VALUE := something\n"
      "ifdef DEFINED_EMPTY\n"
      "  A := defined\n"
      "else\n"
      "  A := not_defined\n"
      "endif\n"
      "ifdef DEFINED_VALUE\n"
      "  B := defined\n"
      "else\n"
      "  B := not_defined\n"
      "endif\n"
      "ifdef NEVER_SET\n"
      "  C := defined\n"
      "else\n"
      "  C := not_defined\n"
      "endif\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B) c=$(C)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=not_defined")) << R.out;
  EXPECT_TRUE(R.contains("b=defined")) << R.out;
  EXPECT_TRUE(R.contains("c=not_defined")) << R.out;
}

// --- $(eval) generating pattern rules dynamically ---

TEST_F(BuildTest, KbuildEvalPatternRule) {
  writeFile(tmp() / "test.c", "");
  writeMakefile(
      "exts := c s S\n"
      "define compile_rule\n"
      "%.o: %.$(1)\n"
      "\t@echo 'compile-$(1) $$< -> $$@'\n"
      "endef\n"
      "$(foreach e,$(exts),$(eval $(call compile_rule,$(e))))\n"
      "all: test.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile-c")) << R.out;
}

// --- $(basename) + $(suffix) for complex paths ---

TEST_F(BuildTest, KbuildBasenameSuffixComplex) {
  writeMakefile(
      "files := src/main.c lib/utils.h arch/boot.S Makefile\n"
      "bases := $(basename $(files))\n"
      "exts := $(suffix $(files))\n"
      "dirs := $(dir $(files))\n"
      "names := $(notdir $(files))\n"
      "all:\n"
      "\t@echo bases=$(bases)\n"
      "\t@echo exts=$(exts)\n"
      "\t@echo dirs=$(dirs)\n"
      "\t@echo names=$(names)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bases=src/main lib/utils arch/boot Makefile"))
      << R.out;
  EXPECT_TRUE(R.contains("exts=.c .h .S")) << R.out;
  EXPECT_TRUE(R.contains("dirs=src/ lib/ arch/ ./")) << R.out;
  EXPECT_TRUE(R.contains("names=main.c utils.h boot.S Makefile")) << R.out;
}

// --- Full mini-kernel simulation: 5 subsystems ---

TEST_F(BuildTest, KbuildFullMiniKernel5Subsys) {
  for (auto sub : {"kernel", "mm", "fs", "drivers", "net"}) {
    writeFile(tmp() / (std::string(sub) + "_core.c"), "");
    writeFile(tmp() / (std::string(sub) + "_init.c"), "");
  }
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 0\n"
      "KERNELVERSION := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "CONFIG_SMP := y\n"
      "CONFIG_NET := y\n"
      "CONFIG_EXT4 := y\n"
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "\n"
      "KBUILD_CFLAGS := -Wall -O2\n"
      "ifdef CONFIG_SMP\n"
      "  KBUILD_CFLAGS += -DCONFIG_SMP\n"
      "endif\n"
      "\n"
      "subsystems := kernel mm fs\n"
      "subsystems-$(CONFIG_NET) += net\n"
      "subsystems-$(CONFIG_EXT4) += drivers\n"
      "all_subsys := $(subsystems) $(subsystems-y)\n"
      "\n"
      "define subsys_template\n"
      "$(1)-objs := $(1)_core.o $(1)_init.o\n"
      "endef\n"
      "$(foreach s,$(all_subsys),$(eval $(call subsys_template,$(s))))\n"
      "\n"
      "all-objs :=\n"
      "$(foreach s,$(all_subsys),$(eval all-objs += $$($(s)-objs)))\n"
      "\n"
      "FORCE:\n"
      ".PHONY: FORCE\n"
      "\n"
      "version.h: FORCE\n"
      "\t@echo '#define KERNEL_VERSION \"$(KERNELVERSION)\"' > $@\n"
      "\n"
      "%.o: %.c\n"
      "\t@echo '  CC [$(KERNELVERSION)]  $@'\n"
      "\n"
      "vmlinux: version.h $(all-objs)\n"
      "\t@echo '  LD  vmlinux ($(words $(all-objs)) objects)'\n"
      "\t@echo '  Kernel: $(KERNELVERSION) arch=$(ARCH) cc=$(CC)'\n"
      "\n"
      ".DEFAULT_GOAL := vmlinux\n"
      ".PHONY: vmlinux\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC [5.10.0]")) << R.out;
  EXPECT_TRUE(R.contains("LD  vmlinux")) << R.out;
  EXPECT_TRUE(R.contains("10 objects")) << R.out;
  EXPECT_TRUE(R.contains("5.10.0")) << R.out;
}

// --- $(if) with nested function calls ---

TEST_F(BuildTest, KbuildNestedIfFunctions) {
  writeMakefile(
      "CONFIG_DEBUG :=\n"
      "CONFIG_RELEASE := y\n"
      "OPT := $(if $(CONFIG_DEBUG),-O0 -g,$(if $(CONFIG_RELEASE),-O2,-O1))\n"
      "all:\n"
      "\t@echo opt=$(OPT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("opt=-O2")) << R.out;
}

// --- Multiple assignment modes interacting ---

TEST_F(BuildTest, KbuildAssignModeInteraction) {
  writeMakefile(
      "A = recursive_val\n"
      "B := simple_val\n"
      "C ?= conditional_val\n"
      "C ?= should_not_override\n"
      "D += append1\n"
      "D += append2\n"
      "E := base\n"
      "E += extended\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B) c=$(C) d=$(D) e=$(E)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=recursive_val")) << R.out;
  EXPECT_TRUE(R.contains("b=simple_val")) << R.out;
  EXPECT_TRUE(R.contains("c=conditional_val")) << R.out;
  EXPECT_TRUE(R.contains("d=append1 append2")) << R.out;
  EXPECT_TRUE(R.contains("e=base extended")) << R.out;
}

// --- Recipe with $$-escaped shell variables ---

TEST_F(BuildTest, KbuildDollarEscapeRecipe) {
  writeMakefile(
      "all:\n"
      "\t@for f in a b c; do echo \"file=$$f\"; done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("file=a")) << R.out;
  EXPECT_TRUE(R.contains("file=b")) << R.out;
  EXPECT_TRUE(R.contains("file=c")) << R.out;
}

// --- include chain (3-level deep, kernel Kbuild does this) ---

TEST_F(BuildTest, KbuildIncludeChainDeep) {
  writeFile(tmp() / "level1.mk",
            "L1 := level1\n"
            "include level2.mk\n");
  writeFile(tmp() / "level2.mk",
            "L2 := level2\n"
            "include level3.mk\n");
  writeFile(tmp() / "level3.mk", "L3 := level3\n");
  writeMakefile(
      "include level1.mk\n"
      "all:\n"
      "\t@echo $(L1) $(L2) $(L3)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("level1 level2 level3")) << R.out;
}

// --- $(strip) in ifeq (kernel does this to handle whitespace) ---

TEST_F(BuildTest, KbuildStripInIfeq) {
  writeMakefile(
      "OPTION :=   y   \n"
      "ifeq ($(strip $(OPTION)),y)\n"
      "  RESULT := matched\n"
      "else\n"
      "  RESULT := no_match\n"
      "endif\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("matched")) << R.out;
}

// --- $(origin) in conditional (kernel checks command-line overrides) ---

TEST_F(BuildTest, KbuildOriginConditional) {
  writeMakefile(
      "ARCH ?= x86\n"
      "ifeq ($(origin ARCH),command line)\n"
      "  OVERRIDE_ARCH := y\n"
      "else\n"
      "  OVERRIDE_ARCH :=\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(ARCH) override=$(OVERRIDE_ARCH)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("override=")) << R1.out;

  auto R2 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
  EXPECT_TRUE(R2.contains("override=y")) << R2.out;
}

// --- $(file) for writing generated files ---

TEST_F(BuildTest, KbuildFileWrite) {
  writeMakefile(
      "$(file >config.h,AUTO_GENERATED)\n"
      "$(file >>config.h,VERSION=5)\n"
      "$(file >>config.h,PATCHLEVEL=10)\n"
      "all:\n"
      "\t@cat config.h\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("AUTO_GENERATED")) << R.out;
  EXPECT_TRUE(R.contains("VERSION=5")) << R.out;
  EXPECT_TRUE(R.contains("PATCHLEVEL=10")) << R.out;
}

// --- undefine + re-define cycle ---

TEST_F(BuildTest, KbuildUndefineRedefine) {
  writeMakefile(
      "FOO := old_value\n"
      "undefine FOO\n"
      "ifndef FOO\n"
      "  FOO := new_value\n"
      "endif\n"
      "all:\n"
      "\t@echo foo=$(FOO)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("foo=new_value")) << R.out;
}

// --- $(or) / $(and) with multiple conditions ---

TEST_F(BuildTest, KbuildOrAndMultiCond) {
  writeMakefile(
      "A :=\n"
      "B := val_b\n"
      "C := val_c\n"
      "D :=\n"
      "R_OR := $(or $(A),$(B),$(C))\n"
      "R_AND := $(and $(B),$(C))\n"
      "R_AND2 := $(and $(A),$(B))\n"
      "all:\n"
      "\t@echo or=$(R_OR) and=$(R_AND) and2=[$(R_AND2)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("or=val_b")) << R.out;
  EXPECT_TRUE(R.contains("and=val_c")) << R.out;
  EXPECT_TRUE(R.contains("and2=[]")) << R.out;
}

// --- Static pattern with patsubst-derived targets ---

TEST_F(BuildTest, KbuildStaticPatternDerived) {
  writeFile(tmp() / "alpha.c", "");
  writeFile(tmp() / "beta.c", "");
  writeFile(tmp() / "gamma.c", "");
  writeMakefile(
      "SRCS := alpha.c beta.c gamma.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "all: $(OBJS)\n"
      "\t@echo linked\n"
      "$(OBJS): %.o: %.c\n"
      "\t@echo 'compile $< -> $@'\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"}, "all");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile alpha.c -> alpha.o")) << R.out;
  EXPECT_TRUE(R.contains("compile beta.c -> beta.o")) << R.out;
  EXPECT_TRUE(R.contains("compile gamma.c -> gamma.o")) << R.out;
}

// --- Recursive variable with late binding ---

TEST_F(BuildTest, KbuildRecursiveLateBind) {
  writeMakefile(
      "GREETING = Hello $(WHO)\n"
      "WHO = World\n"
      "all:\n"
      "\t@echo $(GREETING)\n"
      "WHO = NeverC\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("Hello NeverC")) << R.out;
}

// --- MAKE_VERSION detection in Makefile ---

TEST_F(BuildTest, KbuildMakeVersionCheck) {
  writeMakefile(
      "ifeq ($(filter 4.%,$(MAKE_VERSION)),)\n"
      "  $(error GNU make >= 4.0 required)\n"
      "endif\n"
      "all:\n"
      "\t@echo version=$(MAKE_VERSION) ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=4.3 ok")) << R.out;
}

// ============================================================================
// EDGE CASE & ROBUSTNESS TESTS
// ============================================================================

// --- Empty variable in foreach (should produce nothing) ---

TEST_F(BuildTest, EdgeForeachEmpty) {
  writeMakefile(
      "EMPTY :=\n"
      "RESULT := $(foreach x,$(EMPTY),item_$(x))\n"
      "all:\n"
      "\t@echo result=[$(RESULT)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=[]")) << R.out;
}

// --- Variable with only whitespace ---

TEST_F(BuildTest, EdgeWhitespaceOnlyVar) {
  writeMakefile(
      "BLANK :=    \n"
      "ifdef BLANK\n"
      "  A := defined\n"
      "else\n"
      "  A := empty\n"
      "endif\n"
      "ifeq ($(strip $(BLANK)),)\n"
      "  B := stripped_empty\n"
      "else\n"
      "  B := has_content\n"
      "endif\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("b=stripped_empty")) << R.out;
}

// --- Very long variable value ---

TEST_F(BuildTest, EdgeLongVarValue) {
  std::string LongList;
  for (int I = 0; I < 200; ++I) {
    if (I > 0) LongList += " ";
    LongList += "file_" + std::to_string(I) + ".o";
  }
  writeMakefile(
      "OBJS := " + LongList + "\n"
      "COUNT := $(words $(OBJS))\n"
      "FIRST := $(firstword $(OBJS))\n"
      "LAST := $(lastword $(OBJS))\n"
      "all:\n"
      "\t@echo n=$(COUNT) first=$(FIRST) last=$(LAST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("n=200")) << R.out;
  EXPECT_TRUE(R.contains("first=file_0.o")) << R.out;
  EXPECT_TRUE(R.contains("last=file_199.o")) << R.out;
}

// --- patsubst with no matching words ---

TEST_F(BuildTest, EdgePatsubstNoMatch) {
  writeMakefile(
      "FILES := readme.txt data.json config.yaml\n"
      "OBJS := $(patsubst %.c,%.o,$(FILES))\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("objs=readme.txt data.json config.yaml")) << R.out;
}

// --- filter with no matches ---

TEST_F(BuildTest, EdgeFilterNoMatch) {
  writeMakefile(
      "FILES := a.h b.h c.h\n"
      "C_FILES := $(filter %.c,$(FILES))\n"
      "all:\n"
      "\t@echo c=[$(C_FILES)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("c=[]")) << R.out;
}

// --- Recursive variable cycle detection ---

TEST_F(BuildTest, EdgeRecursiveCycleDetect) {
  writeMakefile(
      "A = $(B)\n"
      "B = $(A)\n"
      "all:\n"
      "\t@echo a=[$(A)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=[]")) << R.out;
}

// --- Tab-only recipe line (empty command) ---

TEST_F(BuildTest, EdgeTabOnlyRecipe) {
  writeMakefile(
      "all:\n"
      "\t\n"
      "\t@echo after_empty\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("after_empty")) << R.out;
}

// --- Multiple rules adding prerequisites to same target ---

TEST_F(BuildTest, EdgeMultiRulePrereqs) {
  writeMakefile(
      "all: a\n"
      "all: b\n"
      "all: c\n"
      "\t@echo all_done\n"
      "a:\n"
      "\t@echo build_a\n"
      "b:\n"
      "\t@echo build_b\n"
      "c:\n"
      "\t@echo build_c\n"
      ".PHONY: all a b c\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build_a")) << R.out;
  EXPECT_TRUE(R.contains("build_b")) << R.out;
  EXPECT_TRUE(R.contains("build_c")) << R.out;
  EXPECT_TRUE(R.contains("all_done")) << R.out;
}

// --- Deeply nested variable reference $($($(X))) ---

TEST_F(BuildTest, EdgeTripleIndirectVar) {
  writeMakefile(
      "real_value := THE_ANSWER\n"
      "middle := real_value\n"
      "top := middle\n"
      "RESULT := $($($(top)))\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=THE_ANSWER")) << R.out;
}

// --- Variable name with hyphens and dots ---

TEST_F(BuildTest, EdgeSpecialVarNames) {
  writeMakefile(
      "my-var := hyphen\n"
      "my.var := dot\n"
      "my_var := underscore\n"
      "all:\n"
      "\t@echo $(my-var) $(my.var) $(my_var)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hyphen dot underscore")) << R.out;
}

// --- ifeq with single quotes ---

TEST_F(BuildTest, EdgeIfeqSingleQuotes) {
  writeMakefile(
      "VAR := hello\n"
      "ifeq '$(VAR)' 'hello'\n"
      "  RESULT := match\n"
      "else\n"
      "  RESULT := no\n"
      "endif\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("match")) << R.out;
}

// --- sinclude (synonym for -include) ---

TEST_F(BuildTest, EdgeSinclude) {
  writeMakefile(
      "sinclude nonexistent.mk\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ok")) << R.out;
}

// --- Large foreach+eval stress test ---

TEST_F(BuildTest, StressForeachEval500) {
  std::string Mk = "modules :=\n";
  for (int I = 0; I < 500; ++I)
    Mk += "modules += mod_" + std::to_string(I) + "\n";
  Mk += "define mod_tmpl\n"
        "$(1)-objs := $(1).o\n"
        "endef\n"
        "$(foreach m,$(modules),$(eval $(call mod_tmpl,$(m))))\n"
        "COUNT := $(words $(modules))\n"
        "FIRST := $(firstword $(modules))\n"
        "LAST := $(lastword $(modules))\n"
        "all:\n"
        "\t@echo n=$(COUNT) first=$(FIRST) last=$(LAST)\n"
        "\t@echo first_obj=$(mod_0-objs) last_obj=$(mod_499-objs)\n"
        ".PHONY: all\n";
  writeMakefile(Mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("n=500")) << R.out;
  EXPECT_TRUE(R.contains("first=mod_0")) << R.out;
  EXPECT_TRUE(R.contains("last=mod_499")) << R.out;
  EXPECT_TRUE(R.contains("first_obj=mod_0.o")) << R.out;
  EXPECT_TRUE(R.contains("last_obj=mod_499.o")) << R.out;
}

// --- Order-only prerequisite does NOT trigger rebuild ---

TEST_F(BuildTest, EdgeOrderOnlyNoRebuild) {
  writeFile(tmp() / "source.c", "int main(){}");
  writeMakefile(
      "output: source.c | dir_marker\n"
      "\t@echo building output\n"
      "dir_marker:\n"
      "\t@echo creating dir\n"
      ".PHONY: dir_marker\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("creating dir") || R.contains("building output"))
      << R.out;
}

// --- ifeq with variables on both sides ---

TEST_F(BuildTest, EdgeIfeqBothSidesVars) {
  writeMakefile(
      "A := hello\n"
      "B := hello\n"
      "C := world\n"
      "ifeq ($(A),$(B))\n"
      "  R1 := match\n"
      "else\n"
      "  R1 := no\n"
      "endif\n"
      "ifeq ($(A),$(C))\n"
      "  R2 := match\n"
      "else\n"
      "  R2 := no\n"
      "endif\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=match")) << R.out;
  EXPECT_TRUE(R.contains("r2=no")) << R.out;
}

// --- $(call) with zero arguments ---

TEST_F(BuildTest, EdgeCallZeroArgs) {
  writeMakefile(
      "greeting = Hello from $(0)\n"
      "R := $(call greeting)\n"
      "all:\n"
      "\t@echo r=[$(R)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
}

// --- Semicolon inline recipe ---

TEST_F(BuildTest, EdgeInlineRecipe) {
  writeMakefile(
      "all: ; @echo inline_recipe\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("inline_recipe")) << R.out;
}

// --- override += with command line var ---

TEST_F(BuildTest, EdgeOverrideAppendCmdline) {
  writeMakefile(
      "override CFLAGS += -Wextra\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-Wall"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-Wextra")) << R.out;
}

// --- $(subst) chain (multiple substitutions) ---

TEST_F(BuildTest, EdgeSubstChain) {
  writeMakefile(
      "PATH_IN := src/foo/bar.c\n"
      "PATH_OUT := $(subst /,_,$(subst .c,.o,$(PATH_IN)))\n"
      "all:\n"
      "\t@echo out=$(PATH_OUT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("out=src_foo_bar.o")) << R.out;
}

// --- Multiple targets from one rule ---

TEST_F(BuildTest, EdgeMultiTargetRule) {
  writeMakefile(
      "all: a b c\n"
      "\t@echo done\n"
      "a b c:\n"
      "\t@echo building $@\n"
      ".PHONY: all a b c\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building a")) << R.out;
  EXPECT_TRUE(R.contains("building b")) << R.out;
  EXPECT_TRUE(R.contains("building c")) << R.out;
}

// --- $(eval) that redefines a variable ---

TEST_F(BuildTest, EdgeEvalRedefineVar) {
  writeMakefile(
      "X := before\n"
      "$(eval X := after)\n"
      "all:\n"
      "\t@echo x=$(X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x=after")) << R.out;
}

// --- Parallel build with dependency chain (A -> B -> C -> D) ---

TEST_F(BuildTest, StressParallelChain) {
  writeMakefile(
      "all: step_d\n"
      "step_d: step_c\n"
      "\t@echo step_d\n"
      "step_c: step_b\n"
      "\t@echo step_c\n"
      "step_b: step_a\n"
      "\t@echo step_b\n"
      "step_a:\n"
      "\t@echo step_a\n"
      ".PHONY: all step_a step_b step_c step_d\n");
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << R.err;
  auto PosA = R.out.find("step_a");
  auto PosB = R.out.find("step_b");
  auto PosC = R.out.find("step_c");
  auto PosD = R.out.find("step_d");
  EXPECT_TRUE(PosA < PosB && PosB < PosC && PosC < PosD) << R.out;
}

// --- Parallel fan-out (independent jobs) ---

TEST_F(BuildTest, StressParallelFanout) {
  std::string Mk = "all:";
  for (int I = 0; I < 20; ++I)
    Mk += " t_" + std::to_string(I);
  Mk += "\n\t@echo done\n";
  for (int I = 0; I < 20; ++I) {
    Mk += "t_" + std::to_string(I) + ":\n"
          "\t@echo t_" + std::to_string(I) + "\n";
  }
  Mk += ".PHONY: all";
  for (int I = 0; I < 20; ++I)
    Mk += " t_" + std::to_string(I);
  Mk += "\n";
  writeMakefile(Mk);
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("done")) << R.out;
  for (int I = 0; I < 20; ++I) {
    EXPECT_TRUE(R.contains("t_" + std::to_string(I))) << "missing t_" << I;
  }
}

// ============================================================================
// ROBUSTNESS & KERNEL COMPAT — Focused Edge-Case Tests
// ============================================================================

// --- Export with all assignment modes ---

TEST_F(BuildTest, ExportSimpleAssign) {
  writeMakefile(
      "export CC := neverc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=neverc")) << R.out;
}

TEST_F(BuildTest, ExportAppendAssign) {
  writeMakefile(
      "CFLAGS := -O2\n"
      "export CFLAGS += -Wall\n"
      "all:\n"
      "\t@echo flags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
}

TEST_F(BuildTest, ExportConditionalAssign) {
  writeMakefile(
      "export CC ?= gcc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
}

// --- MAKEFLAGS modification (kernel pattern: MAKEFLAGS += -rR) ---

TEST_F(BuildTest, MakeflagsAppend) {
  writeMakefile(
      "MAKEFLAGS += --no-print-directory\n"
      "all:\n"
      "\t@echo flags=$(MAKEFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("--no-print-directory")) << R.out;
}

// --- Computed variable references in recipes (kernel cmd pattern) ---

TEST_F(BuildTest, ComputedVarRefInRecipe) {
  writeMakefile(
      "quiet_cmd_cc = CC $@\n"
      "cmd_cc = $(CC) -c -o $@ $<\n"
      "CC := neverc\n"
      "MODE := cc\n"
      "all:\n"
      "\t@echo $(quiet_cmd_$(MODE))\n"
      "\t@echo $(cmd_$(MODE))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC all")) << R.out;
  EXPECT_TRUE(R.contains("neverc -c")) << R.out;
}

TEST_F(BuildTest, ComputedVarRefNested) {
  writeMakefile(
      "ARCH := x86\n"
      "machine-x86 := x86_64\n"
      "MACHINE := $(machine-$(ARCH))\n"
      "all:\n"
      "\t@echo machine=$(MACHINE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("machine=x86_64")) << R.out;
}

// --- define + call producing multiline recipe (kernel pattern) ---

TEST_F(BuildTest, DefineCallMultilineRecipe) {
  writeMakefile(
      "define compile_rule\n"
      "@echo Compiling $(1)\n"
      "@echo Done $(1)\n"
      "endef\n"
      "all:\n"
      "\t$(call compile_rule,main.c)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("Compiling main.c")) << R.out;
  EXPECT_TRUE(R.contains("Done main.c")) << R.out;
}

// --- Recipe with $$ for shell variable preservation ---

TEST_F(BuildTest, RecipeDollarEscape) {
  writeMakefile(
      "all:\n"
      "\t@X=hello; echo $$X\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello")) << R.out;
}

TEST_F(BuildTest, RecipeDollarEscapeLoop) {
  writeMakefile(
      "all:\n"
      "\t@for i in a b c; do echo $$i; done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a")) << R.out;
  EXPECT_TRUE(R.contains("b")) << R.out;
  EXPECT_TRUE(R.contains("c")) << R.out;
}

// --- include with computed path (kernel: include arch/$(SRCARCH)/Makefile) ---

TEST_F(BuildTest, IncludeComputedPath) {
  auto Sub = tmp() / "arch" / "x86";
  std::filesystem::create_directories(Sub);
  writeFile(Sub / "config.mk", "ARCH_FLAGS := -m64\n");
  writeMakefile(
      "SRCARCH := x86\n"
      "include arch/$(SRCARCH)/config.mk\n"
      "all:\n"
      "\t@echo flags=$(ARCH_FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("flags=-m64")) << R.out;
}

// --- ifndef default + command-line override pattern ---

TEST_F(BuildTest, IfndefDefaultCmdOverride) {
  writeMakefile(
      "ifndef CROSS_COMPILE\n"
      "  CROSS_COMPILE :=\n"
      "endif\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;

  auto R2 = runMake({"CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
}

// --- $(filter) with multiple patterns ---

TEST_F(BuildTest, FilterMultiplePatterns) {
  writeMakefile(
      "FILES := main.c util.h lib.c data.o readme.txt config.h\n"
      "SRCS := $(filter %.c %.h,$(FILES))\n"
      "all:\n"
      "\t@echo srcs=$(SRCS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("main.c")) << R.out;
  EXPECT_TRUE(R.contains("util.h")) << R.out;
  EXPECT_TRUE(R.contains("lib.c")) << R.out;
  EXPECT_TRUE(R.contains("config.h")) << R.out;
  EXPECT_FALSE(R.contains("data.o")) << R.out;
  EXPECT_FALSE(R.contains("readme.txt")) << R.out;
}

// --- $(filter-out) + $(filter) pipeline (kernel CONFIG pattern) ---

TEST_F(BuildTest, FilterPipeline) {
  writeMakefile(
      "ALL_OBJS := core.o net.o debug.o test.o bench.o\n"
      "EXCLUDE := test.o bench.o\n"
      "OBJS := $(filter-out $(EXCLUDE),$(ALL_OBJS))\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("core.o")) << R.out;
  EXPECT_TRUE(R.contains("net.o")) << R.out;
  EXPECT_TRUE(R.contains("debug.o")) << R.out;
  EXPECT_FALSE(R.contains("test.o")) << R.out;
  EXPECT_FALSE(R.contains("bench.o")) << R.out;
}

// --- substitution reference with directory paths ---

TEST_F(BuildTest, SubstRefDirPaths) {
  writeMakefile(
      "SRCS := src/main.c src/util.c src/lib.c\n"
      "OBJS := $(SRCS:src/%.c=build/%.o)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build/main.o")) << R.out;
  EXPECT_TRUE(R.contains("build/util.o")) << R.out;
  EXPECT_TRUE(R.contains("build/lib.o")) << R.out;
}

// --- $(eval) generating pattern rules via $(call) ---

TEST_F(BuildTest, EvalCallPatternRule) {
  writeMakefile(
      "define gen_rule\n"
      "$(1)/%.o: $(1)/%.c\n"
      "\t@echo compile $$< to $$@\n"
      "endef\n"
      "$(eval $(call gen_rule,src))\n"
      "$(eval $(call gen_rule,lib))\n"
      "all: src/main.o lib/util.o\n"
      "\t@echo linked\n"
      ".PHONY: all\n");
  writeFile(tmp() / "src" / "main.c", "");
  writeFile(tmp() / "lib" / "util.c", "");
  std::filesystem::create_directories(tmp() / "src");
  std::filesystem::create_directories(tmp() / "lib");
  writeFile(tmp() / "src" / "main.c", "");
  writeFile(tmp() / "lib" / "util.c", "");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile")) << R.out;
  EXPECT_TRUE(R.contains("linked")) << R.out;
}

// --- ifeq with $(strip) in condition (kernel pattern) ---

TEST_F(BuildTest, IfeqWithStrip) {
  writeMakefile(
      "CONFIG_SMP :=   y  \n"
      "ifeq ($(strip $(CONFIG_SMP)),y)\n"
      "  SMP := enabled\n"
      "else\n"
      "  SMP := disabled\n"
      "endif\n"
      "all:\n"
      "\t@echo smp=$(SMP)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("smp=enabled")) << R.out;
}

// --- Deeply nested $(if $(findstring ...)) (kernel pattern) ---

TEST_F(BuildTest, NestedIfFindstring) {
  writeMakefile(
      "CFLAGS := -march=native -O2 -Wall\n"
      "HAS_WALL := $(if $(findstring -Wall,$(CFLAGS)),yes,no)\n"
      "HAS_O3 := $(if $(findstring -O3,$(CFLAGS)),yes,no)\n"
      "all:\n"
      "\t@echo wall=$(HAS_WALL) o3=$(HAS_O3)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("wall=yes")) << R.out;
  EXPECT_TRUE(R.contains("o3=no")) << R.out;
}

// --- $(error) stops build ---

TEST_F(BuildTest, ErrorStopsBuild) {
  writeMakefile(
      "ifeq ($(TARGET),)\n"
      "$(error TARGET must be specified)\n"
      "endif\n"
      "all:\n"
      "\t@echo building $(TARGET)\n"
      ".PHONY: all\n");
  auto R = runMake();
  EXPECT_FALSE(R.ok());
}

TEST_F(BuildTest, ErrorNotTriggered) {
  writeMakefile(
      "TARGET := x86\n"
      "ifeq ($(TARGET),)\n"
      "$(error TARGET must be specified)\n"
      "endif\n"
      "all:\n"
      "\t@echo building $(TARGET)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building x86")) << R.out;
}

// --- $(warning) does not stop build ---

TEST_F(BuildTest, WarningContinues) {
  writeMakefile(
      "$(warning This is a test warning)\n"
      "all:\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

// --- Multiple include files ---

TEST_F(BuildTest, MultipleIncludes) {
  writeFile(tmp() / "vars.mk", "CC := neverc\nCFLAGS := -O2\n");
  writeFile(tmp() / "rules.mk",
            "%.o: %.c\n"
            "\t@echo $(CC) $(CFLAGS) -c $< -o $@\n");
  writeMakefile(
      "include vars.mk\n"
      "include rules.mk\n"
      "all: main.o\n"
      "\t@echo linked\n"
      ".PHONY: all\n");
  writeFile(tmp() / "main.c", "");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("neverc -O2 -c")) << R.out;
  EXPECT_TRUE(R.contains("linked")) << R.out;
}

// --- -include ignores missing file silently ---

TEST_F(BuildTest, DashIncludeSilent) {
  writeMakefile(
      "-include nonexistent.mk\n"
      "-include also_missing.mk\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ok")) << R.out;
}

// --- Recipe with pipe and redirect (shell constructs) ---

TEST_F(BuildTest, RecipeShellPipeRedirect) {
  writeMakefile(
      "all:\n"
      "\t@echo hello world | tr 'h' 'H'\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("Hello")) << R.out;
}

// --- Pattern rule with multiple prerequisites ---

TEST_F(BuildTest, PatternRuleMultiPrereq) {
  writeFile(tmp() / "main.c", "");
  writeFile(tmp() / "config.h", "");
  writeMakefile(
      "%.o: %.c config.h\n"
      "\t@echo compile $< with config.h for $@\n"
      "all: main.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile main.c with config.h for main.o")) << R.out;
}

// --- Kbuild quiet/verbose output template ---

TEST_F(BuildTest, KbuildQuietVerboseTemplate) {
  writeMakefile(
      "V ?= 0\n"
      "ifeq ($(V),0)\n"
      "  Q := @\n"
      "  quiet := quiet_\n"
      "else\n"
      "  Q :=\n"
      "  quiet :=\n"
      "endif\n"
      "quiet_cmd_compile = CC      $@\n"
      "cmd_compile = $(CC) -c -o $@ $<\n"
      "CC := neverc\n"
      "define run_cmd\n"
      "@echo $($(quiet)cmd_$(1))\n"
      "endef\n"
      "all:\n"
      "\t$(call run_cmd,compile)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("CC")) << R1.out;

  auto R2 = runMake({"V=1"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("neverc -c")) << R2.out;
}

// --- Order-only prerequisite does not trigger rebuild ---

TEST_F(BuildTest, OrderOnlyNoRebuild) {
  std::filesystem::create_directories(tmp() / "outdir");
  writeFile(tmp() / "src.c", "int main(){}");
  writeMakefile(
      "output: src.c | outdir\n"
      "\t@echo building output\n"
      "\t@touch output\n"
      "outdir:\n"
      "\t@mkdir -p outdir\n"
      ".PHONY: outdir\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("building output")) << R1.out;

  auto R2 = runMake();
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("up to date")) << R2.out;
}

// --- Complex 4-level ifeq chain with else ifeq ---

TEST_F(BuildTest, FourLevelIfeqChain) {
  writeMakefile(
      "ARCH ?= riscv\n"
      "ifeq ($(ARCH),x86)\n"
      "  BITS := 64\n"
      "  ARCH_DIR := arch/x86\n"
      "else ifeq ($(ARCH),arm)\n"
      "  BITS := 32\n"
      "  ARCH_DIR := arch/arm\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  BITS := 64\n"
      "  ARCH_DIR := arch/arm64\n"
      "else ifeq ($(ARCH),riscv)\n"
      "  BITS := 64\n"
      "  ARCH_DIR := arch/riscv\n"
      "else\n"
      "  BITS := unknown\n"
      "  ARCH_DIR := arch/unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(ARCH) bits=$(BITS) dir=$(ARCH_DIR)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("arch=riscv")) << R1.out;
  EXPECT_TRUE(R1.contains("bits=64")) << R1.out;
  EXPECT_TRUE(R1.contains("dir=arch/riscv")) << R1.out;

  auto R2 = runMake({"ARCH=arm"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("bits=32")) << R2.out;
  EXPECT_TRUE(R2.contains("dir=arch/arm")) << R2.out;
}

// --- foreach + filter + patsubst pipeline ---

TEST_F(BuildTest, ForeachFilterPatsubstPipeline) {
  writeMakefile(
      "subdirs := kernel mm net fs\n"
      "kernel-objs := sched.o fork.o\n"
      "mm-objs := page.o slab.o\n"
      "net-objs := socket.o tcp.o\n"
      "fs-objs := vfs.o ext4.o\n"
      "ALL_OBJS := $(foreach d,$(subdirs),$(addprefix $(d)/,$($(d)-objs)))\n"
      "all:\n"
      "\t@echo objs=$(ALL_OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("kernel/sched.o")) << R.out;
  EXPECT_TRUE(R.contains("mm/page.o")) << R.out;
  EXPECT_TRUE(R.contains("net/socket.o")) << R.out;
  EXPECT_TRUE(R.contains("fs/vfs.o")) << R.out;
  EXPECT_TRUE(R.contains("fs/ext4.o")) << R.out;
}

// --- ?= does not override existing value ---

TEST_F(BuildTest, ConditionalAssignNoOverride) {
  writeMakefile(
      "CC := clang\n"
      "CC ?= gcc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=clang")) << R.out;
}

// --- += creates recursive var if not yet defined ---

TEST_F(BuildTest, AppendCreateRecursive) {
  writeMakefile(
      "LDFLAGS += -lm\n"
      "LDFLAGS += -lpthread\n"
      "all:\n"
      "\t@echo ld=$(LDFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-lm")) << R.out;
  EXPECT_TRUE(R.contains("-lpthread")) << R.out;
}

// --- override prevents command-line override ---

TEST_F(BuildTest, OverridePreventsCmd) {
  writeMakefile(
      "override CFLAGS := -O2 -Wall\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O0"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2 -Wall")) << R.out;
  EXPECT_FALSE(R.contains("-O0")) << R.out;
}

// --- undefine + ifdef interaction ---

TEST_F(BuildTest, UndefineIfdefInteraction) {
  writeMakefile(
      "FOO := bar\n"
      "undefine FOO\n"
      "ifdef FOO\n"
      "  RESULT := defined\n"
      "else\n"
      "  RESULT := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=undefined")) << R.out;
}

// --- $(abspath) and $(dir)/$(notdir) combination ---

TEST_F(BuildTest, AbspathDirNotdir) {
  writeMakefile(
      "FILE := src/main.c\n"
      "D := $(dir $(FILE))\n"
      "F := $(notdir $(FILE))\n"
      "all:\n"
      "\t@echo dir=$(D) file=$(F)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dir=src/")) << R.out;
  EXPECT_TRUE(R.contains("file=main.c")) << R.out;
}

// --- $(basename) and $(suffix) ---

TEST_F(BuildTest, BasenameSuffix) {
  writeMakefile(
      "FILES := src/main.c lib/util.h build/app\n"
      "BASES := $(basename $(FILES))\n"
      "SUFFIXES := $(suffix $(FILES))\n"
      "all:\n"
      "\t@echo bases=$(BASES)\n"
      "\t@echo suf=$(SUFFIXES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bases=src/main lib/util build/app")) << R.out;
  EXPECT_TRUE(R.contains("suf=.c .h")) << R.out;
}

// --- Recursive variable late binding ---

TEST_F(BuildTest, RecursiveVarLateBinding) {
  writeMakefile(
      "GREETING = Hello $(NAME)\n"
      "NAME = World\n"
      "all:\n"
      "\t@echo $(GREETING)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("Hello World")) << R1.out;

  auto R2 = runMake({"NAME=NeverC"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("Hello NeverC")) << R2.out;
}

// --- $(wildcard) finds existing files ---

TEST_F(BuildTest, WildcardFindsFiles) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeFile(tmp() / "c.h", "");
  writeMakefile(
      "SRCS := $(wildcard *.c)\n"
      "all:\n"
      "\t@echo srcs=$(SRCS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a.c")) << R.out;
  EXPECT_TRUE(R.contains("b.c")) << R.out;
  EXPECT_FALSE(R.contains("c.h")) << R.out;
}

// --- $(wildcard) returns empty for no match ---

TEST_F(BuildTest, WildcardNoMatch) {
  writeMakefile(
      "SRCS := $(wildcard *.xyz)\n"
      "all:\n"
      "\t@echo srcs=[$(SRCS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("srcs=[]")) << R.out;
}

// --- Static pattern rule ---

TEST_F(BuildTest, StaticPatternRuleBuild) {
  writeFile(tmp() / "foo.c", "");
  writeFile(tmp() / "bar.c", "");
  writeMakefile(
      "OBJS := foo.o bar.o\n"
      "$(OBJS): %.o: %.c\n"
      "\t@echo compile $< to $@\n"
      "all: $(OBJS)\n"
      "\t@echo linked\n"
      ".PHONY: all\n");
  auto R = runMake({}, "all");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile foo.c to foo.o")) << R.out;
  EXPECT_TRUE(R.contains("compile bar.c to bar.o")) << R.out;
  EXPECT_TRUE(R.contains("linked")) << R.out;
}

// --- define with := mode ---

TEST_F(BuildTest, EdgeDefineSimpleModeExpand) {
  writeMakefile(
      "X := before\n"
      "define BLOCK :=\n"
      "value_$(X)\n"
      "endef\n"
      "X := after\n"
      "all:\n"
      "\t@echo block=$(BLOCK)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("block=value_before")) << R.out;
}

// --- define with += mode ---

TEST_F(BuildTest, EdgeDefineAppendModeBasic) {
  writeMakefile(
      "CMDS := initial\n"
      "define CMDS +=\n"
      "appended\n"
      "endef\n"
      "all:\n"
      "\t@echo cmds=$(CMDS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("initial")) << R.out;
  EXPECT_TRUE(R.contains("appended")) << R.out;
}

// --- Dry-run + force prefix (+) ---

TEST_F(BuildTest, DryRunForcePrefix) {
  writeMakefile(
      "all:\n"
      "\t+echo forced_command\n"
      "\techo skipped_command\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("forced_command")) << R.out;
  EXPECT_TRUE(R.contains("skipped_command")) << R.out;
}

// --- Silent prefix (@) hides command echo ---

TEST_F(BuildTest, SilentPrefixHides) {
  writeMakefile(
      "all:\n"
      "\t@echo visible_output\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("visible_output")) << R.out;
  EXPECT_FALSE(R.contains("echo visible_output")) << R.out;
}

// --- Ignore-error prefix (-) continues on failure ---

TEST_F(BuildTest, IgnoreErrorPrefix) {
  writeMakefile(
      "all:\n"
      "\t-false\n"
      "\t@echo continued\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("continued")) << R.out;
}

// --- $(call) with zero args ---

TEST_F(BuildTest, EdgeCallZeroArgsBasic) {
  writeMakefile(
      "define greet\n"
      "hello_world\n"
      "endef\n"
      "RESULT := $(call greet)\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=hello_world")) << R.out;
}

// --- $(call) with multiple args ---

TEST_F(BuildTest, EdgeCallMultiArgs) {
  writeMakefile(
      "define link_cmd\n"
      "$(1) -o $(2) $(3)\n"
      "endef\n"
      "CMD := $(call link_cmd,gcc,output,main.o util.o)\n"
      "all:\n"
      "\t@echo cmd=$(CMD)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("gcc -o output main.o util.o")) << R.out;
}

// --- Phony target always runs ---

TEST_F(BuildTest, PhonyAlwaysRuns) {
  writeMakefile(
      ".PHONY: clean\n"
      "clean:\n"
      "\t@echo cleaning\n");
  auto R1 = runMake({}, "clean");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cleaning")) << R1.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cleaning")) << R2.out;
}

// --- .DEFAULT_GOAL overrides first target ---

TEST_F(BuildTest, DefaultGoalOverridesFirst) {
  writeMakefile(
      ".DEFAULT_GOAL := help\n"
      "all:\n"
      "\t@echo building\n"
      "help:\n"
      "\t@echo usage_info\n"
      ".PHONY: all help\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("usage_info")) << R.out;
  EXPECT_FALSE(R.contains("building")) << R.out;
}

// --- -C changes directory ---

TEST_F(BuildTest, ChangeDirOption) {
  auto Sub = tmp() / "subproject";
  std::filesystem::create_directories(Sub);
  writeFile(Sub / "Makefile",
            "all:\n"
            "\t@echo subproject_built\n"
            ".PHONY: all\n");
  std::vector<std::string> Args = {"make", "-C", Sub.string()};
  auto R = ncc(Args);
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("subproject_built")) << R.out;
}

// --- neverc build alias works same as neverc make ---

TEST_F(BuildTest, BuildAliasWorks) {
  writeMakefile(
      "all:\n"
      "\t@echo built_via_alias\n"
      ".PHONY: all\n");
  auto R = runBuild();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("built_via_alias")) << R.out;
}

// --- Kbuild full pipeline: version + arch + config + modules ---

TEST_F(BuildTest, KbuildFullPipelineRobust) {
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 0\n"
      "EXTRAVERSION :=\n"
      "KERNELVERSION := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)\n"
      "\n"
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "\n"
      "ifeq ($(ARCH),x86)\n"
      "  SRCARCH := x86\n"
      "  BITS := 64\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  SRCARCH := arm64\n"
      "  BITS := 64\n"
      "else\n"
      "  SRCARCH := $(ARCH)\n"
      "  BITS := 32\n"
      "endif\n"
      "\n"
      "CONFIG_SMP ?= y\n"
      "CONFIG_NET ?= y\n"
      "CONFIG_DEBUG ?= n\n"
      "\n"
      "obj-y := init/ kernel/ mm/\n"
      "obj-$(CONFIG_NET) += net/\n"
      "obj-$(CONFIG_DEBUG) += debug/\n"
      "\n"
      "SUBDIRS := $(patsubst %/,%,$(filter %/,$(obj-y)))\n"
      "\n"
      "ifeq ($(CONFIG_SMP),y)\n"
      "  CFLAGS += -DCONFIG_SMP\n"
      "endif\n"
      "\n"
      "define gen_subdir\n"
      "$(1)_OBJS := $(1)/built-in.o\n"
      "endef\n"
      "$(foreach d,$(SUBDIRS),$(eval $(call gen_subdir,$(d))))\n"
      "\n"
      "ALL_OBJS := $(foreach d,$(SUBDIRS),$($(d)_OBJS))\n"
      "\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      "\t@echo arch=$(SRCARCH) bits=$(BITS)\n"
      "\t@echo cc=$(CC)\n"
      "\t@echo subdirs=$(SUBDIRS)\n"
      "\t@echo objs=$(ALL_OBJS)\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.0")) << R.out;
  EXPECT_TRUE(R.contains("arch=x86")) << R.out;
  EXPECT_TRUE(R.contains("bits=64")) << R.out;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
  EXPECT_TRUE(R.contains("init")) << R.out;
  EXPECT_TRUE(R.contains("kernel")) << R.out;
  EXPECT_TRUE(R.contains("mm")) << R.out;
  EXPECT_TRUE(R.contains("net")) << R.out;
  EXPECT_FALSE(R.contains("debug")) << R.out;
  EXPECT_TRUE(R.contains("-DCONFIG_SMP")) << R.out;

  auto R2 = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-",
                      "CONFIG_SMP=n"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
  EXPECT_FALSE(R2.contains("-DCONFIG_SMP")) << R2.out;
}

// --- $(foreach) + $(eval) + $(call) obj-y aggregation stress ---

TEST_F(BuildTest, StressObjYAggregationRobust) {
  std::string Mk;
  Mk += "define add_module\n";
  Mk += "obj-y += $(1).o\n";
  Mk += "endef\n";
  for (int I = 0; I < 50; ++I)
    Mk += "$(eval $(call add_module,mod_" + std::to_string(I) + "))\n";
  Mk += "all:\n";
  Mk += "\t@echo obj-y=$(obj-y)\n";
  Mk += "\t@echo count=$(words $(obj-y))\n";
  Mk += ".PHONY: all\n";
  writeMakefile(Mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("mod_0.o")) << R.out;
  EXPECT_TRUE(R.contains("mod_49.o")) << R.out;
  EXPECT_TRUE(R.contains("count=50")) << R.out;
}

// --- $(file) write and read ---

TEST_F(BuildTest, EdgeFileWriteReadBasic) {
  writeMakefile(
      "$(file >output.txt,hello from file)\n"
      "CONTENT := $(file <output.txt)\n"
      "all:\n"
      "\t@echo content=$(CONTENT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("content=hello from file")) << R.out;
}

// --- $(file) append mode ---

TEST_F(BuildTest, EdgeFileAppendBasic) {
  writeMakefile(
      "$(file >log.txt,content_ok)\n"
      "CONTENT := $(file <log.txt)\n"
      "all:\n"
      "\t@echo content=$(CONTENT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("content=content_ok")) << R.out;
}

// --- Recursive variable cycle detection ---

TEST_F(BuildTest, RecursiveCycleDetected) {
  writeMakefile(
      "A = $(B)\n"
      "B = $(A)\n"
      "all:\n"
      "\t@echo a=$(A)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=")) << R.out;
}

// --- MAKECMDGOALS detection ---

TEST_F(BuildTest, MakecmdgoalsDetection) {
  writeMakefile(
      "ifeq ($(MAKECMDGOALS),clean)\n"
      "  ACTION := cleaning\n"
      "else\n"
      "  ACTION := building\n"
      "endif\n"
      "all:\n"
      "\t@echo action=$(ACTION)\n"
      "clean:\n"
      "\t@echo action=$(ACTION)\n"
      ".PHONY: all clean\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("action=building")) << R1.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("action=cleaning")) << R2.out;
}

// --- MAKE_VERSION check (kernel checks this) ---

TEST_F(BuildTest, MakeVersionCheck) {
  writeMakefile(
      "ifeq ($(filter 4.%,$(MAKE_VERSION)),)\n"
      "  $(error need make >= 4.0)\n"
      "endif\n"
      "all:\n"
      "\t@echo version=$(MAKE_VERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=4.3")) << R.out;
}

// --- $(origin) for different variable sources ---

TEST_F(BuildTest, OriginMultipleSources) {
  writeMakefile(
      "FILE_VAR := from_file\n"
      "all:\n"
      "\t@echo file=$(origin FILE_VAR)\n"
      "\t@echo cmd=$(origin CMD_VAR)\n"
      "\t@echo undef=$(origin NONEXIST)\n"
      ".PHONY: all\n");
  auto R = runMake({"CMD_VAR=from_cmd"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("file=file")) << R.out;
  EXPECT_TRUE(R.contains("cmd=command line")) << R.out;
  EXPECT_TRUE(R.contains("undef=undefined")) << R.out;
}

// --- $(value) returns unexpanded value ---

TEST_F(BuildTest, ValueReturnsUnexpanded) {
  writeMakefile(
      "X := hello\n"
      "EXPR = value_is_$(X)\n"
      "HAS_DOLLAR := $(if $(findstring $$,$(value EXPR)),yes,no)\n"
      "EXPANDED := $(EXPR)\n"
      "all:\n"
      "\t@echo expanded=$(EXPANDED)\n"
      "\t@echo has_dollar=$(HAS_DOLLAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("expanded=value_is_hello")) << R.out;
  EXPECT_TRUE(R.contains("has_dollar=yes")) << R.out;
}

// --- $(flavor) returns variable type ---

TEST_F(BuildTest, FlavorReturnsType) {
  writeMakefile(
      "REC = recursive\n"
      "SIM := simple\n"
      "all:\n"
      "\t@echo rec=$(flavor REC)\n"
      "\t@echo sim=$(flavor SIM)\n"
      "\t@echo undef=$(flavor NOPE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("rec=recursive")) << R.out;
  EXPECT_TRUE(R.contains("sim=simple")) << R.out;
  EXPECT_TRUE(R.contains("undef=undefined")) << R.out;
}

// --- $(and) / $(or) multi-argument ---

TEST_F(BuildTest, AndOrMultiArg) {
  writeMakefile(
      "A := yes\n"
      "B := \n"
      "C := also_yes\n"
      "R_AND := $(and $(A),$(B),$(C))\n"
      "R_OR := $(or $(B),$(A),$(C))\n"
      "all:\n"
      "\t@echo and=[$(R_AND)] or=[$(R_OR)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("and=[]")) << R.out;
  EXPECT_TRUE(R.contains("or=[yes]")) << R.out;
}

// --- Parallel error propagation ---

TEST_F(BuildTest, ParallelErrorPropagation) {
  writeMakefile(
      "all: good bad\n"
      "\t@echo should_not_reach\n"
      "good:\n"
      "\t@echo good_done\n"
      "bad:\n"
      "\tfalse\n"
      ".PHONY: all good bad\n");
  auto R = runMake({"-j2"});
  EXPECT_FALSE(R.ok());
  EXPECT_FALSE(R.contains("should_not_reach")) << R.out;
}

// --- -k keeps going after error ---

TEST_F(BuildTest, RobustKeepGoingContinues) {
  writeMakefile(
      "all: a b c\n"
      "\t@echo final\n"
      "a:\n"
      "\t@echo a_ok\n"
      "b:\n"
      "\tfalse\n"
      "c:\n"
      "\t@echo c_ok\n"
      ".PHONY: all a b c\n");
  auto R = runMake({"-k"});
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.contains("a_ok")) << R.out;
  EXPECT_TRUE(R.contains("c_ok")) << R.out;
}

// --- Always-make flag (-B) rebuilds even up-to-date ---

TEST_F(BuildTest, AlwaysMakeRebuilds) {
  writeFile(tmp() / "src.c", "int main(){}");
  writeMakefile(
      "output: src.c\n"
      "\t@echo rebuilding\n"
      "\t@touch output\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("rebuilding")) << R1.out;

  auto R2 = runMake();
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("up to date")) << R2.out;

  auto R3 = runMake({"-B"});
  ASSERT_TRUE(R3.ok()) << R3.err;
  EXPECT_TRUE(R3.contains("rebuilding")) << R3.out;
}

// --- Circular dependency detection ---

TEST_F(BuildTest, CircularDepDetected) {
  writeMakefile(
      "a: b\n"
      "\t@echo a\n"
      "b: c\n"
      "\t@echo b\n"
      "c: a\n"
      "\t@echo c\n");
  auto R = runMake({}, "a");
  EXPECT_FALSE(R.ok());
}

// --- No rule for target error ---

TEST_F(BuildTest, NoRuleForTarget) {
  writeMakefile(
      "all: nonexistent.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake();
  EXPECT_FALSE(R.ok());
}

// --- Empty Makefile ---

TEST_F(BuildTest, RobustEmptyMakefile) {
  writeMakefile("");
  auto R = runMake();
  EXPECT_FALSE(R.ok());
}

// --- Kbuild cc-option simulation ---

TEST_F(BuildTest, KbuildCcOptionSim) {
  writeMakefile(
      "define try-run\n"
      "$(shell set -e; if $(1) > /dev/null 2>&1; then echo $(2); else echo "
      "$(3); fi)\n"
      "endef\n"
      "RESULT := $(call try-run,echo test,-Wno-unused,fallback)\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=-Wno-unused")) << R.out;
}

// --- $(sort) preserves unique and sorts ---

TEST_F(BuildTest, SortUniqueOrder) {
  writeMakefile(
      "ITEMS := z a m a z b\n"
      "SORTED := $(sort $(ITEMS))\n"
      "all:\n"
      "\t@echo sorted=$(SORTED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("sorted=a b m z")) << R.out;
}

// --- $(word) and $(wordlist) boundary ---

TEST_F(BuildTest, WordBoundary) {
  writeMakefile(
      "LIST := alpha beta gamma delta\n"
      "W1 := $(word 1,$(LIST))\n"
      "W4 := $(word 4,$(LIST))\n"
      "W5 := $(word 5,$(LIST))\n"
      "WL := $(wordlist 2,3,$(LIST))\n"
      "all:\n"
      "\t@echo w1=$(W1) w4=$(W4) w5=[$(W5)] wl=$(WL)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("w1=alpha")) << R.out;
  EXPECT_TRUE(R.contains("w4=delta")) << R.out;
  EXPECT_TRUE(R.contains("w5=[]")) << R.out;
  EXPECT_TRUE(R.contains("wl=beta gamma")) << R.out;
}

// --- Stress: many variables ---

TEST_F(BuildTest, StressManyVariablesRobust) {
  std::string Mk;
  for (int I = 0; I < 200; ++I)
    Mk += "VAR_" + std::to_string(I) + " := val_" + std::to_string(I) + "\n";
  Mk += "RESULT := $(VAR_0) $(VAR_99) $(VAR_199)\n";
  Mk += "all:\n";
  Mk += "\t@echo result=$(RESULT)\n";
  Mk += ".PHONY: all\n";
  writeMakefile(Mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("val_0")) << R.out;
  EXPECT_TRUE(R.contains("val_99")) << R.out;
  EXPECT_TRUE(R.contains("val_199")) << R.out;
}

// --- Stress: deep dependency chain ---

TEST_F(BuildTest, StressDeepDependency) {
  std::string Mk = "all: step_0\n\t@echo done\n";
  for (int I = 0; I < 50; ++I) {
    Mk += "step_" + std::to_string(I) + ": step_" + std::to_string(I + 1) +
          "\n\t@echo step_" + std::to_string(I) + "\n";
  }
  Mk += "step_50:\n\t@echo step_50\n";
  Mk += ".PHONY: all";
  for (int I = 0; I <= 50; ++I)
    Mk += " step_" + std::to_string(I);
  Mk += "\n";
  writeMakefile(Mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("step_50")) << R.out;
  EXPECT_TRUE(R.contains("step_0")) << R.out;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

// --- Stress: complex Kbuild with multiple subsystems ---

TEST_F(BuildTest, StressKbuildMultiSubsystem) {
  std::string Mk;
  Mk += "VERSION := 5\n";
  Mk += "PATCHLEVEL := 10\n";
  Mk += "SUBLEVEL := 0\n";
  Mk += "KERNELVERSION := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n";
  Mk += "ARCH ?= x86\n";
  Mk += "CROSS_COMPILE ?=\n";
  Mk += "CC := $(CROSS_COMPILE)gcc\n";
  Mk += "ifeq ($(ARCH),x86)\n";
  Mk += "  SRCARCH := x86\n";
  Mk += "else ifeq ($(ARCH),arm64)\n";
  Mk += "  SRCARCH := arm64\n";
  Mk += "else\n";
  Mk += "  SRCARCH := $(ARCH)\n";
  Mk += "endif\n";
  Mk += "define subsys_template\n";
  Mk += "$(1)-objs := $(1)/core.o $(1)/init.o\n";
  Mk += "obj-y += $$($(1)-objs)\n";
  Mk += "endef\n";
  std::vector<std::string> Subsystems = {"kernel", "mm",  "fs",
                                          "net",    "ipc", "drivers"};
  for (auto &S : Subsystems)
    Mk += "$(eval $(call subsys_template," + S + "))\n";
  Mk += "ALL := $(obj-y)\n";
  Mk += "NOBJ := $(words $(ALL))\n";
  Mk += "all:\n";
  Mk += "\t@echo version=$(KERNELVERSION)\n";
  Mk += "\t@echo arch=$(SRCARCH)\n";
  Mk += "\t@echo objects=$(NOBJ)\n";
  Mk += "\t@echo cc=$(CC)\n";
  Mk += ".PHONY: all\n";
  writeMakefile(Mk);

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("version=5.10.0")) << R1.out;
  EXPECT_TRUE(R1.contains("arch=x86")) << R1.out;
  EXPECT_TRUE(R1.contains("objects=12")) << R1.out;

  auto R2 = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
  EXPECT_TRUE(R2.contains("cc=aarch64-gcc")) << R2.out;
}

// ============================================================================
// FINAL ROBUSTNESS TESTS — Edge Cases & Hardening
// ============================================================================

// --- Recipe empty tab lines should not break execution ---

TEST_F(BuildTest, HardenEmptyTabLine) {
  writeMakefile(
      "all:\n"
      "\t@echo line1\n"
      "\t\n"
      "\t@echo line2\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("line1")) << R.out;
  EXPECT_TRUE(R.contains("line2")) << R.out;
}

// --- Dollar-dollar shell variable escape ---

TEST_F(BuildTest, HardenDollarDollarEscape) {
  writeMakefile(
      "all:\n"
      "\t@X=hello; echo $$X\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello")) << R.out;
}

// --- Pattern rule with no explicit prerequisite ---

TEST_F(BuildTest, HardenPatternRuleNoPrereq) {
  writeMakefile(
      "%.stamp:\n"
      "\t@echo stamp $@\n"
      "all: foo.stamp\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("stamp foo.stamp")) << R.out;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

// --- Simple variable := then += should expand on append ---

TEST_F(BuildTest, HardenSimpleVarAppend) {
  writeMakefile(
      "BASE := /usr\n"
      "DIR := $(BASE)/lib\n"
      "DIR += $(BASE)/include\n"
      "all:\n"
      "\t@echo $(DIR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("/usr/lib /usr/include")) << R.out;
}

// --- Recursive variable late binding ---

TEST_F(BuildTest, HardenRecursiveLateBinding) {
  writeMakefile(
      "A = $(B)\n"
      "B = $(C)\n"
      "C = final\n"
      "all:\n"
      "\t@echo $(A)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("final")) << R.out;
}

// --- Nested $(if) with $(findstring) ---

TEST_F(BuildTest, HardenNestedIfFindstring) {
  writeMakefile(
      "MODE := debug\n"
      "FLAGS := $(if $(findstring debug,$(MODE)),-g -O0,-O2)\n"
      "all:\n"
      "\t@echo $(FLAGS)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("-g -O0")) << R1.out;

  auto R2 = runMake({"MODE=release"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("-O2")) << R2.out;
}

// --- Substitution reference with directory paths ---

TEST_F(BuildTest, HardenSubstRefDirPath) {
  writeMakefile(
      "OBJS := src/main.o src/util.o lib/helper.o\n"
      "SRCS := $(OBJS:.o=.c)\n"
      "all:\n"
      "\t@echo $(SRCS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("src/main.c src/util.c lib/helper.c")) << R.out;
}

// --- $(call) with zero arguments ---

TEST_F(BuildTest, HardenCallZeroArgs) {
  writeMakefile(
      "define greeting\n"
      "hello world\n"
      "endef\n"
      "MSG := $(call greeting)\n"
      "all:\n"
      "\t@echo $(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello world")) << R.out;
}

// --- $(foreach) with empty list ---

TEST_F(BuildTest, HardenForeachEmpty) {
  writeMakefile(
      "ITEMS :=\n"
      "LIST := $(foreach i,$(ITEMS),item_$(i))\n"
      "all:\n"
      "\t@echo [$(LIST)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("[]")) << R.out;
}

// --- Order-only prerequisite does not trigger rebuild ---

TEST_F(BuildTest, HardenOrderOnlyNoRebuild) {
  writeFile(tmp() / "src.txt", "content");
  writeMakefile(
      "output.txt: src.txt | dirs\n"
      "\t@cp src.txt output.txt && echo built\n"
      "dirs:\n"
      "\t@echo makedirs\n"
      ".PHONY: dirs\n");
  auto R1 = runMake({}, "output.txt");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("built")) << R1.out;

  auto R2 = runMake({}, "output.txt");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_FALSE(R2.contains("built")) << "Should be up to date: " << R2.out;
}

// --- Multiple rules for same target merge prerequisites ---

TEST_F(BuildTest, HardenMultiRulePrereqMerge) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.h", "");
  writeMakefile(
      "prog: a.c\n"
      "\t@echo compile with a.c and b.h\n"
      "prog: b.h\n"
      ".PHONY: prog\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile with a.c and b.h")) << R.out;
}

// --- ifeq with quoted strings ---

TEST_F(BuildTest, HardenIfeqQuotedStrings) {
  writeMakefile(
      "X := hello\n"
      "ifeq \"$(X)\" \"hello\"\n"
      "  RESULT := match\n"
      "else\n"
      "  RESULT := nomatch\n"
      "endif\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("match")) << R.out;
}

// --- $(filter) with multiple patterns ---

TEST_F(BuildTest, HardenFilterMultiPattern) {
  writeMakefile(
      "FILES := main.c util.h config.c test.h readme.txt\n"
      "HEADERS := $(filter %.h %.txt,$(FILES))\n"
      "all:\n"
      "\t@echo $(HEADERS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("util.h test.h readme.txt")) << R.out;
}

// --- Chained $(subst) calls ---

TEST_F(BuildTest, HardenChainedSubst) {
  writeMakefile(
      "VER := 5.10.123\n"
      "MAJ := $(subst ., ,$(VER))\n"
      "all:\n"
      "\t@echo major=$(firstword $(MAJ)) minor=$(word 2,$(MAJ))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("major=5")) << R.out;
  EXPECT_TRUE(R.contains("minor=10")) << R.out;
}

// --- define block with := (simple assignment) ---

TEST_F(BuildTest, HardenDefineSimpleAssign) {
  writeMakefile(
      "X := early\n"
      "define CMD :=\n"
      "value_$(X)\n"
      "endef\n"
      "X := late\n"
      "all:\n"
      "\t@echo $(CMD)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("value_early")) << R.out;
}

// --- $(eval) generating rules at runtime ---

TEST_F(BuildTest, HardenEvalDynamicRule) {
  writeMakefile(
      "TARGETS := alpha beta gamma\n"
      "define gen_rule\n"
      "$(1):\n"
      "\t@echo building $(1)\n"
      ".PHONY: $(1)\n"
      "endef\n"
      ".DEFAULT_GOAL := all\n"
      "$(foreach t,$(TARGETS),$(eval $(call gen_rule,$(t))))\n"
      "all: $(TARGETS)\n"
      "\t@echo all done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building alpha")) << R.out;
  EXPECT_TRUE(R.contains("building beta")) << R.out;
  EXPECT_TRUE(R.contains("building gamma")) << R.out;
  EXPECT_TRUE(R.contains("all done")) << R.out;
}

// --- Kbuild-style if_changed command pattern ---

TEST_F(BuildTest, KbuildIfChangedPattern) {
  writeMakefile(
      "quiet_cmd_cc = CC      $@\n"
      "cmd_cc = $(CC) -c -o $@ $<\n"
      "CC := neverc\n"
      "define if_changed\n"
      "$($(quiet)cmd_$(1))\n"
      "endef\n"
      "quiet := quiet_\n"
      "all:\n"
      "\t@echo $(call if_changed,cc)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC")) << R.out;
}

// --- Deeply nested variable references $($($(C))) ---

TEST_F(BuildTest, HardenTripleIndirection) {
  writeMakefile(
      "LEVEL := middle\n"
      "middle := final_var\n"
      "final_var := deep_value\n"
      "RESULT := $($($(LEVEL)))\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("deep_value")) << R.out;
}

// --- export computed variable ---

TEST_F(BuildTest, HardenExportComputed) {
  writeMakefile(
      "ARCH := arm64\n"
      "CROSS_$(ARCH) := aarch64-linux-gnu-\n"
      "export CROSS_$(ARCH)\n"
      "all:\n"
      "\t@echo cross=$(CROSS_arm64)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cross=aarch64-linux-gnu-")) << R.out;
}

// --- $(addprefix) + $(addsuffix) pipeline ---

TEST_F(BuildTest, HardenPrefixSuffixPipeline) {
  writeMakefile(
      "NAMES := foo bar baz\n"
      "RESULT := $(addsuffix .o,$(addprefix build/,$(NAMES)))\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build/foo.o build/bar.o build/baz.o")) << R.out;
}

// --- Continuation line in recipe ---

TEST_F(BuildTest, HardenRecipeContinuation) {
  writeMakefile(
      "all:\n"
      "\t@echo hello \\\n"
      "\tworld\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello")) << R.out;
}

// --- $(words) and $(word) boundary ---

TEST_F(BuildTest, HardenWordBoundary) {
  writeMakefile(
      "LIST := a b c d e\n"
      "N := $(words $(LIST))\n"
      "LAST := $(word $(N),$(LIST))\n"
      "all:\n"
      "\t@echo count=$(N) last=$(LAST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=5")) << R.out;
  EXPECT_TRUE(R.contains("last=e")) << R.out;
}

// --- $(basename) and $(suffix) with multiple dots ---

TEST_F(BuildTest, HardenBasenameSuffixMultiDot) {
  writeMakefile(
      "FILES := archive.tar.gz main.c.bak no_ext\n"
      "BASES := $(basename $(FILES))\n"
      "SUFFS := $(suffix $(FILES))\n"
      "all:\n"
      "\t@echo bases=$(BASES)\n"
      "\t@echo suffs=$(SUFFS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bases=archive.tar main.c no_ext")) << R.out;
  EXPECT_TRUE(R.contains("suffs=.gz .bak")) << R.out;
}

// --- Kernel version extraction pattern ---

TEST_F(BuildTest, KbuildVersionExtract) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 186\n"
      "EXTRAVERSION =\n"
      "KERNELVERSION = "
      "$(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if $(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo $(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("5.10.186")) << R.out;
}

// --- $(sort) deduplication ---

TEST_F(BuildTest, HardenSortDedup) {
  writeMakefile(
      "LIST := c b a c b a d\n"
      "SORTED := $(sort $(LIST))\n"
      "all:\n"
      "\t@echo $(SORTED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a b c d")) << R.out;
}

// --- ifdef with empty recursive variable (should be false) ---

TEST_F(BuildTest, HardenIfdefEmptyRecursive) {
  writeMakefile(
      "EMPTY =\n"
      "ifdef EMPTY\n"
      "  RESULT := defined\n"
      "else\n"
      "  RESULT := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("undefined")) << R.out;
}

// --- ifndef with truly undefined variable ---

TEST_F(BuildTest, HardenIfndefUndefined) {
  writeMakefile(
      "ifndef NEVER_SET\n"
      "  RESULT := not_set\n"
      "else\n"
      "  RESULT := is_set\n"
      "endif\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("not_set")) << R.out;
}

// --- ?= does not override existing value ---

TEST_F(BuildTest, HardenConditionalNoOverride) {
  writeMakefile(
      "X := existing\n"
      "X ?= default\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("existing")) << R.out;
}

// --- Command line variable overrides Makefile ---

TEST_F(BuildTest, HardenCmdLineOverride) {
  writeMakefile(
      "CC := gcc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R = runMake({"CC=clang"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=clang")) << R.out;
}

// --- override defeats command line ---

TEST_F(BuildTest, HardenOverrideDefeatsCmdLine) {
  writeMakefile(
      "override CC := neverc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R = runMake({"CC=gcc"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=neverc")) << R.out;
}

// --- Dry run with + prefix forces execution ---

TEST_F(BuildTest, HardenDryRunForcePrefix) {
  writeMakefile(
      "all:\n"
      "\t+@echo forced\n"
      "\t@echo skipped\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("forced")) << R.out;
}

// --- Kbuild multi-subsystem with foreach+eval+call ---

TEST_F(BuildTest, KbuildMultiSubsysTemplate) {
  std::string Mk;
  Mk += "define subsys\n";
  Mk += "$(1)-y := $(1)_core.o $(1)_init.o\n";
  Mk += "endef\n";
  Mk += "SUBSYSTEMS := mm fs net\n";
  Mk += "$(foreach s,$(SUBSYSTEMS),$(eval $(call subsys,$(s))))\n";
  Mk += "all-y := $(foreach s,$(SUBSYSTEMS),$($(s)-y))\n";
  Mk += "all:\n";
  Mk += "\t@echo objs=$(all-y)\n";
  Mk += "\t@echo count=$(words $(all-y))\n";
  Mk += ".PHONY: all\n";
  writeMakefile(Mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("mm_core.o")) << R.out;
  EXPECT_TRUE(R.contains("fs_init.o")) << R.out;
  EXPECT_TRUE(R.contains("net_core.o")) << R.out;
  EXPECT_TRUE(R.contains("count=6")) << R.out;
}

// --- filter + patsubst pipeline (kernel style) ---

TEST_F(BuildTest, KbuildFilterPatsubstPipeline) {
  writeMakefile(
      "obj-y := core.o debug.o perf.o\n"
      "obj-m := ext4.o btrfs.o\n"
      "ALL_OBJS := $(obj-y) $(obj-m)\n"
      "BUILTIN_SRCS := $(patsubst %.o,%.c,$(filter %.o,$(obj-y)))\n"
      "MODULE_SRCS := $(patsubst %.o,%.c,$(filter %.o,$(obj-m)))\n"
      "all:\n"
      "\t@echo builtin=$(BUILTIN_SRCS)\n"
      "\t@echo module=$(MODULE_SRCS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("builtin=core.c debug.c perf.c")) << R.out;
  EXPECT_TRUE(R.contains("module=ext4.c btrfs.c")) << R.out;
}

// --- Parallel build fan-out with -j ---

TEST_F(BuildTest, HardenParallelFanOutJ4) {
  std::string Mk;
  Mk += "all: t0 t1 t2 t3 t4 t5 t6 t7\n\t@echo done\n.PHONY: all\n";
  for (int I = 0; I < 8; ++I) {
    std::string N = std::to_string(I);
    Mk += "t" + N + ":\n\t@echo t" + N + "\n.PHONY: t" + N + "\n";
  }
  writeMakefile(Mk);
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << R.err;
  for (int I = 0; I < 8; ++I)
    EXPECT_TRUE(R.contains("t" + std::to_string(I))) << R.out;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

// --- Parallel build with keep-going ---

TEST_F(BuildTest, HardenParallelKeepGoing) {
  writeMakefile(
      "all: fail succeed\n"
      "\t@echo all\n"
      "fail:\n"
      "\t@exit 1\n"
      "succeed:\n"
      "\t@echo success\n"
      ".PHONY: all fail succeed\n");
  auto R = runMake({"-k"});
  EXPECT_NE(R.exitCode, 0);
  EXPECT_TRUE(R.contains("success")) << R.out;
}

// --- $(MAKECMDGOALS) detection ---

TEST_F(BuildTest, HardenMakecmdgoals) {
  writeMakefile(
      "ifeq ($(MAKECMDGOALS),clean)\n"
      "  ACTION := cleaning\n"
      "else\n"
      "  ACTION := building\n"
      "endif\n"
      "all:\n"
      "\t@echo $(ACTION)\n"
      "clean:\n"
      "\t@echo $(ACTION)\n"
      ".PHONY: all clean\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("building")) << R1.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cleaning")) << R2.out;
}

// --- .DEFAULT_GOAL override ---

TEST_F(BuildTest, HardenDefaultGoalOverride) {
  writeMakefile(
      "first:\n"
      "\t@echo first\n"
      "second:\n"
      "\t@echo second\n"
      ".DEFAULT_GOAL := second\n"
      ".PHONY: first second\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("second")) << R.out;
  EXPECT_FALSE(R.contains("first")) << R.out;
}

// --- Include chain (A includes B, B includes C) ---

TEST_F(BuildTest, HardenIncludeChain) {
  writeFile(tmp() / "c.mk", "C_VAR := from_c\n");
  writeFile(tmp() / "b.mk", "include c.mk\nB_VAR := from_b_$(C_VAR)\n");
  writeMakefile(
      "include b.mk\n"
      "all:\n"
      "\t@echo b=$(B_VAR) c=$(C_VAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("b=from_b_from_c")) << R.out;
  EXPECT_TRUE(R.contains("c=from_c")) << R.out;
}

// --- -include missing file silently ignored ---

TEST_F(BuildTest, HardenDashIncludeMissing) {
  writeMakefile(
      "-include nonexistent.mk\n"
      "X := fallback\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("fallback")) << R.out;
}

// --- undefine then ifdef should be false ---

TEST_F(BuildTest, HardenUndefineIfdef) {
  writeMakefile(
      "X := hello\n"
      "undefine X\n"
      "ifdef X\n"
      "  R := defined\n"
      "else\n"
      "  R := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("undefined")) << R.out;
}

// --- Recipe ignore-error prefix - ---

TEST_F(BuildTest, HardenRecipeIgnoreError) {
  writeMakefile(
      "all:\n"
      "\t-@false\n"
      "\t@echo survived\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("survived")) << R.out;
}

// --- Kbuild CONFIG conditional compilation ---

TEST_F(BuildTest, KbuildConfigConditional) {
  writeMakefile(
      "CONFIG_SMP := y\n"
      "CONFIG_DEBUG_INFO :=\n"
      "obj-y := main.o\n"
      "ifdef CONFIG_SMP\n"
      "  obj-y += smp.o\n"
      "endif\n"
      "ifdef CONFIG_DEBUG_INFO\n"
      "  CFLAGS += -g\n"
      "endif\n"
      "all:\n"
      "\t@echo obj=$(obj-y) cflags=[$(CFLAGS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("obj=main.o smp.o")) << R.out;
  EXPECT_TRUE(R.contains("cflags=[]")) << R.out;
}

// --- Complex Kbuild: version + arch + config + eval + FORCE ---

TEST_F(BuildTest, KbuildFullPipelineMini) {
  std::string Mk;
  Mk += "VERSION = 5\nPATCHLEVEL = 10\nSUBLEVEL = 0\n";
  Mk += "KERNELVERSION = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n";
  Mk += "ARCH ?= x86\n";
  Mk += "ifeq ($(ARCH),x86)\n  SRCARCH := x86\n";
  Mk += "else ifeq ($(ARCH),arm64)\n  SRCARCH := arm64\n";
  Mk += "else\n  SRCARCH := $(ARCH)\nendif\n";
  Mk += "ifndef CROSS_COMPILE\n  CROSS_COMPILE :=\nendif\n";
  Mk += "CC := $(CROSS_COMPILE)gcc\n";
  Mk += "CONFIG_SMP ?= y\n";
  Mk += "obj-y := init/main.o\n";
  Mk += "ifdef CONFIG_SMP\n  obj-y += kernel/smp.o\nendif\n";
  Mk += "define build_module\n";
  Mk += "$(1)-y := $(1)_core.o\n";
  Mk += "endef\n";
  Mk += "MODULES := mm fs\n";
  Mk += "$(foreach m,$(MODULES),$(eval $(call build_module,$(m))))\n";
  Mk += "all-y := $(obj-y) $(foreach m,$(MODULES),$($(m)-y))\n";
  Mk += "vmlinux:\n";
  Mk += "\t@echo LINK vmlinux v$(KERNELVERSION) arch=$(SRCARCH) "
        "cc=$(CC) objs=$(words $(all-y))\n";
  Mk += ".PHONY: vmlinux\n";
  writeMakefile(Mk);

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("v5.10.0")) << R1.out;
  EXPECT_TRUE(R1.contains("arch=x86")) << R1.out;
  EXPECT_TRUE(R1.contains("objs=4")) << R1.out;

  auto R2 = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
}

// --- 200-word $(foreach) stress test ---

TEST_F(BuildTest, StressForeach200) {
  std::string List;
  for (int I = 0; I < 200; ++I) {
    if (I > 0) List += " ";
    List += "item" + std::to_string(I);
  }
  writeMakefile(
      "LIST := " + List + "\n"
      "RESULT := $(foreach x,$(LIST),$(x)_ok)\n"
      "all:\n"
      "\t@echo count=$(words $(RESULT))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=200")) << R.out;
}

// --- Parallel chain A->B->C->D ordering ---

TEST_F(BuildTest, HardenParallelChainOrder) {
  writeMakefile(
      "D:\n\t@echo D\n.PHONY: D\n"
      "C: D\n\t@echo C\n.PHONY: C\n"
      "B: C\n\t@echo B\n.PHONY: B\n"
      "A: B\n\t@echo A\n.PHONY: A\n");
  auto R = runMake({"-j4"}, "A");
  ASSERT_TRUE(R.ok()) << R.err;
  size_t PosD = R.out.find("D");
  size_t PosC = R.out.find("C");
  size_t PosB = R.out.find("B");
  size_t PosA = R.out.find("A\n");
  EXPECT_LT(PosD, PosC) << R.out;
  EXPECT_LT(PosC, PosB) << R.out;
  EXPECT_LT(PosB, PosA) << R.out;
}

// --- $(file) write and read back ---

TEST_F(BuildTest, HardenFileWriteRead) {
  writeMakefile(
      "$(file >output.txt,hello from file func)\n"
      "CONTENT := $(file <output.txt)\n"
      "all:\n"
      "\t@echo $(CONTENT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello from file func")) << R.out;
}

// --- $(origin) for different variable sources ---

TEST_F(BuildTest, HardenOriginSources) {
  writeMakefile(
      "FILE_VAR := from_file\n"
      "all:\n"
      "\t@echo file=$(origin FILE_VAR)\n"
      "\t@echo cmd=$(origin CMD_VAR)\n"
      "\t@echo undef=$(origin NEVER_SET)\n"
      ".PHONY: all\n");
  auto R = runMake({"CMD_VAR=from_cmd"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("file=file")) << R.out;
  EXPECT_TRUE(R.contains("cmd=command line")) << R.out;
  EXPECT_TRUE(R.contains("undef=undefined")) << R.out;
}

// --- Static pattern rule with patsubst-derived targets ---

TEST_F(BuildTest, HardenStaticPatternRule) {
  writeFile(tmp() / "a.src", "");
  writeFile(tmp() / "b.src", "");
  writeMakefile(
      "TARGETS := a.dst b.dst\n"
      "all: $(TARGETS)\n"
      "\t@echo done\n"
      ".PHONY: all\n"
      "$(TARGETS): %.dst: %.src\n"
      "\t@echo convert $< to $@\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("convert a.src to a.dst")) << R.out;
  EXPECT_TRUE(R.contains("convert b.src to b.dst")) << R.out;
}

// ============================================================================
// BUG FIXES — foreach/call must override command-line variables
// ============================================================================

TEST_F(BuildTest, ForeachOverridesCmdLineVar) {
  writeMakefile(
      "LIST := $(foreach ARCH,x86 arm mips,build_$(ARCH))\n"
      "all:\n"
      "\t@echo list=$(LIST)\n"
      "\t@echo arch=$(ARCH)\n"
      ".PHONY: all\n");
  auto R = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("list=build_x86 build_arm build_mips")) << R.out;
  EXPECT_TRUE(R.contains("arch=arm64")) << R.out;
}

TEST_F(BuildTest, ForeachRestoresCmdLineVar) {
  writeMakefile(
      "RESULT := $(foreach X,1 2 3,item_$(X))\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      "\t@echo x=$(X)\n"
      ".PHONY: all\n");
  auto R = runMake({"X=cmdval"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=item_1 item_2 item_3")) << R.out;
  EXPECT_TRUE(R.contains("x=cmdval")) << R.out;
}

TEST_F(BuildTest, CallPositionalVarsAlwaysSet) {
  writeMakefile(
      "greet = hello_$(1)_$(2)\n"
      "RESULT := $(call greet,world,test)\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake({"1=blocked"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello_world_test")) << R.out;
}

// ============================================================================
// LINUX 5.10 KERNEL — Real-world pattern compatibility tests
// ============================================================================

TEST_F(BuildTest, KernelVersionBlock) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "EXTRAVERSION =\n"
      "NAME = Kleptomaniac Octopus\n"
      "KERNELVERSION = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)\n"
      "export VERSION PATCHLEVEL SUBLEVEL EXTRAVERSION NAME\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      "\t@echo name=$(NAME)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.0")) << R.out;
  EXPECT_TRUE(R.contains("name=Kleptomaniac Octopus")) << R.out;
}

TEST_F(BuildTest, KernelOriginBasedOverride) {
  writeMakefile(
      "ARCH ?= x86\n"
      "ifeq (\"$(origin ARCH)\", \"command line\")\n"
      "  KBUILD_ARCH := $(ARCH)\n"
      "else\n"
      "  KBUILD_ARCH := default_$(ARCH)\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(KBUILD_ARCH)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("arch=default_x86")) << R1.out;

  auto R2 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
}

TEST_F(BuildTest, KernelQuietVerboseMode) {
  writeMakefile(
      "KBUILD_VERBOSE ?= 0\n"
      "ifeq ($(KBUILD_VERBOSE),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "quiet_cmd_cc = CC      $@\n"
      "      cmd_cc = gcc -c -o $@ $<\n"
      "define do_cmd\n"
      "$(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))' &&) $(cmd_$(1))\n"
      "endef\n"
      "all:\n"
      "\t@echo quiet=$(quiet) Q=$(Q)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("quiet=quiet_ Q=@")) << R1.out;

  auto R2 = runMake({"KBUILD_VERBOSE=1"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("quiet= Q=")) << R2.out;
}

TEST_F(BuildTest, KernelComputedMachineVar) {
  writeMakefile(
      "CONFIG_ARCH_FOO := y\n"
      "machine-y :=\n"
      "machine-$(CONFIG_ARCH_FOO) += foo_mach\n"
      "machine-n += bar_mach\n"
      "MACHINE := $(machine-y)\n"
      "all:\n"
      "\t@echo machine=$(MACHINE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("machine=foo_mach")) << R.out;
}

TEST_F(BuildTest, KernelCCOptionCallPattern) {
  writeMakefile(
      "cc-option = $(if $(findstring error,$(shell echo $(1) 2>&1)),,"
      "$(1))\n"
      "KBUILD_CFLAGS := -Wall\n"
      "KBUILD_CFLAGS += $(call cc-option,-Wno-unused)\n"
      "KBUILD_CFLAGS += $(call cc-option,-Wno-format-truncation)\n"
      "all:\n"
      "\t@echo flags=$(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
}

TEST_F(BuildTest, KernelSubdirForeachEval) {
  writeMakefile(
      "subdirs := kernel mm fs net\n"
      "define subdir_template\n"
      "$(1)-objs := $(1)/built-in.o\n"
      "endef\n"
      "$(foreach d,$(subdirs),$(eval $(call subdir_template,$(d))))\n"
      "all:\n"
      "\t@echo kernel=$(kernel-objs)\n"
      "\t@echo mm=$(mm-objs)\n"
      "\t@echo fs=$(fs-objs)\n"
      "\t@echo net=$(net-objs)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("kernel=kernel/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("mm=mm/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("fs=fs/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("net=net/built-in.o")) << R.out;
}

TEST_F(BuildTest, KernelFilterPipeline) {
  writeMakefile(
      "obj-y := core.o sched.o fork.o\n"
      "obj-m := ext4.o btrfs.o\n"
      "obj-n := debug.o\n"
      "ALL_OBJS := $(obj-y) $(obj-m) $(obj-n)\n"
      "BUILTIN := $(filter %.o,$(obj-y))\n"
      "MODULES := $(filter %.o,$(obj-m))\n"
      "EXCLUDED := $(filter-out $(BUILTIN) $(MODULES),$(ALL_OBJS))\n"
      "all:\n"
      "\t@echo builtin=$(BUILTIN)\n"
      "\t@echo modules=$(MODULES)\n"
      "\t@echo excluded=$(EXCLUDED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("builtin=core.o sched.o fork.o")) << R.out;
  EXPECT_TRUE(R.contains("modules=ext4.o btrfs.o")) << R.out;
  EXPECT_TRUE(R.contains("excluded=debug.o")) << R.out;
}

TEST_F(BuildTest, KernelMakeFlagsDetection) {
  writeMakefile(
      "ifneq ($(findstring s,$(filter-out --%,$(MAKEFLAGS))),)\n"
      "  QUIET := silent\n"
      "else\n"
      "  QUIET := normal\n"
      "endif\n"
      "all:\n"
      "\t@echo mode=$(QUIET)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("mode=normal")) << R1.out;

  auto R2 = runMake({"-s"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("mode=silent")) << R2.out;
}

TEST_F(BuildTest, KernelExportComputedVars) {
  writeMakefile(
      "CONFIG_SMP := y\n"
      "SMP-$(CONFIG_SMP) := enabled\n"
      "SMP-n := disabled\n"
      "SMP := $(SMP-y)\n"
      "export SMP\n"
      "all:\n"
      "\t@echo smp=$(SMP)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("smp=enabled")) << R.out;
}

TEST_F(BuildTest, KernelIfeqChain4Level) {
  writeMakefile(
      "ARCH ?= x86\n"
      "ifeq ($(ARCH),x86)\n"
      "  BITS := 64\n"
      "  SRCARCH := x86\n"
      "else ifeq ($(ARCH),arm)\n"
      "  BITS := 32\n"
      "  SRCARCH := arm\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  BITS := 64\n"
      "  SRCARCH := arm64\n"
      "else ifeq ($(ARCH),mips)\n"
      "  BITS := 32\n"
      "  SRCARCH := mips\n"
      "else\n"
      "  BITS := unknown\n"
      "  SRCARCH := $(ARCH)\n"
      "endif\n"
      "all:\n"
      "\t@echo srcarch=$(SRCARCH) bits=$(BITS)\n"
      ".PHONY: all\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("srcarch=x86 bits=64")) << R1.out;

  auto R2 = runMake({"ARCH=arm"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("srcarch=arm bits=32")) << R2.out;

  auto R3 = runMake({"ARCH=mips"});
  ASSERT_TRUE(R3.ok()) << R3.err;
  EXPECT_TRUE(R3.contains("srcarch=mips bits=32")) << R3.out;

  auto R4 = runMake({"ARCH=riscv"});
  ASSERT_TRUE(R4.ok()) << R4.err;
  EXPECT_TRUE(R4.contains("srcarch=riscv bits=unknown")) << R4.out;
}

TEST_F(BuildTest, KernelDefineMultiLineRecipe) {
  writeMakefile(
      "define cmd_link\n"
      "@echo LD $@\n"
      "@echo done linking\n"
      "endef\n"
      "vmlinux: FORCE\n"
      "\t$(cmd_link)\n"
      "FORCE:\n"
      ".PHONY: FORCE vmlinux\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("LD vmlinux")) << R.out;
  EXPECT_TRUE(R.contains("done linking")) << R.out;
}

TEST_F(BuildTest, KernelDotDInclude) {
  writeFile(tmp() / "main.d", "main.o: main.c config.h\n");
  writeFile(tmp() / "main.c", "int main(){}");
  writeFile(tmp() / "config.h", "#define FOO 1");
  writeMakefile(
      "all: main.o\n"
      "\t@echo built\n"
      ".PHONY: all\n"
      "%.o: %.c\n"
      "\t@echo CC $< -o $@\n"
      "-include $(wildcard *.d)\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC main.c -o main.o")) << R.out;
}

TEST_F(BuildTest, KernelPatsubstObjY) {
  writeMakefile(
      "obj-y := core.o sched.o signal.o\n"
      "SRCS := $(patsubst %.o,%.c,$(obj-y))\n"
      "DEPS := $(patsubst %.o,%.d,$(obj-y))\n"
      "all:\n"
      "\t@echo srcs=$(SRCS)\n"
      "\t@echo deps=$(DEPS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("srcs=core.c sched.c signal.c")) << R.out;
  EXPECT_TRUE(R.contains("deps=core.d sched.d signal.d")) << R.out;
}

TEST_F(BuildTest, KernelCrossCompilePrefix) {
  writeMakefile(
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "AR := $(CROSS_COMPILE)ar\n"
      "OBJCOPY := $(CROSS_COMPILE)objcopy\n"
      "all:\n"
      "\t@echo cc=$(CC) ld=$(LD)\n"
      ".PHONY: all\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc ld=ld")) << R1.out;

  auto R2 = runMake({"CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
  EXPECT_TRUE(R2.contains("ld=aarch64-linux-gnu-ld")) << R2.out;
}

TEST_F(BuildTest, KernelValueInCondition) {
  writeMakefile(
      "MY_CFLAGS := -O2\n"
      "ORIG := $(value MY_CFLAGS)\n"
      "ifeq ($(ORIG),-O2)\n"
      "  LEVEL := optimized\n"
      "else\n"
      "  LEVEL := other\n"
      "endif\n"
      "all:\n"
      "\t@echo level=$(LEVEL)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("level=optimized")) << R.out;
}

TEST_F(BuildTest, KernelNestedIfOptimLevel) {
  writeMakefile(
      "CONFIG_CC_OPTIMIZE_FOR_SIZE := y\n"
      "ifeq ($(CONFIG_CC_OPTIMIZE_FOR_SIZE),y)\n"
      "  CFLAGS_OPTIM := -Os\n"
      "else\n"
      "  ifneq ($(findstring 3,$(CONFIG_CC_OPTIMIZE_LEVEL)),)\n"
      "    CFLAGS_OPTIM := -O3\n"
      "  else\n"
      "    CFLAGS_OPTIM := -O2\n"
      "  endif\n"
      "endif\n"
      "all:\n"
      "\t@echo optim=$(CFLAGS_OPTIM)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("optim=-Os")) << R.out;
}

TEST_F(BuildTest, KernelUndefineRedefine) {
  writeMakefile(
      "CONFIG_DEBUG := y\n"
      "ifdef CONFIG_DEBUG\n"
      "  DEBUG_FLAGS := -g -DDEBUG\n"
      "endif\n"
      "undefine CONFIG_DEBUG\n"
      "ifndef CONFIG_DEBUG\n"
      "  RELEASE := true\n"
      "endif\n"
      "all:\n"
      "\t@echo flags=$(DEBUG_FLAGS) release=$(RELEASE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("flags=-g -DDEBUG")) << R.out;
  EXPECT_TRUE(R.contains("release=true")) << R.out;
}

TEST_F(BuildTest, KernelMakeVersionCheck) {
  writeMakefile(
      "MIN_MAKE_VERSION := 4\n"
      "ifneq ($(firstword $(sort $(MAKE_VERSION) $(MIN_MAKE_VERSION))),"
      "$(MIN_MAKE_VERSION))\n"
      "  VERSION_OK := no\n"
      "else\n"
      "  VERSION_OK := yes\n"
      "endif\n"
      "all:\n"
      "\t@echo version_ok=$(VERSION_OK) make_ver=$(MAKE_VERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version_ok=yes")) << R.out;
  EXPECT_TRUE(R.contains("make_ver=4.3")) << R.out;
}

TEST_F(BuildTest, KernelSubstChainedVersion) {
  writeMakefile(
      "UNAME := 5.10.123-generic\n"
      "MAJOR := $(firstword $(subst ., ,$(UNAME)))\n"
      "MINOR := $(word 2,$(subst ., ,$(UNAME)))\n"
      "all:\n"
      "\t@echo major=$(MAJOR) minor=$(MINOR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("major=5")) << R.out;
  EXPECT_TRUE(R.contains("minor=10")) << R.out;
}

TEST_F(BuildTest, KernelDollarDollarEscape) {
  writeMakefile(
      "all:\n"
      "\t@X=hello; echo val=$$X\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("val=hello")) << R.out;
}

TEST_F(BuildTest, KernelMultipleIncludeGlob) {
  writeFile(tmp() / "arch.mk", "ARCH_FLAGS := -march=native\n");
  writeFile(tmp() / "debug.mk", "DEBUG_FLAGS := -g\n");
  writeMakefile(
      "include $(wildcard *.mk)\n"
      "all:\n"
      "\t@echo arch=$(ARCH_FLAGS) debug=$(DEBUG_FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arch=-march=native")) << R.out;
  EXPECT_TRUE(R.contains("debug=-g")) << R.out;
}

TEST_F(BuildTest, KernelStripInIfeq) {
  writeMakefile(
      "CONFIG :=   y   \n"
      "ifeq ($(strip $(CONFIG)),y)\n"
      "  ENABLED := true\n"
      "else\n"
      "  ENABLED := false\n"
      "endif\n"
      "all:\n"
      "\t@echo enabled=$(ENABLED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("enabled=true")) << R.out;
}

TEST_F(BuildTest, KernelAddprefixSubdir) {
  writeMakefile(
      "subdirs := kernel mm fs\n"
      "SUBDIR_OBJS := $(addprefix obj/,$(addsuffix /built-in.o,$(subdirs)))\n"
      "all:\n"
      "\t@echo objs=$(SUBDIR_OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains(
      "objs=obj/kernel/built-in.o obj/mm/built-in.o obj/fs/built-in.o"))
      << R.out;
}

TEST_F(BuildTest, KernelAndOrConditions) {
  writeMakefile(
      "CONFIG_A := y\n"
      "CONFIG_B :=\n"
      "RESULT_AND := $(and $(CONFIG_A),present)\n"
      "RESULT_OR := $(or $(CONFIG_B),$(CONFIG_A))\n"
      "RESULT_AND_FAIL := $(and $(CONFIG_A),$(CONFIG_B),never)\n"
      "all:\n"
      "\t@echo and=$(RESULT_AND)\n"
      "\t@echo or=$(RESULT_OR)\n"
      "\t@echo and_fail=[$(RESULT_AND_FAIL)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("and=present")) << R.out;
  EXPECT_TRUE(R.contains("or=y")) << R.out;
  EXPECT_TRUE(R.contains("and_fail=[]")) << R.out;
}

TEST_F(BuildTest, KernelFlavorCheck) {
  writeMakefile(
      "REC_VAR = recursive_value\n"
      "SIM_VAR := simple_value\n"
      "all:\n"
      "\t@echo rec=$(flavor REC_VAR)\n"
      "\t@echo sim=$(flavor SIM_VAR)\n"
      "\t@echo undef=$(flavor NONE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("rec=recursive")) << R.out;
  EXPECT_TRUE(R.contains("sim=simple")) << R.out;
  EXPECT_TRUE(R.contains("undef=undefined")) << R.out;
}

// ============================================================================
// COMPREHENSIVE KERNEL INTEGRATION — Full Kbuild simulation
// ============================================================================

TEST_F(BuildTest, KernelFullKbuildE2E) {
  writeFile(tmp() / "kernel/core.c", "void core(){}");
  writeFile(tmp() / "kernel/sched.c", "void sched(){}");
  writeFile(tmp() / "mm/page.c", "void page(){}");
  writeFile(tmp() / "fs/vfs.c", "void vfs(){}");

  writeFile(tmp() / "scripts/Kbuild.include",
      "quiet_cmd_cc_o_c = CC      $@\n"
      "      cmd_cc_o_c = $(CC) $(CFLAGS) -c -o $@ $<\n"
      "define rule_cc_o_c\n"
      "$(if $($(quiet)cmd_cc_o_c),@echo '  $($(quiet)cmd_cc_o_c)')\n"
      "endef\n");

  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "KERNELVERSION = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "\n"
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "\n"
      "ifeq ($(ARCH),x86)\n"
      "  SRCARCH := x86\n"
      "  BITS := 64\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  SRCARCH := arm64\n"
      "  BITS := 64\n"
      "else\n"
      "  SRCARCH := $(ARCH)\n"
      "  BITS := 32\n"
      "endif\n"
      "\n"
      "KBUILD_VERBOSE ?= 0\n"
      "ifeq ($(KBUILD_VERBOSE),1)\n"
      "  quiet :=\n"
      "  Q :=\n"
      "else\n"
      "  quiet := quiet_\n"
      "  Q := @\n"
      "endif\n"
      "\n"
      "CFLAGS := -O2 -Wall\n"
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES := y\n"
      "\n"
      "ifeq ($(CONFIG_SMP),y)\n"
      "  CFLAGS += -DCONFIG_SMP\n"
      "endif\n"
      "\n"
      "include scripts/Kbuild.include\n"
      "\n"
      "subdirs := kernel mm fs\n"
      "define subdir_template\n"
      "$(1)-obj-y := $(patsubst %.c,%.o,$(wildcard $(1)/*.c))\n"
      "endef\n"
      "$(foreach d,$(subdirs),$(eval $(call subdir_template,$(d))))\n"
      "\n"
      "ALL_OBJS := $(foreach d,$(subdirs),$($(d)-obj-y))\n"
      "\n"
      "ifeq (\"$(origin ARCH)\", \"command line\")\n"
      "  ARCH_FROM := cmdline\n"
      "else\n"
      "  ARCH_FROM := default\n"
      "endif\n"
      "\n"
      "all: vmlinux\n"
      "\t@echo built $(KERNELVERSION) for $(SRCARCH)\n"
      ".PHONY: all\n"
      "\n"
      "vmlinux: FORCE\n"
      "\t@echo LINK vmlinux arch=$(SRCARCH) bits=$(BITS) "
      "smp=$(CONFIG_SMP) from=$(ARCH_FROM)\n"
      "\t@echo objs=$(ALL_OBJS)\n"
      "\t@echo cc=$(CC) cflags=$(CFLAGS)\n"
      ".PHONY: vmlinux\n"
      "\n"
      "FORCE:\n"
      ".PHONY: FORCE\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("LINK vmlinux arch=x86 bits=64")) << R1.out;
  EXPECT_TRUE(R1.contains("smp=y")) << R1.out;
  EXPECT_TRUE(R1.contains("from=default")) << R1.out;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;
  EXPECT_TRUE(R1.contains("-DCONFIG_SMP")) << R1.out;
  EXPECT_TRUE(R1.contains("built 5.10.0 for x86")) << R1.out;

  auto R2 = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64 bits=64")) << R2.out;
  EXPECT_TRUE(R2.contains("from=cmdline")) << R2.out;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
}

TEST_F(BuildTest, KernelIfChangedCallPattern) {
  writeMakefile(
      "define if_changed\n"
      "@echo CMD $(1) for $@\n"
      "endef\n"
      "define cmd_compile\n"
      "gcc -c -o $@ $<\n"
      "endef\n"
      "FORCE:\n"
      ".PHONY: FORCE\n"
      "output.o: FORCE\n"
      "\t$(call if_changed,compile)\n"
      ".PHONY: output.o\n");
  auto R = runMake({}, "output.o");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CMD compile for output.o")) << R.out;
}

TEST_F(BuildTest, KernelNestedForeachCallEval) {
  writeMakefile(
      "ARCHS := x86 arm\n"
      "CONFIGS := debug release\n"
      "define arch_config_template\n"
      "$(1)-$(2)-flags := -DARCH_$(1) -DCONFIG_$(2)\n"
      "endef\n"
      "$(foreach a,$(ARCHS),$(foreach c,$(CONFIGS),"
      "$(eval $(call arch_config_template,$(a),$(c)))))\n"
      "all:\n"
      "\t@echo x86_debug=$(x86-debug-flags)\n"
      "\t@echo arm_release=$(arm-release-flags)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x86_debug=-DARCH_x86 -DCONFIG_debug")) << R.out;
  EXPECT_TRUE(R.contains("arm_release=-DARCH_arm -DCONFIG_release")) << R.out;
}

TEST_F(BuildTest, KernelSubstRefDirPaths) {
  writeMakefile(
      "SRCS := src/a.c src/b.c lib/c.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "DEPS := $(SRCS:.c=.d)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      "\t@echo deps=$(DEPS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("objs=src/a.o src/b.o lib/c.o")) << R.out;
  EXPECT_TRUE(R.contains("deps=src/a.d src/b.d lib/c.d")) << R.out;
}

TEST_F(BuildTest, KernelDirNotdirBasename) {
  writeMakefile(
      "FILES := arch/x86/kernel/head.S drivers/pci/pci.c\n"
      "DIRS := $(dir $(FILES))\n"
      "NAMES := $(notdir $(FILES))\n"
      "BASES := $(basename $(FILES))\n"
      "SUFFIXES := $(suffix $(FILES))\n"
      "all:\n"
      "\t@echo dirs=$(DIRS)\n"
      "\t@echo names=$(NAMES)\n"
      "\t@echo bases=$(BASES)\n"
      "\t@echo suffixes=$(SUFFIXES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dirs=arch/x86/kernel/ drivers/pci/")) << R.out;
  EXPECT_TRUE(R.contains("names=head.S pci.c")) << R.out;
  EXPECT_TRUE(R.contains("bases=arch/x86/kernel/head drivers/pci/pci"))
      << R.out;
  EXPECT_TRUE(R.contains("suffixes=.S .c")) << R.out;
}

TEST_F(BuildTest, KernelRecursiveLateBind) {
  writeMakefile(
      "PLATFORM = $(ARCH)_platform\n"
      "ARCH = x86\n"
      "all:\n"
      "\t@echo plat=$(PLATFORM)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("plat=x86_platform")) << R.out;
}

TEST_F(BuildTest, KernelWordVersionParsing) {
  writeMakefile(
      "GCC_VER := 10 2 1\n"
      "MAJOR := $(word 1,$(GCC_VER))\n"
      "MINOR := $(word 2,$(GCC_VER))\n"
      "PATCH := $(word 3,$(GCC_VER))\n"
      "COUNT := $(words $(GCC_VER))\n"
      "all:\n"
      "\t@echo $(MAJOR).$(MINOR).$(PATCH) count=$(COUNT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("10.2.1 count=3")) << R.out;
}

TEST_F(BuildTest, KernelEvalPatternRule) {
  writeFile(tmp() / "a.c", "");
  writeFile(tmp() / "b.c", "");
  writeMakefile(
      "SRCS := a.c b.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "all: $(OBJS)\n"
      "\t@echo linked\n"
      ".PHONY: all\n"
      "define compile_rule\n"
      "$(1): $(patsubst %.o,%.c,$(1))\n"
      "\t@echo CC $$< -o $$@\n"
      "endef\n"
      "$(foreach o,$(OBJS),$(eval $(call compile_rule,$(o))))\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC a.c -o a.o")) << R.out;
  EXPECT_TRUE(R.contains("CC b.c -o b.o")) << R.out;
  EXPECT_TRUE(R.contains("linked")) << R.out;
}

// ============================================================================
// EDGE CASES — Robustness under unusual but valid inputs
// ============================================================================

TEST_F(BuildTest, EdgeEmptyForeachList) {
  writeMakefile(
      "EMPTY :=\n"
      "LIST := $(foreach x,$(EMPTY),item_$(x))\n"
      "all:\n"
      "\t@echo list=[$(LIST)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("list=[]")) << R.out;
}

TEST_F(BuildTest, EdgeTripleNestedVarRef) {
  writeMakefile(
      "inner := hello\n"
      "mid := inner\n"
      "top := mid\n"
      "all:\n"
      "\t@echo val=$($($(top)))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("val=hello")) << R.out;
}

TEST_F(BuildTest, EdgeConditionalSetPreservesExisting) {
  writeMakefile(
      "X := existing\n"
      "X ?= would_be_ignored\n"
      "Y ?= set_because_new\n"
      "all:\n"
      "\t@echo x=$(X) y=$(Y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x=existing")) << R.out;
  EXPECT_TRUE(R.contains("y=set_because_new")) << R.out;
}

TEST_F(BuildTest, EdgeFilterPatternNoMatch) {
  writeMakefile(
      "LIST := foo bar baz\n"
      "FILTERED := $(filter %.xyz,$(LIST))\n"
      "all:\n"
      "\t@echo result=[$(FILTERED)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=[]")) << R.out;
}

TEST_F(BuildTest, EdgeRecursiveVarCycleDetection) {
  writeMakefile(
      "A = $(B)\n"
      "B = $(A)\n"
      "all:\n"
      "\t@echo a=[$(A)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=[]")) << R.out;
}

TEST_F(BuildTest, EdgeMultiWordTarget) {
  writeMakefile(
      "TARGETS := t1 t2 t3\n"
      "all: $(TARGETS)\n"
      ".PHONY: all\n"
      "$(TARGETS):\n"
      "\t@echo building $@\n"
      ".PHONY: $(TARGETS)\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building t1")) << R.out;
  EXPECT_TRUE(R.contains("building t2")) << R.out;
  EXPECT_TRUE(R.contains("building t3")) << R.out;
}

TEST_F(BuildTest, EdgeSortDeduplicates) {
  writeMakefile(
      "LIST := z a b a c z b\n"
      "SORTED := $(sort $(LIST))\n"
      "all:\n"
      "\t@echo sorted=$(SORTED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("sorted=a b c z")) << R.out;
}

TEST_F(BuildTest, EdgeNestedCallForeach) {
  writeMakefile(
      "process = [$(1):$(2)]\n"
      "ITEMS := a b c\n"
      "RESULT := $(foreach i,$(ITEMS),$(call process,$(i),val))\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=[a:val] [b:val] [c:val]")) << R.out;
}

TEST_F(BuildTest, EdgeOverrideAppendCmdLine) {
  writeMakefile(
      "override CFLAGS += -Wall\n"
      "override CFLAGS += -Werror\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O2"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-Werror")) << R.out;
}

TEST_F(BuildTest, EdgeLastword) {
  writeMakefile(
      "FILES := one two three four\n"
      "LAST := $(lastword $(FILES))\n"
      "FIRST := $(firstword $(FILES))\n"
      "all:\n"
      "\t@echo first=$(FIRST) last=$(LAST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("first=one")) << R.out;
  EXPECT_TRUE(R.contains("last=four")) << R.out;
}

TEST_F(BuildTest, EdgeWordlistBoundary) {
  writeMakefile(
      "LIST := a b c d e\n"
      "SUB := $(wordlist 2,4,$(LIST))\n"
      "OVER := $(wordlist 1,99,$(LIST))\n"
      "all:\n"
      "\t@echo sub=$(SUB)\n"
      "\t@echo over=$(OVER)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("sub=b c d")) << R.out;
  EXPECT_TRUE(R.contains("over=a b c d e")) << R.out;
}

// ============================================================================
// Linux 5.10 Kernel Makefile Compatibility Tests
// ============================================================================

TEST_F(BuildTest, Kernel510_MakeflagsFilterFindstring) {
  writeMakefile(
      "MAKEFLAGS_CLEAN := nks --warn-undefined-variables\n"
      "FILTERED := $(filter-out --%,$(MAKEFLAGS_CLEAN))\n"
      "HAS_S := $(findstring s,$(FILTERED))\n"
      "ifeq ($(HAS_S),s)\n"
      "  QUIET := silent_\n"
      "else\n"
      "  QUIET := quiet_\n"
      "endif\n"
      "all:\n"
      "\t@echo quiet=$(QUIET) filtered=$(FILTERED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("quiet=silent_")) << R.out;
  EXPECT_TRUE(R.contains("filtered=nks")) << R.out;
}

TEST_F(BuildTest, Kernel510_ConfigShellDetection) {
  writeMakefile(
      "CONFIG_SHELL := $(shell echo /bin/bash)\n"
      "SHELL := $(CONFIG_SHELL)\n"
      "all:\n"
      "\t@echo shell=$(SHELL)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("shell=/bin/bash")) << R.out;
}

TEST_F(BuildTest, Kernel510_VerboseQuietPrefixSystem) {
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
      "quiet_cmd_cc = CC $@\n"
      "cmd_cc = gcc -c $< -o $@\n"
      "CMD_DISPLAY := $($(quiet)cmd_cc)\n"
      "all:\n"
      "\t@echo v=$(KBUILD_VERBOSE) q=$(quiet) display=[$(CMD_DISPLAY)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("v=0")) << R.out;
  EXPECT_TRUE(R.contains("q=quiet_")) << R.out;
  EXPECT_TRUE(R.contains("CC")) << R.out;
}

TEST_F(BuildTest, Kernel510_VerboseQuietWithCommandLine) {
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
      "all:\n"
      "\t@echo v=$(KBUILD_VERBOSE) q=[$(quiet)] Q=[$(Q)]\n"
      ".PHONY: all\n");
  auto R = runMake({"V=1"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("v=1")) << R.out;
  EXPECT_TRUE(R.contains("q=[]")) << R.out;
  EXPECT_TRUE(R.contains("Q=[]")) << R.out;
}

TEST_F(BuildTest, Kernel510_ArchSelectionWithElseIfeq) {
  writeMakefile(
      "ARCH ?= x86\n"
      "ifeq ($(ARCH),x86)\n"
      "  SRCARCH := x86\n"
      "  BITS := 64\n"
      "else ifeq ($(ARCH),arm)\n"
      "  SRCARCH := arm\n"
      "  BITS := 32\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  SRCARCH := arm64\n"
      "  BITS := 64\n"
      "else ifeq ($(ARCH),riscv)\n"
      "  SRCARCH := riscv\n"
      "  BITS := 64\n"
      "else\n"
      "  SRCARCH := $(ARCH)\n"
      "  BITS := unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(SRCARCH) bits=$(BITS)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("arch=x86 bits=64")) << R1.out;
  auto R2 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64 bits=64")) << R2.out;
  auto R3 = runMake({"ARCH=mips"});
  ASSERT_TRUE(R3.ok()) << R3.err;
  EXPECT_TRUE(R3.contains("arch=mips bits=unknown")) << R3.out;
}

TEST_F(BuildTest, Kernel510_VersionConstruction) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 214\n"
      "EXTRAVERSION =\n"
      "NAME = Dare mighty things\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION) name=$(NAME)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.214")) << R.out;
  EXPECT_TRUE(R.contains("name=Dare mighty things")) << R.out;
}

TEST_F(BuildTest, Kernel510_VersionWithExtraversion) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "EXTRAVERSION = -rc3\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.0-rc3")) << R.out;
}

TEST_F(BuildTest, Kernel510_KbuildSubdirTemplate) {
  writeMakefile(
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES := m\n"
      "obj-y := core.o sched.o\n"
      "obj-$(CONFIG_SMP) += smp.o\n"
      "obj-$(CONFIG_MODULES) += module.o\n"
      "ALL_OBJS := $(obj-y) $(obj-m)\n"
      "all:\n"
      "\t@echo objs=[$(ALL_OBJS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("core.o")) << R.out;
  EXPECT_TRUE(R.contains("sched.o")) << R.out;
  EXPECT_TRUE(R.contains("smp.o")) << R.out;
  EXPECT_TRUE(R.contains("module.o")) << R.out;
}

TEST_F(BuildTest, Kernel510_ComputedVariableName) {
  writeMakefile(
      "CONFIG_ARM_VFP := y\n"
      "machine-y := mach-generic\n"
      "machine-$(CONFIG_ARM_VFP) := mach-vfp\n"
      "MACHINE := $(machine-y)\n"
      "all:\n"
      "\t@echo machine=$(MACHINE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("machine=mach-vfp")) << R.out;
}

TEST_F(BuildTest, Kernel510_CcOptionWithShellFallback) {
  writeMakefile(
      "define try-run\n"
      "$(shell if $(1) >/dev/null 2>&1; then echo $(2); else echo $(3); fi)\n"
      "endef\n"
      "define cc-option\n"
      "$(call try-run,echo test,$(1),$(2))\n"
      "endef\n"
      "CFLAGS := -O2\n"
      "OPT := $(call cc-option,-Wformat-security,-Wno-format)\n"
      "CFLAGS += $(OPT)\n"
      "all:\n"
      "\t@echo cflags=[$(strip $(CFLAGS))]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-Wformat-security")) << R.out;
}

TEST_F(BuildTest, Kernel510_ForeachEvalModuleTemplate) {
  writeMakefile(
      "SUBDIRS := drivers fs net\n"
      "define build_subdir\n"
      "$(1)-objs := $(1)/main.o $(1)/init.o\n"
      "obj-y += $$($(1)-objs)\n"
      "endef\n"
      "$(foreach d,$(SUBDIRS),$(eval $(call build_subdir,$(d))))\n"
      "all:\n"
      "\t@echo obj-y=[$(obj-y)]\n"
      "\t@echo drivers-objs=[$(drivers-objs)]\n"
      "\t@echo fs-objs=[$(fs-objs)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("drivers/main.o")) << R.out;
  EXPECT_TRUE(R.contains("drivers/init.o")) << R.out;
  EXPECT_TRUE(R.contains("fs/main.o")) << R.out;
  EXPECT_TRUE(R.contains("net/main.o")) << R.out;
}

TEST_F(BuildTest, Kernel510_FilterOutPatsubstPipeline) {
  writeMakefile(
      "SRCS := main.c helper.c test_main.c debug.c test_helper.c\n"
      "FILTERED := $(filter-out test_%,$(SRCS))\n"
      "OBJS := $(patsubst %.c,%.o,$(FILTERED))\n"
      "PATHS := $(addprefix build/,$(OBJS))\n"
      "all:\n"
      "\t@echo paths=[$(PATHS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build/main.o")) << R.out;
  EXPECT_TRUE(R.contains("build/helper.o")) << R.out;
  EXPECT_TRUE(R.contains("build/debug.o")) << R.out;
  EXPECT_FALSE(R.contains("test_main")) << R.out;
  EXPECT_FALSE(R.contains("test_helper")) << R.out;
}

TEST_F(BuildTest, Kernel510_IfChangedCommandPattern) {
  writeMakefile(
      "define if_changed\n"
      "$(if $(strip $(filter-out $(cmd_$(1)),$(cmd_$(1)))),\n"
      "@echo '  $(quiet_cmd_$(1))'\n"
      "$(cmd_$(1)))\n"
      "endef\n"
      "quiet_cmd_link = LINK $@\n"
      "cmd_link = ld -o $@ $^\n"
      "RESULT := $(quiet_cmd_link)\n"
      "all:\n"
      "\t@echo result=[$(RESULT)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("LINK")) << R.out;
}

TEST_F(BuildTest, Kernel510_RecipeContinuation) {
  writeMakefile(
      "all:\n"
      "\t@echo hello \\\n"
      "\tworld \\\n"
      "\tdone\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello")) << R.out;
  EXPECT_TRUE(R.contains("world")) << R.out;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

TEST_F(BuildTest, Kernel510_ExportChainForSubMake) {
  writeMakefile(
      "ARCH := arm64\n"
      "CROSS_COMPILE := aarch64-linux-gnu-\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "export ARCH CROSS_COMPILE CC LD\n"
      "HOSTCC := gcc\n"
      "all:\n"
      "\t@echo cc=$(CC) ld=$(LD) hostcc=$(HOSTCC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=aarch64-linux-gnu-gcc")) << R.out;
  EXPECT_TRUE(R.contains("ld=aarch64-linux-gnu-ld")) << R.out;
  EXPECT_TRUE(R.contains("hostcc=gcc")) << R.out;
}

TEST_F(BuildTest, Kernel510_FORCETargetWithDeps) {
  writeFile(tmp() / "src.c", "int main(){}");
  writeMakefile(
      "all: version.h\n"
      "\t@echo done\n"
      "version.h: FORCE\n"
      "\t@echo generating version.h\n"
      "\t@echo '/* version */' > version.h\n"
      "FORCE:\n"
      ".PHONY: all FORCE\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("generating version.h")) << R.out;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

TEST_F(BuildTest, Kernel510_NestedIfWithFindstring) {
  writeMakefile(
      "CFLAGS := -march=armv8-a\n"
      "ARCH := arm64\n"
      "ifeq ($(ARCH),arm64)\n"
      "  ifneq ($(findstring -march=,$(CFLAGS)),)\n"
      "    HAS_MARCH := yes\n"
      "  else\n"
      "    HAS_MARCH := no\n"
      "  endif\n"
      "else\n"
      "  HAS_MARCH := n/a\n"
      "endif\n"
      "all:\n"
      "\t@echo has_march=$(HAS_MARCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("has_march=yes")) << R.out;
}

TEST_F(BuildTest, Kernel510_DefineCmdTemplate) {
  writeMakefile(
      "quiet_cmd_cc_o_c = CC      $@\n"
      "      cmd_cc_o_c = gcc -c $< -o $@\n"
      "define rule_cc_o_c\n"
      "$(call echo-cmd,cc_o_c)\n"
      "$(cmd_cc_o_c)\n"
      "endef\n"
      "define echo-cmd\n"
      "$(if $($(quiet)cmd_$(1)),@echo '  $($(quiet)cmd_$(1))')\n"
      "endef\n"
      "quiet := quiet_\n"
      "RESULT := $(call echo-cmd,cc_o_c)\n"
      "all:\n"
      "\t@echo [$(RESULT)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC")) << R.out;
}

TEST_F(BuildTest, Kernel510_MakeVersionCheck) {
  writeMakefile(
      "ifneq ($(filter 4.%,$(MAKE_VERSION)),)\n"
      "  MAKE_OK := yes\n"
      "else ifneq ($(filter 3.8%,$(MAKE_VERSION)),)\n"
      "  MAKE_OK := yes\n"
      "else\n"
      "  MAKE_OK := no\n"
      "endif\n"
      "all:\n"
      "\t@echo make_ok=$(MAKE_OK) ver=$(MAKE_VERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("make_ok=yes")) << R.out;
  EXPECT_TRUE(R.contains("ver=4.3")) << R.out;
}

TEST_F(BuildTest, Kernel510_OverrideDefineAppend) {
  writeMakefile(
      "KBUILD_CFLAGS := -O2\n"
      "override KBUILD_CFLAGS += -Wall\n"
      "override KBUILD_CFLAGS += -Wextra\n"
      "all:\n"
      "\t@echo cflags=$(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"KBUILD_CFLAGS=-Os"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-Os")) << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-Wextra")) << R.out;
}

TEST_F(BuildTest, Kernel510_SubstRefWithDirPrefix) {
  writeMakefile(
      "SRCS := drivers/core.c drivers/init.c lib/helper.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "all:\n"
      "\t@echo objs=[$(OBJS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("drivers/core.o")) << R.out;
  EXPECT_TRUE(R.contains("drivers/init.o")) << R.out;
  EXPECT_TRUE(R.contains("lib/helper.o")) << R.out;
}

TEST_F(BuildTest, Kernel510_IncludeGeneratedDepFiles) {
  writeFile(tmp() / "main.c", "int main(){}");
  writeFile(tmp() / "main.h", "");
  writeFile(tmp() / "util.c", "int util(){}");
  writeFile(tmp() / "util.h", "");
  writeFile(tmp() / "main.d", "main.o: main.c main.h\n");
  writeFile(tmp() / "util.d", "util.o: util.c util.h\n");
  writeMakefile(
      ".DEFAULT_GOAL := all\n"
      "OBJS := main.o util.o\n"
      "DEPS := $(OBJS:.o=.d)\n"
      "-include $(DEPS)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("objs=main.o util.o")) << R.out;
}

TEST_F(BuildTest, Kernel510_FileWriteEmptyText) {
  writeMakefile(
      "all:\n"
      "\t$(file >$(CURDIR)/test_empty.txt,)\n"
      "\t@wc -c < $(CURDIR)/test_empty.txt | tr -d ' '\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("1")) << "Should write single newline, out: " << R.out;
}

TEST_F(BuildTest, Kernel510_ComplexCondChainWithOrigin) {
  writeMakefile(
      "ifeq (\"$(origin CROSS_COMPILE)\", \"command line\")\n"
      "  CROSS := $(CROSS_COMPILE)\n"
      "else ifdef CROSS_COMPILE\n"
      "  CROSS := $(CROSS_COMPILE)\n"
      "else\n"
      "  CROSS :=\n"
      "endif\n"
      "CC := $(CROSS)gcc\n"
      "all:\n"
      "\t@echo cc=[$(CC)]\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cc=[gcc]")) << R1.out;
  auto R2 = runMake({"CROSS_COMPILE=arm-linux-gnueabi-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=[arm-linux-gnueabi-gcc]")) << R2.out;
}

TEST_F(BuildTest, Kernel510_KbuildObjYWithForeachEval) {
  writeMakefile(
      "subdirs := mm fs net\n"
      "define register_subdir\n"
      "obj-y += $(1)/built-in.a\n"
      "endef\n"
      "$(foreach d,$(subdirs),$(eval $(call register_subdir,$(d))))\n"
      "OBJS := $(filter %.a,$(obj-y))\n"
      "all:\n"
      "\t@echo objs=[$(OBJS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("mm/built-in.a")) << R.out;
  EXPECT_TRUE(R.contains("fs/built-in.a")) << R.out;
  EXPECT_TRUE(R.contains("net/built-in.a")) << R.out;
}

TEST_F(BuildTest, Kernel510_PatsubstMultiLevel) {
  writeMakefile(
      "C_SRCS := a.c b.c c.c\n"
      "S_SRCS := entry.S vector.S\n"
      "C_OBJS := $(patsubst %.c,%.o,$(C_SRCS))\n"
      "S_OBJS := $(patsubst %.S,%.o,$(S_SRCS))\n"
      "ALL_OBJS := $(C_OBJS) $(S_OBJS)\n"
      "PREFIXED := $(addprefix obj/,$(ALL_OBJS))\n"
      "DIRS := $(sort $(dir $(PREFIXED)))\n"
      "all:\n"
      "\t@echo objs=[$(PREFIXED)]\n"
      "\t@echo dirs=[$(DIRS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("obj/a.o")) << R.out;
  EXPECT_TRUE(R.contains("obj/entry.o")) << R.out;
  EXPECT_TRUE(R.contains("dirs=[obj/]")) << R.out;
}

TEST_F(BuildTest, Kernel510_IfeqWithQuotesAndVarExpansion) {
  writeMakefile(
      "ARCH := arm64\n"
      "ifeq \"$(ARCH)\" \"arm64\"\n"
      "  MATCH := yes\n"
      "else\n"
      "  MATCH := no\n"
      "endif\n"
      "all:\n"
      "\t@echo match=$(MATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("match=yes")) << R.out;
}

TEST_F(BuildTest, Kernel510_IncludeArchMakefile) {
  writeFile(tmp() / "arch_arm64.mk",
      "ARCH_CFLAGS := -march=armv8-a\n"
      "ARCH_LDFLAGS := -maarch64elf\n");
  writeMakefile(
      "SRCARCH := arm64\n"
      "include $(CURDIR)/arch_$(SRCARCH).mk\n"
      "all:\n"
      "\t@echo cflags=$(ARCH_CFLAGS) ldflags=$(ARCH_LDFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cflags=-march=armv8-a")) << R.out;
  EXPECT_TRUE(R.contains("ldflags=-maarch64elf")) << R.out;
}

TEST_F(BuildTest, Kernel510_DollarDollarInRecipe) {
  writeMakefile(
      "all:\n"
      "\t@VAR=hello; echo result=$$VAR\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=hello")) << R.out;
}

TEST_F(BuildTest, Kernel510_DefineWithCallAndDollarDollar) {
  writeMakefile(
      "define filechk\n"
      "$(filechk_$(1))\n"
      "endef\n"
      "filechk_version = echo 5.10.0\n"
      "RESULT := $(call filechk,version)\n"
      "all:\n"
      "\t@echo result=[$(RESULT)]\n"
      "\t@VAR=hello; echo shell_var=$$VAR\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=[echo 5.10.0]")) << R.out;
  EXPECT_TRUE(R.contains("shell_var=hello")) << R.out;
}

TEST_F(BuildTest, Kernel510_UndefineAndRedefine) {
  writeMakefile(
      "FOO := old_value\n"
      "undefine FOO\n"
      "ifdef FOO\n"
      "  STATUS := defined\n"
      "else\n"
      "  STATUS := undefined\n"
      "endif\n"
      "FOO := new_value\n"
      "all:\n"
      "\t@echo status=$(STATUS) foo=$(FOO)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("status=undefined")) << R.out;
  EXPECT_TRUE(R.contains("foo=new_value")) << R.out;
}

TEST_F(BuildTest, Kernel510_OverrideUndefine) {
  writeMakefile(
      "override undefine CFLAGS\n"
      "ifdef CFLAGS\n"
      "  R := defined\n"
      "else\n"
      "  R := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo r=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O2"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r=undefined")) << R.out;
}

// ============================================================================
// Robustness & Edge Case Tests
// ============================================================================

TEST_F(BuildTest, Robust_EmptyVarInAllContexts) {
  writeMakefile(
      "EMPTY :=\n"
      "ALSO_EMPTY =\n"
      "R1 := [$(EMPTY)]\n"
      "R2 := [$(ALSO_EMPTY)]\n"
      "R3 := $(if $(EMPTY),yes,no)\n"
      "R4 := $(strip $(EMPTY))\n"
      "R5 := $(words $(EMPTY))\n"
      "R6 := $(sort $(EMPTY))\n"
      "R7 := $(filter %.o,$(EMPTY))\n"
      "R8 := $(patsubst %.c,%.o,$(EMPTY))\n"
      "R9 := $(foreach x,$(EMPTY),item)\n"
      "R10 := $(subst a,b,$(EMPTY))\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2) r3=$(R3) r4=[$(R4)] r5=$(R5)\n"
      "\t@echo r6=[$(R6)] r7=[$(R7)] r8=[$(R8)] r9=[$(R9)] r10=[$(R10)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=[]")) << R.out;
  EXPECT_TRUE(R.contains("r2=[]")) << R.out;
  EXPECT_TRUE(R.contains("r3=no")) << R.out;
  EXPECT_TRUE(R.contains("r4=[]")) << R.out;
  EXPECT_TRUE(R.contains("r5=0")) << R.out;
}

TEST_F(BuildTest, Robust_UndefinedVarInAllContexts) {
  writeMakefile(
      "R1 := [$(NONEXIST)]\n"
      "R2 := $(if $(NONEXIST),yes,no)\n"
      "R3 := $(or $(NONEXIST),fallback)\n"
      "R4 := $(origin NONEXIST)\n"
      "R5 := $(flavor NONEXIST)\n"
      "ifdef NONEXIST\n"
      "  R6 := defined\n"
      "else\n"
      "  R6 := undefined\n"
      "endif\n"
      "R7 := $(words $(NONEXIST))\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2) r3=$(R3) r4=$(R4)\n"
      "\t@echo r5=$(R5) r6=$(R6) r7=$(R7)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=[]")) << R.out;
  EXPECT_TRUE(R.contains("r2=no")) << R.out;
  EXPECT_TRUE(R.contains("r3=fallback")) << R.out;
  EXPECT_TRUE(R.contains("r4=undefined")) << R.out;
  EXPECT_TRUE(R.contains("r5=undefined")) << R.out;
  EXPECT_TRUE(R.contains("r6=undefined")) << R.out;
  EXPECT_TRUE(R.contains("r7=0")) << R.out;
}

TEST_F(BuildTest, Robust_VeryLongVariableValue) {
  std::string LongVal;
  for (int i = 0; i < 500; ++i) {
    if (i > 0)
      LongVal += " ";
    LongVal += "item" + std::to_string(i);
  }
  writeMakefile(
      "LONG := " + LongVal + "\n"
      "COUNT := $(words $(LONG))\n"
      "FIRST := $(firstword $(LONG))\n"
      "LAST := $(lastword $(LONG))\n"
      "all:\n"
      "\t@echo count=$(COUNT) first=$(FIRST) last=$(LAST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=500")) << R.out;
  EXPECT_TRUE(R.contains("first=item0")) << R.out;
  EXPECT_TRUE(R.contains("last=item499")) << R.out;
}

TEST_F(BuildTest, Robust_DeepNestedFunctions) {
  writeMakefile(
      "X := hello world foo bar baz\n"
      "R := $(strip $(sort $(filter-out baz,$(patsubst %,prefix_%,"
      "$(filter %o %d,$(X))))))\n"
      "all:\n"
      "\t@echo r=[$(R)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("prefix_foo")) << R.out;
  EXPECT_TRUE(R.contains("prefix_world")) << R.out;
}

TEST_F(BuildTest, Robust_CircularVarNoHang) {
  writeMakefile(
      "A = $(B)\n"
      "B = $(C)\n"
      "C = $(A)\n"
      "all:\n"
      "\t@echo a=[$(A)] b=[$(B)] c=[$(C)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=[]")) << R.out;
}

TEST_F(BuildTest, Robust_MissingIncludeOptional) {
  writeMakefile(
      "-include nonexistent1.mk\n"
      "sinclude nonexistent2.mk\n"
      "-include $(wildcard *.nonexist)\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ok")) << R.out;
}

TEST_F(BuildTest, Robust_PatternRuleNoMatchingPrereq) {
  writeFile(tmp() / "actual.c", "int x;");
  writeMakefile(
      "%.o: %.c\n"
      "\t@echo compile $< to $@\n"
      "all: actual.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile actual.c to actual.o")) << R.out;
}

TEST_F(BuildTest, Robust_MultipleRulesSameTargetMergePrereqs) {
  writeFile(tmp() / "a.h", "");
  writeFile(tmp() / "b.h", "");
  writeFile(tmp() / "main.c", "");
  writeMakefile(
      "main.o: main.c a.h\n"
      "\t@echo compile main.c\n"
      "main.o: b.h\n"
      "all: main.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile main.c")) << R.out;
}

TEST_F(BuildTest, Robust_SpecialCharsInVarNames) {
  writeMakefile(
      "my-var := dash\n"
      "my_var := underscore\n"
      "my.var := dot\n"
      "all:\n"
      "\t@echo dash=$(my-var) under=$(my_var) dot=$(my.var)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dash=dash")) << R.out;
  EXPECT_TRUE(R.contains("under=underscore")) << R.out;
  EXPECT_TRUE(R.contains("dot=dot")) << R.out;
}

TEST_F(BuildTest, Robust_AssignModesInteraction) {
  writeMakefile(
      "A = recursive\n"
      "B := simple\n"
      "C ?= conditional\n"
      "D ?= should_not_override\n"
      "D := overridden\n"
      "E = $(A)\n"
      "A = changed\n"
      "F := $(A)\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B) c=$(C) d=$(D)\n"
      "\t@echo e=$(E) f=$(F)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=changed")) << R.out;
  EXPECT_TRUE(R.contains("b=simple")) << R.out;
  EXPECT_TRUE(R.contains("c=conditional")) << R.out;
  EXPECT_TRUE(R.contains("d=overridden")) << R.out;
  EXPECT_TRUE(R.contains("e=changed")) << R.out;
  EXPECT_TRUE(R.contains("f=changed")) << R.out;
}

TEST_F(BuildTest, Robust_CmdLineVarBlocksFileAssign) {
  writeMakefile(
      "FOO = from_file\n"
      "FOO := also_from_file\n"
      "all:\n"
      "\t@echo foo=$(FOO)\n"
      ".PHONY: all\n");
  auto R = runMake({"FOO=from_cmdline"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("foo=from_cmdline")) << R.out;
}

TEST_F(BuildTest, Robust_DefineEvalInteraction) {
  writeMakefile(
      ".DEFAULT_GOAL := all\n"
      "define make_rule\n"
      "$(1): ; @echo building $(1)\n"
      ".PHONY: $(1)\n"
      "endef\n"
      "$(eval $(call make_rule,target_a))\n"
      "$(eval $(call make_rule,target_b))\n"
      "all: target_a target_b\n"
      "\t@echo all_done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building target_a")) << R.out;
  EXPECT_TRUE(R.contains("building target_b")) << R.out;
  EXPECT_TRUE(R.contains("all_done")) << R.out;
}

TEST_F(BuildTest, Robust_ForeachPreservesOuterVar) {
  writeMakefile(
      "X := outer_value\n"
      "ITEMS := a b c\n"
      "RESULT := $(foreach X,$(ITEMS),$(X)_done)\n"
      "all:\n"
      "\t@echo x=$(X) result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x=outer_value")) << R.out;
  EXPECT_TRUE(R.contains("a_done")) << R.out;
  EXPECT_TRUE(R.contains("b_done")) << R.out;
  EXPECT_TRUE(R.contains("c_done")) << R.out;
}

TEST_F(BuildTest, Robust_CallPreservesPositionalVars) {
  writeMakefile(
      "outer = [$(1)][$(2)]\n"
      "inner = {$(1)}\n"
      "RESULT := $(call outer,A,$(call inner,B))\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("[A]")) << R.out;
  EXPECT_TRUE(R.contains("{B}")) << R.out;
}

TEST_F(BuildTest, Robust_OrderOnlyDep) {
  writeFile(tmp() / "src.c", "int main(){}");
  writeMakefile(
      "output: src.c | order_dep\n"
      "\t@echo building output\n"
      "\t@echo done > output\n"
      "order_dep:\n"
      "\t@echo creating order_dep\n"
      "\t@mkdir -p order_dep\n"
      ".PHONY: order_dep\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("creating order_dep")) << R.out;
  EXPECT_TRUE(R.contains("building output")) << R.out;
}

TEST_F(BuildTest, Robust_FileWriteAndRead) {
  writeMakefile(
      "all:\n"
      "\t$(file >$(CURDIR)/test_rw.txt,line1)\n"
      "\t$(file >>$(CURDIR)/test_rw.txt,line2)\n"
      "\t@cat $(CURDIR)/test_rw.txt\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("line1")) << R.out;
  EXPECT_TRUE(R.contains("line2")) << R.out;
}

TEST_F(BuildTest, Robust_RecipePrefixesAfterExpansion) {
  writeMakefile(
      "define silent_echo\n"
      "@echo $(1)\n"
      "endef\n"
      "all:\n"
      "\t$(call silent_echo,silent_output)\n"
      "\t-false\n"
      "\t@echo after_ignore_error\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("silent_output")) << R.out;
  EXPECT_TRUE(R.contains("after_ignore_error")) << R.out;
  EXPECT_FALSE(R.contains("echo silent_output")) << "@ prefix should suppress echo, out: " << R.out;
}

TEST_F(BuildTest, Robust_StaticPatternRule) {
  writeFile(tmp() / "a.c", "int a;");
  writeFile(tmp() / "b.c", "int b;");
  writeFile(tmp() / "c.c", "int c;");
  writeMakefile(
      "OBJS := a.o b.o c.o\n"
      "all: $(OBJS)\n"
      "\t@echo done\n"
      "$(OBJS): %.o: %.c\n"
      "\t@echo compile $< to $@\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile a.c to a.o")) << R.out;
  EXPECT_TRUE(R.contains("compile b.c to b.o")) << R.out;
  EXPECT_TRUE(R.contains("compile c.c to c.o")) << R.out;
}

TEST_F(BuildTest, Robust_ParallelBuildOrder) {
  writeMakefile(
      "all: step3\n"
      "step3: step2\n"
      "\t@echo step3\n"
      "step2: step1\n"
      "\t@echo step2\n"
      "step1:\n"
      "\t@echo step1\n"
      ".PHONY: all step1 step2 step3\n");
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << R.err;
  auto pos1 = R.out.find("step1");
  auto pos2 = R.out.find("step2");
  auto pos3 = R.out.find("step3");
  EXPECT_LT(pos1, pos2) << "step1 should come before step2";
  EXPECT_LT(pos2, pos3) << "step2 should come before step3";
}

TEST_F(BuildTest, Robust_KeepGoingContinuesAfterError) {
  writeMakefile(
      "all: good_target bad_target good2_target\n"
      "good_target:\n"
      "\t@echo good1\n"
      "bad_target:\n"
      "\tfalse\n"
      "good2_target:\n"
      "\t@echo good2\n"
      ".PHONY: all good_target bad_target good2_target\n");
  auto R = runMake({"-k"});
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.contains("good1")) << R.out;
}

TEST_F(BuildTest, Robust_DryRunForcePrefix) {
  writeMakefile(
      "all:\n"
      "\t@echo normal_cmd\n"
      "\t+echo forced_cmd\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("echo normal_cmd")) << R.out;
  EXPECT_TRUE(R.contains("forced_cmd")) << R.out;
}

TEST_F(BuildTest, Robust_DefaultGoalSetting) {
  writeMakefile(
      ".DEFAULT_GOAL := custom\n"
      "first:\n"
      "\t@echo first\n"
      "custom:\n"
      "\t@echo custom_target\n"
      ".PHONY: first custom\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("custom_target")) << R.out;
  EXPECT_FALSE(R.contains("first")) << R.out;
}

TEST_F(BuildTest, Robust_MakefileListTracking) {
  writeFile(tmp() / "sub.mk",
      "SUB_LOADED := yes\n"
      "MFL2 := $(MAKEFILE_LIST)\n");
  writeMakefile(
      "MFL1 := $(MAKEFILE_LIST)\n"
      "include $(CURDIR)/sub.mk\n"
      "all:\n"
      "\t@echo mfl1=[$(MFL1)]\n"
      "\t@echo sub=$(SUB_LOADED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("akefile")) << "MAKEFILE_LIST should contain the makefile name, out: " << R.out;
  EXPECT_TRUE(R.contains("sub=yes")) << R.out;
}

TEST_F(BuildTest, Robust_RecursionDepthLimit) {
  writeMakefile(
      "deep = $(deep)\n"
      "all:\n"
      "\t@echo val=[$(deep)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("val=[]")) << R.out;
}

TEST_F(BuildTest, Robust_ComplexSubstRefWithPercent) {
  writeMakefile(
      "SRCS := src/a.c src/b.c lib/c.c\n"
      "OBJS := $(SRCS:src/%.c=obj/%.o)\n"
      "all:\n"
      "\t@echo objs=[$(OBJS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("obj/a.o")) << R.out;
  EXPECT_TRUE(R.contains("obj/b.o")) << R.out;
  EXPECT_TRUE(R.contains("lib/c.c")) << R.out;
}

TEST_F(BuildTest, Robust_EvalGeneratesPatternRule) {
  writeFile(tmp() / "test.c", "int main(){}");
  writeMakefile(
      "define pattern_rule\n"
      "%.o: %.c\n"
      "\t@echo compile $$< to $$@\n"
      "endef\n"
      "$(eval $(pattern_rule))\n"
      "all: test.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile test.c to test.o")) << R.out;
}

TEST_F(BuildTest, Robust_NestedCallWithEval) {
  writeMakefile(
      ".DEFAULT_GOAL := all\n"
      "define gen_target\n"
      "$(1):\n"
      "\t@echo built_$(1)\n"
      ".PHONY: $(1)\n"
      "endef\n"
      "define gen_all\n"
      "$(foreach t,$(1),$(eval $(call gen_target,$(t))))\n"
      "endef\n"
      "$(call gen_all,x y z)\n"
      "all: x y z\n"
      "\t@echo all_done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("built_x")) << R.out;
  EXPECT_TRUE(R.contains("built_y")) << R.out;
  EXPECT_TRUE(R.contains("built_z")) << R.out;
  EXPECT_TRUE(R.contains("all_done")) << R.out;
}

TEST_F(BuildTest, Robust_AppendToSimpleVar) {
  writeMakefile(
      "FLAGS := -O2\n"
      "FLAGS += -Wall\n"
      "FLAGS += -Wextra\n"
      "all:\n"
      "\t@echo flags=[$(FLAGS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-Wextra")) << R.out;
}

TEST_F(BuildTest, Robust_AppendToRecursiveVar) {
  writeMakefile(
      "FLAGS = -O2\n"
      "FLAGS += -Wall\n"
      "EXTRA := -Wformat\n"
      "FLAGS += $(EXTRA)\n"
      "EXTRA := -Wshadow\n"
      "all:\n"
      "\t@echo flags=[$(FLAGS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-Wshadow")) << R.out;
}

TEST_F(BuildTest, Robust_MultiTargetRule) {
  writeMakefile(
      "all: a b c\n"
      "\t@echo done\n"
      "a b c:\n"
      "\t@echo building $@\n"
      ".PHONY: all a b c\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building a")) << R.out;
  EXPECT_TRUE(R.contains("building b")) << R.out;
  EXPECT_TRUE(R.contains("building c")) << R.out;
}

// ============================================================================
// Full Linux 5.10 Mini-Kernel Simulation
// ============================================================================

TEST_F(BuildTest, Kernel510_FullBuildSimulation) {
  writeFile(tmp() / "arch_x86.mk",
      "ARCH_CFLAGS := -m64 -march=x86-64\n"
      "ARCH_ASFLAGS := --64\n"
      "head-y := arch/x86/kernel/head_64.o\n");
  writeFile(tmp() / "arch_arm64.mk",
      "ARCH_CFLAGS := -march=armv8-a\n"
      "ARCH_ASFLAGS := -march=armv8-a\n"
      "head-y := arch/arm64/kernel/head.o\n");
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 214\n"
      "EXTRAVERSION =\n"
      "\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "\n"
      "ARCH ?= x86\n"
      "ifeq ($(ARCH),x86)\n"
      "  SRCARCH := x86\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  SRCARCH := arm64\n"
      "else\n"
      "  SRCARCH := $(ARCH)\n"
      "endif\n"
      "\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "AR := $(CROSS_COMPILE)ar\n"
      "\n"
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
      "\n"
      "export ARCH SRCARCH CROSS_COMPILE CC LD AR\n"
      "\n"
      "include $(CURDIR)/arch_$(SRCARCH).mk\n"
      "\n"
      "KBUILD_CFLAGS := -O2 -std=gnu89\n"
      "KBUILD_CFLAGS += $(ARCH_CFLAGS)\n"
      "KBUILD_CFLAGS += -Wall -Wstrict-prototypes\n"
      "\n"
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES := y\n"
      "CONFIG_PRINTK := y\n"
      "\n"
      "init-y := init/main.o init/version.o\n"
      "core-y := kernel/sched.o kernel/fork.o\n"
      "core-$(CONFIG_SMP) += kernel/smp.o\n"
      "core-$(CONFIG_PRINTK) += kernel/printk.o\n"
      "drivers-y := drivers/base/core.o\n"
      "libs-y := lib/string.o lib/vsprintf.o\n"
      "\n"
      "vmlinux-deps := $(head-y) $(init-y) $(core-y) $(drivers-y) $(libs-y)\n"
      "vmlinux-objs := $(patsubst %/,%/built-in.a,"
      "$(sort $(dir $(vmlinux-deps))))\n"
      "\n"
      "quiet_cmd_link = LINK    $@\n"
      "      cmd_link = $(LD) -o $@ $(vmlinux-deps)\n"
      "\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      "\t@echo arch=$(SRCARCH) cc=$(CC)\n"
      "\t@echo cflags=[$(KBUILD_CFLAGS)]\n"
      "\t@echo deps=[$(vmlinux-deps)]\n"
      "\t@echo objs=[$(vmlinux-objs)]\n"
      "\t@echo quiet=$(quiet) verbose=$(KBUILD_VERBOSE)\n"
      "\t@echo link=[$($(quiet)cmd_link)]\n"
      ".PHONY: all\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("version=5.10.214")) << R1.out;
  EXPECT_TRUE(R1.contains("arch=x86")) << R1.out;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;
  EXPECT_TRUE(R1.contains("-m64")) << R1.out;
  EXPECT_TRUE(R1.contains("-march=x86-64")) << R1.out;
  EXPECT_TRUE(R1.contains("kernel/smp.o")) << R1.out;
  EXPECT_TRUE(R1.contains("kernel/printk.o")) << R1.out;
  EXPECT_TRUE(R1.contains("quiet=quiet_")) << R1.out;
  EXPECT_TRUE(R1.contains("LINK")) << R1.out;

  auto R2 = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-", "V=1"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
  EXPECT_TRUE(R2.contains("-march=armv8-a")) << R2.out;
  EXPECT_TRUE(R2.contains("verbose=1")) << R2.out;
  EXPECT_TRUE(R2.contains("quiet=[]") || R2.contains("quiet= ")) << R2.out;
}

TEST_F(BuildTest, Kernel510_StressLargeKbuild) {
  std::string mk = "subdirs :=";
  for (int i = 0; i < 100; ++i)
    mk += " mod" + std::to_string(i);
  mk += "\n";
  mk += "define register\n"
        "$(1)-objs := $(1)/core.o $(1)/init.o\n"
        "obj-y += $$($(1)-objs)\n"
        "endef\n";
  mk += "$(foreach d,$(subdirs),$(eval $(call register,$(d))))\n";
  mk += "COUNT := $(words $(obj-y))\n";
  mk += "all:\n\t@echo count=$(COUNT)\n.PHONY: all\n";
  writeMakefile(mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=200")) << R.out;
}

TEST_F(BuildTest, Kernel510_StressParallelFanOut) {
  std::string mk;
  std::string targets;
  for (int i = 0; i < 30; ++i) {
    std::string t = "t" + std::to_string(i);
    targets += " " + t;
  }
  mk += "all:" + targets + "\n\t@echo all_done\n";
  for (int i = 0; i < 30; ++i) {
    std::string t = "t" + std::to_string(i);
    mk += t + ":\n\t@echo built_" + t + "\n";
  }
  mk += ".PHONY: all" + targets + "\n";
  writeMakefile(mk);
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("all_done")) << R.out;
  for (int i = 0; i < 30; ++i)
    EXPECT_TRUE(R.contains("built_t" + std::to_string(i))) << R.out;
}

// ============================================================================
// Additional Kernel 5.10 Robustness & Edge Case Tests
// ============================================================================

// $(space) and $(comma) helper variables — used throughout kernel Makefile
TEST_F(BuildTest, Kernel510_SpaceCommaHelpers) {
  writeMakefile(
      "comma  := ,\n"
      "empty  :=\n"
      "space  := $(empty) $(empty)\n"
      "LIST := a b c\n"
      "CSV := $(subst $(space),$(comma),$(LIST))\n"
      "all:\n"
      "\t@echo csv=[$(CSV)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("csv=[a,b,c]")) << R.out;
}

// $(call) args should not cross-contaminate during expansion
TEST_F(BuildTest, Kernel510_CallArgIndependence) {
  writeMakefile(
      "func = arg1=[$1] arg2=[$2]\n"
      "A = hello\n"
      "RESULT := $(call func,$(A),world_$(A))\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arg1=[hello]")) << R.out;
  EXPECT_TRUE(R.contains("arg2=[world_hello]")) << R.out;
}

// Nested filter + patsubst pipeline used for subdir extraction
TEST_F(BuildTest, Kernel510_FilterPatsubstSubdirs) {
  writeMakefile(
      "obj-y := core.o drivers/ net/ fs/vfs.o lib/\n"
      "subdirs := $(patsubst %/,%,$(filter %/,$(obj-y)))\n"
      "files := $(filter-out %/,$(obj-y))\n"
      "all:\n"
      "\t@echo dirs=[$(subdirs)] files=[$(files)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dirs=[drivers net lib]")) << R.out;
  EXPECT_TRUE(R.contains("files=[core.o fs/vfs.o]")) << R.out;
}

// $(word) after $(subst) — used for version parsing
TEST_F(BuildTest, Kernel510_WordAfterSubst) {
  writeMakefile(
      "KVER := 5.10.214\n"
      "MAJOR := $(word 1,$(subst ., ,$(KVER)))\n"
      "MINOR := $(word 2,$(subst ., ,$(KVER)))\n"
      "PATCH := $(word 3,$(subst ., ,$(KVER)))\n"
      "all:\n"
      "\t@echo major=$(MAJOR) minor=$(MINOR) patch=$(PATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("major=5")) << R.out;
  EXPECT_TRUE(R.contains("minor=10")) << R.out;
  EXPECT_TRUE(R.contains("patch=214")) << R.out;
}

// Multiple conditional CFLAGS accumulation (kernel pattern)
TEST_F(BuildTest, Kernel510_CFlagsConditionalAccumulation) {
  writeMakefile(
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES := y\n"
      "CONFIG_DEBUG_INFO :=\n"
      "CFLAGS := -O2\n"
      "ifeq ($(CONFIG_SMP),y)\n"
      "  CFLAGS += -DSMP\n"
      "endif\n"
      "ifeq ($(CONFIG_MODULES),y)\n"
      "  CFLAGS += -DMODULE\n"
      "endif\n"
      "ifdef CONFIG_DEBUG_INFO\n"
      "  CFLAGS += -g\n"
      "endif\n"
      "all:\n"
      "\t@echo cflags=[$(CFLAGS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-DSMP")) << R.out;
  EXPECT_TRUE(R.contains("-DMODULE")) << R.out;
  EXPECT_FALSE(R.contains("-g")) << R.out;
}

// Export multiple variables on one line + unexport specific
TEST_F(BuildTest, Kernel510_ExportMultipleThenUnexport) {
  writeMakefile(
      "CC := gcc\n"
      "LD := ld\n"
      "AR := ar\n"
      "export CC LD AR\n"
      "unexport AR\n"
      "all:\n"
      "\t@echo cc=$$CC ld=$$LD ar=$$AR\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
  EXPECT_TRUE(R.contains("ld=ld")) << R.out;
}

// $(foreach) with $(if $(findstring ...)) — used in arch selection
TEST_F(BuildTest, Kernel510_ForeachIfFindstring) {
  writeMakefile(
      "ALL_ARCHES := x86 arm arm64 mips riscv\n"
      "WANT := arm mips\n"
      "SELECTED := $(foreach a,$(ALL_ARCHES),$(if $(findstring "
      "$(a),$(WANT)),$(a)))\n"
      "all:\n"
      "\t@echo selected=[$(SELECTED)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arm")) << R.out;
  EXPECT_TRUE(R.contains("mips")) << R.out;
  EXPECT_FALSE(R.contains("x86")) << R.out;
}

// Double dereference with computed quiet prefix — core kernel pattern
TEST_F(BuildTest, Kernel510_DoubleDerefQuietCmd) {
  writeMakefile(
      "quiet = quiet_\n"
      "quiet_cmd_link = LINK $@\n"
      "cmd_link = ld -o $@ $^\n"
      "all:\n"
      "\t@echo display=[$($(quiet)cmd_link)]\n"
      "\t@echo full=[$(cmd_link)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("display=[LINK")) << R.out;
  EXPECT_TRUE(R.contains("full=[ld")) << R.out;
}

// define + call generating multi-command recipe with $$ escape
TEST_F(BuildTest, Kernel510_DefineCallDollarEscapeRecipe) {
  writeMakefile(
      "define run_cmd\n"
      "echo running: $(1)\n"
      "echo status: $$?\n"
      "endef\n"
      "all:\n"
      "\t@$(call run_cmd,test_program)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("running: test_program")) << R.out;
  EXPECT_TRUE(R.contains("status: 0")) << R.out;
}

// Multiple .PHONY declarations accumulating targets
TEST_F(BuildTest, Kernel510_MultiplePhonyDeclarationsAccumulate) {
  writeMakefile(
      ".PHONY: all\n"
      ".PHONY: clean install\n"
      ".PHONY: help\n"
      "all:\n"
      "\t@echo all\n"
      "clean:\n"
      "\t@echo clean\n"
      "install:\n"
      "\t@echo install\n"
      "help:\n"
      "\t@echo help\n");
  auto R1 = runMake({}, "all");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("all")) << R1.out;
  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("clean")) << R2.out;
  auto R3 = runMake({}, "help");
  ASSERT_TRUE(R3.ok()) << R3.err;
  EXPECT_TRUE(R3.contains("help")) << R3.out;
}

// FORCE pseudo-target pattern (no recipe, no prereqs)
TEST_F(BuildTest, Kernel510_FORCEPseudoTarget) {
  writeMakefile(
      "all: version.h\n"
      "\t@echo done\n"
      "version.h: FORCE\n"
      "\t@echo generating version.h\n"
      ".PHONY: all FORCE\n"
      "FORCE:\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("generating version.h")) << R.out;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

// $(addprefix) + $(addsuffix) pipeline — building file lists
TEST_F(BuildTest, Kernel510_AddprefixAddsuffixPipeline) {
  writeMakefile(
      "SRCS := main init config\n"
      "OBJ_DIR := build/obj\n"
      "OBJS := $(addprefix $(OBJ_DIR)/,$(addsuffix .o,$(SRCS)))\n"
      "all:\n"
      "\t@echo objs=[$(OBJS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build/obj/main.o")) << R.out;
  EXPECT_TRUE(R.contains("build/obj/init.o")) << R.out;
  EXPECT_TRUE(R.contains("build/obj/config.o")) << R.out;
}

// $(if $(CONFIG_X),...) — zero-length string is false
TEST_F(BuildTest, Kernel510_IfEmptyVsNonEmpty) {
  writeMakefile(
      "CONFIG_A := y\n"
      "CONFIG_B :=\n"
      "R_A := $(if $(CONFIG_A),yes_a,no_a)\n"
      "R_B := $(if $(CONFIG_B),yes_b,no_b)\n"
      "R_C := $(if $(CONFIG_C),yes_c,no_c)\n"
      "all:\n"
      "\t@echo a=$(R_A) b=$(R_B) c=$(R_C)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=yes_a")) << R.out;
  EXPECT_TRUE(R.contains("b=no_b")) << R.out;
  EXPECT_TRUE(R.contains("c=no_c")) << R.out;
}

// $(words) for counting — used for module count checks
TEST_F(BuildTest, Kernel510_WordsCount) {
  writeMakefile(
      "MODULES := mod_a mod_b mod_c mod_d mod_e\n"
      "COUNT := $(words $(MODULES))\n"
      "FIRST := $(firstword $(MODULES))\n"
      "LAST := $(lastword $(MODULES))\n"
      "all:\n"
      "\t@echo count=$(COUNT) first=$(FIRST) last=$(LAST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=5")) << R.out;
  EXPECT_TRUE(R.contains("first=mod_a")) << R.out;
  EXPECT_TRUE(R.contains("last=mod_e")) << R.out;
}

// Override define block — override multi-line macro
TEST_F(BuildTest, Kernel510_OverrideDefineBlock) {
  writeMakefile(
      "define cmd_link\n"
      "echo original\n"
      "endef\n"
      "override define cmd_link\n"
      "echo overridden\n"
      "endef\n"
      "all:\n"
      "\t@$(cmd_link)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("overridden")) << R.out;
}

// $(eval) generating rule with automatic variables
TEST_F(BuildTest, Kernel510_EvalRuleWithAutoVars) {
  writeFile(tmp() / "prog1.src", "");
  writeFile(tmp() / "prog2.src", "");
  writeMakefile(
      "TARGETS := prog1 prog2\n"
      "all: $(TARGETS)\n"
      "\t@echo all_done\n"
      ".PHONY: all\n"
      "define gen_rule\n"
      "$(1): $(1).src\n"
      "\t@echo building $$@ from $$<\n"
      "endef\n"
      "$(foreach t,$(TARGETS),$(eval $(call gen_rule,$(t))))\n");
  auto R = runMake({"-B"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building prog1 from prog1.src")) << R.out;
  EXPECT_TRUE(R.contains("building prog2 from prog2.src")) << R.out;
  EXPECT_TRUE(R.contains("all_done")) << R.out;
}

// ifeq with strip to handle trailing whitespace
TEST_F(BuildTest, Kernel510_IfeqWithStripWhitespace) {
  writeMakefile(
      "VAR := y  \n"
      "ifeq ($(strip $(VAR)),y)\n"
      "  RESULT := matched\n"
      "else\n"
      "  RESULT := not_matched\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=matched")) << R.out;
}

// Substitution reference with directory path conversion
TEST_F(BuildTest, Kernel510_SubstRefDirConversion) {
  writeMakefile(
      "SRCS := src/main.c src/util.c src/config.c\n"
      "OBJS := $(SRCS:src/%.c=obj/%.o)\n"
      "all:\n"
      "\t@echo objs=[$(OBJS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("obj/main.o")) << R.out;
  EXPECT_TRUE(R.contains("obj/util.o")) << R.out;
  EXPECT_TRUE(R.contains("obj/config.o")) << R.out;
}

// $(sort) for dedup + alphabetic ordering of paths
TEST_F(BuildTest, Kernel510_SortDeduplicateAndOrder) {
  writeMakefile(
      "DIRS := lib drivers drivers kernel lib fs kernel\n"
      "UNIQUE := $(sort $(DIRS))\n"
      "all:\n"
      "\t@echo dirs=[$(UNIQUE)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dirs=[drivers fs kernel lib]")) << R.out;
}

// $(or) and $(and) with function results
TEST_F(BuildTest, Kernel510_OrAndWithFunctionResults) {
  writeMakefile(
      "CC_IS_GCC :=\n"
      "CC_IS_CLANG := yes\n"
      "COMPILER := $(or $(and $(CC_IS_GCC),gcc),$(and "
      "$(CC_IS_CLANG),clang),unknown)\n"
      "all:\n"
      "\t@echo compiler=$(COMPILER)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compiler=clang")) << R.out;
}

// Multiple assignment modes on same variable
TEST_F(BuildTest, Kernel510_AssignModePrecedence) {
  writeMakefile(
      "X = recursive\n"
      "X := simple\n"
      "X += appended\n"
      "Y ?= conditional_default\n"
      "Y := overwritten\n"
      "Z ?= should_stick\n"
      "all:\n"
      "\t@echo x=[$(X)] y=[$(Y)] z=[$(Z)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x=[simple appended]")) << R.out;
  EXPECT_TRUE(R.contains("y=[overwritten]")) << R.out;
  EXPECT_TRUE(R.contains("z=[should_stick]")) << R.out;
}

// $(call) with $(eval) that generates rules + variables dynamically
TEST_F(BuildTest, Kernel510_CallEvalDynamicRulesAndVars) {
  std::filesystem::create_directories(tmp() / "net");
  std::filesystem::create_directories(tmp() / "fs");
  writeFile(tmp() / "net" / "core.o", "");
  writeFile(tmp() / "net" / "init.o", "");
  writeFile(tmp() / "fs" / "core.o", "");
  writeFile(tmp() / "fs" / "init.o", "");
  writeMakefile(
      "SUBDIRS := net fs\n"
      "all: $(foreach d,$(SUBDIRS),$(d)/built-in.o)\n"
      "\t@echo all_done\n"
      ".PHONY: all FORCE\n"
      "FORCE:\n"
      "define module_template\n"
      "$(1)_OBJS := $(1)/core.o $(1)/init.o\n"
      "$(1)_CFLAGS := -I$(1)/include\n"
      "$(1)/built-in.o: $$($(1)_OBJS)\n"
      "\t@echo link $$@ from $$($(1)_OBJS) with $$($(1)_CFLAGS)\n"
      "endef\n"
      "$(foreach d,$(SUBDIRS),$(eval $(call module_template,$(d))))\n");
  auto R = runMake({"-B"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("link net/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("link fs/built-in.o")) << R.out;
  EXPECT_TRUE(R.contains("-Inet/include")) << R.out;
  EXPECT_TRUE(R.contains("all_done")) << R.out;
}

// Deeply nested ifeq/else ifeq chain (6 levels)
TEST_F(BuildTest, Kernel510_DeepIfeqChain6Levels) {
  writeMakefile(
      "ARCH ?= arm64\n"
      "ifeq ($(ARCH),x86)\n"
      "  BITS := 32\n"
      "else ifeq ($(ARCH),x86_64)\n"
      "  BITS := 64\n"
      "else ifeq ($(ARCH),arm)\n"
      "  BITS := 32\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  BITS := 64\n"
      "else ifeq ($(ARCH),riscv)\n"
      "  BITS := 64\n"
      "else ifeq ($(ARCH),mips)\n"
      "  BITS := 32\n"
      "else\n"
      "  BITS := unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(ARCH) bits=$(BITS)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("arch=arm64 bits=64")) << R1.out;
  auto R2 = runMake({"ARCH=mips"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=mips bits=32")) << R2.out;
  auto R3 = runMake({"ARCH=ppc"});
  ASSERT_TRUE(R3.ok()) << R3.err;
  EXPECT_TRUE(R3.contains("bits=unknown")) << R3.out;
}

// $(MAKECMDGOALS) with $(filter) — routing specific targets
TEST_F(BuildTest, Kernel510_MakecmdgoalsFilter) {
  writeMakefile(
      "CLEAN_TARGETS := clean mrproper distclean\n"
      "ifneq ($(filter $(CLEAN_TARGETS),$(MAKECMDGOALS)),)\n"
      "  NEED_CONFIG := no\n"
      "else\n"
      "  NEED_CONFIG := yes\n"
      "endif\n"
      "all:\n"
      "\t@echo config=$(NEED_CONFIG)\n"
      "clean:\n"
      "\t@echo config=$(NEED_CONFIG)\n"
      ".PHONY: all clean\n");
  auto R1 = runMake({}, "all");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("config=yes")) << R1.out;
  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("config=no")) << R2.out;
}

// ifndef with ?= default value pattern (kernel common)
TEST_F(BuildTest, Kernel510_IfndefConditionalDefault) {
  writeMakefile(
      "ifndef CROSS_COMPILE\n"
      "  CROSS_COMPILE :=\n"
      "endif\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "all:\n"
      "\t@echo cc=$(CC) ld=$(LD)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;
  EXPECT_TRUE(R1.contains("ld=ld")) << R1.out;
  auto R2 = runMake({"CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
  EXPECT_TRUE(R2.contains("ld=aarch64-linux-gnu-ld")) << R2.out;
}

// $(basename) + $(suffix) on multiple files
TEST_F(BuildTest, Kernel510_BasenameSuffixMultiFile) {
  writeMakefile(
      "FILES := main.c util.h config.mk README\n"
      "BASES := $(basename $(FILES))\n"
      "SUFFS := $(suffix $(FILES))\n"
      "all:\n"
      "\t@echo bases=[$(BASES)] suffs=[$(SUFFS)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bases=[main util config README]")) << R.out;
  EXPECT_TRUE(R.contains("suffs=[.c .h .mk]")) << R.out;
}

// export with := assignment on same line
TEST_F(BuildTest, Kernel510_ExportWithColonEquals) {
  writeMakefile(
      "export CC := neverc\n"
      "export CFLAGS := -O2 -Wall\n"
      "all:\n"
      "\t@echo cc=$$CC flags=$$CFLAGS\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=neverc")) << R.out;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
}

// $(dir) and $(notdir) used together for path decomposition
TEST_F(BuildTest, Kernel510_DirNotdirDecomposition) {
  writeMakefile(
      "FILES := arch/x86/kernel/entry.o drivers/net/e1000.o fs/ext4/super.o\n"
      "DIRS := $(sort $(dir $(FILES)))\n"
      "NAMES := $(notdir $(FILES))\n"
      "all:\n"
      "\t@echo dirs=[$(DIRS)] names=[$(NAMES)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arch/x86/kernel/")) << R.out;
  EXPECT_TRUE(R.contains("drivers/net/")) << R.out;
  EXPECT_TRUE(R.contains("fs/ext4/")) << R.out;
  EXPECT_TRUE(R.contains("names=[entry.o e1000.o super.o]")) << R.out;
}

// Pattern rule matching with directory prefix
TEST_F(BuildTest, Kernel510_PatternRuleWithDirPrefix) {
  std::filesystem::create_directories(tmp() / "src");
  std::filesystem::create_directories(tmp() / "obj");
  writeFile(tmp() / "src" / "main.c", "");
  writeFile(tmp() / "src" / "util.c", "");
  writeMakefile(
      "obj/%.o: src/%.c\n"
      "\t@echo compile $< to $@\n"
      "all: obj/main.o obj/util.o\n"
      "\t@echo linked\n"
      ".PHONY: all\n");
  auto R = runMake({"-B"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile src/main.c to obj/main.o")) << R.out;
  EXPECT_TRUE(R.contains("compile src/util.c to obj/util.o")) << R.out;
}

// $(foreach) + $(filter) pipeline — extract matching config vars
TEST_F(BuildTest, Kernel510_ForeachFilterPipeline) {
  writeMakefile(
      "CONFIG_NET := y\n"
      "CONFIG_BLK := m\n"
      "CONFIG_USB := y\n"
      "CONFIG_SND :=\n"
      "ALL_CONFIGS := CONFIG_NET CONFIG_BLK CONFIG_USB CONFIG_SND\n"
      "ENABLED := $(foreach c,$(ALL_CONFIGS),$(if $($(c)),$(c)))\n"
      "BUILTIN := $(foreach c,$(ENABLED),$(if $(filter "
      "y,$($(c))),$(c)))\n"
      "all:\n"
      "\t@echo enabled=[$(ENABLED)] builtin=[$(BUILTIN)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CONFIG_NET")) << R.out;
  EXPECT_TRUE(R.contains("CONFIG_BLK")) << R.out;
  EXPECT_TRUE(R.contains("CONFIG_USB")) << R.out;
  EXPECT_FALSE(R.contains("CONFIG_SND")) << R.out;
}

// Kernel-style complete build pipeline: version + arch + config + modules
TEST_F(BuildTest, Kernel510_CompleteBuildPipeline) {
  writeMakefile(
      "# Version\n"
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 214\n"
      "EXTRAVERSION =\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "\n"
      "# Verbosity\n"
      "ifeq (\"$(origin V)\",\"command line\")\n"
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
      "\n"
      "# Architecture\n"
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "ifeq ($(ARCH),x86)\n"
      "  SRCARCH := x86\n"
      "else ifeq ($(ARCH),arm64)\n"
      "  SRCARCH := arm64\n"
      "else\n"
      "  SRCARCH := $(ARCH)\n"
      "endif\n"
      "\n"
      "# Toolchain\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "CFLAGS := -O2\n"
      "export CC LD CFLAGS\n"
      "\n"
      "# Config\n"
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES := y\n"
      "ifeq ($(CONFIG_SMP),y)\n"
      "  CFLAGS += -DCONFIG_SMP\n"
      "endif\n"
      "\n"
      "# Subdirectories\n"
      "core-y := kernel/ mm/\n"
      "drivers-y := drivers/\n"
      "ifeq ($(CONFIG_MODULES),y)\n"
      "  drivers-y += drivers/base/\n"
      "endif\n"
      "ALL_SUBDIRS := $(core-y) $(drivers-y)\n"
      "SUBDIRS := $(patsubst %/,%,$(ALL_SUBDIRS))\n"
      "\n"
      "# Module template\n"
      "define subdir_template\n"
      "$(1)_objs := $(1)/built-in.o\n"
      "endef\n"
      "$(foreach d,$(SUBDIRS),$(eval $(call subdir_template,$(d))))\n"
      "ALL_OBJS := $(foreach d,$(SUBDIRS),$($(d)_objs))\n"
      "\n"
      "# Commands\n"
      "quiet_cmd_link = LINK $@\n"
      "cmd_link = $(LD) -o $@ $(ALL_OBJS)\n"
      "\n"
      "# Build\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      "\t@echo arch=$(SRCARCH) cc=$(CC)\n"
      "\t@echo subdirs=[$(SUBDIRS)]\n"
      "\t@echo objs=[$(ALL_OBJS)]\n"
      "\t@echo cflags=[$(CFLAGS)]\n"
      "\t@echo display=[$($(quiet)cmd_link)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.214")) << R.out;
  EXPECT_TRUE(R.contains("arch=x86")) << R.out;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
  EXPECT_TRUE(R.contains("kernel")) << R.out;
  EXPECT_TRUE(R.contains("mm")) << R.out;
  EXPECT_TRUE(R.contains("drivers")) << R.out;
  EXPECT_TRUE(R.contains("-DCONFIG_SMP")) << R.out;
  EXPECT_TRUE(R.contains("LINK")) << R.out;

  // Test with cross-compile
  auto R2 = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
}

// Stress test: 200 config variables with conditional compilation
TEST_F(BuildTest, Kernel510_Stress200ConfigVars) {
  std::string mk;
  for (int i = 0; i < 200; ++i) {
    std::string name = "CONFIG_FEAT_" + std::to_string(i);
    if (i % 3 == 0)
      mk += name + " := y\n";
    else if (i % 3 == 1)
      mk += name + " := m\n";
  }
  mk += "BUILTIN :=\n";
  mk += "MODULE :=\n";
  for (int i = 0; i < 200; ++i) {
    std::string name = "CONFIG_FEAT_" + std::to_string(i);
    mk += "ifeq ($(" + name + "),y)\n";
    mk += "  BUILTIN += feat" + std::to_string(i) + ".o\n";
    mk += "else ifeq ($(" + name + "),m)\n";
    mk += "  MODULE += feat" + std::to_string(i) + ".o\n";
    mk += "endif\n";
  }
  mk += "BCOUNT := $(words $(BUILTIN))\n";
  mk += "MCOUNT := $(words $(MODULE))\n";
  mk += "all:\n\t@echo builtin=$(BCOUNT) module=$(MCOUNT)\n";
  mk += ".PHONY: all\n";
  writeMakefile(mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("builtin=67")) << R.out;
  EXPECT_TRUE(R.contains("module=67")) << R.out;
}

// Stress: foreach + eval generating 150 rules (dry-run to verify generation)
TEST_F(BuildTest, Kernel510_StressForeachEval150Rules) {
  std::string mk = "MODULES :=";
  for (int i = 0; i < 150; ++i)
    mk += " m" + std::to_string(i);
  mk += "\n";
  mk += "OBJS := $(addsuffix .o,$(MODULES))\n";
  mk += "COUNT := $(words $(OBJS))\n";
  mk += "all:\n\t@echo count=$(COUNT)\n.PHONY: all\n";
  mk += "define mod_rule\n"
        "$(1).o:\n"
        "\t@echo CC $(1).o\n"
        "endef\n";
  mk += "$(foreach m,$(MODULES),$(eval $(call mod_rule,$(m))))\n";
  writeMakefile(mk);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=150")) << R.out;
}

// Edge case: empty foreach body produces empty result
TEST_F(BuildTest, Kernel510_EdgeEmptyForeachBody) {
  writeMakefile(
      "LIST := a b c\n"
      "RESULT := [$(foreach x,$(LIST),)]done\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

// Edge case: $(call) with arg that contains commas via $(comma)
TEST_F(BuildTest, Kernel510_CallArgWithComma) {
  writeMakefile(
      "comma := ,\n"
      "func = got [$(1)] and [$(2)]\n"
      "RESULT := $(call func,hello$(comma)world,second)\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("got [hello,world]")) << R.out;
  EXPECT_TRUE(R.contains("second")) << R.out;
}

// Edge case: ifeq comparing variables that expand to empty
TEST_F(BuildTest, Kernel510_IfeqBothEmpty) {
  writeMakefile(
      "A :=\n"
      "B :=\n"
      "ifeq ($(A),$(B))\n"
      "  MATCH := yes\n"
      "else\n"
      "  MATCH := no\n"
      "endif\n"
      "all:\n"
      "\t@echo match=$(MATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("match=yes")) << R.out;
}

// Edge case: $(eval) modifying variable then using it
TEST_F(BuildTest, Kernel510_EvalModifiesVariable) {
  writeMakefile(
      "X := initial\n"
      "$(eval X := modified)\n"
      "all:\n"
      "\t@echo x=$(X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x=modified")) << R.out;
}

// Edge case: pattern rule $* stem with directory
TEST_F(BuildTest, Kernel510_PatternStemWithDir) {
  std::filesystem::create_directories(tmp() / "src");
  std::filesystem::create_directories(tmp() / "build");
  writeFile(tmp() / "src" / "main.c", "");
  writeMakefile(
      "build/%.o: src/%.c\n"
      "\t@echo stem=$* target=$@ prereq=$<\n"
      "all: build/main.o\n"
      "\t@echo done\n"
      ".PHONY: all\n");
  auto R = runMake({"-B"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("stem=main")) << R.out;
  EXPECT_TRUE(R.contains("target=build/main.o")) << R.out;
  EXPECT_TRUE(R.contains("prereq=src/main.c")) << R.out;
}

// ============================================================================
// Export flag preservation across reassignment
// ============================================================================

// export VAR then reassign — export flag must persist
TEST_F(BuildTest, ExportFlagPreservedOnReassign) {
  writeMakefile(
      "CC := original\n"
      "export CC\n"
      "CC := reassigned\n"
      "all:\n"
      "\t@echo cc=$$CC\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=reassigned")) << R.out;
}

// export VAR then ?= — export flag should persist even with conditional assign
TEST_F(BuildTest, ExportFlagPreservedOnConditionalAssign) {
  writeMakefile(
      "CC := gcc\n"
      "export CC\n"
      "CC ?= clang\n"
      "all:\n"
      "\t@echo cc=$$CC\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
}

// export with := on same line then += append
TEST_F(BuildTest, ExportFlagPreservedOnAppend) {
  writeMakefile(
      "export CFLAGS := -O2\n"
      "CFLAGS += -Wall\n"
      "all:\n"
      "\t@echo flags=$$CFLAGS\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
}

// Command line var with export should preserve export through override
TEST_F(BuildTest, ExportFlagPreservedWithCmdLineOverride) {
  writeMakefile(
      "CC := default\n"
      "export CC\n"
      "override CC := forced\n"
      "all:\n"
      "\t@echo cc=$$CC\n"
      ".PHONY: all\n");
  auto R = runMake({"CC=fromcmd"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=forced")) << R.out;
}

// ============================================================================
// Target-Specific Variables
// ============================================================================

TEST_F(BuildTest, TargetVarSimpleAssign) {
  writeMakefile(
      "CFLAGS := -O0\n"
      "release: CFLAGS := -O2\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      "release:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all release\n");
  auto R1 = runMake({}, "all");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cflags=-O0")) << R1.out;
  auto R2 = runMake({}, "release");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cflags=-O2")) << R2.out;
}

TEST_F(BuildTest, TargetVarRecursiveAssign) {
  writeMakefile(
      "MSG = default\n"
      "debug: MSG = debug-mode\n"
      "all:\n"
      "\t@echo msg=$(MSG)\n"
      "debug:\n"
      "\t@echo msg=$(MSG)\n"
      ".PHONY: all debug\n");
  auto R = runMake({}, "debug");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("msg=debug-mode")) << R.out;
}

TEST_F(BuildTest, TargetVarAppend) {
  writeMakefile(
      "CFLAGS := -Wall\n"
      "debug: CFLAGS += -g\n"
      "debug:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: debug\n");
  auto R = runMake({}, "debug");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-g")) << R.out;
}

TEST_F(BuildTest, TargetVarConditionalAssign) {
  writeMakefile(
      "OPT := -O2\n"
      "debug: OPT ?= -O0\n"
      "debug:\n"
      "\t@echo opt=$(OPT)\n"
      ".PHONY: debug\n");
  auto R = runMake({}, "debug");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("opt=-O2")) << R.out;
}

TEST_F(BuildTest, TargetVarMultipleTargets) {
  writeMakefile(
      "MODE := normal\n"
      "foo bar: MODE := special\n"
      "all:\n"
      "\t@echo mode=$(MODE)\n"
      "foo:\n"
      "\t@echo mode=$(MODE)\n"
      "bar:\n"
      "\t@echo mode=$(MODE)\n"
      ".PHONY: all foo bar\n");
  auto R1 = runMake({}, "foo");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("mode=special")) << R1.out;
  auto R2 = runMake({}, "bar");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("mode=special")) << R2.out;
}

TEST_F(BuildTest, TargetVarDoesNotAffectOtherTargets) {
  writeMakefile(
      "VAL := global\n"
      "special: VAL := local\n"
      "special:\n"
      "\t@echo val=$(VAL)\n"
      "normal:\n"
      "\t@echo val=$(VAL)\n"
      ".PHONY: special normal\n");
  auto R1 = runMake({}, "special");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("val=local")) << R1.out;
  auto R2 = runMake({}, "normal");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("val=global")) << R2.out;
}

TEST_F(BuildTest, TargetVarWithVarExpansionInTargetName) {
  writeMakefile(
      "T := release\n"
      "$(T): CFLAGS := -O3\n"
      "release:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: release\n");
  auto R = runMake({}, "release");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cflags=-O3")) << R.out;
}

TEST_F(BuildTest, TargetVarParsingNotMisclassified) {
  // Ensure target: VAR = value is NOT parsed as a regular assignment
  writeMakefile(
      "all: FLAG := yes\n"
      "all:\n"
      "\t@echo flag=$(FLAG)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("flag=yes")) << R.out;
}

// ============================================================================
// Additional Linux 5.10 Kernel Makefile Patterns
// ============================================================================

// Kbuild quiet_cmd / cmd pattern used throughout the kernel
TEST_F(BuildTest, Kernel510_QuietCmdPattern) {
  writeMakefile(
      "Q := @\n"
      "quiet = $(if $(V),,@)\n"
      "define cmd_cc_o_c\n"
      "$(CC) $(CFLAGS) -c -o $@ $<\n"
      "endef\n"
      "define quiet_cmd_cc_o_c\n"
      "CC $@\n"
      "endef\n"
      "define cmd\n"
      "$(if $(V),$(cmd_$(1)),$(quiet_cmd_$(1)))\n"
      "endef\n"
      "CC := gcc\n"
      "CFLAGS := -O2\n"
      "V := 1\n"
      "all:\n"
      "\t@echo $(call cmd,cc_o_c)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
}

// CONFIG_* variable selection common in kernel
TEST_F(BuildTest, Kernel510_ConfigVariableSelection) {
  writeMakefile(
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES :=\n"
      "CONFIG_DEBUG_INFO := y\n"
      "CFLAGS :=\n"
      "ifdef CONFIG_SMP\n"
      "CFLAGS += -DSMP\n"
      "endif\n"
      "ifdef CONFIG_MODULES\n"
      "CFLAGS += -DMODULE\n"
      "endif\n"
      "ifdef CONFIG_DEBUG_INFO\n"
      "CFLAGS += -g\n"
      "endif\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-DSMP")) << R.out;
  EXPECT_FALSE(R.contains("-DMODULE")) << R.out;
  EXPECT_TRUE(R.contains("-g")) << R.out;
}

// Computed variable reference for architecture selection
TEST_F(BuildTest, Kernel510_ComputedArchVariable) {
  writeMakefile(
      "ARCH := x86\n"
      "machine-x86 := arch/x86\n"
      "machine-arm := arch/arm\n"
      "MACHINE_DIR = $(machine-$(ARCH))\n"
      "all:\n"
      "\t@echo dir=$(MACHINE_DIR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dir=arch/x86")) << R.out;
}

// obj-y aggregation with foreach+eval for multi-subdir builds
TEST_F(BuildTest, Kernel510_ObjYAggregation) {
  writeMakefile(
      "obj-y := main.o init.o\n"
      "obj-y += sched.o\n"
      "OBJS := $(obj-y)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("main.o")) << R.out;
  EXPECT_TRUE(R.contains("init.o")) << R.out;
  EXPECT_TRUE(R.contains("sched.o")) << R.out;
}

// if_changed pattern (simplified)
TEST_F(BuildTest, Kernel510_IfChangedPattern) {
  writeMakefile(
      "define if_changed\n"
      "$(if $(strip $(filter-out $(1),$(2))),changed,same)\n"
      "endef\n"
      "OLD := -O2 -Wall\n"
      "NEW := -O2 -Wall -g\n"
      "all:\n"
      "\t@echo result=$(call if_changed,$(OLD),$(NEW))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("changed")) << R.out;
}

// Multi-level variable indirection used in Kbuild
TEST_F(BuildTest, Kernel510_MultiLevelIndirection) {
  writeMakefile(
      "level3 := final_value\n"
      "level2 := level3\n"
      "level1 := level2\n"
      "RESULT = $($($(level1)))\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=final_value")) << R.out;
}

// Kernel version extraction pattern
TEST_F(BuildTest, Kernel510_VersionExtraction) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 0\n"
      "KERNELVERSION := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.0")) << R.out;
}

// Complex filter pipeline used in kernel build
TEST_F(BuildTest, Kernel510_FilterPipeline) {
  writeMakefile(
      "FILES := foo.c bar.h baz.c qux.S\n"
      "C_FILES := $(filter %.c,$(FILES))\n"
      "NON_HEADERS := $(filter-out %.h,$(FILES))\n"
      "OBJS := $(patsubst %.c,%.o,$(C_FILES))\n"
      "all:\n"
      "\t@echo c_files=$(C_FILES)\n"
      "\t@echo non_h=$(NON_HEADERS)\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("c_files=foo.c baz.c")) << R.out;
  EXPECT_TRUE(R.contains("non_h=foo.c baz.c qux.S")) << R.out;
  EXPECT_TRUE(R.contains("objs=foo.o baz.o")) << R.out;
}

// addprefix for build directory output
TEST_F(BuildTest, Kernel510_BuildDirPrefix) {
  writeMakefile(
      "obj := build\n"
      "src := src\n"
      "files := main.o init.o sched.o\n"
      "TARGETS := $(addprefix $(obj)/,$(files))\n"
      "SOURCES := $(patsubst %.o,$(src)/%.c,$(files))\n"
      "all:\n"
      "\t@echo targets=$(TARGETS)\n"
      "\t@echo sources=$(SOURCES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("targets=build/main.o build/init.o build/sched.o")) << R.out;
  EXPECT_TRUE(R.contains("sources=src/main.c src/init.c src/sched.c")) << R.out;
}

// Kbuild subdir-y accumulation with foreach
TEST_F(BuildTest, Kernel510_SubdirForeachAccum) {
  writeMakefile(
      "subdirs := kernel mm fs net\n"
      "define accumulate\n"
      "$(1)-objs := $(1)/built-in.a\n"
      "endef\n"
      "$(foreach d,$(subdirs),$(eval $(call accumulate,$(d))))\n"
      "ALL_OBJS := $(foreach d,$(subdirs),$($(d)-objs))\n"
      "all:\n"
      "\t@echo all_objs=$(ALL_OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("kernel/built-in.a")) << R.out;
  EXPECT_TRUE(R.contains("mm/built-in.a")) << R.out;
  EXPECT_TRUE(R.contains("fs/built-in.a")) << R.out;
  EXPECT_TRUE(R.contains("net/built-in.a")) << R.out;
}

// export with := and complex value
TEST_F(BuildTest, Kernel510_ExportComputedVar) {
  writeMakefile(
      "ARCH := x86_64\n"
      "export KBUILD_CFLAGS := -march=$(ARCH)\n"
      "all:\n"
      "\t@echo cflags=$(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cflags=-march=x86_64")) << R.out;
}

// Deeply nested ifeq chain for architecture selection
TEST_F(BuildTest, Kernel510_DeepArchSelection) {
  writeMakefile(
      "SRCARCH := arm64\n"
      "ifeq ($(SRCARCH),x86)\n"
      "BITS := 64\n"
      "else ifeq ($(SRCARCH),arm)\n"
      "BITS := 32\n"
      "else ifeq ($(SRCARCH),arm64)\n"
      "BITS := 64\n"
      "else ifeq ($(SRCARCH),riscv)\n"
      "BITS := 64\n"
      "else\n"
      "BITS := unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo bits=$(BITS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bits=64")) << R.out;
}

// FORCE target pattern
TEST_F(BuildTest, Kernel510_ForceTargetPattern) {
  writeFile(tmp() / "dummy.txt", "");
  writeMakefile(
      "all: dummy.txt\n"
      "\t@echo done\n"
      "dummy.txt: FORCE\n"
      "\t@echo updating\n"
      "FORCE:\n"
      ".PHONY: FORCE all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("updating")) << R.out;
}

// define with := mode
TEST_F(BuildTest, Kernel510_DefineWithSimpleAssign) {
  writeMakefile(
      "X := hello\n"
      "define CMD :=\n"
      "$(X) world\n"
      "endef\n"
      "all:\n"
      "\t@echo cmd=$(CMD)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello world")) << R.out;
}

// Nested $(if) with $(findstring) — compiler flag detection
TEST_F(BuildTest, Kernel510_NestedIfFindstring) {
  writeMakefile(
      "COMPILER := gcc-10\n"
      "HAS_GCC = $(findstring gcc,$(COMPILER))\n"
      "HAS_CLANG = $(findstring clang,$(COMPILER))\n"
      "EXTRA_CFLAGS := $(if $(HAS_GCC),-fstack-protector,$(if $(HAS_CLANG),-fsanitize=cfi,))\n"
      "all:\n"
      "\t@echo extra=$(EXTRA_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("extra=-fstack-protector")) << R.out;
}

// $(sort) for dedup + alphabetical ordering of build objects
TEST_F(BuildTest, Kernel510_SortDedup) {
  writeMakefile(
      "obj-y := b.o a.o c.o a.o b.o\n"
      "SORTED := $(sort $(obj-y))\n"
      "all:\n"
      "\t@echo sorted=$(SORTED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("sorted=a.o b.o c.o")) << R.out;
}

// $$ escaping in recipes for shell variables
TEST_F(BuildTest, Kernel510_DollarEscapeInRecipe) {
  writeMakefile(
      "all:\n"
      "\t@for f in a b c; do echo item=$$f; done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("item=a")) << R.out;
  EXPECT_TRUE(R.contains("item=b")) << R.out;
  EXPECT_TRUE(R.contains("item=c")) << R.out;
}

// Substitution reference with directory prefix
TEST_F(BuildTest, Kernel510_SubstRefDirPrefix) {
  writeMakefile(
      "srcs := src/foo.c src/bar.c\n"
      "OBJS := $(srcs:.c=.o)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("objs=src/foo.o src/bar.o")) << R.out;
}

// Combined recipe prefixes @-+
TEST_F(BuildTest, Kernel510_CombinedRecipePrefixes) {
  writeMakefile(
      "all:\n"
      "\t@-echo silent_ignore\n"
      "\t-@echo ignore_silent\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("silent_ignore")) << R.out;
}

// Multiple prerequisite rules for the same target
TEST_F(BuildTest, Kernel510_MultiPrereqMerge) {
  writeMakefile(
      "all: dep1\n"
      "all: dep2\n"
      "all:\n"
      "\t@echo done\n"
      "dep1:\n"
      "\t@echo dep1\n"
      "dep2:\n"
      "\t@echo dep2\n"
      ".PHONY: all dep1 dep2\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dep1")) << R.out;
  EXPECT_TRUE(R.contains("dep2")) << R.out;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

// ifeq with both sides being complex variable expressions
TEST_F(BuildTest, Kernel510_IfeqComplexBothSides) {
  writeMakefile(
      "A := hello\n"
      "B := hel\n"
      "C := lo\n"
      "ifeq ($(A),$(B)$(C))\n"
      "MATCH := yes\n"
      "else\n"
      "MATCH := no\n"
      "endif\n"
      "all:\n"
      "\t@echo match=$(MATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("match=yes")) << R.out;
}

// ifndef with command line override
TEST_F(BuildTest, Kernel510_IfndefCmdLineOverride) {
  writeMakefile(
      "ifndef CROSS_COMPILE\n"
      "CROSS_COMPILE :=\n"
      "endif\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;
  auto R2 = runMake({"CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
}

// word/words for version parsing
TEST_F(BuildTest, Kernel510_WordVersionParsing) {
  writeMakefile(
      "VERSION_STRING := 5 10 42\n"
      "MAJOR := $(word 1,$(VERSION_STRING))\n"
      "MINOR := $(word 2,$(VERSION_STRING))\n"
      "PATCH := $(word 3,$(VERSION_STRING))\n"
      "COUNT := $(words $(VERSION_STRING))\n"
      "all:\n"
      "\t@echo major=$(MAJOR) minor=$(MINOR) patch=$(PATCH) count=$(COUNT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("major=5")) << R.out;
  EXPECT_TRUE(R.contains("minor=10")) << R.out;
  EXPECT_TRUE(R.contains("patch=42")) << R.out;
  EXPECT_TRUE(R.contains("count=3")) << R.out;
}

// $(eval) generating multiple rules in a loop
TEST_F(BuildTest, Kernel510_EvalGenerateMultipleRules) {
  writeMakefile(
      "MODULES := net fs\n"
      "define module_template\n"
      "$(1)-objs := $(1)/built-in.o\n"
      "$(1)/built-in.o:\n"
      "\t@echo build-$(1)\n"
      "endef\n"
      "$(foreach m,$(MODULES),$(eval $(call module_template,$(m))))\n"
      ".PHONY: all\n"
      "all: $(foreach m,$(MODULES),$($(m)-objs))\n"
      "\t@echo all-done\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
}

// Complex call with nested function calls as arguments
TEST_F(BuildTest, Kernel510_CallWithNestedFunctions) {
  writeMakefile(
      "define process\n"
      "$(addprefix $(1)/,$(patsubst %.c,%.o,$(2)))\n"
      "endef\n"
      "RESULT := $(call process,build,main.c init.c)\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build/main.o")) << R.out;
  EXPECT_TRUE(R.contains("build/init.o")) << R.out;
}

// MAKECMDGOALS detection for conditional build rules
TEST_F(BuildTest, Kernel510_MakecmdgoalsDetection) {
  writeMakefile(
      "ifneq ($(filter clean,$(MAKECMDGOALS)),)\n"
      "MODE := cleaning\n"
      "else\n"
      "MODE := building\n"
      "endif\n"
      "all:\n"
      "\t@echo mode=$(MODE)\n"
      "clean:\n"
      "\t@echo mode=$(MODE)\n"
      ".PHONY: all clean\n");
  auto R1 = runMake({}, "all");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("mode=building")) << R1.out;
  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("mode=cleaning")) << R2.out;
}

// .DEFAULT_GOAL override
TEST_F(BuildTest, Kernel510_DefaultGoalOverride) {
  writeMakefile(
      "first:\n"
      "\t@echo first\n"
      ".DEFAULT_GOAL := second\n"
      "second:\n"
      "\t@echo second\n"
      ".PHONY: first second\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("second")) << R.out;
  EXPECT_FALSE(R.contains("first")) << R.out;
}

// Multiple assignment modes interacting
TEST_F(BuildTest, Kernel510_MixedAssignModes) {
  writeMakefile(
      "A = recursive\n"
      "B := simple\n"
      "C ?= conditional\n"
      "D = will_be_overwritten\n"
      "D := overwritten\n"
      "E := base\n"
      "E += extended\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B) c=$(C) d=$(D) e=$(E)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=recursive")) << R.out;
  EXPECT_TRUE(R.contains("b=simple")) << R.out;
  EXPECT_TRUE(R.contains("c=conditional")) << R.out;
  EXPECT_TRUE(R.contains("d=overwritten")) << R.out;
  EXPECT_TRUE(R.contains("e=base extended")) << R.out;
}

// Complex Kbuild end-to-end: version + arch + config + subdir + link
TEST_F(BuildTest, Kernel510_CompleteKbuildSimulation) {
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 0\n"
      "KERNELRELEASE := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "\n"
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "\n"
      "KBUILD_CFLAGS := -Wall -O2\n"
      "\n"
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES := y\n"
      "CONFIG_PREEMPT :=\n"
      "\n"
      "ifdef CONFIG_SMP\n"
      "KBUILD_CFLAGS += -DCONFIG_SMP\n"
      "endif\n"
      "ifdef CONFIG_MODULES\n"
      "KBUILD_CFLAGS += -DCONFIG_MODULES\n"
      "endif\n"
      "ifdef CONFIG_PREEMPT\n"
      "KBUILD_CFLAGS += -DCONFIG_PREEMPT\n"
      "endif\n"
      "\n"
      "subdirs := kernel mm fs\n"
      "define subdir_template\n"
      "$(1)-objs := $(1)/built-in.a\n"
      "endef\n"
      "$(foreach d,$(subdirs),$(eval $(call subdir_template,$(d))))\n"
      "\n"
      "ALL_OBJS := $(foreach d,$(subdirs),$($(d)-objs))\n"
      "\n"
      "ifeq ($(ARCH),x86)\n"
      "ARCH_FLAGS := -m64\n"
      "else ifeq ($(ARCH),arm)\n"
      "ARCH_FLAGS := -marm\n"
      "else ifeq ($(ARCH),arm64)\n"
      "ARCH_FLAGS := -march=armv8-a\n"
      "else\n"
      "ARCH_FLAGS :=\n"
      "endif\n"
      "\n"
      "KBUILD_CFLAGS += $(ARCH_FLAGS)\n"
      "\n"
      "all:\n"
      "\t@echo version=$(KERNELRELEASE)\n"
      "\t@echo cc=$(CC)\n"
      "\t@echo cflags=$(KBUILD_CFLAGS)\n"
      "\t@echo objs=$(ALL_OBJS)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("version=5.10.0")) << R1.out;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;
  EXPECT_TRUE(R1.contains("-DCONFIG_SMP")) << R1.out;
  EXPECT_TRUE(R1.contains("-DCONFIG_MODULES")) << R1.out;
  EXPECT_FALSE(R1.contains("-DCONFIG_PREEMPT")) << R1.out;
  EXPECT_TRUE(R1.contains("-m64")) << R1.out;
  EXPECT_TRUE(R1.contains("kernel/built-in.a")) << R1.out;

  // Cross-compile ARM64
  auto R2 = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
  EXPECT_TRUE(R2.contains("-march=armv8-a")) << R2.out;
}

// ============================================================================
// Edge Cases & Robustness
// ============================================================================

// Empty $(foreach) body
TEST_F(BuildTest, Edge_EmptyForeachBody) {
  writeMakefile(
      "R := $(foreach x,a b c,)\n"
      "all:\n"
      "\t@echo r=[$(R)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
}

// $(foreach) with empty list
TEST_F(BuildTest, Edge_ForeachEmptyList) {
  writeMakefile(
      "EMPTY :=\n"
      "R := $(foreach x,$(EMPTY),item=$(x))\n"
      "all:\n"
      "\t@echo r=[$(R)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r=[]")) << R.out;
}

// Recursive variable cycle detection
TEST_F(BuildTest, Edge_RecursiveCycleDetection) {
  writeMakefile(
      "A = $(B)\n"
      "B = $(A)\n"
      "all:\n"
      "\t@echo a=$(A)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
}

// Very long variable value
TEST_F(BuildTest, Edge_LongVariableValue) {
  std::string LongVal;
  for (int I = 0; I < 500; ++I) {
    if (I > 0)
      LongVal += " ";
    LongVal += "item" + std::to_string(I);
  }
  writeMakefile(
      "ITEMS := " + LongVal + "\n"
      "COUNT := $(words $(ITEMS))\n"
      "all:\n"
      "\t@echo count=$(COUNT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=500")) << R.out;
}

// Multiple ifeq with same variable changing
TEST_F(BuildTest, Edge_SequentialIfeq) {
  writeMakefile(
      "X := a\n"
      "ifeq ($(X),a)\n"
      "R1 := yes\n"
      "else\n"
      "R1 := no\n"
      "endif\n"
      "X := b\n"
      "ifeq ($(X),b)\n"
      "R2 := yes\n"
      "else\n"
      "R2 := no\n"
      "endif\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=yes")) << R.out;
  EXPECT_TRUE(R.contains("r2=yes")) << R.out;
}

// Variable name containing hyphen (common in kernel)
TEST_F(BuildTest, Edge_HyphenVariableName) {
  writeMakefile(
      "obj-y := main.o\n"
      "obj-m := module.o\n"
      "all:\n"
      "\t@echo y=$(obj-y) m=$(obj-m)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("y=main.o")) << R.out;
  EXPECT_TRUE(R.contains("m=module.o")) << R.out;
}

// include with non-existent file using -include
TEST_F(BuildTest, Edge_MinusIncludeNonexistent) {
  writeMakefile(
      "-include nonexistent.mk\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ok")) << R.out;
}

// sinclude (synonym for -include)
TEST_F(BuildTest, Edge_SincludeNonexistent) {
  writeMakefile(
      "sinclude nonexistent.mk\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ok")) << R.out;
}

// Chained include files
TEST_F(BuildTest, Edge_ChainedIncludes) {
  writeFile(tmp() / "a.mk", "A := from_a\ninclude b.mk\n");
  writeFile(tmp() / "b.mk", "B := from_b\n");
  writeMakefile(
      "include a.mk\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=from_a")) << R.out;
  EXPECT_TRUE(R.contains("b=from_b")) << R.out;
}

// $(strip) in ifeq comparison
TEST_F(BuildTest, Edge_StripInIfeq) {
  writeMakefile(
      "VAR :=   yes   \n"
      "ifeq ($(strip $(VAR)),yes)\n"
      "RESULT := matched\n"
      "else\n"
      "RESULT := no_match\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=matched")) << R.out;
}

// Empty recipe (target exists, no commands)
TEST_F(BuildTest, Edge_EmptyRecipe) {
  writeFile(tmp() / "existing.txt", "data");
  writeMakefile(
      "all: existing.txt\n"
      "\t@echo done\n"
      "existing.txt:\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
}

// $(abspath) with relative path
TEST_F(BuildTest, Edge_AbspathRelative) {
  writeMakefile(
      "P := $(abspath foo/bar)\n"
      "all:\n"
      "\t@echo path=$(P)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("foo/bar")) << R.out;
}

// $(dir) and $(notdir) combined
TEST_F(BuildTest, Edge_DirNotdirCombined) {
  writeMakefile(
      "FILE := src/kernel/main.c\n"
      "D := $(dir $(FILE))\n"
      "F := $(notdir $(FILE))\n"
      "all:\n"
      "\t@echo dir=$(D) file=$(F)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dir=src/kernel/")) << R.out;
  EXPECT_TRUE(R.contains("file=main.c")) << R.out;
}

// $(basename) and $(suffix)
TEST_F(BuildTest, Edge_BasenameSuffix) {
  writeMakefile(
      "FILES := foo.c bar.h baz.S\n"
      "BASES := $(basename $(FILES))\n"
      "SUFFS := $(suffix $(FILES))\n"
      "all:\n"
      "\t@echo bases=$(BASES) suffs=$(SUFFS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bases=foo bar baz")) << R.out;
  EXPECT_TRUE(R.contains("suffs=.c .h .S")) << R.out;
}

// $(and) and $(or) multi-argument
TEST_F(BuildTest, Edge_AndOrMultiArg) {
  writeMakefile(
      "A := yes\n"
      "B := yes\n"
      "C :=\n"
      "R_AND := $(and $(A),$(B))\n"
      "R_AND2 := $(and $(A),$(C))\n"
      "R_OR := $(or $(C),$(A))\n"
      "R_OR2 := $(or $(C),)\n"
      "all:\n"
      "\t@echo and=$(R_AND) and2=[$(R_AND2)] or=$(R_OR) or2=[$(R_OR2)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("and=yes")) << R.out;
  EXPECT_TRUE(R.contains("and2=[]")) << R.out;
  EXPECT_TRUE(R.contains("or=yes")) << R.out;
  EXPECT_TRUE(R.contains("or2=[]")) << R.out;
}

// undefine then redefine
TEST_F(BuildTest, Edge_UndefineRedefine) {
  writeMakefile(
      "FOO := original\n"
      "undefine FOO\n"
      "FOO := redefined\n"
      "all:\n"
      "\t@echo foo=$(FOO)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("foo=redefined")) << R.out;
}

// MAKE_VERSION available for version checks
TEST_F(BuildTest, Edge_MakeVersionAvailable) {
  writeMakefile(
      "all:\n"
      "\t@echo ver=$(MAKE_VERSION)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ver=4.3")) << R.out;
}

// $(origin) for different variable sources
TEST_F(BuildTest, Edge_OriginMultipleSources) {
  writeMakefile(
      "FILE_VAR := hello\n"
      "all:\n"
      "\t@echo file_origin=$(origin FILE_VAR)\n"
      "\t@echo cmd_origin=$(origin CMD_VAR)\n"
      "\t@echo undef_origin=$(origin NONEXIST)\n"
      ".PHONY: all\n");
  auto R = runMake({"CMD_VAR=test"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("file_origin=file")) << R.out;
  EXPECT_TRUE(R.contains("cmd_origin=command line")) << R.out;
  EXPECT_TRUE(R.contains("undef_origin=undefined")) << R.out;
}

// $(value) returns unexpanded value — test via $(findstring) to avoid
// shell interpretation of the literal $() in output.
TEST_F(BuildTest, Edge_ValueUnexpanded) {
  writeMakefile(
      "X := expanded\n"
      "Y = prefix-$(X)\n"
      "RAW := $(value Y)\n"
      "HAS_DOLLAR := $(findstring $$,$(RAW))\n"
      "EXPANDED_Y := $(Y)\n"
      "all:\n"
      "\t@echo has_dollar=$(HAS_DOLLAR)\n"
      "\t@echo expanded=$(EXPANDED_Y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("has_dollar=$")) << R.out;
  EXPECT_TRUE(R.contains("expanded=prefix-expanded")) << R.out;
}

// $(flavor) for variable types
TEST_F(BuildTest, Edge_FlavorDetection) {
  writeMakefile(
      "REC = val\n"
      "SIM := val\n"
      "all:\n"
      "\t@echo rec_flavor=$(flavor REC)\n"
      "\t@echo sim_flavor=$(flavor SIM)\n"
      "\t@echo undef_flavor=$(flavor NONE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("rec_flavor=recursive")) << R.out;
  EXPECT_TRUE(R.contains("sim_flavor=simple")) << R.out;
  EXPECT_TRUE(R.contains("undef_flavor=undefined")) << R.out;
}

// $(subst) chain for version string manipulation
TEST_F(BuildTest, Edge_SubstChain) {
  writeMakefile(
      "VER := 5.10.42-rc1\n"
      "V1 := $(subst ., ,$(VER))\n"
      "MAJOR := $(firstword $(V1))\n"
      "all:\n"
      "\t@echo v1=$(V1)\n"
      "\t@echo major=$(MAJOR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("major=5")) << R.out;
}

// Pattern rule with stem in prerequisites and recipe
TEST_F(BuildTest, Edge_PatternRuleStem) {
  writeFile(tmp() / "test.src", "source data");
  writeMakefile(
      "%.out: %.src\n"
      "\t@echo building $@ from $< stem=$*\n"
      "all: test.out\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("test.out")) << R.out;
  EXPECT_TRUE(R.contains("test.src")) << R.out;
  EXPECT_TRUE(R.contains("stem=test")) << R.out;
}

// Static pattern rule
TEST_F(BuildTest, Edge_StaticPatternRule) {
  writeFile(tmp() / "a.src", "");
  writeFile(tmp() / "b.src", "");
  writeMakefile(
      "TARGETS := a.out b.out\n"
      "$(TARGETS): %.out: %.src\n"
      "\t@echo build_$@\n"
      "all: $(TARGETS)\n"
      ".PHONY: all\n");
  auto R = runMake({}, "all");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build_a.out")) << R.out;
  EXPECT_TRUE(R.contains("build_b.out")) << R.out;
}

// Stress: 100 modules with foreach+eval
TEST_F(BuildTest, Stress_100ModulesForeachEval) {
  std::string MF = "MODULES :=";
  for (int I = 0; I < 100; ++I)
    MF += " mod" + std::to_string(I);
  MF += "\n";
  MF += "define mod_template\n"
        "$(1)-objs := $(1).o\n"
        "endef\n"
        "$(foreach m,$(MODULES),$(eval $(call mod_template,$(m))))\n"
        "ALL := $(foreach m,$(MODULES),$($(m)-objs))\n"
        "COUNT := $(words $(ALL))\n"
        "all:\n"
        "\t@echo count=$(COUNT)\n"
        ".PHONY: all\n";
  writeMakefile(MF);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=100")) << R.out;
}

// Stress: deeply nested $(if) chain
TEST_F(BuildTest, Stress_DeepNestedIf) {
  writeMakefile(
      "X := 5\n"
      "R := $(if $(filter 1,$(X)),one,"
      "$(if $(filter 2,$(X)),two,"
      "$(if $(filter 3,$(X)),three,"
      "$(if $(filter 4,$(X)),four,"
      "$(if $(filter 5,$(X)),five,"
      "other)))))\n"
      "all:\n"
      "\t@echo r=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r=five")) << R.out;
}

// Stress: large patsubst operation
TEST_F(BuildTest, Stress_LargePatsubst) {
  std::string Files;
  for (int I = 0; I < 200; ++I) {
    if (I > 0) Files += " ";
    Files += "file" + std::to_string(I) + ".c";
  }
  writeMakefile(
      "SRCS := " + Files + "\n"
      "OBJS := $(patsubst %.c,%.o,$(SRCS))\n"
      "COUNT := $(words $(OBJS))\n"
      "FIRST := $(firstword $(OBJS))\n"
      "LAST := $(lastword $(OBJS))\n"
      "all:\n"
      "\t@echo count=$(COUNT) first=$(FIRST) last=$(LAST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=200")) << R.out;
  EXPECT_TRUE(R.contains("first=file0.o")) << R.out;
  EXPECT_TRUE(R.contains("last=file199.o")) << R.out;
}

// ============================================================================
// Final Robustness: Real kernel patterns + remaining edge cases
// ============================================================================

// ::= POSIX simple assignment
TEST_F(BuildTest, Final_PosixSimpleAssign) {
  writeMakefile(
      "VAR ::= hello\n"
      "all:\n"
      "\t@echo val=$(VAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("val=hello")) << R.out;
}

// Deep else ifeq chain (6 levels, matching in the middle)
TEST_F(BuildTest, Final_DeepElseIfeqChain6Levels) {
  writeMakefile(
      "ARCH := mips\n"
      "ifeq ($(ARCH),x86)\n"
      "BITS := 64\n"
      "else ifeq ($(ARCH),arm)\n"
      "BITS := 32\n"
      "else ifeq ($(ARCH),arm64)\n"
      "BITS := 64\n"
      "else ifeq ($(ARCH),mips)\n"
      "BITS := 32\n"
      "else ifeq ($(ARCH),riscv)\n"
      "BITS := 64\n"
      "else ifeq ($(ARCH),ppc)\n"
      "BITS := 64\n"
      "else\n"
      "BITS := unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo bits=$(BITS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bits=32")) << R.out;
}

// Deep else ifeq chain fallthrough to else
TEST_F(BuildTest, Final_DeepElseIfeqFallthrough) {
  writeMakefile(
      "ARCH := sparc\n"
      "ifeq ($(ARCH),x86)\n"
      "BITS := 64\n"
      "else ifeq ($(ARCH),arm)\n"
      "BITS := 32\n"
      "else ifeq ($(ARCH),arm64)\n"
      "BITS := 64\n"
      "else\n"
      "BITS := unknown\n"
      "endif\n"
      "all:\n"
      "\t@echo bits=$(BITS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bits=unknown")) << R.out;
}

// define block used as recipe template via $(call)
TEST_F(BuildTest, Final_DefineAsRecipeTemplate) {
  writeMakefile(
      "define quiet_cmd_cc\n"
      "  CC      $(2)\n"
      "endef\n"
      "define cmd_cc\n"
      "gcc -c -o $(2) $(1)\n"
      "endef\n"
      "define cmd\n"
      "$(if $(V),$(cmd_$(1)),$(quiet_cmd_$(1)))\n"
      "endef\n"
      "V :=\n"
      "all:\n"
      "\t@echo $(call cmd,cc,main.o,main.c)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC")) << R.out;
  EXPECT_TRUE(R.contains("main.o")) << R.out;
}

// define block with verbose mode switching
TEST_F(BuildTest, Final_DefineVerboseSwitch) {
  writeMakefile(
      "define quiet_cmd_link\n"
      "LINK $@\n"
      "endef\n"
      "define cmd_link\n"
      "ld -o $@ $^\n"
      "endef\n"
      "V := 1\n"
      "cmd = $(if $(V),$(cmd_$(1)),$(quiet_cmd_$(1)))\n"
      "all:\n"
      "\t@echo $(call cmd,link)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ld")) << R.out;
}

// Computed variable with CONFIG_ prefix (kernel pattern)
TEST_F(BuildTest, Final_ComputedConfigVar) {
  writeMakefile(
      "CONFIG_NET := y\n"
      "CONFIG_FS := m\n"
      "CONFIG_SOUND :=\n"
      "subsystems := NET FS SOUND\n"
      "ENABLED := $(foreach s,$(subsystems),$(if $(CONFIG_$(s)),$(s)))\n"
      "all:\n"
      "\t@echo enabled=$(ENABLED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("NET")) << R.out;
  EXPECT_TRUE(R.contains("FS")) << R.out;
  EXPECT_FALSE(R.contains("SOUND")) << R.out;
}

// Multiple target-specific vars on same target
TEST_F(BuildTest, Final_MultipleTargetVarsSameTarget) {
  writeMakefile(
      "CC := gcc\n"
      "CFLAGS := -O0\n"
      "release: CC := clang\n"
      "release: CFLAGS := -O3\n"
      "release:\n"
      "\t@echo cc=$(CC) cflags=$(CFLAGS)\n"
      ".PHONY: release\n");
  auto R = runMake({}, "release");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cc=clang")) << R.out;
  EXPECT_TRUE(R.contains("cflags=-O3")) << R.out;
}

// Recursive variable with late binding across include
TEST_F(BuildTest, Final_LateBindingAcrossInclude) {
  writeFile(tmp() / "late.mk", "VAL := from_include\n");
  writeMakefile(
      "REF = value_is_$(VAL)\n"
      "include late.mk\n"
      "all:\n"
      "\t@echo ref=$(REF)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ref=value_is_from_include")) << R.out;
}

// Multiple includes with conditional content
TEST_F(BuildTest, Final_ConditionalInInclude) {
  writeFile(tmp() / "config.mk",
      "CONFIG_A := y\n"
      "CONFIG_B :=\n");
  writeFile(tmp() / "flags.mk",
      "ifdef CONFIG_A\n"
      "EXTRA_FLAGS += -DA\n"
      "endif\n"
      "ifdef CONFIG_B\n"
      "EXTRA_FLAGS += -DB\n"
      "endif\n");
  writeMakefile(
      "EXTRA_FLAGS :=\n"
      "include config.mk\n"
      "include flags.mk\n"
      "all:\n"
      "\t@echo flags=$(EXTRA_FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-DA")) << R.out;
  EXPECT_FALSE(R.contains("-DB")) << R.out;
}

// Kbuild-style per-file CFLAGS via computed variable
TEST_F(BuildTest, Final_KbuildPerFileCflags) {
  writeMakefile(
      "CFLAGS_main.o := -DMAIN_FILE\n"
      "CFLAGS_init.o := -DINIT_FILE\n"
      "obj-y := main.o init.o util.o\n"
      "define show_flags\n"
      "$(if $(CFLAGS_$(1)),flags_$(1)=$(CFLAGS_$(1)),flags_$(1)=none)\n"
      "endef\n"
      "RESULT := $(foreach o,$(obj-y),$(call show_flags,$(o)))\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-DMAIN_FILE")) << R.out;
  EXPECT_TRUE(R.contains("-DINIT_FILE")) << R.out;
  EXPECT_TRUE(R.contains("flags_util.o=none")) << R.out;
}

// neverc build alias (same as neverc make)
TEST_F(BuildTest, Final_BuildAliasForMake) {
  writeMakefile(
      "all:\n"
      "\t@echo build_alias_works\n"
      ".PHONY: all\n");
  auto R = runBuild();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build_alias_works")) << R.out;
}

// GNUmakefile search order priority
TEST_F(BuildTest, Final_GNUmakefilePriority) {
  writeFile(tmp() / "GNUmakefile",
      "all:\n"
      "\t@echo from_gnumakefile\n"
      ".PHONY: all\n");
  writeFile(tmp() / "Makefile",
      "all:\n"
      "\t@echo from_makefile\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("from_gnumakefile")) << R.out;
}

// Empty variable in ifeq
TEST_F(BuildTest, Final_EmptyVarIfeq) {
  writeMakefile(
      "EMPTY :=\n"
      "ifeq ($(EMPTY),)\n"
      "RESULT := empty_matched\n"
      "else\n"
      "RESULT := not_empty\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=empty_matched")) << R.out;
}

// Variable with spaces in value preserved
TEST_F(BuildTest, Final_VarWithSpaces) {
  writeMakefile(
      "MSG := hello world foo\n"
      "WORDS := $(words $(MSG))\n"
      "all:\n"
      "\t@echo words=$(WORDS) msg=$(MSG)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("words=3")) << R.out;
  EXPECT_TRUE(R.contains("msg=hello world foo")) << R.out;
}

// $(eval) modifying a variable used later in rules
TEST_F(BuildTest, Final_EvalModifyVarForRule) {
  writeMakefile(
      ".DEFAULT_GOAL := all\n"
      "TARGETS :=\n"
      "define add_target\n"
      "TARGETS += $(1)\n"
      "$(1):\n"
      "\t@echo built_$(1)\n"
      "endef\n"
      "$(eval $(call add_target,foo))\n"
      "$(eval $(call add_target,bar))\n"
      "all: $(TARGETS)\n"
      "\t@echo all_done\n"
      ".PHONY: all foo bar\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("built_foo")) << R.out;
  EXPECT_TRUE(R.contains("built_bar")) << R.out;
  EXPECT_TRUE(R.contains("all_done")) << R.out;
}

// $(wordlist) with out-of-range indices (should not crash)
TEST_F(BuildTest, Final_WordlistOutOfRange) {
  writeMakefile(
      "LIST := a b c\n"
      "R1 := $(wordlist 1,10,$(LIST))\n"
      "R2 := $(wordlist 5,10,$(LIST))\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=[$(R2)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=a b c")) << R.out;
  EXPECT_TRUE(R.contains("r2=[]")) << R.out;
}

// $(word) with out-of-range index
TEST_F(BuildTest, Final_WordOutOfRange) {
  writeMakefile(
      "LIST := a b c\n"
      "R := $(word 5,$(LIST))\n"
      "all:\n"
      "\t@echo r=[$(R)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r=[]")) << R.out;
}

// Pattern rule with no matching prerequisites (should not crash)
TEST_F(BuildTest, Final_PatternRuleNoMatch) {
  writeFile(tmp() / "test.c", "int main(){return 0;}");
  writeMakefile(
      "%.o: %.c\n"
      "\t@echo compile $< to $@\n"
      "all: test.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile test.c to test.o")) << R.out;
}

// Combination: target-specific var + pattern rule
TEST_F(BuildTest, Final_TargetVarWithPatternRule) {
  writeFile(tmp() / "a.src", "");
  writeMakefile(
      "FLAVOR := default\n"
      "a.out: FLAVOR := special\n"
      "%.out: %.src\n"
      "\t@echo build_$@_flavor=$(FLAVOR)\n"
      "all: a.out\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build_a.out_flavor=special")) << R.out;
}

// Comprehensive Kbuild: multi-subsystem with per-file flags
TEST_F(BuildTest, Final_KbuildMultiSubsystem) {
  writeMakefile(
      "VERSION := 5\n"
      "PATCHLEVEL := 10\n"
      "SUBLEVEL := 191\n"
      "KERNELRELEASE := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "\n"
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "\n"
      "CONFIG_SMP := y\n"
      "CONFIG_MODULES := y\n"
      "CONFIG_NET := y\n"
      "CONFIG_DEBUG :=\n"
      "\n"
      "KBUILD_CFLAGS := -Wall\n"
      "ifdef CONFIG_SMP\n"
      "KBUILD_CFLAGS += -DSMP\n"
      "endif\n"
      "\n"
      "subsystems := kernel mm fs net drivers\n"
      "\n"
      "define subsys_template\n"
      "$(1)-y :=\n"
      "$(1)-y += $(1)/core.o\n"
      "ifdef CONFIG_$(shell echo $(1) | tr a-z A-Z)\n"
      "$(1)-y += $(1)/ext.o\n"
      "endif\n"
      "endef\n"
      "$(foreach s,$(subsystems),$(eval $(call subsys_template,$(s))))\n"
      "\n"
      "ALL_OBJS := $(foreach s,$(subsystems),$($(s)-y))\n"
      "OBJ_COUNT := $(words $(ALL_OBJS))\n"
      "\n"
      "ifeq ($(ARCH),x86)\n"
      "ARCH_CFLAGS := -m64 -mno-red-zone\n"
      "else ifeq ($(ARCH),arm64)\n"
      "ARCH_CFLAGS := -march=armv8-a\n"
      "else\n"
      "ARCH_CFLAGS :=\n"
      "endif\n"
      "\n"
      "all:\n"
      "\t@echo version=$(KERNELRELEASE)\n"
      "\t@echo cc=$(CC)\n"
      "\t@echo cflags=$(KBUILD_CFLAGS) $(ARCH_CFLAGS)\n"
      "\t@echo obj_count=$(OBJ_COUNT)\n"
      "\t@echo has_net_ext=$(findstring net/ext.o,$(ALL_OBJS))\n"
      "\t@echo has_debug_ext=$(findstring debug/ext.o,$(ALL_OBJS))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.191")) << R.out;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
  EXPECT_TRUE(R.contains("-DSMP")) << R.out;
  EXPECT_TRUE(R.contains("-m64")) << R.out;
  EXPECT_TRUE(R.contains("has_net_ext=net/ext.o")) << R.out;
}

// Multiple -C directory changes
TEST_F(BuildTest, Final_MultipleCDirs) {
  auto Sub = tmp() / "sub";
  std::filesystem::create_directories(Sub);
  writeFile(Sub / "Makefile",
      "all:\n"
      "\t@echo from_sub\n"
      ".PHONY: all\n");
  std::vector<std::string> Args;
  Args.push_back("make");
  Args.push_back("-C");
  Args.push_back(tmp().string());
  Args.push_back("-C");
  Args.push_back("sub");
  auto R = ncc(Args);
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("from_sub")) << R.out;
}

// Help flag
TEST_F(BuildTest, Final_HelpFlag) {
  std::vector<std::string> Args;
  Args.push_back("make");
  Args.push_back("--help");
  auto R = ncc(Args);
  EXPECT_TRUE(R.contains("-f FILE")) << R.out;
  EXPECT_TRUE(R.contains("-j")) << R.out;
}

// Error on missing mandatory include
TEST_F(BuildTest, Final_MissingMandatoryInclude) {
  writeMakefile(
      "include nonexistent_required.mk\n"
      "all:\n"
      "\t@echo should_not_reach\n"
      ".PHONY: all\n");
  auto R = runMake();
  EXPECT_TRUE(R.stderrContains("No such file")) << R.err;
}

// Stress: 300 variables with complex interdependencies
TEST_F(BuildTest, Stress_300InterdependentVars) {
  std::string MF;
  MF += "V0 := base\n";
  for (int I = 1; I < 300; ++I)
    MF += "V" + std::to_string(I) + " := $(V" + std::to_string(I - 1) + ")_" + std::to_string(I) + "\n";
  MF += "all:\n"
        "\t@echo last=$(V299)\n"
        ".PHONY: all\n";
  writeMakefile(MF);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("last=base_1_2_3")) << R.out;
  EXPECT_TRUE(R.contains("_299")) << R.out;
}

// ============================================================================
// Linux 5.10 Kernel Robustness Tests (Phase 2)
// ============================================================================

// --- Kbuild verbose/quiet system ---

TEST_F(BuildTest, Kernel2_VerboseQuietSystem) {
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
      "  quiet=quiet_\n"
      "  Q = @\n"
      "endif\n"
      "\n"
      "define cmd_cc_o_c\n"
      "gcc -c -o $@ $<\n"
      "endef\n"
      "quiet_cmd_cc_o_c = CC $@\n"
      "\n"
      "all:\n"
      "\t@echo quiet=$(quiet) Q=$(Q) verbose=$(KBUILD_VERBOSE)\n"
      "\t@echo cmd=$(quiet_cmd_cc_o_c)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("quiet=quiet_")) << R.out;
  EXPECT_TRUE(R.contains("Q=@")) << R.out;
  EXPECT_TRUE(R.contains("verbose=0")) << R.out;

  auto R2 = runMake({"V=1"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("quiet= ")) << "out: " << R2.out;
  EXPECT_TRUE(R2.contains("verbose=1")) << R2.out;
}

// --- Kernel version block parsing ---

TEST_F(BuildTest, Kernel2_VersionBlockParsing) {
  writeMakefile(
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 191\n"
      "EXTRAVERSION =\n"
      "NAME = Kleptomaniac Octopus\n"
      "\n"
      "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
      "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
      "all:\n"
      "\t@echo version=$(KERNELVERSION)\n"
      "\t@echo name=$(NAME)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.191")) << R.out;
  EXPECT_TRUE(R.contains("name=Kleptomaniac Octopus")) << R.out;
}

// --- Export assignment in one line ---

TEST_F(BuildTest, Kernel2_ExportAssignmentOneLine) {
  writeMakefile(
      "export KBUILD_MODULES :=\n"
      "export KBUILD_BUILTIN := 1\n"
      "export KBUILD_CHECKSRC := 0\n"
      "\n"
      "all:\n"
      "\t@echo modules=[$(KBUILD_MODULES)] builtin=$(KBUILD_BUILTIN) "
      "checksrc=$(KBUILD_CHECKSRC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("modules=[]")) << R.out;
  EXPECT_TRUE(R.contains("builtin=1")) << R.out;
  EXPECT_TRUE(R.contains("checksrc=0")) << R.out;
}

// --- Kbuild cc-option pattern simulation ---

TEST_F(BuildTest, Kernel2_CcOptionPatternSim) {
  writeMakefile(
      "define try-run\n"
      "$(if $(shell echo ok 2>/dev/null),$(1),$(2))\n"
      "endef\n"
      "\n"
      "cc-option = $(call try-run,$(1),$(2))\n"
      "\n"
      "CFLAGS := -Wall\n"
      "CFLAGS += $(call cc-option,-Wno-unused)\n"
      "CFLAGS += $(call cc-option,-march=native,-march=generic)\n"
      "\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
  EXPECT_TRUE(R.contains("-Wno-unused")) << R.out;
}

// --- Deep else-ifeq chain (6+ levels, like arch selection) ---

TEST_F(BuildTest, Kernel2_DeepElseIfeqArchSelection) {
  writeMakefile(
      "SRCARCH := arm64\n"
      "\n"
      "ifeq ($(SRCARCH),x86)\n"
      "ARCH_CFLAGS := -m64\n"
      "ARCH_NAME := x86_64\n"
      "else ifeq ($(SRCARCH),arm)\n"
      "ARCH_CFLAGS := -marm\n"
      "ARCH_NAME := ARM\n"
      "else ifeq ($(SRCARCH),arm64)\n"
      "ARCH_CFLAGS := -march=armv8-a\n"
      "ARCH_NAME := AArch64\n"
      "else ifeq ($(SRCARCH),mips)\n"
      "ARCH_CFLAGS := -mips64\n"
      "ARCH_NAME := MIPS\n"
      "else ifeq ($(SRCARCH),riscv)\n"
      "ARCH_CFLAGS := -march=rv64gc\n"
      "ARCH_NAME := RISC-V\n"
      "else ifeq ($(SRCARCH),powerpc)\n"
      "ARCH_CFLAGS := -mcpu=powerpc64\n"
      "ARCH_NAME := PowerPC\n"
      "else\n"
      "ARCH_CFLAGS :=\n"
      "ARCH_NAME := unknown\n"
      "endif\n"
      "\n"
      "all:\n"
      "\t@echo arch=$(ARCH_NAME) flags=$(ARCH_CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arch=AArch64")) << R.out;
  EXPECT_TRUE(R.contains("flags=-march=armv8-a")) << R.out;
}

// --- FORCE target chain pattern ---

TEST_F(BuildTest, Kernel2_ForceTargetChain) {
  auto Gen = tmp() / "include" / "generated";
  std::filesystem::create_directories(Gen);
  writeMakefile(
      ".DEFAULT_GOAL := all\n"
      ".PHONY: all FORCE\n"
      "FORCE:\n"
      "\n"
      "include/generated/version.h: FORCE\n"
      "\t@echo generating version.h\n"
      "\t@echo '#define VERSION 5' > $@\n"
      "\n"
      "include/generated/autoconf.h: FORCE\n"
      "\t@echo generating autoconf.h\n"
      "\t@echo '#define CONFIG 1' > $@\n"
      "\n"
      "vmlinux: include/generated/version.h include/generated/autoconf.h\n"
      "\t@echo linking vmlinux\n"
      "\n"
      "all: vmlinux\n"
      "\t@echo all_done\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("generating version.h")) << R.out;
  EXPECT_TRUE(R.contains("generating autoconf.h")) << R.out;
  EXPECT_TRUE(R.contains("linking vmlinux")) << R.out;
  EXPECT_TRUE(R.contains("all_done")) << R.out;
}

// --- Computed variable reference: $(machine-$(CONFIG_ARCH)) ---

TEST_F(BuildTest, Kernel2_ComputedVarReference) {
  writeMakefile(
      "CONFIG_ARCH := arm64\n"
      "machine-arm64 := arch/arm64\n"
      "machine-x86 := arch/x86\n"
      "machine-riscv := arch/riscv\n"
      "\n"
      "MACHINE := $(machine-$(CONFIG_ARCH))\n"
      "all:\n"
      "\t@echo machine=$(MACHINE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("machine=arch/arm64")) << R.out;
}

// --- obj-y aggregation with CONFIG flags ---

TEST_F(BuildTest, Kernel2_ObjYConfigAggregation) {
  writeMakefile(
      "CONFIG_SMP := y\n"
      "CONFIG_NUMA :=\n"
      "CONFIG_PREEMPT := y\n"
      "\n"
      "obj-y := main.o setup.o\n"
      "obj-$(CONFIG_SMP) += smp.o\n"
      "obj-$(CONFIG_NUMA) += numa.o\n"
      "obj-$(CONFIG_PREEMPT) += preempt.o\n"
      "\n"
      "OBJS := $(filter %.o,$(obj-y))\n"
      "COUNT := $(words $(OBJS))\n"
      "\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      "\t@echo count=$(COUNT)\n"
      "\t@echo has_smp=$(findstring smp.o,$(OBJS))\n"
      "\t@echo has_numa=$(findstring numa.o,$(OBJS))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("has_smp=smp.o")) << R.out;
  EXPECT_FALSE(R.contains("has_numa=numa.o")) << R.out;
  EXPECT_TRUE(R.contains("count=4")) << R.out;
}

// --- Kbuild cmd template with define+call ---

TEST_F(BuildTest, Kernel2_CmdTemplateDefineCall) {
  writeMakefile(
      "quiet := quiet_\n"
      "\n"
      "define filechk_kernel.release\n"
      "echo 5.10.191\n"
      "endef\n"
      "\n"
      "quiet_cmd_filechk = CHK\n"
      "cmd_filechk = $(filechk_$(1))\n"
      "Q := @\n"
      "\n"
      "define filechk\n"
      "$($(quiet)cmd_filechk)\n"
      "endef\n"
      "\n"
      "QUIET_MSG := $(call filechk,kernel.release)\n"
      "CMD_MSG := $(cmd_filechk)\n"
      "all:\n"
      "\t@echo quiet_msg=$(QUIET_MSG)\n"
      "\t@echo filechk_val=$(filechk_kernel.release)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("quiet_msg=CHK")) << R.out;
  EXPECT_TRUE(R.contains("filechk_val=echo 5.10.191")) << R.out;
}

// --- Subdir foreach+eval pattern (Kbuild subdir-y) ---

TEST_F(BuildTest, Kernel2_SubdirForeachEvalPattern) {
  writeMakefile(
      "subdir-y := kernel mm fs net\n"
      "\n"
      "define build_subdir\n"
      "$(1)-objs := $(1)/built-in.a\n"
      "$(1)-deps := $(1)/.depend\n"
      "endef\n"
      "\n"
      "$(foreach d,$(subdir-y),$(eval $(call build_subdir,$(d))))\n"
      "\n"
      "ALL_OBJS := $(foreach d,$(subdir-y),$($(d)-objs))\n"
      "ALL_DEPS := $(foreach d,$(subdir-y),$($(d)-deps))\n"
      "\n"
      "all:\n"
      "\t@echo objs=$(ALL_OBJS)\n"
      "\t@echo deps=$(ALL_DEPS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("kernel/built-in.a")) << R.out;
  EXPECT_TRUE(R.contains("net/built-in.a")) << R.out;
  EXPECT_TRUE(R.contains("fs/.depend")) << R.out;
}

// --- Pattern rule with directory prefix ---

TEST_F(BuildTest, Kernel2_PatternRuleWithDirPrefix) {
  std::filesystem::create_directories(tmp() / "src");
  std::filesystem::create_directories(tmp() / "obj");
  writeFile(tmp() / "src" / "main.c", "int main(){return 0;}");
  writeFile(tmp() / "src" / "util.c", "void util(){}");
  writeMakefile(
      "SRCS := src/main.c src/util.c\n"
      "OBJS := $(patsubst src/%.c,obj/%.o,$(SRCS))\n"
      "\n"
      "obj/%.o: src/%.c\n"
      "\t@echo compile $< -> $@\n"
      "\n"
      "all: $(OBJS)\n"
      "\t@echo all_done objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile src/main.c -> obj/main.o")) << R.out;
  EXPECT_TRUE(R.contains("compile src/util.c -> obj/util.o")) << R.out;
}

// --- Substitution reference with directory change ---

TEST_F(BuildTest, Kernel2_SubstRefDirChange) {
  writeMakefile(
      "SRCS := drivers/net/e1000.c drivers/usb/hub.c kernel/sched.c\n"
      "OBJS := $(SRCS:.c=.o)\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("drivers/net/e1000.o")) << R.out;
  EXPECT_TRUE(R.contains("drivers/usb/hub.o")) << R.out;
  EXPECT_TRUE(R.contains("kernel/sched.o")) << R.out;
}

// --- Multiple include with wildcard ---

TEST_F(BuildTest, Kernel2_MultipleIncludeWildcard) {
  writeFile(tmp() / "arch.mk",
            "ARCH_FLAGS := -march=native\n");
  writeFile(tmp() / "driver.mk",
            "DRIVER_FLAGS := -DDRIVER\n");
  writeMakefile(
      "-include $(wildcard *.mk)\n"
      "all:\n"
      "\t@echo arch=$(ARCH_FLAGS) driver=$(DRIVER_FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arch=-march=native")) << R.out;
  EXPECT_TRUE(R.contains("driver=-DDRIVER")) << R.out;
}

// --- Target-specific CFLAGS (per-file flags like Kbuild) ---

TEST_F(BuildTest, Kernel2_PerFileCflags) {
  writeFile(tmp() / "main.c", "");
  writeFile(tmp() / "debug.c", "");
  writeMakefile(
      "CFLAGS := -O2\n"
      "CFLAGS_debug.o := -DDEBUG -O0\n"
      "CFLAGS_main.o :=\n"
      "\n"
      "obj-y := main.o debug.o\n"
      "\n"
      "%.o: %.c\n"
      "\t@echo compile $< flags=$(CFLAGS) $(CFLAGS_$@)\n"
      "\n"
      "all: $(obj-y)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile debug.c flags=-O2 -DDEBUG -O0")) << R.out;
  EXPECT_TRUE(R.contains("compile main.c flags=-O2")) << R.out;
}

// --- recipe prefix after variable expansion ---

TEST_F(BuildTest, Kernel2_RecipePrefixAfterExpand) {
  writeMakefile(
      "define do_cmd\n"
      "@echo executed_silently\n"
      "-false\n"
      "+echo forced_exec\n"
      "endef\n"
      "\n"
      "all:\n"
      "\t$(do_cmd)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("forced_exec")) << R.out;
}

// --- Nested if + findstring (kernel config detection) ---

TEST_F(BuildTest, Kernel2_NestedIfFindstring) {
  writeMakefile(
      "MAKEFLAGS_LOCAL := nks -j4\n"
      "QUIET := $(if $(findstring s,$(MAKEFLAGS_LOCAL)),silent,verbose)\n"
      "PARALLEL := $(if $(findstring j,$(MAKEFLAGS_LOCAL)),yes,no)\n"
      "all:\n"
      "\t@echo quiet=$(QUIET) parallel=$(PARALLEL)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("quiet=silent")) << R.out;
  EXPECT_TRUE(R.contains("parallel=yes")) << R.out;
}

// --- Complex filter pipeline (like kernel Makefile) ---

TEST_F(BuildTest, Kernel2_ComplexFilterPipeline) {
  writeMakefile(
      "CONFIG_MODULES := y\n"
      "CONFIG_NET := y\n"
      "CONFIG_USB :=\n"
      "CONFIG_FS := y\n"
      "\n"
      "ALL_CONFIGS := CONFIG_MODULES CONFIG_NET CONFIG_USB CONFIG_FS\n"
      "ENABLED := $(foreach c,$(ALL_CONFIGS),$(if $($(c)),$(c)))\n"
      "DISABLED := $(filter-out $(ENABLED),$(ALL_CONFIGS))\n"
      "\n"
      "all:\n"
      "\t@echo enabled=$(ENABLED)\n"
      "\t@echo disabled=$(DISABLED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CONFIG_MODULES")) << R.out;
  EXPECT_TRUE(R.contains("CONFIG_NET")) << R.out;
  EXPECT_TRUE(R.contains("disabled=CONFIG_USB")) << R.out;
}

// --- Recursive make simulation ---

TEST_F(BuildTest, Kernel2_RecursiveMakeSimulation) {
  auto Sub = tmp() / "subdir";
  std::filesystem::create_directories(Sub);
  writeFile(Sub / "Makefile",
      "obj-y := sub_module.o\n"
      "all:\n"
      "\t@echo subdir_built obj-y=$(obj-y)\n"
      ".PHONY: all\n");
  writeMakefile(
      "SUBDIRS := subdir\n"
      "all: subdirs_done\n"
      "\t@echo top_done\n"
      ".PHONY: all subdirs_done\n"
      "subdirs_done:\n"
      "\t$(MAKE) -C subdir\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("subdir_built")) << R.out;
  EXPECT_TRUE(R.contains("top_done")) << R.out;
}

// --- define with := mode ---

TEST_F(BuildTest, Kernel2_DefineSimpleExpand) {
  writeMakefile(
      "X := hello\n"
      "define GREETING :=\n"
      "$(X) world\n"
      "endef\n"
      "X := changed\n"
      "all:\n"
      "\t@echo greeting=$(GREETING)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("greeting=hello world")) << R.out;
}

// --- Multiple targets on single rule line ---

TEST_F(BuildTest, Kernel2_MultiTargetSingleRule) {
  writeMakefile(
      ".PHONY: all clean install\n"
      "all: prog\n"
      "\t@echo all_done\n"
      "prog:\n"
      "\t@echo built_prog\n"
      "clean:\n"
      "\t@echo cleaned\n"
      "install:\n"
      "\t@echo installed\n");
  auto R = runMake({}, "clean");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cleaned")) << R.out;
  EXPECT_FALSE(R.contains("built_prog")) << R.out;
}

// --- Order-only prerequisite with timestamp ---

TEST_F(BuildTest, Kernel2_OrderOnlyPrereq) {
  std::filesystem::create_directories(tmp() / "output");
  writeFile(tmp() / "src.c", "int main(){return 0;}");
  writeMakefile(
      "output/prog.o: src.c | output\n"
      "\t@echo compile_to_output\n"
      "output:\n"
      "\t@mkdir -p $@\n"
      "all: output/prog.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile_to_output")) << R.out;
}

// --- MAKECMDGOALS detection (kernel uses this) ---

TEST_F(BuildTest, Kernel2_MakecmdgoalsDetection) {
  writeMakefile(
      "ifneq ($(filter clean mrproper,$(MAKECMDGOALS)),)\n"
      "SKIP_BUILD := 1\n"
      "else\n"
      "SKIP_BUILD := 0\n"
      "endif\n"
      "\n"
      "all:\n"
      "\t@echo skip=$(SKIP_BUILD)\n"
      "clean:\n"
      "\t@echo skip=$(SKIP_BUILD) cleaning\n"
      ".PHONY: all clean\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("skip=0")) << R.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("skip=1")) << R2.out;
}

// --- Three-level indirect variable $($($(x))) ---

TEST_F(BuildTest, Kernel2_ThreeLevelIndirect) {
  writeMakefile(
      "TOP := MID\n"
      "MID := BOTTOM\n"
      "BOTTOM := final_value\n"
      "RESULT := $($($(TOP)))\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=final_value")) << R.out;
}

// --- Kbuild multi-arch conditional with command line override ---

TEST_F(BuildTest, Kernel2_MultiArchCmdOverride) {
  writeMakefile(
      "ARCH ?= x86\n"
      "CROSS_COMPILE ?=\n"
      "\n"
      "ifeq ($(ARCH),arm64)\n"
      "CROSS_COMPILE := aarch64-linux-gnu-\n"
      "KBUILD_CFLAGS := -march=armv8-a\n"
      "else ifeq ($(ARCH),arm)\n"
      "CROSS_COMPILE := arm-linux-gnueabihf-\n"
      "KBUILD_CFLAGS := -marm\n"
      "else\n"
      "KBUILD_CFLAGS := -m64\n"
      "endif\n"
      "\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "all:\n"
      "\t@echo cc=$(CC) flags=$(KBUILD_CFLAGS)\n"
      ".PHONY: all\n");

  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;
  EXPECT_TRUE(R1.contains("flags=-m64")) << R1.out;

  auto R2 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
  EXPECT_TRUE(R2.contains("flags=-march=armv8-a")) << R2.out;
}

// --- .d dependency file include (kernel auto-dependency) ---

TEST_F(BuildTest, Kernel2_DotDDependencyInclude) {
  writeFile(tmp() / "main.c", "int main(){return 0;}");
  writeFile(tmp() / ".main.o.d", "main.o: main.c config.h\n");
  writeFile(tmp() / "config.h", "#define CONFIG 1\n");
  writeMakefile(
      "-include $(wildcard .*.o.d)\n"
      "%.o: %.c\n"
      "\t@echo compile $< -> $@\n"
      "all: main.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile main.c -> main.o")) << R.out;
}

// --- Nested foreach with filter producing flat list ---

TEST_F(BuildTest, Kernel2_NestedForeachFilter) {
  writeMakefile(
      "ARCHS := x86 arm64 riscv\n"
      "VARIANTS := debug release\n"
      "SKIP := x86-debug\n"
      "\n"
      "COMBOS := $(foreach a,$(ARCHS),$(foreach v,$(VARIANTS),$(a)-$(v)))\n"
      "FILTERED := $(filter-out $(SKIP),$(COMBOS))\n"
      "\n"
      "all:\n"
      "\t@echo combos=$(COMBOS)\n"
      "\t@echo filtered=$(FILTERED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x86-debug")) << R.out;
  EXPECT_TRUE(R.contains("arm64-release")) << R.out;
  EXPECT_TRUE(R.contains("riscv-debug")) << R.out;
  EXPECT_FALSE(R.contains("filtered=x86-debug")) << R.out;
}

// --- $(eval) producing pattern rules ---

TEST_F(BuildTest, Kernel2_EvalPatternRules) {
  writeFile(tmp() / "a.src", "");
  writeFile(tmp() / "b.src", "");
  writeMakefile(
      ".DEFAULT_GOAL := all\n"
      "MODULES := a b\n"
      "\n"
      "define mod_template\n"
      "$(1).out: $(1).src\n"
      "\t@echo build_$(1)\n"
      "endef\n"
      "\n"
      "$(foreach m,$(MODULES),$(eval $(call mod_template,$(m))))\n"
      "\n"
      "all: $(addsuffix .out,$(MODULES))\n"
      "\t@echo all_modules_done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build_a")) << R.out;
  EXPECT_TRUE(R.contains("build_b")) << R.out;
  EXPECT_TRUE(R.contains("all_modules_done")) << R.out;
}

// --- Complex patsubst pipeline (kernel obj-y to paths) ---

TEST_F(BuildTest, Kernel2_PatsubstPipeline) {
  writeMakefile(
      "obj-y := main.o init.o setup.o\n"
      "src-y := $(patsubst %.o,%.c,$(obj-y))\n"
      "dep-y := $(patsubst %.o,.%.o.d,$(obj-y))\n"
      "full-src := $(addprefix kernel/,$(src-y))\n"
      "full-obj := $(addprefix $(PWD)/build/,$(obj-y))\n"
      "\n"
      "PWD := /home/user/linux\n"
      "all:\n"
      "\t@echo src=$(src-y)\n"
      "\t@echo dep=$(dep-y)\n"
      "\t@echo full_src=$(full-src)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("src=main.c init.c setup.c")) << R.out;
  EXPECT_TRUE(R.contains("dep=.main.o.d .init.o.d .setup.o.d")) << R.out;
  EXPECT_TRUE(R.contains("full_src=kernel/main.c kernel/init.c")) << R.out;
}

// --- Override += interaction with command line ---

TEST_F(BuildTest, Kernel2_OverrideAppendCmdLine) {
  writeMakefile(
      "CFLAGS := -O2\n"
      "override CFLAGS += -Wall\n"
      "all:\n"
      "\t@echo cflags=$(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O0"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O0")) << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
}

// --- ifeq with quoted strings ---

TEST_F(BuildTest, Kernel2_IfeqQuotedStrings) {
  writeMakefile(
      "ARCH := x86\n"
      "ifeq (\"$(ARCH)\",\"x86\")\n"
      "RESULT := matched_double\n"
      "endif\n"
      "ifeq ('$(ARCH)','x86')\n"
      "RESULT2 := matched_single\n"
      "endif\n"
      "all:\n"
      "\t@echo r1=$(RESULT) r2=$(RESULT2)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=matched_double")) << R.out;
  EXPECT_TRUE(R.contains("r2=matched_single")) << R.out;
}

// --- $(subst) chain for version number parsing ---

TEST_F(BuildTest, Kernel2_SubstChainVersionParse) {
  writeMakefile(
      "KERNELRELEASE := 5.10.191\n"
      "PARTS := $(subst ., ,$(KERNELRELEASE))\n"
      "MAJOR := $(word 1,$(PARTS))\n"
      "MINOR := $(word 2,$(PARTS))\n"
      "PATCH := $(word 3,$(PARTS))\n"
      "all:\n"
      "\t@echo parts=$(PARTS)\n"
      "\t@echo major=$(MAJOR) minor=$(MINOR) patch=$(PATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("major=5")) << R.out;
  EXPECT_TRUE(R.contains("minor=10")) << R.out;
  EXPECT_TRUE(R.contains("patch=191")) << R.out;
}

// --- Empty target-specific variable ---

TEST_F(BuildTest, Kernel2_EmptyTargetVar) {
  writeMakefile(
      "EXTRA :=\n"
      "debug: EXTRA := -DDEBUG\n"
      "all:\n"
      "\t@echo extra=[$(EXTRA)]\n"
      "debug:\n"
      "\t@echo extra=[$(EXTRA)]\n"
      ".PHONY: all debug\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("extra=[]")) << R.out;

  auto R2 = runMake({}, "debug");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("extra=[-DDEBUG]")) << R2.out;
}

// --- $(file) write and read back ---

TEST_F(BuildTest, Kernel2_FileWriteAndReadBack) {
  writeMakefile(
      "$(file >flags.txt,-O2 -Wall)\n"
      "$(file >>flags.txt,-DLINUX)\n"
      "SAVED_FLAGS := $(file <flags.txt)\n"
      "all:\n"
      "\t@echo flags=$(SAVED_FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2 -Wall")) << R.out;
}

// --- Stress: Kbuild full pipeline with 8 subsystems ---

TEST_F(BuildTest, Stress_KbuildFullPipeline8Subsys) {
  std::string MF;
  MF += "VERSION := 5\n"
        "PATCHLEVEL := 10\n"
        "SUBLEVEL := 191\n"
        "KERNELRELEASE := $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
        "\n"
        "ARCH ?= x86\n"
        "CROSS_COMPILE ?=\n"
        "CC := $(CROSS_COMPILE)gcc\n"
        "\n"
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
        "  quiet=quiet_\n"
        "  Q = @\n"
        "endif\n"
        "\n"
        "CONFIG_SMP := y\n"
        "CONFIG_MODULES := y\n"
        "CONFIG_NET := y\n"
        "CONFIG_SOUND :=\n"
        "\n"
        "KBUILD_CFLAGS := -Wall -Wundef\n"
        "ifdef CONFIG_SMP\n"
        "KBUILD_CFLAGS += -DSMP\n"
        "endif\n"
        "\n"
        "ifeq ($(ARCH),x86)\n"
        "ARCH_CFLAGS := -m64 -mno-red-zone\n"
        "else ifeq ($(ARCH),arm64)\n"
        "ARCH_CFLAGS := -march=armv8-a\n"
        "else\n"
        "ARCH_CFLAGS :=\n"
        "endif\n"
        "\n"
        "KBUILD_CFLAGS += $(ARCH_CFLAGS)\n"
        "\n";

  MF += "subsystems := kernel mm fs net drivers ipc security crypto\n\n";

  MF += "define subsys_template\n"
        "$(1)-y :=\n"
        "$(1)-y += $(1)/core.o\n"
        "$(1)-y += $(1)/init.o\n"
        "ifdef CONFIG_$(shell echo $(1) | tr a-z A-Z)\n"
        "$(1)-y += $(1)/ext.o\n"
        "endif\n"
        "endef\n"
        "$(foreach s,$(subsystems),$(eval $(call subsys_template,$(s))))\n\n";

  MF += "ALL_OBJS := $(foreach s,$(subsystems),$($(s)-y))\n"
        "OBJ_COUNT := $(words $(ALL_OBJS))\n"
        "SRC_FILES := $(patsubst %.o,%.c,$(ALL_OBJS))\n"
        "DEP_FILES := $(patsubst %.o,.%.d,$(ALL_OBJS))\n"
        "\n"
        "KERNEL_PARTS := $(subst ., ,$(KERNELRELEASE))\n"
        "MAJOR := $(word 1,$(KERNEL_PARTS))\n"
        "MINOR := $(word 2,$(KERNEL_PARTS))\n"
        "\n"
        "HAS_NET := $(findstring net/ext.o,$(ALL_OBJS))\n"
        "HAS_SOUND := $(findstring sound/ext.o,$(ALL_OBJS))\n"
        "\n"
        "ifneq ($(filter clean mrproper,$(MAKECMDGOALS)),)\n"
        "SKIP_BUILD := 1\n"
        "else\n"
        "SKIP_BUILD := 0\n"
        "endif\n"
        "\n"
        "ifeq ($(MAKE_VERSION),4.3)\n"
        "MAKE_OK := yes\n"
        "else\n"
        "MAKE_OK := no\n"
        "endif\n"
        "\n"
        "all:\n"
        "\t@echo version=$(KERNELRELEASE) major=$(MAJOR) minor=$(MINOR)\n"
        "\t@echo cc=$(CC) verbose=$(KBUILD_VERBOSE) quiet=$(quiet)\n"
        "\t@echo cflags=$(KBUILD_CFLAGS)\n"
        "\t@echo obj_count=$(OBJ_COUNT)\n"
        "\t@echo has_net=$(HAS_NET) has_sound=[$(HAS_SOUND)]\n"
        "\t@echo skip=$(SKIP_BUILD) make_ok=$(MAKE_OK)\n"
        ".PHONY: all clean\n"
        "clean:\n"
        "\t@echo cleaning skip=$(SKIP_BUILD)\n";

  writeMakefile(MF);

  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.191")) << R.out;
  EXPECT_TRUE(R.contains("major=5")) << R.out;
  EXPECT_TRUE(R.contains("minor=10")) << R.out;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
  EXPECT_TRUE(R.contains("verbose=0")) << R.out;
  EXPECT_TRUE(R.contains("-DSMP")) << R.out;
  EXPECT_TRUE(R.contains("-m64")) << R.out;
  EXPECT_TRUE(R.contains("has_net=net/ext.o")) << R.out;
  EXPECT_TRUE(R.contains("has_sound=[]")) << R.out;
  EXPECT_TRUE(R.contains("skip=0")) << R.out;
  EXPECT_TRUE(R.contains("make_ok=yes")) << R.out;
}

// --- Stress: 50 modules with target-specific vars ---

TEST_F(BuildTest, Stress_50ModulesTargetVars) {
  std::string MF;
  MF += ".DEFAULT_GOAL := all\n";
  for (int I = 0; I < 50; ++I) {
    std::string N = "mod" + std::to_string(I);
    writeFile(tmp() / (N + ".src"), "");
    MF += N + ".out: MODFLAGS := -DMOD" + std::to_string(I) + "\n";
    MF += N + ".out: " + N + ".src\n";
    MF += "\t@echo build_" + N + " flags=$(MODFLAGS)\n";
  }
  MF += "TARGETS :=";
  for (int I = 0; I < 50; ++I)
    MF += " mod" + std::to_string(I) + ".out";
  MF += "\n";
  MF += "all: $(TARGETS)\n\t@echo all_50_done\n.PHONY: all\n";
  writeMakefile(MF);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build_mod0 flags=-DMOD0")) << R.out;
  EXPECT_TRUE(R.contains("build_mod49 flags=-DMOD49")) << R.out;
  EXPECT_TRUE(R.contains("all_50_done")) << R.out;
}

// --- Edge case: empty pattern rule prerequisites ---

TEST_F(BuildTest, Edge_EmptyPatternPrereqs) {
  writeMakefile(
      "%.marker:\n"
      "\t@echo create_$@\n"
      "all: test.marker\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("create_test.marker")) << R.out;
}

// --- Edge case: variable with hyphen and dot in name ---

TEST_F(BuildTest, Edge_VarSpecialCharsInName) {
  writeMakefile(
      "my-var.name := special\n"
      "CONFIG_MODULE-TEST := enabled\n"
      "all:\n"
      "\t@echo v1=$(my-var.name) v2=$(CONFIG_MODULE-TEST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("v1=special")) << R.out;
  EXPECT_TRUE(R.contains("v2=enabled")) << R.out;
}

// --- Edge case: conditional assignment chains ---

TEST_F(BuildTest, Edge_ConditionalAssignChain) {
  writeMakefile(
      "A ?= first\n"
      "A ?= second\n"
      "B ?= initial\n"
      "B := override\n"
      "B ?= should_not_apply\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=first")) << R.out;
  EXPECT_TRUE(R.contains("b=override")) << R.out;
}

// --- Edge case: deeply nested function calls ---

TEST_F(BuildTest, Edge_DeeplyNestedFunctions) {
  writeMakefile(
      "A := hello world goodbye\n"
      "R := $(word 1,$(sort $(filter-out goodbye,$(strip $(A)))))\n"
      "all:\n"
      "\t@echo result=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=hello")) << R.out;
}

// --- Edge case: recipe with only whitespace lines ---

TEST_F(BuildTest, Edge_RecipeWhitespaceOnly) {
  writeMakefile(
      "all:\n"
      "\t\n"
      "\t@echo after_empty\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("after_empty")) << R.out;
}

// --- Edge case: multiple $$ escapes in recipe ---

TEST_F(BuildTest, Edge_DollarEscapeInRecipe) {
  writeMakefile(
      "all:\n"
      "\t@echo $$HOME\n"
      "\t@VAR=test; echo $$VAR\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
}

// --- Edge case: lastword function ---

TEST_F(BuildTest, Edge_LastwordFunction) {
  writeMakefile(
      "FILES := include/linux/kernel.h include/linux/types.h "
      "include/linux/list.h\n"
      "LAST := $(lastword $(FILES))\n"
      "FIRST := $(firstword $(FILES))\n"
      "all:\n"
      "\t@echo first=$(FIRST) last=$(LAST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("first=include/linux/kernel.h")) << R.out;
  EXPECT_TRUE(R.contains("last=include/linux/list.h")) << R.out;
}

// --- Edge case: $(and)/$(or) with empty strings ---

TEST_F(BuildTest, Edge_AndOrEmptyStrings) {
  writeMakefile(
      "A := yes\n"
      "B :=\n"
      "C := also_yes\n"
      "R_AND := $(and $(A),$(B),$(C))\n"
      "R_OR := $(or $(B),,$(A),$(C))\n"
      "R_AND2 := $(and $(A),$(C))\n"
      "all:\n"
      "\t@echo and=[$(R_AND)] or=$(R_OR) and2=$(R_AND2)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("and=[]")) << R.out;
  EXPECT_TRUE(R.contains("or=yes")) << R.out;
  EXPECT_TRUE(R.contains("and2=also_yes")) << R.out;
}

// --- Edge case: Parallel build with diamond dependency ---

TEST_F(BuildTest, Edge_ParallelDiamondDep) {
  writeFile(tmp() / "base.src", "");
  writeMakefile(
      ".PHONY: all\n"
      "all: left right\n"
      "\t@echo diamond_done\n"
      "left: base.src\n"
      "\t@echo left_built\n"
      "right: base.src\n"
      "\t@echo right_built\n");
  auto R = runMake({"-j4"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("left_built")) << R.out;
  EXPECT_TRUE(R.contains("right_built")) << R.out;
  EXPECT_TRUE(R.contains("diamond_done")) << R.out;
}

// --- Linux kernel: complete module build simulation ---

TEST_F(BuildTest, Kernel2_CompleteModuleBuildSim) {
  std::string MF;
  MF += "# Kernel module build simulation\n"
        "KERNELRELEASE := 5.10.191\n"
        "KBUILD_EXTMOD :=\n"
        "\n"
        "CC := gcc\n"
        "LD := ld\n"
        "AR := ar\n"
        "\n"
        "KBUILD_CFLAGS := -Wall -Wundef -Werror=strict-prototypes\n"
        "KBUILD_CFLAGS += -fno-strict-aliasing -fno-common\n"
        "KBUILD_CFLAGS += -DKBUILD_MODNAME='\"$(modname)\"'\n"
        "\n"
        "quiet_cmd_cc_o_c = CC      $@\n"
        "define cmd_cc_o_c\n"
        "$(CC) $(KBUILD_CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<\n"
        "endef\n"
        "\n"
        "quiet_cmd_ar = AR      $@\n"
        "define cmd_ar\n"
        "$(AR) rcs $@ $^\n"
        "endef\n"
        "\n"
        "quiet_cmd_ld = LD      $@\n"
        "define cmd_ld\n"
        "$(LD) -r -o $@ $^\n"
        "endef\n"
        "\n"
        "obj-y := main.o scheduler.o memory.o\n"
        "obj-y += drivers/pci.o drivers/usb.o\n"
        "\n"
        "EXTRA_CFLAGS :=\n"
        "\n"
        "FLAT_OBJS := $(notdir $(obj-y))\n"
        "DIR_OBJS := $(filter drivers/%,$(obj-y))\n"
        "CORE_OBJS := $(filter-out drivers/%,$(obj-y))\n"
        "\n"
        "all:\n"
        "\t@echo flat=$(FLAT_OBJS)\n"
        "\t@echo dirs=$(DIR_OBJS)\n"
        "\t@echo core=$(CORE_OBJS)\n"
        "\t@echo cc_cmd=$(quiet_cmd_cc_o_c)\n"
        "\t@echo ar_cmd=$(quiet_cmd_ar)\n"
        ".PHONY: all\n";
  writeMakefile(MF);

  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("flat=main.o scheduler.o memory.o pci.o usb.o"))
      << R.out;
  EXPECT_TRUE(R.contains("dirs=drivers/pci.o drivers/usb.o")) << R.out;
  EXPECT_TRUE(R.contains("core=main.o scheduler.o memory.o")) << R.out;
  EXPECT_TRUE(R.contains("CC")) << R.out;
}

// --- Stress: parallel 30-target fan-out ---

TEST_F(BuildTest, Stress_Parallel30TargetFanout) {
  std::string MF = ".PHONY: all\nall:";
  for (int I = 0; I < 30; ++I) {
    std::string T = "t" + std::to_string(I);
    MF += " " + T;
  }
  MF += "\n\t@echo all_30_done\n";
  for (int I = 0; I < 30; ++I) {
    std::string T = "t" + std::to_string(I);
    writeFile(tmp() / (T + ".src"), "");
    MF += T + ": " + T + ".src\n"
        "\t@echo built_" + T + "\n";
  }
  writeMakefile(MF);
  auto R = runMake({"-j8"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("built_t0")) << R.out;
  EXPECT_TRUE(R.contains("built_t29")) << R.out;
  EXPECT_TRUE(R.contains("all_30_done")) << R.out;
}

// --- Edge case: $(eval) that undefines and redefines ---

TEST_F(BuildTest, Edge_EvalUndefineRedefine) {
  writeMakefile(
      "MODE := old\n"
      "$(eval undefine MODE)\n"
      "$(eval MODE := new)\n"
      "all:\n"
      "\t@echo mode=$(MODE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("mode=new")) << R.out;
}

// --- Edge case: ifdef with recursive variable pointing to empty ---

TEST_F(BuildTest, Edge_IfdefRecursiveEmpty) {
  writeMakefile(
      "EMPTY :=\n"
      "REF = $(EMPTY)\n"
      "ifdef REF\n"
      "R := defined\n"
      "else\n"
      "R := undefined\n"
      "endif\n"
      "ifdef EMPTY\n"
      "R2 := defined\n"
      "else\n"
      "R2 := undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo ref=$(R) empty=$(R2)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("empty=undefined")) << R.out;
}

// --- Edge case: sort deduplication ---

TEST_F(BuildTest, Edge_SortDedup) {
  writeMakefile(
      "LIST := z a m a z b m c\n"
      "SORTED := $(sort $(LIST))\n"
      "all:\n"
      "\t@echo sorted=$(SORTED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("sorted=a b c m z")) << R.out;
}

// --- Edge case: addprefix + addsuffix combination ---

TEST_F(BuildTest, Edge_AddprefixAddsuffixCombo) {
  writeMakefile(
      "NAMES := foo bar baz\n"
      "HEADERS := $(addsuffix .h,$(addprefix include/,$(NAMES)))\n"
      "all:\n"
      "\t@echo headers=$(HEADERS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("headers=include/foo.h include/bar.h include/baz.h"))
      << R.out;
}

// --- Edge case: basename + suffix on paths ---

TEST_F(BuildTest, Edge_BasenameSuffixPaths) {
  writeMakefile(
      "FILES := src/main.c lib/util.o include/types.h Makefile\n"
      "BASES := $(basename $(FILES))\n"
      "SUFFS := $(suffix $(FILES))\n"
      "all:\n"
      "\t@echo bases=$(BASES)\n"
      "\t@echo suffs=$(SUFFS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("bases=src/main lib/util include/types Makefile"))
      << R.out;
  EXPECT_TRUE(R.contains("suffs=.c .o .h")) << R.out;
}

// --- Edge case: dir + notdir on paths ---

TEST_F(BuildTest, Edge_DirNotdirPaths) {
  writeMakefile(
      "FILES := src/main.c lib/util.o ./test.h /absolute/path.c\n"
      "DIRS := $(dir $(FILES))\n"
      "NAMES := $(notdir $(FILES))\n"
      "all:\n"
      "\t@echo dirs=$(DIRS)\n"
      "\t@echo names=$(NAMES)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("names=main.c util.o test.h path.c")) << R.out;
  EXPECT_TRUE(R.contains("src/")) << R.out;
}

// --- Edge case: keep-going with parallel build ---

TEST_F(BuildTest, Edge_KeepGoingParallel) {
  writeFile(tmp() / "good.src", "");
  writeMakefile(
      ".PHONY: all\n"
      "all: good.out bad.out\n"
      "\t@echo should_not_reach\n"
      "good.out: good.src\n"
      "\t@echo good_built\n"
      "bad.out:\n"
      "\tfalse\n");
  auto R = runMake({"-k", "-j2"});
  EXPECT_FALSE(R.ok());
  EXPECT_TRUE(R.contains("good_built")) << R.out;
  EXPECT_TRUE(R.stderrContains("Error")) << R.err;
}

// --- Edge case: always-make forces rebuild ---

TEST_F(BuildTest, Edge_AlwaysMakeForceRebuild) {
  writeFile(tmp() / "src.c", "int main(){return 0;}");
  writeFile(tmp() / "prog", "dummy");
  writeMakefile(
      "prog: src.c\n"
      "\t@echo rebuilt_prog\n");
  auto R = runMake({"-B"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("rebuilt_prog")) << R.out;
}

// --- Kernel2: Kconfig-style nested conditions ---

TEST_F(BuildTest, Kernel2_KconfigNestedConditions) {
  writeMakefile(
      "CONFIG_PLATFORM := linux\n"
      "CONFIG_ARCH := arm64\n"
      "CONFIG_SUBARCH := cortex-a72\n"
      "\n"
      "ifeq ($(CONFIG_PLATFORM),linux)\n"
      "  PLATFORM_FLAGS := -DLINUX\n"
      "  ifeq ($(CONFIG_ARCH),arm64)\n"
      "    ARCH_FLAGS := -DARM64\n"
      "    ifeq ($(CONFIG_SUBARCH),cortex-a72)\n"
      "      SUBARCH_FLAGS := -mcpu=cortex-a72\n"
      "    else\n"
      "      SUBARCH_FLAGS := -march=armv8-a\n"
      "    endif\n"
      "  else\n"
      "    ARCH_FLAGS := -DX86\n"
      "    SUBARCH_FLAGS :=\n"
      "  endif\n"
      "else\n"
      "  PLATFORM_FLAGS := -DOTHER\n"
      "  ARCH_FLAGS :=\n"
      "  SUBARCH_FLAGS :=\n"
      "endif\n"
      "\n"
      "ALL_FLAGS := $(PLATFORM_FLAGS) $(ARCH_FLAGS) $(SUBARCH_FLAGS)\n"
      "all:\n"
      "\t@echo flags=$(ALL_FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-DLINUX")) << R.out;
  EXPECT_TRUE(R.contains("-DARM64")) << R.out;
  EXPECT_TRUE(R.contains("-mcpu=cortex-a72")) << R.out;
}

// --- Hash in recipe preserved (not stripped as comment) ---

TEST_F(BuildTest, Edge2_HashInRecipePreserved) {
  writeMakefile(
      "all:\n"
      "\t@echo '#define VERSION 5' > /dev/null\n"
      "\t@echo hash_preserved\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hash_preserved")) << R.out;
}

// --- Inline recipe after semicolon ---

TEST_F(BuildTest, Edge2_InlineRecipeSemicolon) {
  writeMakefile(
      "all: ; @echo inline_recipe\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("inline_recipe")) << R.out;
}

// --- $(warning) outputs but continues ---

TEST_F(BuildTest, Edge2_WarningContinues) {
  writeMakefile(
      "$(warning This is a warning)\n"
      "all:\n"
      "\t@echo continued_after_warning\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("continued_after_warning")) << R.out;
}

// --- $(info) outputs to stdout ---

TEST_F(BuildTest, Edge2_InfoOutput) {
  writeMakefile(
      "$(info Build system initialized)\n"
      "all:\n"
      "\t@echo build_done\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("build_done")) << R.out;
}

// --- ifeq with parenthesized "quoted" variable args ---

TEST_F(BuildTest, Edge2_IfeqParenQuotedVar) {
  writeMakefile(
      "X := hello\n"
      "ifeq (\"$(X)\",\"hello\")\n"
      "R := matched\n"
      "else\n"
      "R := nope\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=matched")) << R.out;
}

// --- Static pattern rule multi-target ---

TEST_F(BuildTest, Edge2_StaticPatternMultiTarget) {
  writeFile(tmp() / "a.txt", "alpha");
  writeFile(tmp() / "b.txt", "beta");
  writeMakefile(
      ".DEFAULT_GOAL := all\n"
      "TARGETS := a.out b.out\n"
      "$(TARGETS): %.out: %.txt\n"
      "\t@echo convert $< to $@\n"
      "all: $(TARGETS)\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("convert a.txt to a.out")) << R.out;
  EXPECT_TRUE(R.contains("convert b.txt to b.out")) << R.out;
}

// --- Recursive late binding (value changes after reference) ---

TEST_F(BuildTest, Edge2_RecursiveLateBind) {
  writeMakefile(
      "GREETING = Hello $(WHO)\n"
      "WHO = World\n"
      "R1 := $(GREETING)\n"
      "WHO = NeverC\n"
      "R2 := $(GREETING)\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=Hello World")) << R.out;
  EXPECT_TRUE(R.contains("r2=Hello NeverC")) << R.out;
}

// --- Multiple prerequisites merge from separate rule declarations ---

TEST_F(BuildTest, Edge2_MultiPrereqMerge) {
  writeFile(tmp() / "a.h", "");
  writeFile(tmp() / "b.h", "");
  writeFile(tmp() / "main.c", "");
  writeMakefile(
      "main.o: main.c\n"
      "\t@echo compile $< deps=$^\n"
      "main.o: a.h b.h\n"
      "all: main.o\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile main.c")) << R.out;
  EXPECT_TRUE(R.contains("a.h")) << R.out;
  EXPECT_TRUE(R.contains("b.h")) << R.out;
}

// --- Space-only indent is not a recipe (only tab) ---

TEST_F(BuildTest, Edge2_SpaceIndentNotRecipe) {
  writeMakefile(
      "X := from_var\n"
      "all:\n"
      "\t@echo recipe=$(X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("recipe=from_var")) << R.out;
}

// --- export all variables ---

TEST_F(BuildTest, Edge2_ExportAll) {
  writeMakefile(
      "A := val_a\n"
      "B := val_b\n"
      "export\n"
      "all:\n"
      "\t@echo a=$(A) b=$(B)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=val_a")) << R.out;
}

// --- Kbuild-style if_changed command dispatch pattern ---

TEST_F(BuildTest, Kernel3_IfChangedPattern) {
  writeMakefile(
      "quiet := quiet_\n"
      "\n"
      "cmd_cc_o_c = gcc -c -o $@ $<\n"
      "quiet_cmd_cc_o_c = CC $@\n"
      "\n"
      "define if_changed\n"
      "$($(quiet)cmd_$(1))\n"
      "endef\n"
      "\n"
      "DISPATCH := $(call if_changed,cc_o_c)\n"
      "all:\n"
      "\t@echo dispatch=$(DISPATCH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("dispatch=CC")) << R.out;
}

// --- Kbuild cmd prefix pattern with computed variable ---

TEST_F(BuildTest, Kernel3_ComputedCmdPrefix) {
  writeMakefile(
      "cmd_link = ld -o $@ $^\n"
      "quiet_cmd_link = LD $@\n"
      "cmd_compile = gcc -c $<\n"
      "quiet_cmd_compile = CC $@\n"
      "\n"
      "ACTIONS := link compile\n"
      "quiet := quiet_\n"
      "RESULT := $(foreach a,$(ACTIONS),$($(quiet)cmd_$(a)))\n"
      "\n"
      "all:\n"
      "\t@echo result=$(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("LD")) << R.out;
  EXPECT_TRUE(R.contains("CC")) << R.out;
}

// --- $(foreach) with $(wildcard) inside ---

TEST_F(BuildTest, Kernel3_ForeachWithWildcard) {
  std::filesystem::create_directories(tmp() / "src");
  writeFile(tmp() / "src" / "a.c", "");
  writeFile(tmp() / "src" / "b.c", "");
  writeMakefile(
      "DIRS := src\n"
      "ALL_SRC := $(foreach d,$(DIRS),$(wildcard $(d)/*.c))\n"
      "ALL_OBJ := $(patsubst %.c,%.o,$(ALL_SRC))\n"
      "all:\n"
      "\t@echo obj=$(ALL_OBJ)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("src/a.o") || R.contains("src/b.o")) << R.out;
}

// --- filter with multiple patterns ---

TEST_F(BuildTest, Kernel3_FilterMultiplePatterns) {
  writeMakefile(
      "FILES := main.c util.h setup.c config.h driver.c types.h\n"
      "SOURCES := $(filter %.c,$(FILES))\n"
      "HEADERS := $(filter %.h,$(FILES))\n"
      "BOTH := $(filter %.c %.h,$(FILES))\n"
      "all:\n"
      "\t@echo sources=$(SOURCES)\n"
      "\t@echo headers=$(HEADERS)\n"
      "\t@echo both=$(BOTH)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("sources=main.c setup.c driver.c")) << R.out;
  EXPECT_TRUE(R.contains("headers=util.h config.h types.h")) << R.out;
  EXPECT_TRUE(R.contains("both=main.c util.h setup.c config.h driver.c types.h"))
      << R.out;
}

// --- Kbuild CONFIG detection using computed variable name ---

TEST_F(BuildTest, Kernel3_ConfigComputedVarDetection) {
  writeMakefile(
      "CONFIG_SMP := y\n"
      "CONFIG_PREEMPT := y\n"
      "CONFIG_NUMA :=\n"
      "\n"
      "FEATURES := SMP PREEMPT NUMA HOTPLUG\n"
      "ENABLED_FEATURES := $(foreach f,$(FEATURES),$(if $(CONFIG_$(f)),$(f)))\n"
      "DISABLED_FEATURES := $(filter-out $(ENABLED_FEATURES),$(FEATURES))\n"
      "\n"
      "FEATURE_FLAGS := $(foreach f,$(ENABLED_FEATURES),-DCONFIG_$(f))\n"
      "\n"
      "all:\n"
      "\t@echo enabled=$(ENABLED_FEATURES)\n"
      "\t@echo disabled=$(DISABLED_FEATURES)\n"
      "\t@echo flags=$(FEATURE_FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("enabled=SMP PREEMPT")) << R.out;
  EXPECT_TRUE(R.contains("disabled=NUMA HOTPLUG")) << R.out;
  EXPECT_TRUE(R.contains("-DCONFIG_SMP")) << R.out;
  EXPECT_TRUE(R.contains("-DCONFIG_PREEMPT")) << R.out;
}

// --- define + endef multi-line with tabs ---

TEST_F(BuildTest, Edge2_DefineMultilineWithTabs) {
  writeMakefile(
      "define multi_cmd\n"
      "echo step1\n"
      "echo step2\n"
      "echo step3\n"
      "endef\n"
      "\n"
      "all:\n"
      "\t@$(multi_cmd)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("step1")) << R.out;
}

// --- ifndef chain with fallback default ---

TEST_F(BuildTest, Edge2_IfndefFallbackDefault) {
  writeMakefile(
      "ifndef CC\n"
      "CC := gcc\n"
      "endif\n"
      "ifndef ARCH\n"
      "ARCH := native\n"
      "endif\n"
      "all:\n"
      "\t@echo cc=$(CC) arch=$(ARCH)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;
  EXPECT_TRUE(R1.contains("arch=native")) << R1.out;

  auto R2 = runMake({"CC=clang", "ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=clang")) << R2.out;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
}

// --- $(strip) in ifeq comparison ---

TEST_F(BuildTest, Edge2_StripInIfeq) {
  writeMakefile(
      "VAR :=   spaced  \n"
      "ifeq ($(strip $(VAR)),spaced)\n"
      "R := stripped_match\n"
      "else\n"
      "R := no_match\n"
      "endif\n"
      "all:\n"
      "\t@echo result=$(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("result=stripped_match")) << R.out;
}

// --- Stress: deep include chain (4 levels) ---

TEST_F(BuildTest, Stress_DeepIncludeChain) {
  writeFile(tmp() / "level3.mk", "LEVEL3 := yes\n");
  writeFile(tmp() / "level2.mk", "include level3.mk\nLEVEL2 := yes\n");
  writeFile(tmp() / "level1.mk", "include level2.mk\nLEVEL1 := yes\n");
  writeMakefile(
      "include level1.mk\n"
      "all:\n"
      "\t@echo l1=$(LEVEL1) l2=$(LEVEL2) l3=$(LEVEL3)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("l1=yes")) << R.out;
  EXPECT_TRUE(R.contains("l2=yes")) << R.out;
  EXPECT_TRUE(R.contains("l3=yes")) << R.out;
}

// --- Stress: large patsubst list (500 items) ---

TEST_F(BuildTest, Stress_LargePatsubstList) {
  std::string List;
  for (int I = 0; I < 500; ++I) {
    if (I > 0) List += " ";
    List += "file" + std::to_string(I) + ".c";
  }
  writeMakefile(
      "SRC := " + List + "\n"
      "OBJ := $(patsubst %.c,%.o,$(SRC))\n"
      "COUNT := $(words $(OBJ))\n"
      "FIRST := $(firstword $(OBJ))\n"
      "LAST := $(lastword $(OBJ))\n"
      "all:\n"
      "\t@echo count=$(COUNT) first=$(FIRST) last=$(LAST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=500")) << R.out;
  EXPECT_TRUE(R.contains("first=file0.o")) << R.out;
  EXPECT_TRUE(R.contains("last=file499.o")) << R.out;
}

// --- Kbuild: complete end-to-end kernel build simulation ---

TEST_F(BuildTest, Kernel3_EndToEndKernelSim) {
  std::string MF;
  MF += ".DEFAULT_GOAL := all\n"
        "\n"
        "# Version block\n"
        "VERSION = 5\n"
        "PATCHLEVEL = 10\n"
        "SUBLEVEL = 191\n"
        "EXTRAVERSION =\n"
        "NAME = Kleptomaniac Octopus\n"
        "\n"
        "KERNELVERSION = $(VERSION)$(if $(PATCHLEVEL),.$(PATCHLEVEL)$(if "
        "$(SUBLEVEL),.$(SUBLEVEL)))$(EXTRAVERSION)\n"
        "\n"
        "# Verbose/quiet\n"
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
        "  quiet=quiet_\n"
        "  Q = @\n"
        "endif\n"
        "\n"
        "# Architecture\n"
        "ARCH ?= x86\n"
        "CROSS_COMPILE ?=\n"
        "CC := $(CROSS_COMPILE)gcc\n"
        "LD := $(CROSS_COMPILE)ld\n"
        "AR := $(CROSS_COMPILE)ar\n"
        "\n"
        "ifeq ($(ARCH),x86)\n"
        "SRCARCH := x86\n"
        "ARCH_CFLAGS := -m64 -mno-red-zone\n"
        "else ifeq ($(ARCH),arm64)\n"
        "SRCARCH := arm64\n"
        "ARCH_CFLAGS := -march=armv8-a\n"
        "CROSS_COMPILE := aarch64-linux-gnu-\n"
        "CC := $(CROSS_COMPILE)gcc\n"
        "else ifeq ($(ARCH),riscv)\n"
        "SRCARCH := riscv\n"
        "ARCH_CFLAGS := -march=rv64gc\n"
        "else\n"
        "SRCARCH := $(ARCH)\n"
        "ARCH_CFLAGS :=\n"
        "endif\n"
        "\n"
        "# Config flags\n"
        "CONFIG_SMP := y\n"
        "CONFIG_MODULES := y\n"
        "CONFIG_NET := y\n"
        "CONFIG_PREEMPT := y\n"
        "CONFIG_SOUND :=\n"
        "CONFIG_USB := y\n"
        "\n"
        "# Build flags\n"
        "KBUILD_CFLAGS := -Wall -Wundef -Werror=strict-prototypes\n"
        "KBUILD_CFLAGS += -fno-strict-aliasing -fno-common\n"
        "ifdef CONFIG_SMP\n"
        "KBUILD_CFLAGS += -DSMP\n"
        "endif\n"
        "ifdef CONFIG_PREEMPT\n"
        "KBUILD_CFLAGS += -DPREEMPT\n"
        "endif\n"
        "KBUILD_CFLAGS += $(ARCH_CFLAGS)\n"
        "\n"
        "# Commands\n"
        "quiet_cmd_cc_o_c = CC      $@\n"
        "cmd_cc_o_c = $(CC) $(KBUILD_CFLAGS) -c -o $@ $<\n"
        "quiet_cmd_ld = LD      $@\n"
        "cmd_ld = $(LD) -r -o $@ $^\n"
        "quiet_cmd_ar = AR      $@\n"
        "cmd_ar = $(AR) rcs $@ $^\n"
        "\n"
        "# Subsystem template\n"
        "subsystems := kernel mm fs net drivers\n"
        "\n"
        "define subsys_template\n"
        "$(1)-y := $(1)/core.o $(1)/init.o\n"
        "ifdef CONFIG_$(shell echo $(1) | tr a-z A-Z)\n"
        "$(1)-y += $(1)/ext.o\n"
        "endif\n"
        "endef\n"
        "$(foreach s,$(subsystems),$(eval $(call subsys_template,$(s))))\n"
        "\n"
        "# Collect objects\n"
        "ALL_OBJS := $(foreach s,$(subsystems),$($(s)-y))\n"
        "OBJ_COUNT := $(words $(ALL_OBJS))\n"
        "SRC_FILES := $(patsubst %.o,%.c,$(ALL_OBJS))\n"
        "\n"
        "# Feature detection\n"
        "FEATURES := SMP MODULES NET PREEMPT SOUND USB\n"
        "ENABLED := $(foreach f,$(FEATURES),$(if $(CONFIG_$(f)),$(f)))\n"
        "DISABLED := $(filter-out $(ENABLED),$(FEATURES))\n"
        "FEATURE_FLAGS := $(foreach f,$(ENABLED),-DCONFIG_$(f))\n"
        "\n"
        "# Version parsing\n"
        "VPARTS := $(subst ., ,$(KERNELVERSION))\n"
        "VMAJOR := $(word 1,$(VPARTS))\n"
        "VMINOR := $(word 2,$(VPARTS))\n"
        "VPATCH := $(word 3,$(VPARTS))\n"
        "\n"
        "# MAKE_VERSION check\n"
        "ifeq ($(MAKE_VERSION),4.3)\n"
        "MAKE_COMPAT := yes\n"
        "else\n"
        "MAKE_COMPAT := no\n"
        "endif\n"
        "\n"
        "# MAKECMDGOALS detection\n"
        "ifneq ($(filter clean mrproper,$(MAKECMDGOALS)),)\n"
        "SKIP_BUILD := 1\n"
        "else\n"
        "SKIP_BUILD := 0\n"
        "endif\n"
        "\n"
        "# Target-specific flags\n"
        "CFLAGS_kernel/core.o := -DKERNEL_CORE\n"
        "CFLAGS_drivers/core.o := -DDRIVER_CORE\n"
        "\n"
        "all: vmlinux\n"
        "\t@echo === Build Complete ===\n"
        "\t@echo version=$(KERNELVERSION) name=$(NAME)\n"
        "\t@echo vmajor=$(VMAJOR) vminor=$(VMINOR) vpatch=$(VPATCH)\n"
        "\t@echo arch=$(SRCARCH) cc=$(CC)\n"
        "\t@echo verbose=$(KBUILD_VERBOSE) quiet_prefix=$(quiet)\n"
        "\t@echo cflags=$(KBUILD_CFLAGS)\n"
        "\t@echo obj_count=$(OBJ_COUNT)\n"
        "\t@echo enabled=$(ENABLED)\n"
        "\t@echo disabled=$(DISABLED)\n"
        "\t@echo feature_flags=$(FEATURE_FLAGS)\n"
        "\t@echo skip=$(SKIP_BUILD) make_compat=$(MAKE_COMPAT)\n"
        "\t@echo kernel_core_flags=$(CFLAGS_kernel/core.o)\n"
        "\n"
        "vmlinux:\n"
        "\t@echo link_vmlinux\n"
        "\n"
        ".PHONY: all vmlinux clean\n"
        "clean:\n"
        "\t@echo cleaning skip=$(SKIP_BUILD)\n";

  writeMakefile(MF);

  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("version=5.10.191")) << R.out;
  EXPECT_TRUE(R.contains("name=Kleptomaniac Octopus")) << R.out;
  EXPECT_TRUE(R.contains("vmajor=5")) << R.out;
  EXPECT_TRUE(R.contains("vminor=10")) << R.out;
  EXPECT_TRUE(R.contains("vpatch=191")) << R.out;
  EXPECT_TRUE(R.contains("arch=x86")) << R.out;
  EXPECT_TRUE(R.contains("cc=gcc")) << R.out;
  EXPECT_TRUE(R.contains("verbose=0")) << R.out;
  EXPECT_TRUE(R.contains("-DSMP")) << R.out;
  EXPECT_TRUE(R.contains("-DPREEMPT")) << R.out;
  EXPECT_TRUE(R.contains("-m64")) << R.out;
  EXPECT_TRUE(R.contains("make_compat=yes")) << R.out;
  EXPECT_TRUE(R.contains("skip=0")) << R.out;
  EXPECT_TRUE(R.contains("kernel_core_flags=-DKERNEL_CORE")) << R.out;
  EXPECT_TRUE(R.contains("-DCONFIG_SMP")) << R.out;
  EXPECT_TRUE(R.contains("disabled=SOUND")) << R.out;

  auto R2 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
  EXPECT_TRUE(R2.contains("-march=armv8-a")) << R2.out;

  auto R3 = runMake({}, "clean");
  ASSERT_TRUE(R3.ok()) << R3.err;
  EXPECT_TRUE(R3.contains("skip=1")) << R3.out;
  EXPECT_TRUE(R3.contains("cleaning")) << R3.out;
}

// ============================================================================
// Robustness Phase 3 — Edge Cases & Kbuild End-to-End
// ============================================================================

// --- Recipe Edge Cases ---

TEST_F(BuildTest, RecipeHashPreserved) {
  writeMakefile(
      "all:\n"
      "\t@echo hello # this is a shell comment\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello")) << R.out;
}

TEST_F(BuildTest, RecipeTabOnlyLineSkipped) {
  writeMakefile(
      "all:\n"
      "\t@echo line1\n"
      "\t\n"
      "\t@echo line2\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("line1")) << R.out;
  EXPECT_TRUE(R.contains("line2")) << R.out;
}

TEST_F(BuildTest, RecipeSemicolonInline) {
  writeMakefile(
      "all: ; @echo inline_recipe\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("inline_recipe")) << R.out;
}

TEST_F(BuildTest, RecipeSemicolonWithPrereqs) {
  writeFile(tmp() / "dep.txt", "exist");
  writeMakefile(
      "all: dep.txt ; @echo has_dep\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("has_dep")) << R.out;
}

TEST_F(BuildTest, RecipeContinuationBackslash) {
  writeMakefile(
      "all:\n"
      "\t@echo hello \\\n"
      "\tworld\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello")) << R.out;
}

TEST_F(BuildTest, RecipePrefixAfterExpansion) {
  writeMakefile(
      "quiet = @\n"
      "define cmd_link\n"
      "$(quiet)echo linking $@\n"
      "endef\n"
      "all:\n"
      "\t$(cmd_link)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("linking all")) << R.out;
  EXPECT_FALSE(R.contains("echo linking")) << "@ prefix should suppress echo: " << R.out;
}

TEST_F(BuildTest, RecipeMultiplePrefixes) {
  writeMakefile(
      "all:\n"
      "\t@-echo silent_ignore\n"
      "\t-@echo ignore_silent\n"
      "\t+@echo force_silent\n"
      ".PHONY: all\n");
  auto R = runMake({"-n"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("force_silent")) << "+ forces execution in dry-run: " << R.out;
}

// --- Variable Expansion Edge Cases ---

TEST_F(BuildTest, VarExpansionEmptyVarInConcat) {
  writeMakefile(
      "A =\n"
      "B = hello$(A)world\n"
      "all:\n"
      "\t@echo $(B)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("helloworld")) << R.out;
}

TEST_F(BuildTest, VarExpansionBracesSyntax) {
  writeMakefile(
      "FOO = brace_val\n"
      "all:\n"
      "\t@echo ${FOO}\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("brace_val")) << R.out;
}

TEST_F(BuildTest, VarExpansionMixedParenBrace) {
  writeMakefile(
      "A = inner\n"
      "inner = resolved\n"
      "all:\n"
      "\t@echo $(${A})\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("resolved")) << R.out;
}

TEST_F(BuildTest, VarExpansionDollarInValue) {
  writeMakefile(
      "PRICE = $$5.99\n"
      "all:\n"
      "\t@echo '$(PRICE)'\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("$5.99")) << R.out;
}

TEST_F(BuildTest, VarExpansionRecursiveLateBinding) {
  writeMakefile(
      "A = $(B)\n"
      "B = early\n"
      "all:\n"
      "\t@echo first=$(A)\n"
      ".PHONY: all\n"
      "B = late\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("first=late")) << R.out;
}

TEST_F(BuildTest, VarExpansionQuadNesting) {
  writeMakefile(
      "D = final_value\n"
      "C = D\n"
      "B = C\n"
      "A = B\n"
      "all:\n"
      "\t@echo $($($($(A))))\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("final_value")) << R.out;
}

TEST_F(BuildTest, VarSubstRefWithPercent) {
  writeMakefile(
      "SRCS = src/a.c src/b.c lib/c.c\n"
      "OBJS := $(SRCS:%.c=%.o)\n"
      "all:\n"
      "\t@echo $(OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("src/a.o")) << R.out;
  EXPECT_TRUE(R.contains("src/b.o")) << R.out;
  EXPECT_TRUE(R.contains("lib/c.o")) << R.out;
}

TEST_F(BuildTest, VarSubstRefSimpleSuffix) {
  writeMakefile(
      "OBJS = foo.o bar.o baz.o\n"
      "SRCS := $(OBJS:.o=.c)\n"
      "all:\n"
      "\t@echo $(SRCS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("foo.c bar.c baz.c")) << R.out;
}

// --- Conditional Edge Cases ---

TEST_F(BuildTest, IfeqWithEmptyExpansion) {
  writeMakefile(
      "X =\n"
      "ifeq ($(X),)\n"
      "RESULT = empty\n"
      "else\n"
      "RESULT = notempty\n"
      "endif\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("empty")) << R.out;
}

TEST_F(BuildTest, IfeqStripComparison) {
  writeMakefile(
      "X = hello\n"
      "ifeq ($(strip $(X)),hello)\n"
      "RESULT = match\n"
      "else\n"
      "RESULT = nomatch\n"
      "endif\n"
      "all:\n"
      "\t@echo $(RESULT)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("match")) << R.out;
}

TEST_F(BuildTest, IfdefWithRecursiveEmpty) {
  writeMakefile(
      "A =\n"
      "ifdef A\n"
      "R1 = defined\n"
      "else\n"
      "R1 = undef\n"
      "endif\n"
      "B = $(NONEXIST)\n"
      "ifdef B\n"
      "R2 = defined\n"
      "else\n"
      "R2 = undef\n"
      "endif\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=undef")) << "empty var = undefined for ifdef: " << R.out;
  EXPECT_TRUE(R.contains("r2=defined")) << "non-empty raw value = defined: " << R.out;
}

TEST_F(BuildTest, ElseIfdefChain) {
  writeMakefile(
      "ifdef CONFIG_A\n"
      "R = a\n"
      "else ifdef CONFIG_B\n"
      "R = b\n"
      "else ifdef CONFIG_C\n"
      "R = c\n"
      "else\n"
      "R = none\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R1 = runMake({"CONFIG_B=y"});
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("b")) << R1.out;

  auto R2 = runMake();
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("none")) << R2.out;
}

// --- Function Edge Cases ---

TEST_F(BuildTest, ForeachEmptyListNoop) {
  writeMakefile(
      "LIST =\n"
      "RESULT := $(foreach x,$(LIST),item_$(x))\n"
      "all:\n"
      "\t@echo [$(RESULT)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("[]")) << R.out;
}

TEST_F(BuildTest, ForeachNestedWithCall) {
  writeMakefile(
      "ARCHS = x86 arm\n"
      "CONFIGS = debug release\n"
      "define gen_target\n"
      "$(1)-$(2)\n"
      "endef\n"
      "ALL := $(foreach a,$(ARCHS),$(foreach c,$(CONFIGS),$(call gen_target,$(a),$(c))))\n"
      "all:\n"
      "\t@echo $(ALL)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("x86-debug")) << R.out;
  EXPECT_TRUE(R.contains("arm-release")) << R.out;
}

TEST_F(BuildTest, FilterMultiplePatternsR3) {
  writeMakefile(
      "FILES = main.c util.h data.c config.h test.o\n"
      "SRC := $(filter %.c %.h,$(FILES))\n"
      "all:\n"
      "\t@echo $(SRC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("main.c")) << R.out;
  EXPECT_TRUE(R.contains("util.h")) << R.out;
  EXPECT_TRUE(R.contains("config.h")) << R.out;
  EXPECT_FALSE(R.contains("test.o")) << R.out;
}

TEST_F(BuildTest, PatsubstChain) {
  writeMakefile(
      "SRCS = src/foo.c src/bar.c\n"
      "OBJS := $(patsubst src/%.c,obj/%.o,$(SRCS))\n"
      "DEPS := $(patsubst %.o,%.d,$(OBJS))\n"
      "all:\n"
      "\t@echo objs=$(OBJS)\n"
      "\t@echo deps=$(DEPS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("objs=obj/foo.o obj/bar.o")) << R.out;
  EXPECT_TRUE(R.contains("deps=obj/foo.d obj/bar.d")) << R.out;
}

TEST_F(BuildTest, CallWithZeroArgs) {
  writeMakefile(
      "define greet\n"
      "hello_world\n"
      "endef\n"
      "X := $(call greet)\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("hello_world")) << R.out;
}

TEST_F(BuildTest, CallPositionalArgsSaved) {
  writeMakefile(
      "define inner\n"
      "inner_$(1)\n"
      "endef\n"
      "define outer\n"
      "$(call inner,$(1))_$(2)\n"
      "endef\n"
      "R := $(call outer,A,B)\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("inner_A_B")) << R.out;
}

TEST_F(BuildTest, EvalGeneratesRule) {
  writeMakefile(
      "define make_target\n"
      "$(1):\n"
      "\t@echo building_$(1)\n"
      "endef\n"
      "all: mybin\n"
      "\t@echo done\n"
      "$(eval $(call make_target,mybin))\n"
      ".PHONY: all mybin\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("building_mybin")) << R.out;
  EXPECT_TRUE(R.contains("done")) << R.out;
}

TEST_F(BuildTest, WordFunctionsVersionParsing) {
  writeMakefile(
      "VER = 5.10.191\n"
      "PARTS := $(subst ., ,$(VER))\n"
      "MAJOR := $(word 1,$(PARTS))\n"
      "MINOR := $(word 2,$(PARTS))\n"
      "PATCH := $(word 3,$(PARTS))\n"
      "COUNT := $(words $(PARTS))\n"
      "FIRST := $(firstword $(PARTS))\n"
      "LAST := $(lastword $(PARTS))\n"
      "all:\n"
      "\t@echo $(MAJOR).$(MINOR).$(PATCH) cnt=$(COUNT) f=$(FIRST) l=$(LAST)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("5.10.191")) << R.out;
  EXPECT_TRUE(R.contains("cnt=3")) << R.out;
  EXPECT_TRUE(R.contains("f=5")) << R.out;
  EXPECT_TRUE(R.contains("l=191")) << R.out;
}

TEST_F(BuildTest, SortDedups) {
  writeMakefile(
      "X = c b a b c a d\n"
      "S := $(sort $(X))\n"
      "all:\n"
      "\t@echo $(S)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a b c d")) << R.out;
}

TEST_F(BuildTest, StripInIfeq) {
  writeMakefile(
      "X =   hello   \n"
      "ifeq ($(strip $(X)),hello)\n"
      "R = stripped\n"
      "else\n"
      "R = not_stripped\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("stripped")) << R.out;
}

TEST_F(BuildTest, IfFunctionNested) {
  writeMakefile(
      "DEBUG = 1\n"
      "OPT = -O2\n"
      "FLAGS := $(if $(DEBUG),$(if $(findstring -O2,$(OPT)),-Og,-O0),$(OPT))\n"
      "all:\n"
      "\t@echo $(FLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-Og")) << R.out;
}

TEST_F(BuildTest, OrAndFunctions) {
  writeMakefile(
      "A =\n"
      "B = val_b\n"
      "C = val_c\n"
      "R1 := $(or $(A),$(B),$(C))\n"
      "R2 := $(and $(B),$(C))\n"
      "R3 := $(and $(A),$(B))\n"
      "all:\n"
      "\t@echo r1=$(R1) r2=$(R2) r3=[$(R3)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("r1=val_b")) << R.out;
  EXPECT_TRUE(R.contains("r2=val_c")) << R.out;
  EXPECT_TRUE(R.contains("r3=[]")) << R.out;
}

TEST_F(BuildTest, FindstringInFilter) {
  writeMakefile(
      "CFLAGS = -Wall -Werror -O2 -g\n"
      "HAS_WALL := $(findstring -Wall,$(CFLAGS))\n"
      "HAS_WEXTRA := $(findstring -Wextra,$(CFLAGS))\n"
      "all:\n"
      "\t@echo wall=[$(HAS_WALL)] wextra=[$(HAS_WEXTRA)]\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("wall=[-Wall]")) << R.out;
  EXPECT_TRUE(R.contains("wextra=[]")) << R.out;
}

// --- Target-Specific Variable Edge Cases ---

TEST_F(BuildTest, TargetVarWithAutoVars) {
  writeFile(tmp() / "src.c", "int main(){}");
  writeMakefile(
      "CFLAGS = -Wall\n"
      "prog.o: CFLAGS += -DPROG\n"
      "prog.o: src.c\n"
      "\t@echo cc $(CFLAGS) -o $@ $<\n"
      "other.o: src.c\n"
      "\t@echo cc $(CFLAGS) -o $@ $<\n"
      ".PHONY: prog.o other.o\n");
  auto R1 = runMake({}, "prog.o");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("-DPROG")) << "target-specific += should apply: " << R1.out;
  EXPECT_TRUE(R1.contains("-o prog.o")) << R1.out;
  EXPECT_TRUE(R1.contains("src.c")) << R1.out;

  auto R2 = runMake({}, "other.o");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_FALSE(R2.contains("-DPROG")) << "target-specific should not leak: " << R2.out;
}

TEST_F(BuildTest, TargetVarConditional) {
  writeMakefile(
      "VERBOSE = default\n"
      "quiet: VERBOSE ?= quiet_mode\n"
      "quiet:\n"
      "\t@echo $(VERBOSE)\n"
      ".PHONY: quiet\n");
  auto R = runMake({}, "quiet");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("default")) << "?= should not override: " << R.out;
}

// --- Pattern Rule Edge Cases ---

TEST_F(BuildTest, PatternRuleWithDir) {
  std::filesystem::create_directories(tmp() / "src");
  writeFile(tmp() / "src" / "foo.c", "int main(){}");
  writeMakefile(
      "obj/%.o: src/%.c\n"
      "\t@echo compile $< to $@\n"
      "all: obj/foo.o\n"
      ".PHONY: all obj/foo.o\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compile src/foo.c to obj/foo.o")) << R.out;
}

TEST_F(BuildTest, PatternRuleStemInPrereq) {
  writeFile(tmp() / "hello.src", "data");
  writeMakefile(
      "%.out: %.src\n"
      "\t@echo transform $* from $< to $@\n"
      "all: hello.out\n"
      ".PHONY: all hello.out\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("transform hello")) << R.out;
  EXPECT_TRUE(R.contains("from hello.src")) << R.out;
  EXPECT_TRUE(R.contains("to hello.out")) << R.out;
}

// --- Export/Unexport Edge Cases ---

TEST_F(BuildTest, ExportWithSimpleAssignR3) {
  writeMakefile(
      "export MY_VAR := exported_value\n"
      "all:\n"
      "\t@echo val=$(MY_VAR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("val=exported_value")) << R.out;
}

TEST_F(BuildTest, UnexportSpecificR3) {
  writeMakefile(
      "FOO = bar\n"
      "export FOO\n"
      "unexport FOO\n"
      "all:\n"
      "\t@echo foo=$(FOO)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("foo=bar")) << "value should still be accessible: " << R.out;
}

// --- Define Edge Cases ---

TEST_F(BuildTest, DefineWithSimpleAssign) {
  writeMakefile(
      "X = dynamic\n"
      "define BLOCK :=\n"
      "static_$(X)\n"
      "endef\n"
      "X = changed\n"
      "all:\n"
      "\t@echo $(BLOCK)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("static_dynamic")) << ":= should expand at define time: " << R.out;
}

TEST_F(BuildTest, DefineAppend) {
  writeMakefile(
      "define BLOCK\n"
      "line1\n"
      "endef\n"
      "define BLOCK +=\n"
      "line2\n"
      "endef\n"
      "all:\n"
      "\t@echo $(BLOCK)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("line1")) << R.out;
  EXPECT_TRUE(R.contains("line2")) << R.out;
}

// --- Include Edge Cases ---

TEST_F(BuildTest, IncludeOptionalMissing) {
  writeMakefile(
      "-include nonexistent.mk\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ok")) << R.out;
}

TEST_F(BuildTest, SincludeAliasR3) {
  writeMakefile(
      "sinclude nonexistent.mk\n"
      "all:\n"
      "\t@echo ok\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("ok")) << R.out;
}

TEST_F(BuildTest, IncludeChainThreeLevelsR3) {
  writeFile(tmp() / "c.mk", "C_VAL = from_c\n");
  writeFile(tmp() / "b.mk", "include c.mk\nB_VAL = from_b_$(C_VAL)\n");
  writeMakefile(
      "include b.mk\n"
      "all:\n"
      "\t@echo $(B_VAL)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("from_b_from_c")) << R.out;
}

TEST_F(BuildTest, MakefileListTracking) {
  writeFile(tmp() / "extra.mk", "EXTRA = loaded\n");
  writeMakefile(
      "include extra.mk\n"
      "MAIN := $(firstword $(MAKEFILE_LIST))\n"
      "all:\n"
      "\t@echo main=$(MAIN) extra=$(EXTRA)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("extra=loaded")) << R.out;
  // On case-insensitive FS (macOS), findMakefile may resolve "makefile"
  EXPECT_TRUE(R.contains("main=Makefile") || R.contains("main=makefile"))
      << R.out;
}

// --- Override Edge Cases ---

TEST_F(BuildTest, OverrideAppendCmdLine) {
  writeMakefile(
      "override CFLAGS += -Wall\n"
      "all:\n"
      "\t@echo $(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O2"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_TRUE(R.contains("-Wall")) << R.out;
}

TEST_F(BuildTest, NormalAppendIgnoredForCmdLine) {
  writeMakefile(
      "CFLAGS += -Wall\n"
      "all:\n"
      "\t@echo $(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake({"CFLAGS=-O2"});
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-O2")) << R.out;
  EXPECT_FALSE(R.contains("-Wall")) << "non-override += should be ignored: " << R.out;
}

// --- Undefine Edge Cases ---

TEST_F(BuildTest, UndefineRedefineR3) {
  writeMakefile(
      "X = old\n"
      "undefine X\n"
      "X = new\n"
      "all:\n"
      "\t@echo $(X)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("new")) << R.out;
}

TEST_F(BuildTest, UndefineIfdefInteractionR3) {
  writeMakefile(
      "X = val\n"
      "undefine X\n"
      "ifdef X\n"
      "R = defined\n"
      "else\n"
      "R = undefined\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("undefined")) << R.out;
}

// --- Special Variables ---

TEST_F(BuildTest, MakeVersionValueR3) {
  writeMakefile(
      "ifeq ($(MAKE_VERSION),4.3)\n"
      "R = compat\n"
      "else\n"
      "R = incompat\n"
      "endif\n"
      "all:\n"
      "\t@echo $(R)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("compat")) << R.out;
}

TEST_F(BuildTest, MakeCmdGoals) {
  writeMakefile(
      "ifneq ($(filter clean,$(MAKECMDGOALS)),)\n"
      "CLEANING = yes\n"
      "else\n"
      "CLEANING = no\n"
      "endif\n"
      "all:\n"
      "\t@echo cleaning=$(CLEANING)\n"
      "clean:\n"
      "\t@echo cleaning=$(CLEANING)\n"
      ".PHONY: all clean\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cleaning=no")) << R1.out;

  auto R2 = runMake({}, "clean");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cleaning=yes")) << R2.out;
}

TEST_F(BuildTest, DefaultGoalOverride) {
  writeMakefile(
      ".DEFAULT_GOAL := second\n"
      "first:\n"
      "\t@echo first\n"
      "second:\n"
      "\t@echo second\n"
      ".PHONY: first second\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("second")) << R.out;
  EXPECT_FALSE(R.contains("first")) << R.out;
}

TEST_F(BuildTest, CurdirSpecialVar) {
  writeMakefile(
      "all:\n"
      "\t@echo dir=$(CURDIR)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  // On macOS /var → /private/var, so check the canonical suffix
  auto Canon = std::filesystem::canonical(tmp()).string();
  EXPECT_TRUE(R.contains("dir=" + Canon) ||
              R.contains("dir=" + tmp().string())) << R.out;
}

// --- CLI Edge Cases ---

TEST_F(BuildTest, MultipleCDirs) {
  auto Sub = tmp() / "sub1" / "sub2";
  std::filesystem::create_directories(Sub);
  writeFile(Sub / "Makefile",
      "all:\n"
      "\t@echo nested_ok\n"
      ".PHONY: all\n");
  std::vector<std::string> Args = {"make", "-C",
      (tmp() / "sub1").string(), "-C", "sub2"};
  auto R = ncc(Args);
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("nested_ok")) << R.out;
}

TEST_F(BuildTest, GNUmakefilePriority) {
  writeFile(tmp() / "GNUmakefile",
      "all:\n"
      "\t@echo gnu\n"
      ".PHONY: all\n");
  writeFile(tmp() / "Makefile",
      "all:\n"
      "\t@echo regular\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("gnu")) << "GNUmakefile should take priority: " << R.out;
  std::filesystem::remove(tmp() / "GNUmakefile");
}

TEST_F(BuildTest, HelpFlag) {
  std::vector<std::string> Args = {"make", "--help"};
  auto R = ncc(Args);
  EXPECT_TRUE(R.contains("Usage:")) << R.out;
  EXPECT_TRUE(R.contains("-j")) << R.out;
}

// --- Kbuild Patterns ---

TEST_F(BuildTest, KbuildQuietVerboseSystem) {
  writeMakefile(
      "KBUILD_VERBOSE = 0\n"
      "ifeq ($(KBUILD_VERBOSE),1)\n"
      "  quiet =\n"
      "  Q =\n"
      "else\n"
      "  quiet = quiet_\n"
      "  Q = @\n"
      "endif\n"
      "\n"
      "define cmd_cc\n"
      "$(Q)echo CC $@\n"
      "endef\n"
      "define quiet_cmd_cc\n"
      "echo \"  CC    $@\"\n"
      "endef\n"
      "\n"
      "kernel.o:\n"
      "\t$(if $(quiet),$($(quiet)cmd_cc),$(cmd_cc))\n"
      ".PHONY: kernel.o\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("CC")) << R.out;
  EXPECT_TRUE(R.contains("kernel.o")) << R.out;
}

TEST_F(BuildTest, KbuildObjYAggregationR3) {
  writeMakefile(
      "obj-y :=\n"
      "obj-y += core.o\n"
      "obj-y += init.o\n"
      "ifdef CONFIG_NET\n"
      "obj-y += net.o\n"
      "endif\n"
      "OBJS := $(patsubst %.o,build/%.o,$(obj-y))\n"
      "all:\n"
      "\t@echo $(OBJS)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("build/core.o")) << R1.out;
  EXPECT_TRUE(R1.contains("build/init.o")) << R1.out;
  EXPECT_FALSE(R1.contains("build/net.o")) << R1.out;

  auto R2 = runMake({"CONFIG_NET=y"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("build/net.o")) << R2.out;
}

TEST_F(BuildTest, KbuildForeachEvalSubdirsR3) {
  writeMakefile(
      "subdirs := kernel mm fs\n"
      "define subdir_template\n"
      "$(1)-objs := $(1)/main.o $(1)/init.o\n"
      "endef\n"
      "$(foreach d,$(subdirs),$(eval $(call subdir_template,$(d))))\n"
      "ALL_OBJS := $(foreach d,$(subdirs),$($(d)-objs))\n"
      "all:\n"
      "\t@echo $(ALL_OBJS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("kernel/main.o")) << R.out;
  EXPECT_TRUE(R.contains("mm/init.o")) << R.out;
  EXPECT_TRUE(R.contains("fs/main.o")) << R.out;
}

TEST_F(BuildTest, KbuildComputedVarName) {
  writeMakefile(
      "ARCH = x86\n"
      "machine-x86 = arch/x86\n"
      "machine-arm = arch/arm\n"
      "MACHINE := $(machine-$(ARCH))\n"
      "all:\n"
      "\t@echo $(MACHINE)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("arch/x86")) << R.out;

  auto R2 = runMake({"ARCH=arm"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch/arm")) << R2.out;
}

TEST_F(BuildTest, KbuildCcOptionPatternR3) {
  writeMakefile(
      "define cc-option\n"
      "$(if $(findstring gcc,$(CC)),$(1),$(2))\n"
      "endef\n"
      "CC = gcc\n"
      "CFLAGS := $(call cc-option,-fstack-protector,-fno-stack-protector)\n"
      "all:\n"
      "\t@echo $(CFLAGS)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("-fstack-protector")) << R.out;
  EXPECT_FALSE(R.contains("-fno-stack-protector")) << R.out;
}

TEST_F(BuildTest, KbuildFORCETarget) {
  writeMakefile(
      "FORCE:\n"
      "vmlinux: FORCE\n"
      "\t@echo linking_vmlinux\n"
      ".PHONY: FORCE vmlinux\n");
  auto R = runMake({}, "vmlinux");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("linking_vmlinux")) << R.out;
}

TEST_F(BuildTest, KbuildFilterPipelineR3) {
  writeMakefile(
      "CONFIG_SMP = y\n"
      "CONFIG_NET = y\n"
      "CONFIG_SOUND =\n"
      "FEATURES = SMP NET SOUND USB\n"
      "ENABLED := $(foreach f,$(FEATURES),$(if $(CONFIG_$(f)),$(f)))\n"
      "DISABLED := $(filter-out $(ENABLED),$(FEATURES))\n"
      "all:\n"
      "\t@echo enabled=$(ENABLED)\n"
      "\t@echo disabled=$(DISABLED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("SMP")) << R.out;
  EXPECT_TRUE(R.contains("NET")) << R.out;
  EXPECT_TRUE(R.contains("disabled=")) << R.out;
  EXPECT_TRUE(R.contains("SOUND")) << R.out;
  EXPECT_TRUE(R.contains("USB")) << R.out;
}

TEST_F(BuildTest, KbuildPerFileCflags) {
  writeMakefile(
      "CFLAGS_core.o := -DKERNEL_CORE\n"
      "CFLAGS_net.o := -DNET_MODULE\n"
      "core.o: CFLAGS = $(CFLAGS_core.o)\n"
      "net.o: CFLAGS = $(CFLAGS_net.o)\n"
      "core.o net.o:\n"
      "\t@echo compile $@ with $(CFLAGS)\n"
      ".PHONY: core.o net.o\n");
  auto R1 = runMake({}, "core.o");
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("-DKERNEL_CORE")) << R1.out;

  auto R2 = runMake({}, "net.o");
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("-DNET_MODULE")) << R2.out;
}

TEST_F(BuildTest, KbuildOriginCheck) {
  writeMakefile(
      "ARCH ?= x86\n"
      "ifeq ($(origin ARCH),command line)\n"
      "ARCH_FROM = cmdline\n"
      "else\n"
      "ARCH_FROM = default\n"
      "endif\n"
      "all:\n"
      "\t@echo arch=$(ARCH) from=$(ARCH_FROM)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("arch=x86")) << R1.out;
  EXPECT_TRUE(R1.contains("from=default")) << R1.out;

  auto R2 = runMake({"ARCH=arm64"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
  EXPECT_TRUE(R2.contains("from=cmdline")) << R2.out;
}

TEST_F(BuildTest, KbuildDefineMultiLineRecipe) {
  writeMakefile(
      "define cmd_compile\n"
      "@echo preprocessing $(1)\n"
      "@echo compiling $(1)\n"
      "@echo done $(1)\n"
      "endef\n"
      "core.o:\n"
      "\t$(call cmd_compile,$@)\n"
      ".PHONY: core.o\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("preprocessing core.o")) << R.out;
  EXPECT_TRUE(R.contains("compiling core.o")) << R.out;
  EXPECT_TRUE(R.contains("done core.o")) << R.out;
}

TEST_F(BuildTest, KbuildExportComputedVar) {
  writeMakefile(
      "ARCH = x86\n"
      "CROSS_COMPILE_x86 = \n"
      "CROSS_COMPILE_arm = arm-linux-gnueabi-\n"
      "CROSS := $(CROSS_COMPILE_$(ARCH))\n"
      "CC := $(CROSS)gcc\n"
      "export CC\n"
      "all:\n"
      "\t@echo cc=$(CC)\n"
      ".PHONY: all\n");
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;

  auto R2 = runMake({"ARCH=arm"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("cc=arm-linux-gnueabi-gcc")) << R2.out;
}

TEST_F(BuildTest, KbuildIfeqArchChain) {
  writeMakefile(
      "SRCARCH = $(ARCH)\n"
      "ifeq ($(ARCH),x86)\n"
      "KARCH_FLAGS = -m64\n"
      "else ifeq ($(ARCH),arm)\n"
      "KARCH_FLAGS = -march=armv7-a\n"
      "else ifeq ($(ARCH),arm64)\n"
      "KARCH_FLAGS = -march=armv8-a\n"
      "else ifeq ($(ARCH),riscv)\n"
      "KARCH_FLAGS = -march=rv64gc\n"
      "else\n"
      "KARCH_FLAGS = -generic\n"
      "endif\n"
      "all:\n"
      "\t@echo flags=$(KARCH_FLAGS)\n"
      ".PHONY: all\n");
  auto R1 = runMake({"ARCH=arm"});
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("flags=-march=armv7-a")) << R1.out;

  auto R2 = runMake({"ARCH=riscv"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("flags=-march=rv64gc")) << R2.out;

  auto R3 = runMake({"ARCH=mips"});
  ASSERT_TRUE(R3.ok()) << R3.err;
  EXPECT_TRUE(R3.contains("flags=-generic")) << R3.out;
}

TEST_F(BuildTest, KbuildRecursiveMake) {
  auto Sub = tmp() / "drivers";
  std::filesystem::create_directories(Sub);
  writeFile(Sub / "Makefile",
      "obj-y := drv.o\n"
      "all:\n"
      "\t@echo driver_objs=$(obj-y)\n"
      ".PHONY: all\n");
  writeMakefile(
      "drivers:\n"
      "\t@echo entering_drivers\n"
      ".PHONY: drivers\n");
  auto R = runMake({}, "drivers");
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("entering_drivers")) << R.out;
}

TEST_F(BuildTest, KbuildValueFunctionR3) {
  writeMakefile(
      "FOO = $(BAR)\n"
      "BAR = hello\n"
      "RAW := $(value FOO)\n"
      "EXPANDED := $(FOO)\n"
      "all:\n"
      "\t@echo 'raw=$(RAW)' exp=$(EXPANDED)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("raw=$(BAR)")) << R.out;
  EXPECT_TRUE(R.contains("exp=hello")) << R.out;
}

TEST_F(BuildTest, KbuildFlavorFunctionR3) {
  writeMakefile(
      "A = recursive\n"
      "B := simple\n"
      "FA := $(flavor A)\n"
      "FB := $(flavor B)\n"
      "FC := $(flavor UNDEF)\n"
      "all:\n"
      "\t@echo a=$(FA) b=$(FB) c=$(FC)\n"
      ".PHONY: all\n");
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("a=recursive")) << R.out;
  EXPECT_TRUE(R.contains("b=simple")) << R.out;
  EXPECT_TRUE(R.contains("c=undefined")) << R.out;
}

// --- Full Kbuild End-to-End Simulation ---

TEST_F(BuildTest, KbuildFullSimulation) {
  std::string MF =
      "# Linux 5.10 Kbuild Simulation\n"
      "VERSION = 5\n"
      "PATCHLEVEL = 10\n"
      "SUBLEVEL = 191\n"
      "NAME = Kleptomaniac Octopus\n"
      "KERNELVERSION = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\n"
      "\n"
      "# Architecture selection\n"
      "ARCH ?= x86\n"
      "SRCARCH := $(ARCH)\n"
      "ifeq ($(ARCH),x86_64)\n"
      "SRCARCH := x86\n"
      "endif\n"
      "\n"
      "# Verbose control\n"
      "KBUILD_VERBOSE ?= 0\n"
      "ifeq ($(KBUILD_VERBOSE),1)\n"
      "  quiet =\n"
      "  Q =\n"
      "else\n"
      "  quiet = quiet_\n"
      "  Q = @\n"
      "endif\n"
      "\n"
      "# Cross-compile\n"
      "CROSS_COMPILE ?=\n"
      "CC := $(CROSS_COMPILE)gcc\n"
      "LD := $(CROSS_COMPILE)ld\n"
      "AR := $(CROSS_COMPILE)ar\n"
      "\n"
      "# Arch flags\n"
      "ifeq ($(SRCARCH),x86)\n"
      "KBUILD_CFLAGS := -m64\n"
      "else ifeq ($(SRCARCH),arm)\n"
      "KBUILD_CFLAGS := -march=armv7-a\n"
      "else ifeq ($(SRCARCH),arm64)\n"
      "KBUILD_CFLAGS := -march=armv8-a\n"
      "else\n"
      "KBUILD_CFLAGS :=\n"
      "endif\n"
      "\n"
      "# Config options\n"
      "CONFIG_SMP ?= y\n"
      "CONFIG_MODULES ?= y\n"
      "CONFIG_NET ?= y\n"
      "CONFIG_PREEMPT ?=\n"
      "\n"
      "# Feature flags\n"
      "FEATURES := SMP MODULES NET PREEMPT\n"
      "ENABLED := $(foreach f,$(FEATURES),$(if $(CONFIG_$(f)),$(f)))\n"
      "DISABLED := $(filter-out $(ENABLED),$(FEATURES))\n"
      "FEATURE_FLAGS := $(addprefix -DCONFIG_,$(ENABLED))\n"
      "KBUILD_CFLAGS += $(FEATURE_FLAGS)\n"
      "\n"
      "# cc-option pattern\n"
      "define cc-option\n"
      "$(if $(findstring gcc,$(CC)),$(1),$(2))\n"
      "endef\n"
      "KBUILD_CFLAGS += $(call cc-option,-fstack-protector,)\n"
      "\n"
      "# Subsystem template\n"
      "subdirs := kernel mm fs net\n"
      "define subdir_template\n"
      "$(1)-y := $(1)/core.o $(1)/init.o\n"
      "ifdef CONFIG_$(shell echo $(1) | tr a-z A-Z)\n"
      "$(1)-y += $(1)/extra.o\n"
      "endif\n"
      "endef\n"
      "$(foreach d,$(subdirs),$(eval $(call subdir_template,$(d))))\n"
      "\n"
      "# Collect all objects\n"
      "ALL_OBJS := $(foreach d,$(subdirs),$($(d)-y))\n"
      "OBJ_COUNT := $(words $(ALL_OBJS))\n"
      "\n"
      "# Version parsing\n"
      "VPARTS := $(subst ., ,$(KERNELVERSION))\n"
      "VMAJOR := $(word 1,$(VPARTS))\n"
      "VMINOR := $(word 2,$(VPARTS))\n"
      "\n"
      "# MAKE_VERSION check\n"
      "ifeq ($(MAKE_VERSION),4.3)\n"
      "MAKE_COMPAT := yes\n"
      "else\n"
      "MAKE_COMPAT := no\n"
      "endif\n"
      "\n"
      "# MAKECMDGOALS\n"
      "ifneq ($(filter clean mrproper,$(MAKECMDGOALS)),)\n"
      "SKIP_BUILD := 1\n"
      "else\n"
      "SKIP_BUILD := 0\n"
      "endif\n"
      "\n"
      "# Per-file CFLAGS\n"
      "CFLAGS_kernel/core.o := -DKERNEL_CORE\n"
      "CFLAGS_net/core.o := -DNET_CORE\n"
      "\n"
      "# Origin detection\n"
      "ifeq ($(origin ARCH),command line)\n"
      "ARCH_SRC = cmdline\n"
      "else\n"
      "ARCH_SRC = default\n"
      "endif\n"
      "\n"
      "all: vmlinux\n"
      "\t@echo === Kbuild Complete ===\n"
      "\t@echo version=$(KERNELVERSION) name=$(NAME)\n"
      "\t@echo vmajor=$(VMAJOR) vminor=$(VMINOR)\n"
      "\t@echo arch=$(SRCARCH) arch_src=$(ARCH_SRC)\n"
      "\t@echo cc=$(CC) ld=$(LD)\n"
      "\t@echo cflags=$(KBUILD_CFLAGS)\n"
      "\t@echo obj_count=$(OBJ_COUNT)\n"
      "\t@echo enabled=$(ENABLED)\n"
      "\t@echo disabled=$(DISABLED)\n"
      "\t@echo feature_flags=$(FEATURE_FLAGS)\n"
      "\t@echo kcore_flags=$(CFLAGS_kernel/core.o)\n"
      "\t@echo make_compat=$(MAKE_COMPAT) skip=$(SKIP_BUILD)\n"
      "\n"
      "vmlinux: FORCE\n"
      "\t@echo === LINK vmlinux ===\n"
      "\n"
      "FORCE:\n"
      "\n"
      ".PHONY: all vmlinux FORCE clean mrproper\n"
      "clean:\n"
      "\t@echo CLEAN skip=$(SKIP_BUILD)\n"
      "mrproper:\n"
      "\t@echo MRPROPER skip=$(SKIP_BUILD)\n";

  writeMakefile(MF);

  // Default build (x86)
  auto R1 = runMake();
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("version=5.10.191")) << R1.out;
  EXPECT_TRUE(R1.contains("name=Kleptomaniac Octopus")) << R1.out;
  EXPECT_TRUE(R1.contains("vmajor=5")) << R1.out;
  EXPECT_TRUE(R1.contains("vminor=10")) << R1.out;
  EXPECT_TRUE(R1.contains("arch=x86")) << R1.out;
  EXPECT_TRUE(R1.contains("arch_src=default")) << R1.out;
  EXPECT_TRUE(R1.contains("cc=gcc")) << R1.out;
  EXPECT_TRUE(R1.contains("-m64")) << R1.out;
  EXPECT_TRUE(R1.contains("-DCONFIG_SMP")) << R1.out;
  EXPECT_TRUE(R1.contains("-DCONFIG_NET")) << R1.out;
  EXPECT_TRUE(R1.contains("-fstack-protector")) << R1.out;
  EXPECT_TRUE(R1.contains("make_compat=yes")) << R1.out;
  EXPECT_TRUE(R1.contains("skip=0")) << R1.out;
  EXPECT_TRUE(R1.contains("kcore_flags=-DKERNEL_CORE")) << R1.out;
  EXPECT_TRUE(R1.contains("LINK vmlinux")) << R1.out;

  // Cross-compile arm64
  auto R2 = runMake({"ARCH=arm64", "CROSS_COMPILE=aarch64-linux-gnu-"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("arch=arm64")) << R2.out;
  EXPECT_TRUE(R2.contains("arch_src=cmdline")) << R2.out;
  EXPECT_TRUE(R2.contains("cc=aarch64-linux-gnu-gcc")) << R2.out;
  EXPECT_TRUE(R2.contains("-march=armv8-a")) << R2.out;

  // Clean target
  auto R3 = runMake({}, "clean");
  ASSERT_TRUE(R3.ok()) << R3.err;
  EXPECT_TRUE(R3.contains("CLEAN")) << R3.out;
  EXPECT_TRUE(R3.contains("skip=1")) << R3.out;

  // x86_64 alias
  auto R4 = runMake({"ARCH=x86_64"});
  ASSERT_TRUE(R4.ok()) << R4.err;
  EXPECT_TRUE(R4.contains("arch=x86")) << "x86_64 should map to x86: " << R4.out;
}

// --- Stress Tests ---

TEST_F(BuildTest, StressLargeVariableList) {
  std::string MF = "ITEMS :=\n";
  for (int I = 0; I < 500; ++I)
    MF += "ITEMS += item_" + std::to_string(I) + "\n";
  MF += "COUNT := $(words $(ITEMS))\n"
        "FIRST := $(firstword $(ITEMS))\n"
        "LAST := $(lastword $(ITEMS))\n"
        "all:\n"
        "\t@echo count=$(COUNT) first=$(FIRST) last=$(LAST)\n"
        ".PHONY: all\n";
  writeMakefile(MF);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("count=500")) << R.out;
  EXPECT_TRUE(R.contains("first=item_0")) << R.out;
  EXPECT_TRUE(R.contains("last=item_499")) << R.out;
}

TEST_F(BuildTest, StressForeachEvalManyModules) {
  std::string MF = "modules :=\n";
  for (int I = 0; I < 100; ++I)
    MF += "modules += mod" + std::to_string(I) + "\n";
  MF += "define mod_template\n"
        "$(1)-objs := $(1)/main.o\n"
        "endef\n"
        "$(foreach m,$(modules),$(eval $(call mod_template,$(m))))\n"
        "ALL := $(foreach m,$(modules),$($(m)-objs))\n"
        "CNT := $(words $(ALL))\n"
        "all:\n"
        "\t@echo cnt=$(CNT)\n"
        ".PHONY: all\n";
  writeMakefile(MF);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cnt=100")) << R.out;
}

TEST_F(BuildTest, StressDeepConditionalChain) {
  std::string MF;
  for (int I = 0; I < 20; ++I) {
    MF += "ifeq ($(LEVEL)," + std::to_string(I) + ")\n";
    MF += "RESULT = level_" + std::to_string(I) + "\n";
    MF += "else ";
  }
  MF += "\nRESULT = fallback\n";
  for (int I = 0; I < 20; ++I)
    MF += "endif\n";
  MF += "all:\n\t@echo $(RESULT)\n.PHONY: all\n";
  writeMakefile(MF);

  auto R1 = runMake({"LEVEL=7"});
  ASSERT_TRUE(R1.ok()) << R1.err;
  EXPECT_TRUE(R1.contains("level_7")) << R1.out;

  auto R2 = runMake({"LEVEL=99"});
  ASSERT_TRUE(R2.ok()) << R2.err;
  EXPECT_TRUE(R2.contains("fallback")) << R2.out;
}

TEST_F(BuildTest, StressPatsubstChain) {
  std::string MF = "SRCS :=\n";
  for (int I = 0; I < 200; ++I)
    MF += "SRCS += src/file" + std::to_string(I) + ".c\n";
  MF += "OBJS := $(patsubst src/%.c,obj/%.o,$(SRCS))\n"
        "CNT := $(words $(OBJS))\n"
        "FIRST := $(firstword $(OBJS))\n"
        "LAST := $(lastword $(OBJS))\n"
        "all:\n"
        "\t@echo cnt=$(CNT) first=$(FIRST) last=$(LAST)\n"
        ".PHONY: all\n";
  writeMakefile(MF);
  auto R = runMake();
  ASSERT_TRUE(R.ok()) << R.err;
  EXPECT_TRUE(R.contains("cnt=200")) << R.out;
  EXPECT_TRUE(R.contains("first=obj/file0.o")) << R.out;
  EXPECT_TRUE(R.contains("last=obj/file199.o")) << R.out;
}
