/* Original-byte NeverD V10 state projection, included after exported source.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
void *memcpy(void *to, const void *from, __SIZE_TYPE__ size) {
  volatile unsigned char *out = to;
  const volatile unsigned char *in = from;
  for (__SIZE_TYPE__ i = 0; i < size; ++i)
    out[i] = in[i];
  return to;
}
extern void *memset(void *, int, __SIZE_TYPE__);
static volatile unsigned continuation_failed;
extern uint64_t original_vmrun_invalid_cr2(uint64_t, uint64_t);
extern uint64_t original_vmrun_addr32_invalid_cr2(uint64_t, uint64_t);
static void continuation_vmrun(void *opaque, unsigned width) {
  uint64_t *context = opaque;
  uint8_t *xstate = (void *)(uintptr_t)context[1];
  neverd_x64_state_v10 state = {0};
  const unsigned registers[14] = {3, 1,  2,  6,  7,  5,  8,
                                  9, 10, 11, 12, 13, 14, 15};
  for (unsigned i = 0; i < 14; ++i)
    state.GPR[registers[i]] = context[4 + i];
  state.GPR[0] = context[0];
  state.GPR[4] = 0x70000; /* Original host RSP is hardware-restored. */
  const uint64_t prior_cr2 = 0x12345000 + (context[5] & 0xfff);
  state.CR2 = prior_cr2;
  state.RFlags = 0x10246; /* RF clears on completed VMRUN. */
  state.PC = width == 32 ? 0x1010 : 0x1020;
  state.SVM.GuestXState = context[1];
  state.SVM.HostXState = context[2];
  memcpy(&state.MXCSR, xstate + 24, 4);
  uint64_t present;
  memcpy(&present, xstate + 512, 8);
  for (unsigned r = 0; r < 16; ++r) {
    if (present & 2)
      memcpy(state.ZMM[r], xstate + 160 + r * 16, 16);
    if (present & 4)
      memcpy(state.ZMM[r] + 16, xstate + 576 + r * 16, 16);
  }
  uint64_t budget = 5;
  uint64_t status = neverd_x64_continue_v10(&state, &budget);
  uint64_t actual_cr2;
  __asm__ volatile("mov %%cr2,%0" : "=r"(actual_cr2));
  if (status != 13 || budget != 4 ||
      state.PC != (width == 32 ? 0x1014 : 0x1023) ||
      state.GPR[0] != context[0] || state.GPR[4] != 0x70000 ||
      state.RFlags != 0x246 || state.CR2 != actual_cr2)
    continuation_failed = 1;
  /* ASID zero exits before installing guest CR2. The explicit original state
   * must survive, independently of the compiler host's ambient CR2. */
  const uintptr_t vmcb = width == 32 ? (uint32_t)context[0] : context[0];
  unsigned asid;
  memcpy(&asid, (const void *)(vmcb + 0x58), 4);
  if (!asid) {
    const uint64_t original =
        width == 32 ? original_vmrun_addr32_invalid_cr2(context[0], prior_cr2)
                    : original_vmrun_invalid_cr2(context[0], prior_cr2);
    if (original != prior_cr2)
      continuation_failed = 7;
    else if (state.CR2 != original)
      continuation_failed = 6;
  }
  for (unsigned i = 0; i < 14; ++i)
    context[4 + i] = state.GPR[registers[i]];
  memcpy(&present, xstate + 512, 8);
  for (unsigned r = 0; r < 16; ++r)
    for (unsigned lane = 0; lane < 4; ++lane) {
      uint64_t value, expected = 0;
      memcpy(&value, state.ZMM[r] + lane * 8, 8);
      if (present & (lane < 2 ? 2 : 4))
        memcpy(&expected,
               xstate + (lane < 2 ? 160 : 576) + r * 16 + (lane % 2) * 8, 8);
      if (value != expected)
        continuation_failed = 2;
    }
  unsigned mxcsr;
  memcpy(&mxcsr, xstate + 24, 4);
  if (mxcsr != state.MXCSR)
    continuation_failed = 3;
}
#define VMRUN64(p) continuation_vmrun(p, 64)
#define VMRUN32(p) continuation_vmrun(p, 32)

/* Also execute the complete original IDA example as exported instruction steps,
 * through its original stack/RET continuation after the explicit VMEXIT stop.
 */
static uint64_t continuation_rcx(void *opaque) {
  uint64_t *context = opaque;
  uint64_t stack[2] = {0x7000, 0};
  neverd_x64_state_v10 state = {0};
  state.PC = 0x1000;
  state.GPR[4] = (uint64_t)stack;
  state.GPR[7] = context[0];
  state.RFlags = 0x246;
  state.MXCSR = 0x1f80;
  state.SVM.GuestXState = context[1];
  state.SVM.HostXState = context[2];
  uint64_t budget = 10;
  if (neverd_x64_continue_v10(&state, &budget) != 13 || budget != 7 ||
      state.PC != 0x100b || state.GPR[1] != 0x5678)
    continuation_failed = 4;
  if (neverd_x64_continue_v10(&state, &budget) != 3 || budget != 5 ||
      state.PC != 0x7000 || state.GPR[4] != (uint64_t)(stack + 1))
    continuation_failed = 5;
  context[5] = state.GPR[1];
  return state.GPR[0];
}
