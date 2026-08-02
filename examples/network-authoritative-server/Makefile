# Cross-platform authoritative game server example.

NEVERC ?= ../../build-neverc/bin/neverc
TARGET ?=
OUTPUT ?= authoritative-server

TARGET_FLAG = $(if $(TARGET),--target=$(TARGET),)
FLAGS = $(TARGET_FLAG) -std=c11 -Wall -Wextra -Werror -O2

all: $(OUTPUT)

$(OUTPUT): main.c
	$(NEVERC) $(FLAGS) -o $@ $<

clean:
	rm -f $(OUTPUT) $(OUTPUT).exe

.PHONY: all clean
