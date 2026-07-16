# NeverC Plugin Examples — build each example as a standalone shared library
#
# Each .c file compiles into a corresponding shared library:
#   ExamplePlugin.c        -> ExamplePlugin.{dll,dylib,so}
#   CrtShimPlugin.c        -> CrtShimPlugin.{dll,dylib,so}
#   BenchPlugin.c          -> BenchPlugin.{dll,dylib,so}
#   CustomCallConvPlugin.c -> CustomCallConvPlugin.{dll,dylib,so}
#   DriverTracePlugin.c    -> DriverTracePlugin.{dll,dylib,so}
#
# Usage:
#   neverc make                        # build all plugins
#   neverc make ExamplePlugin.dylib    # build a single plugin
#   neverc make clean                  # remove built plugins
#   neverc make NEVERC=/path/to/neverc # override compiler path

NEVERC ?= ../../build-neverc/bin/neverc

# Each entry becomes a separate shared library (one .c -> one plugin).
PLUGINS = ExamplePlugin CrtShimPlugin BenchPlugin CustomCallConvPlugin \
          DriverTracePlugin

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
  ifeq ($(UNAME_M),aarch64)
    HOST_TARGET ?= aarch64-unknown-linux-gnu
  else
    HOST_TARGET ?= x86_64-pc-linux-gnu
  endif
  SHARED_EXT = .so
else
  ifeq ($(UNAME_M),aarch64)
    HOST_TARGET ?= aarch64-pc-windows-msvc
  else
    HOST_TARGET ?= x86_64-pc-windows-msvc
  endif
  SHARED_EXT = .dll
endif

# SDK header search path (parent of neverc/Plugin/NevercPluginAPI.h).
#   Distributed SDK:  ../include  (pluginsdk/include/ sits next to examples/)
#   Source tree:       ../../neverc/include
ifneq ($(wildcard ../include/neverc/Plugin/NevercPluginAPI.h),)
  INCLUDE_DIR ?= ../include
else ifneq ($(wildcard ../../neverc/include/neverc/Plugin/NevercPluginAPI.h),)
  INCLUDE_DIR ?= ../../neverc/include
else
  $(error NevercPluginAPI.h not found. Set INCLUDE_DIR=/path/to/include)
endif

# -shared: produce a loadable plugin library
# -I: SDK header include path (single-header, zero LLVM/CRT dependencies)
FLAGS = --target=$(HOST_TARGET) -shared -I$(INCLUDE_DIR)

OUTPUTS = $(addsuffix $(SHARED_EXT),$(PLUGINS))

all: $(OUTPUTS)

# Pattern rule: compile each .c into a shared library with matching name.
%$(SHARED_EXT): %.c
	$(NEVERC) $(FLAGS) -o $@ $<

clean:
	rm -f $(OUTPUTS)

.PHONY: all clean
