# Android Shared Library — ARM64 .so for rooted devices
# Usage: neverc make

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
OUTPUT  = libneverc.so

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-shared \
	-fPIC \
	-ldl

SRCS = lib.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

all: $(OUTPUT)

# Link
$(OUTPUT): $(OBJS)
	$(NEVERC) $(FLAGS) -o $@ $^

# Compile (generates .d dependency files for incremental builds)
%.o: %.c
	$(NEVERC) $(FLAGS) -MMD -c $< -o $@

push: $(OUTPUT)
	adb push $(OUTPUT) /data/local/tmp/
	adb shell chmod 755 /data/local/tmp/$(OUTPUT)

clean:
	rm -f $(OUTPUT) $(OBJS) $(DEPS)

# Auto-generated header dependencies
-include $(DEPS)

.PHONY: all push clean
