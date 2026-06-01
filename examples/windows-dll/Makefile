# Windows Ring3 DLL — user-mode dynamic library
# Usage: neverc make

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
OUTPUT  = example.dll

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-shared \
	-Xlinker --entry=DllMain \
	-Xlinker --subsystem=windows \
	-lkernel32 -luser32

SRCS = dllmain.c
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
	rm -f $(OUTPUT) $(OUTPUT:.dll=.lib) $(OBJS) $(DEPS)

# Auto-generated header dependencies
-include $(DEPS)

.PHONY: all clean
