# Android GKI kernel inline-hook demo (.ko)
#
# Usage:
#   make                                       (simple hook mode)
#   make EXTRA=-DNVK_CONTEXT_HOOK              (context hook mode)
#   make KERNEL=601                            (target 6.1)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_inline_hook.ko
SRCS    = main.c

KERNEL ?= 510
EXTRA  ?=

FLAGS = \
	--target=$(TARGET) \
	-fandroid-kernel-driver-mode \
	-DNVK_KERNEL=$(KERNEL) \
	$(EXTRA) \
	-Wall -Wno-unused

all: $(MODULE)

$(MODULE): $(SRCS)
	$(NEVERC) $(FLAGS) -r -nostdlib -o $@ $(SRCS)

clean:
	rm -f $(MODULE) *.o

.PHONY: all clean
