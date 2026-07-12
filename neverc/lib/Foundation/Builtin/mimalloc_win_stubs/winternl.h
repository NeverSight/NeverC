/* Minimal stand-in for <winternl.h>.
 *
 * NeverC's bundled MSVC SDK omits this internal header. Upstream mimalloc
 * includes it only so NtCurrentTeb() / struct _TEB are visible; NtCurrentTeb
 * already lives in <winnt.h> (via <windows.h>), and mimalloc treats _TEB as
 * an incomplete type plus hardcoded offsets. A full WinSDK copy is not
 * required for -fbuiltin-mimalloc bitcode bootstrap.
 */
#ifndef _WINTERNL_
#define _WINTERNL_

struct _TEB;

#endif /* _WINTERNL_ */
