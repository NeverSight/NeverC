/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NEVERC_KRT_ASM_BYTEORDER_H
#define _NEVERC_KRT_ASM_BYTEORDER_H

#include <linux/types.h>

#define __LITTLE_ENDIAN 1234

__always_inline __le16 cpu_to_le16(u16 x) { return (__le16)x; }
__always_inline __le32 cpu_to_le32(u32 x) { return (__le32)x; }
__always_inline __le64 cpu_to_le64(u64 x) { return (__le64)x; }
__always_inline u16 le16_to_cpu(__le16 x) { return (u16)x; }
__always_inline u32 le32_to_cpu(__le32 x) { return (u32)x; }
__always_inline u64 le64_to_cpu(__le64 x) { return (u64)x; }

__always_inline __be16 cpu_to_be16(u16 x) { return (__be16)__builtin_bswap16(x); }
__always_inline __be32 cpu_to_be32(u32 x) { return (__be32)__builtin_bswap32(x); }
__always_inline __be64 cpu_to_be64(u64 x) { return (__be64)__builtin_bswap64(x); }
__always_inline u16 be16_to_cpu(__be16 x) { return __builtin_bswap16((u16)x); }
__always_inline u32 be32_to_cpu(__be32 x) { return __builtin_bswap32((u32)x); }
__always_inline u64 be64_to_cpu(__be64 x) { return __builtin_bswap64((u64)x); }

#define htons(x) cpu_to_be16(x)
#define ntohs(x) be16_to_cpu(x)
#define htonl(x) cpu_to_be32(x)
#define ntohl(x) be32_to_cpu(x)

#endif
