#ifndef NEVERC_TESTS_X86_NATIVEXSTATEFIXTURE_H
#define NEVERC_TESTS_X86_NATIVEXSTATEFIXTURE_H

#include <linux/kvm.h>
#include <stdint.h>
#include <string.h>

// KVM may materialize an initial component while saving its host FPU context.
// XINUSE=1 does not imply non-initial register values (SDM 13.6). Compare the
// represented architectural values, not that permitted tracking choice.
static inline void canonical_xsave7(struct kvm_xsave *state) {
  uint8_t *b = (void *)state;
  const uint64_t bv = *(uint64_t *)(b + 512);
  if (!(bv & 1)) {
    memset(b, 0, 24);
    *(uint16_t *)b = 0x37f;
    memset(b + 32, 0, 128);
  }
  if (!(bv & 2))
    memset(b + 160, 0, 256);
  if (!(bv & 4))
    memset(b + 576, 0, 256);
  // Standard XRSTOR loads MXCSR even for initial SSE/AVX components.
  *(uint64_t *)(b + 512) = bv | 7;
}

#endif
