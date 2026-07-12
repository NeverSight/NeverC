/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_LINUX_GFP_H
#define _NEVERC_KRT_LINUX_GFP_H

#include <linux/types.h>
#include <nvkmod_version.h>

#define ___GFP_DMA 0x01u
#define ___GFP_HIGHMEM 0x02u
#define ___GFP_MOVABLE 0x08u
#define ___GFP_RECLAIMABLE 0x10u
#define ___GFP_HIGH 0x20u
#define ___GFP_IO 0x40u
#define ___GFP_FS 0x80u
#define ___GFP_ZERO 0x100u
#if NEVERC_KRT_KERNEL < 606
#define ___GFP_ATOMIC 0x200u
#endif
#define ___GFP_DIRECT_RECLAIM 0x400u
#define ___GFP_KSWAPD_RECLAIM 0x800u
#define ___GFP_NOWARN 0x2000u
#define ___GFP_HARDWALL 0x100000u

#define __GFP_ZERO ((gfp_t)___GFP_ZERO)
#define __GFP_HIGH ((gfp_t)___GFP_HIGH)
#define __GFP_IO ((gfp_t)___GFP_IO)
#define __GFP_FS ((gfp_t)___GFP_FS)
#define __GFP_NOWARN ((gfp_t)___GFP_NOWARN)
#define __GFP_HARDWALL ((gfp_t)___GFP_HARDWALL)
#define __GFP_RECLAIM ((gfp_t)(___GFP_DIRECT_RECLAIM | ___GFP_KSWAPD_RECLAIM))

#if NEVERC_KRT_KERNEL < 606
#define GFP_ATOMIC ((gfp_t)(___GFP_HIGH | ___GFP_ATOMIC | ___GFP_KSWAPD_RECLAIM))
#else
#define GFP_ATOMIC ((gfp_t)(___GFP_HIGH | ___GFP_KSWAPD_RECLAIM))
#endif
#define GFP_KERNEL ((gfp_t)(___GFP_IO | ___GFP_FS | __GFP_RECLAIM))
#if NEVERC_KRT_KERNEL >= 612
#define GFP_NOWAIT ((gfp_t)(___GFP_KSWAPD_RECLAIM | ___GFP_NOWARN))
#else
#define GFP_NOWAIT ((gfp_t)(___GFP_KSWAPD_RECLAIM))
#endif
#define GFP_USER ((gfp_t)(___GFP_IO | ___GFP_FS | __GFP_RECLAIM | ___GFP_HARDWALL))

#endif /* _NEVERC_KRT_LINUX_GFP_H */
