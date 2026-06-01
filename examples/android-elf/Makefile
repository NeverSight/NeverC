# Android Native ELF — ARM64 binary for rooted devices
# Usage: neverc make
#        neverc make run    (build + push via adb + execute)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
OUTPUT  = android-elf

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-fPIE \
	-lm \
	-ldl \
	-llog

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

push: $(OUTPUT)
	adb push $(OUTPUT) /data/local/tmp/
	adb shell chmod 755 /data/local/tmp/$(OUTPUT)

run: push
	adb shell /data/local/tmp/$(OUTPUT)

clean:
	rm -f $(OUTPUT) $(OBJS) $(DEPS)

# Auto-generated header dependencies
-include $(DEPS)

.PHONY: all push run clean
