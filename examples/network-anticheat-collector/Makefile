# Cross-platform mTLS anti-cheat telemetry collector example.

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

TARGET ?=
OUTPUT ?= anticheat-collector

TARGET_FLAG = $(if $(TARGET),--target=$(TARGET),)

# PROFILE=debug|release selects debuggable or distribution output.
# Command-line PROFILE= wins; otherwise reuse the stamp so later builds keep
# the selected artifact. Persist via $(file): no host shell, so `neverc make`
# works on Windows too.
FLAGS_STAMP := .nvk-build-flags
SAVED_PROFILE := $(patsubst PROFILE=%,%,$(filter PROFILE=%,$(file <$(FLAGS_STAMP))))
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

FLAGS = $(TARGET_FLAG) -std=c11 -Wall -Wextra -Werror $(PROFILE_FLAGS)

BUILD_ID := PROFILE=$(PROFILE)
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
