/*===---- neverc/xorstr/xorstr_impl.inc - XOR string decrypt impl ---------===*\
 *
 * Stack-allocated string decrypt helper for NC_XORSTR().
 * Included by <neverc/xorstr/xorstr.h> -- do not include directly.
 *
 * The decrypt byte operation avoids a direct XOR expression:
 * a ^ b == a + b - 2*(a & b).
 *
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_XORSTR_IMPL_INC_
#define _NEVERC_XORSTR_IMPL_INC_

static __attribute__((always_inline, nodebug)) unsigned char
__neverc_xorstr_dec_byte(unsigned char a, unsigned char b) {
  volatile __SIZE_TYPE__ va = a;
  volatile __SIZE_TYPE__ vb = b;
  return (unsigned char)(va + vb - ((va & vb) + (vb & va)));
}

static __attribute__((always_inline, nodebug)) __SIZE_TYPE__
__neverc_xorstr_rotl(__SIZE_TYPE__ value, unsigned int shift) {
  const unsigned int bits = (unsigned int)(sizeof(__SIZE_TYPE__) * 8);
  return (value << shift) | (value >> (bits - shift));
}

static __attribute__((always_inline, nodebug)) unsigned int
__neverc_xorstr_schedule_shift(__SIZE_TYPE__ value, unsigned int lane) {
  const unsigned int bits = (unsigned int)(sizeof(__SIZE_TYPE__) * 8);
  const unsigned int offset = ((unsigned int)value + lane) & (bits - 1U);
  return ((unsigned int)(value >> offset) & (bits - 1U)) | 1U;
}

static __attribute__((always_inline, nodebug)) __SIZE_TYPE__
__neverc_xorstr_length_mask(__SIZE_TYPE__ key) {
  __SIZE_TYPE__ inverted = ~key;
  return __neverc_xorstr_rotl(key, __neverc_xorstr_schedule_shift(key, 0U)) ^
         __neverc_xorstr_rotl(inverted,
                              __neverc_xorstr_schedule_shift(inverted, 1U));
}

struct __neverc_xorstr_schedule {
  __SIZE_TYPE__ initial_state;
  __SIZE_TYPE__ addend;
  __SIZE_TYPE__ index_step;
  __SIZE_TYPE__ multiplier;
  unsigned int shift_a;
  unsigned int shift_b;
  unsigned int shift_c;
  unsigned int stream_shift_a;
  unsigned int stream_shift_b;
};

static __attribute__((always_inline, nodebug)) struct __neverc_xorstr_schedule
__neverc_xorstr_make_schedule(__SIZE_TYPE__ key, __SIZE_TYPE__ len) {
  __SIZE_TYPE__ inverted = ~key;
  struct __neverc_xorstr_schedule schedule;
  schedule.initial_state =
      __neverc_xorstr_rotl(key, __neverc_xorstr_schedule_shift(key, 2U)) ^
      __neverc_xorstr_rotl(inverted, __neverc_xorstr_schedule_shift(key, 3U));
  schedule.initial_state += len;
  schedule.addend =
      __neverc_xorstr_rotl(key, __neverc_xorstr_schedule_shift(inverted, 4U)) ^
      (inverted >> __neverc_xorstr_schedule_shift(key, 5U));
  schedule.index_step =
      __neverc_xorstr_rotl(inverted ^ len,
                           __neverc_xorstr_schedule_shift(key, 6U)) |
      1U;
  schedule.multiplier =
      (__neverc_xorstr_rotl(key ^ len,
                            __neverc_xorstr_schedule_shift(inverted, 7U)) ^
       __neverc_xorstr_rotl(inverted,
                            __neverc_xorstr_schedule_shift(key, 8U))) |
      1U;
  schedule.shift_a = __neverc_xorstr_schedule_shift(key ^ len, 9U);
  schedule.shift_b = __neverc_xorstr_schedule_shift(inverted ^ len, 10U);
  schedule.shift_c = __neverc_xorstr_schedule_shift(key + len, 11U);
  schedule.stream_shift_a = __neverc_xorstr_schedule_shift(key, 12U);
  schedule.stream_shift_b = __neverc_xorstr_schedule_shift(inverted, 13U);
  return schedule;
}

static __attribute__((always_inline, nodebug)) __SIZE_TYPE__
__neverc_xorstr_advance(__SIZE_TYPE__ state, __SIZE_TYPE__ index,
                        const struct __neverc_xorstr_schedule *schedule) {
  state += schedule->addend + (index + 1) * schedule->index_step;
  state ^= state >> schedule->shift_a;
  state *= schedule->multiplier;
  state ^= state << schedule->shift_b;
  state ^= state >> schedule->shift_c;
  return state;
}

static __attribute__((always_inline, nodebug)) unsigned char
__neverc_xorstr_stream_byte(__SIZE_TYPE__ state,
                            const struct __neverc_xorstr_schedule *schedule) {
  state ^= state >> schedule->stream_shift_a;
  state ^= state << schedule->stream_shift_b;
  state ^= state >> 8;
  state ^= state >> 16;
  if (sizeof(__SIZE_TYPE__) > 4)
    state ^= state >> 32;
  return (unsigned char)state;
}

/* Zero-initialised, module-local route state.  The volatile load keeps the
 * generic decoder multi-path after interprocedural optimisation. */
