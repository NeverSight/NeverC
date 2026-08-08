# mTLS anti-cheat telemetry collector — default target x86_64-linux-gnu
# Usage: neverc make [TARGET=aarch64-linux-gnu]

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

OUTPUT ?= anticheat-collector

# TARGET defaults to Linux x86_64. PROFILE=debug|release selects debuggable or
# distribution output. Command-line values win; otherwise reuse the stamp so
# later builds keep the selected artifact. Persist via $(file): no host shell,
# so `neverc make` works on Windows too.
FLAGS_STAMP := .nvk-build-flags
SAVED_BUILD_ID := $(file <$(FLAGS_STAMP))
SAVED_TARGET := $(patsubst TARGET=%,%,$(filter TARGET=%,$(SAVED_BUILD_ID)))
SAVED_PROFILE := $(patsubst PROFILE=%,%,$(filter PROFILE=%,$(SAVED_BUILD_ID)))
ifneq ($(origin TARGET),command line)
TARGET := $(if $(SAVED_TARGET),$(SAVED_TARGET),x86_64-linux-gnu)
endif
ifneq ($(origin PROFILE),command line)
PROFILE := $(if $(SAVED_PROFILE),$(SAVED_PROFILE),debug)
endif

VALID_PROFILES := debug release
ifeq ($(filter $(PROFILE),$(VALID_PROFILES)),)
$(error unsupported PROFILE '$(PROFILE)'; expected one of: $(VALID_PROFILES))
endif

PROFILE_FLAGS_debug   := -g
PROFILE_FLAGS_release := -O2 --strip
PROFILE_FLAGS := $(PROFILE_FLAGS_$(PROFILE))

FLAGS = \
	--target=$(TARGET) \
	-std=c11 \
	-Wall -Wextra -Werror \
	$(PROFILE_FLAGS)

BUILD_ID := TARGET=$(TARGET) PROFILE=$(PROFILE)
ifneq ($(strip $(file <$(FLAGS_STAMP))),$(BUILD_ID))
_ := $(file >$(FLAGS_STAMP),$(BUILD_ID))
endif

all: $(OUTPUT)

debug:
	$(MAKE) PROFILE=debug all

release:
	$(MAKE) PROFILE=release all

$(OUTPUT): main.c $(FLAGS_STAMP)
	$(NEVERC) $(FLAGS) -o $@ $<

clean:
	rm -f $(OUTPUT) $(OUTPUT).exe $(FLAGS_STAMP)

.PHONY: all debug release clean
