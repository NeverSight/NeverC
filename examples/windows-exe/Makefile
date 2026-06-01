# Windows Ring3 EXE — user-mode console application
# Usage: neverc make

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
OUTPUT  = example.exe

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-Xlinker --subsystem=console \
	-lkernel32 -luser32 -lmsvcrt

SRCS = main.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

all: $(OUTPUT)

# Link
$(OUTPUT): $(OBJS)
	$(NEVERC) $(FLAGS) -o $@ $^

# Compile (generates .d dependency files for incremental builds)
%.o: %.c
	$(NEVERC) $(FLAGS) -MMD -c $< -o $@

clean:
	rm -f $(OUTPUT) $(OUTPUT:.exe=.lib) $(OBJS) $(DEPS)

# Auto-generated header dependencies
-include $(DEPS)

.PHONY: all clean
