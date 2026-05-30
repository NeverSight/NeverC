# ============================================================================
# NeverC Plugin Examples — Makefile
# ============================================================================
#
# Builds each example plugin as a standalone shared library:
#   ExamplePlugin.c  -> ExamplePlugin.{dll,dylib,so}
#   CrtShimPlugin.c  -> CrtShimPlugin.{dll,dylib,so}
#   BenchPlugin.c    -> BenchPlugin.{dll,dylib,so}
#
# Usage:
#   make                        # build all plugins (runs the default "all" target)
#   make all                    # same as above — explicit form
#   make ExamplePlugin.dylib    # build a single plugin (pattern rule matches %.c)
#   make clean                  # remove built plugins
#
# Override variables as needed:
#   make NEVERC=/path/to/neverc INCLUDE_DIR=/path/to/include
# ============================================================================

# Path to the neverc compiler (assumes neverc is on PATH for SDK users).
# In-tree override: make NEVERC=../../build-neverc/bin/neverc
NEVERC ?= neverc

# Each entry becomes a separate shared library (one .c -> one .dll/.dylib/.so).
PLUGINS = ExamplePlugin CrtShimPlugin BenchPlugin

# Auto-detect host platform for shared library suffix and target triple.
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
UNAME_M := $(shell uname -m 2>/dev/null || echo x86_64)

ifeq ($(UNAME_S),Darwin)
  ifeq ($(UNAME_M),arm64)
    HOST_TARGET ?= arm64-apple-macosx
  else
    HOST_TARGET ?= x86_64-apple-macosx
  endif
  SHARED_EXT = .dylib
else ifeq ($(UNAME_S),Linux)
  HOST_TARGET ?= x86_64-pc-linux-gnu
  SHARED_EXT = .so
else
  HOST_TARGET ?= x86_64-pc-windows-msvc
  SHARED_EXT = .dll
endif

# SDK header search path (parent of neverc/Plugin/NevercPluginAPI.h).
# In-tree default: ../../neverc/include
# Distributed SDK:  ../include  (pluginsdk/include/ sits next to examples/)
# CI overrides this via: make INCLUDE_DIR=$PWD/build/pluginsdk/include
INCLUDE_DIR ?= ../include
FLAGS = --target=$(HOST_TARGET) -shared -I$(INCLUDE_DIR)

# Expand plugin names to platform-specific output filenames.
OUTPUTS = $(addsuffix $(SHARED_EXT),$(PLUGINS))

all: $(OUTPUTS)

# Pattern rule: compile each .c into a shared library with matching name.
%$(SHARED_EXT): %.c
	$(NEVERC) $(FLAGS) -o $@ $<

clean:
	rm -f $(OUTPUTS)

.PHONY: all clean
