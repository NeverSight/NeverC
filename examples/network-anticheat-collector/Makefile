# Cross-platform mTLS anti-cheat telemetry collector example.

NEVERC ?= ../../build-neverc/bin/neverc
TARGET ?=
OUTPUT ?= anticheat-collector

TARGET_FLAG = $(if $(TARGET),--target=$(TARGET),)
FLAGS = $(TARGET_FLAG) -std=c11 -Wall -Wextra -Werror -O2

all: $(OUTPUT)

$(OUTPUT): main.c
	$(NEVERC) $(FLAGS) -o $@ $<

clean:
	rm -f $(OUTPUT) $(OUTPUT).exe

.PHONY: all clean
