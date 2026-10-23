/*===--------------------- amxintrin.h - AMX intrinsics ---------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===------------------------------------------------------------------------===
 */
#ifndef __IMMINTRIN_H
#error "Never use <amxintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __AMXINTRIN_H
#define __AMXINTRIN_H

/* Physical tile numbers are compile-time constants in [0, 7]. Configuration
 * and the architectural tile bank belong to the caller. These APIs must not
 * be mixed with compiler-managed tile values in one function. */
#define __DEFAULT_FN_ATTRS_AMX_TILE                                            \
  __attribute__((__always_inline__, __nodebug__, __target__("amx-tile")))

static __inline__ void __DEFAULT_FN_ATTRS_AMX_TILE
_tile_loadconfig(const void *__config) {
  __builtin_ia32_tile_loadconfig(__config);
}

static __inline__ void __DEFAULT_FN_ATTRS_AMX_TILE
_tile_storeconfig(void *__config) {
  __builtin_ia32_tile_storeconfig(__config);
}

static __inline__ void __DEFAULT_FN_ATTRS_AMX_TILE _tile_release(void) {
  __builtin_ia32_tilerelease();
}

#undef __DEFAULT_FN_ATTRS_AMX_TILE

#define _tile_zero(tile) __builtin_ia32_tilezero((tile))

#define _tile_loadd(tile, base, stride)                                        \
  __builtin_ia32_tileloadd64((tile), (const void *)(base),                     \
                             (__SIZE_TYPE__)(stride))
#define _tile_stream_loadd(tile, base, stride)                                 \
  __builtin_ia32_tileloaddt164((tile), (const void *)(base),                   \
                               (__SIZE_TYPE__)(stride))
#define _tile_loaddrs(tile, base, stride)                                      \
  __builtin_ia32_tileloaddrs64((tile), (const void *)(base),                   \
                               (__SIZE_TYPE__)(stride))
#define _tile_stream_loaddrs(tile, base, stride)                               \
  __builtin_ia32_tileloaddrst164((tile), (const void *)(base),                 \
                                 (__SIZE_TYPE__)(stride))
#define _tile_stored(tile, base, stride)                                       \
  __builtin_ia32_tilestored64((tile), (void *)(base), (__SIZE_TYPE__)(stride))

/* Compute operands name three distinct physical tiles. */
#define _tile_dpbssd(dst, src0, src1)                                          \
  __builtin_ia32_tdpbssd((dst), (src0), (src1))
#define _tile_dpbsud(dst, src0, src1)                                          \
  __builtin_ia32_tdpbsud((dst), (src0), (src1))
#define _tile_dpbusd(dst, src0, src1)                                          \
  __builtin_ia32_tdpbusd((dst), (src0), (src1))
#define _tile_dpbuud(dst, src0, src1)                                          \
  __builtin_ia32_tdpbuud((dst), (src0), (src1))
#define _tile_dpbf16ps(dst, src0, src1)                                        \
  __builtin_ia32_tdpbf16ps((dst), (src0), (src1))
#define _tile_dpfp16ps(dst, src0, src1)                                        \
  __builtin_ia32_tdpfp16ps((dst), (src0), (src1))
#define _tile_cmmimfp16ps(dst, src0, src1)                                     \
  __builtin_ia32_tcmmimfp16ps((dst), (src0), (src1))
#define _tile_cmmrlfp16ps(dst, src0, src1)                                     \
  __builtin_ia32_tcmmrlfp16ps((dst), (src0), (src1))
#define _tile_dpbf8ps(dst, src0, src1)                                         \
  __builtin_ia32_tdpbf8ps((dst), (src0), (src1))
#define _tile_dpbhf8ps(dst, src0, src1)                                        \
  __builtin_ia32_tdpbhf8ps((dst), (src0), (src1))
#define _tile_dphbf8ps(dst, src0, src1)                                        \
  __builtin_ia32_tdphbf8ps((dst), (src0), (src1))
#define _tile_dphf8ps(dst, src0, src1)                                         \
  __builtin_ia32_tdphf8ps((dst), (src0), (src1))
#define _tile_mmultf32ps(dst, src0, src1)                                      \
  __builtin_ia32_tmmultf32ps((dst), (src0), (src1))

/* Row results contain all 512 bits. Dynamic indices retain all 32 bits;
 * immediate forms require a compile-time value in [0, 255]. */
#define _tile_cvtrowd2ps(tile, row) __builtin_ia32_tcvtrowd2ps((tile), (row))
#define _tile_cvtrowd2psi(tile, row) __builtin_ia32_tcvtrowd2psi((tile), (row))
#define _tile_cvtrowps2bf16h(tile, row)                                        \
  __builtin_ia32_tcvtrowps2bf16h((tile), (row))
#define _tile_cvtrowps2bf16hi(tile, row)                                       \
  __builtin_ia32_tcvtrowps2bf16hi((tile), (row))
#define _tile_cvtrowps2bf16l(tile, row)                                        \
  __builtin_ia32_tcvtrowps2bf16l((tile), (row))
#define _tile_cvtrowps2bf16li(tile, row)                                       \
  __builtin_ia32_tcvtrowps2bf16li((tile), (row))
#define _tile_cvtrowps2phh(tile, row)                                          \
  __builtin_ia32_tcvtrowps2phh((tile), (row))
#define _tile_cvtrowps2phhi(tile, row)                                         \
  __builtin_ia32_tcvtrowps2phhi((tile), (row))
#define _tile_cvtrowps2phl(tile, row)                                          \
  __builtin_ia32_tcvtrowps2phl((tile), (row))
#define _tile_cvtrowps2phli(tile, row)                                         \
  __builtin_ia32_tcvtrowps2phli((tile), (row))
#define _tile_movrow(tile, row)                                                \
  ((__m512i)__builtin_ia32_tilemovrow((tile), (row)))
#define _tile_movrowi(tile, row)                                               \
  ((__m512i)__builtin_ia32_tilemovrowi((tile), (row)))

#endif /* __AMXINTRIN_H */
