# Android GKI kernel full SDK demo (.ko)
#
# Usage:
#   make                       (default kernel 510)
#   make KERNEL=606            (target 6.6)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_full.ko
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
