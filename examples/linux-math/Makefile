# Linux Math + zlib — trig, special functions, compress/decompress
# Usage: neverc make [TARGET=aarch64-linux-gnu]

NEVERC ?= ../../build-neverc/bin/neverc

TARGET ?= x86_64-linux-gnu
OUTPUT  = math-demo

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-lm -lz

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
	rm -f $(OUTPUT) $(OBJS) $(DEPS)

# Auto-generated header dependencies
-include $(DEPS)

.PHONY: all clean
