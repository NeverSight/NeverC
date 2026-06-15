# Android GKI kernel inline-hook demo (.ko)
# Usage: make            (or: make KERNEL=601 for 6.1, etc.)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_inline_hook.ko
SRCS    = main.c

# -DNVK_KERNEL=510|515|601|606|612 selects the target kernel preset.
KERNEL ?= 510

FLAGS = \
	--target=$(TARGET) \
	-fandroid-kernel-driver-mode \
	-DNVK_KERNEL=$(KERNEL) \
	-Wall -Wno-unused

all: $(MODULE)

$(MODULE): $(SRCS)
	$(NEVERC) $(FLAGS) -r -nostdlib -o $@ $(SRCS)

clean:
	rm -f $(MODULE) *.o

.PHONY: all clean
