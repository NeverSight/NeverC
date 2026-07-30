/*
 * Minimal WinDNS declarations used by resolve.c.
 *
 * NeverC ships a compact Windows SDK for cross-target compilation.  Some SDK
 * snapshots omit <windns.h>, although the corresponding APIs remain available
 * from dnsapi.dll.  Keep this fallback private to the std runtime so a complete
 * platform SDK, when present, remains authoritative.
 */
#ifndef NEVERC_STD_NET_RESOLVE_WINDNS_COMPAT_H
#define NEVERC_STD_NET_RESOLVE_WINDNS_COMPAT_H

#ifdef _WIN32

#include <windows.h>

#define DNS_TYPE_NS     0x0002
#define DNS_TYPE_CNAME  0x0005
#define DNS_TYPE_MX     0x000f
#define DNS_TYPE_TEXT   0x0010
#define DNS_TYPE_SRV    0x0021

#define DNS_QUERY_STANDARD 0x00000000

typedef LONG DNS_STATUS;

typedef struct {
    PSTR pNameHost;
} DNS_PTR_DATAA;

typedef struct {
    PSTR pNameExchange;
    WORD wPreference;
    WORD Pad;
} DNS_MX_DATAA;

typedef struct {
    DWORD dwStringCount;
    PSTR pStringArray[1];
} DNS_TXT_DATAA;

typedef struct {
    PSTR pNameTarget;
    WORD wPriority;
    WORD wWeight;
    WORD wPort;
    WORD Pad;
} DNS_SRV_DATAA;

typedef struct _DnsRecordFlags {
    DWORD Section : 2;
    DWORD Delete : 1;
    DWORD CharSet : 2;
    DWORD Unused : 3;
    DWORD Reserved : 24;
} DNS_RECORD_FLAGS;

typedef struct _DnsRecordA {
    struct _DnsRecordA *pNext;
    PSTR pName;
    WORD wType;
    WORD wDataLength;
    union {
        DWORD DW;
        DNS_RECORD_FLAGS S;
    } Flags;
    DWORD dwTtl;
    DWORD dwReserved;
    union {
        DNS_PTR_DATAA PTR, Ptr, NS, Ns, CNAME, Cname;
        DNS_MX_DATAA MX, Mx;
        DNS_TXT_DATAA TXT, Txt;
        DNS_SRV_DATAA SRV, Srv;
        PBYTE pDataPtr;
    } Data;
} DNS_RECORDA, *PDNS_RECORDA;

typedef DNS_RECORDA DNS_RECORD, *PDNS_RECORD;

typedef enum {
    DnsFreeFlat = 0,
    DnsFreeRecordList,
    DnsFreeParsedMessageFields
} DNS_FREE_TYPE;

DNS_STATUS WINAPI DnsQuery_A(PCSTR pszName, WORD wType, DWORD Options,
                             PVOID pExtra, PDNS_RECORD *ppQueryResults,
                             PVOID *pReserved);
VOID WINAPI DnsFree(PVOID pData, DNS_FREE_TYPE FreeType);

#define DnsRecordListFree(p, t) DnsFree((p), (t))

#endif /* _WIN32 */

#endif /* NEVERC_STD_NET_RESOLVE_WINDNS_COMPAT_H */