static volatile __SIZE_TYPE__ __neverc_xorstr_route_state
    __attribute__((__unused__, __nodebug__));

/*
 * The caller-provided buf must be at least len+1 bytes.  The Sema for
 * __builtin_neverc_xorstr passes a compound literal (char[len+1]){0}
 * whose size is a compile-time constant, producing a static alloca in
 * LLVM IR that lives until the enclosing function returns — no
 * stacksave/stackrestore and exact allocation.
 */
static __attribute__((__noinline__, __unused__, __nodebug__)) const char *
__neverc_xorstr_decrypt(const char *enc, __SIZE_TYPE__ length_token,
                        __SIZE_TYPE__ key, char *buf) {
  /* Volatile reloads sever constant-specialisation across full LTO. */
  volatile __SIZE_TYPE__ opaque_key = key;
  volatile __SIZE_TYPE__ opaque_length = length_token;
  key = opaque_key;
  length_token = opaque_length;

  __SIZE_TYPE__ len = length_token ^ __neverc_xorstr_length_mask(key);
  struct __neverc_xorstr_schedule schedule =
      __neverc_xorstr_make_schedule(key, len);
  __SIZE_TYPE__ state = schedule.initial_state;
  __SIZE_TYPE__ runtime_route = __neverc_xorstr_route_state;

  /* Keep an alternate, valid path without touching stack-control state. */
  if (runtime_route != 0) {
    state ^= runtime_route;
    state = __neverc_xorstr_advance(state, runtime_route ^ len, &schedule);
    state += schedule.addend ^ schedule.index_step;
  }

  for (__SIZE_TYPE__ i = 0; i < len; i++) {
    state = __neverc_xorstr_advance(state, i, &schedule);
    unsigned char kb = __neverc_xorstr_stream_byte(state, &schedule);
    buf[i] = (char)__neverc_xorstr_dec_byte((unsigned char)enc[i], kb);
  }
  buf[len] = 0;
  return buf;
}

/* Retain the generic decoder ABI even when a translation unit has only one
 * direct call.  This prevents interprocedural dead-argument specialisation. */
typedef const char *(*__neverc_xorstr_decrypt_fn)(const char *, __SIZE_TYPE__,
                                                  __SIZE_TYPE__, char *);
static __neverc_xorstr_decrypt_fn const __neverc_xorstr_decrypt_abi_anchor
    __attribute__((__used__, __nodebug__)) = __neverc_xorstr_decrypt;

#endif /* _NEVERC_XORSTR_IMPL_INC_ */
