/*++ BUILD Version: 0006    // Increment this if a change has global effects

Copyright (c) 1989-1999  Microsoft Corporation

Module Name:

    ntsam.h

Abstract:

    This module describes the data types and procedure prototypes
    that make up the NT Security Accounts Manager. This includes
    API's exported by SAM and related subsystems.

Author:

--*/

#ifndef _NTSAM_
#define _NTSAM_

#if _MSC_VER > 1000
#pragma once
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PPULONG
typedef PULONG *PPULONG;
#endif  //PPULONG

//
// An attempt to lookup more than this number of names or SIDs in
// a single call will be rejected with an INSUFFICIENT_RESOURCES
// status.
//

#define SAM_MAXIMUM_LOOKUP_COUNT    (1000)


//
// An attempt to pass names totalling more than the following number
// of bytes in length will be rejected with an INSUFFICIENT_RESOURCES
// status.
//

#define SAM_MAXIMUM_LOOKUP_LENGTH   (32000)

//
// An attempt to set a password longer than this number of characters
// will fail.
//

#define SAM_MAX_PASSWORD_LENGTH     (256)


//
// Length of the salt used in the clear password encryption
//

#define SAM_PASSWORD_ENCRYPTION_SALT_LEN  (16)






#ifndef _NTSAM_SAM_HANDLE_               // ntsubauth
typedef PVOID SAM_HANDLE, *PSAM_HANDLE;  // ntsubauth
#define _NTSAM_SAM_HANDLE_               // ntsubauth
#endif                                   // ntsubauth

typedef ULONG SAM_ENUMERATE_HANDLE, *PSAM_ENUMERATE_HANDLE;

typedef struct _SAM_RID_ENUMERATION {
    ULONG RelativeId;
    UNICODE_STRING Name;
} SAM_RID_ENUMERATION, *PSAM_RID_ENUMERATION;

typedef struct _SAM_SID_ENUMERATION {
    PSID Sid;
    UNICODE_STRING Name;
} SAM_SID_ENUMERATION, *PSAM_SID_ENUMERATION;

#ifdef MIDL_PASS
#define SIZE_IS(x)      [size_is(x)]
#define SWITCH_IS(x)    [switch_is(x)]
#define SWITCH_TYPE(x)  [switch_type(x)]
#define CASE(x)         [case(x)]
#define RANGE(x,y)      [range(x,y)]
#define VAR_SIZE_ARRAY
#else
#define SIZE_IS(x)
#define SWITCH_IS(x)
#define SWITCH_TYPE(x)
#define CASE(x)
#define RANGE(x,y)      __in_range(x,y)
#define VAR_SIZE_ARRAY  (1)
#endif

typedef struct _SAM_BYTE_ARRAY {

                            ULONG   Size;
    SIZE_IS( Size )         PUCHAR  Data;

} SAM_BYTE_ARRAY, *PSAM_BYTE_ARRAY;

typedef struct _SAM_BYTE_ARRAY_32K {

    RANGE( 0, MAXSHORT )    ULONG   Size;
    SIZE_IS( Size )         PUCHAR  Data;

} SAM_BYTE_ARRAY_32K, *PSAM_BYTE_ARRAY_32K;

typedef SAM_BYTE_ARRAY_32K  SAM_SHELL_OBJECT_PROPERTIES, *PSAM_SHELL_OBJECT_PROPERTIES;

/////////////////////////////////////////////////////////////////////////////
//                                                                         //
// obsolete well-known account names.                                      //
// These became obsolete with the flexadmin model.                         //
// These will be deleted shortly - DON'T USE THESE                         //
//                                                                         //
/////////////////////////////////////////////////////////////////////////////

#define DOMAIN_ADMIN_USER_NAME         "ADMIN"
#define DOMAIN_ADMIN_NAME              "D_ADMIN"
#define DOMAIN_ADMIN_NAMEW             L"D_ADMIN"
#define DOMAIN_USERS_NAME              "D_USERS"
#define DOMAIN_USERS_NAMEW             L"D_USERS"
#define DOMAIN_GUESTS_NAME             "D_GUESTS"
#define DOMAIN_ACCOUNT_OPERATORS_NAME  "D_ACCOUN"
#define DOMAIN_ACCOUNT_OPERATORS_NAMEW L"D_ACCOUN"
#define DOMAIN_SERVER_OPERATORS_NAME   "D_SERVER"
#define DOMAIN_SERVER_OPERATORS_NAMEW L"D_SERVER"
#define DOMAIN_PRINT_OPERATORS_NAME    "D_PRINT"
#define DOMAIN_PRINT_OPERATORS_NAMEW  L"D_PRINT"
#define DOMAIN_COMM_OPERATORS_NAME     "D_COMM"
#define DOMAIN_COMM_OPERATORS_NAMEW   L"D_COMM"
#define DOMAIN_BACKUP_OPERATORS_NAME   "D_BACKUP"
#define DOMAIN_RESTORE_OPERATORS_NAME  "D_RESTOR"





///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//  Server Object Related Definitions                                        //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

//
// Access rights for server object
//

#define SAM_SERVER_CONNECT               0x0001
#define SAM_SERVER_SHUTDOWN              0x0002
#define SAM_SERVER_INITIALIZE            0x0004
#define SAM_SERVER_CREATE_DOMAIN         0x0008
#define SAM_SERVER_ENUMERATE_DOMAINS     0x0010
#define SAM_SERVER_LOOKUP_DOMAIN         0x0020


#define SAM_SERVER_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED     |\
                               SAM_SERVER_CONNECT           |\
                               SAM_SERVER_INITIALIZE        |\
                               SAM_SERVER_CREATE_DOMAIN     |\
                               SAM_SERVER_SHUTDOWN          |\
                               SAM_SERVER_ENUMERATE_DOMAINS |\
                               SAM_SERVER_LOOKUP_DOMAIN)

#define SAM_SERVER_READ       (STANDARD_RIGHTS_READ         |\
                               SAM_SERVER_ENUMERATE_DOMAINS)

#define SAM_SERVER_WRITE      (STANDARD_RIGHTS_WRITE        |\
                               SAM_SERVER_INITIALIZE        |\
                               SAM_SERVER_CREATE_DOMAIN     |\
                               SAM_SERVER_SHUTDOWN)

#define SAM_SERVER_EXECUTE    (STANDARD_RIGHTS_EXECUTE      |\
                               SAM_SERVER_CONNECT           |\
                               SAM_SERVER_LOOKUP_DOMAIN)






///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//  Domain Object Related Definitions                                        //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////


//
// Access rights for domain object
//

#define DOMAIN_READ_PASSWORD_PARAMETERS  0x0001
#define DOMAIN_WRITE_PASSWORD_PARAMS     0x0002
#define DOMAIN_READ_OTHER_PARAMETERS     0x0004
#define DOMAIN_WRITE_OTHER_PARAMETERS    0x0008
#define DOMAIN_CREATE_USER               0x0010
#define DOMAIN_CREATE_GROUP              0x0020
#define DOMAIN_CREATE_ALIAS              0x0040
#define DOMAIN_GET_ALIAS_MEMBERSHIP      0x0080
#define DOMAIN_LIST_ACCOUNTS             0x0100
#define DOMAIN_LOOKUP                    0x0200
#define DOMAIN_ADMINISTER_SERVER         0x0400

#define DOMAIN_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED         |\
                           DOMAIN_READ_OTHER_PARAMETERS     |\
                           DOMAIN_WRITE_OTHER_PARAMETERS    |\
                           DOMAIN_WRITE_PASSWORD_PARAMS     |\
                           DOMAIN_CREATE_USER               |\
                           DOMAIN_CREATE_GROUP              |\
                           DOMAIN_CREATE_ALIAS              |\
                           DOMAIN_GET_ALIAS_MEMBERSHIP      |\
                           DOMAIN_LIST_ACCOUNTS             |\
                           DOMAIN_READ_PASSWORD_PARAMETERS  |\
                           DOMAIN_LOOKUP                    |\
                           DOMAIN_ADMINISTER_SERVER)

#define DOMAIN_READ        (STANDARD_RIGHTS_READ            |\
                           DOMAIN_GET_ALIAS_MEMBERSHIP      |\
                           DOMAIN_READ_OTHER_PARAMETERS)


#define DOMAIN_WRITE       (STANDARD_RIGHTS_WRITE           |\
                           DOMAIN_WRITE_OTHER_PARAMETERS    |\
                           DOMAIN_WRITE_PASSWORD_PARAMS     |\
                           DOMAIN_CREATE_USER               |\
                           DOMAIN_CREATE_GROUP              |\
                           DOMAIN_CREATE_ALIAS              |\
                           DOMAIN_ADMINISTER_SERVER)

#define DOMAIN_EXECUTE     (STANDARD_RIGHTS_EXECUTE         |\
                           DOMAIN_READ_PASSWORD_PARAMETERS  |\
                           DOMAIN_LIST_ACCOUNTS             |\
                           DOMAIN_LOOKUP)



//
// Normal modifications cause a domain's ModifiedCount to be
// incremented by 1.  Domain promotion to Primary domain controller
// cause the ModifiedCount to be incremented by the following
// amount.  This causes the upper 24-bits of the ModifiedCount
// to be a promotion count and the lower 40-bits as a modification
// count.
//

#define DOMAIN_PROMOTION_INCREMENT      {0x0,0x10}
#define DOMAIN_PROMOTION_MASK           {0x0,0xFFFFFFF0}

//
// Domain information classes and their corresponding data structures
//

typedef enum _DOMAIN_INFORMATION_CLASS {
    DomainPasswordInformation = 1,
    DomainGeneralInformation,
    DomainLogoffInformation,
    DomainOemInformation,
    DomainNameInformation,
    DomainReplicationInformation,
    DomainServerRoleInformation,
    DomainModifiedInformation,
    DomainStateInformation,
    DomainUasInformation,
    DomainGeneralInformation2,
    DomainLockoutInformation,
    DomainModifiedInformation2
} DOMAIN_INFORMATION_CLASS;

typedef enum _DOMAIN_SERVER_ENABLE_STATE {
    DomainServerEnabled = 1,
    DomainServerDisabled
} DOMAIN_SERVER_ENABLE_STATE, *PDOMAIN_SERVER_ENABLE_STATE;

typedef enum _DOMAIN_SERVER_ROLE {
    DomainServerRoleBackup = 2,
    DomainServerRolePrimary
} DOMAIN_SERVER_ROLE, *PDOMAIN_SERVER_ROLE;

#include "pshpack4.h"
typedef struct _DOMAIN_GENERAL_INFORMATION {
    LARGE_INTEGER ForceLogoff;
    UNICODE_STRING OemInformation;
    UNICODE_STRING DomainName;
    UNICODE_STRING ReplicaSourceNodeName;
    LARGE_INTEGER DomainModifiedCount;
    DOMAIN_SERVER_ENABLE_STATE DomainServerState;
    DOMAIN_SERVER_ROLE DomainServerRole;
    BOOLEAN UasCompatibilityRequired;
    ULONG UserCount;
    ULONG GroupCount;
    ULONG AliasCount;
} DOMAIN_GENERAL_INFORMATION, *PDOMAIN_GENERAL_INFORMATION;
#include "poppack.h"

#include "pshpack4.h"
typedef struct _DOMAIN_GENERAL_INFORMATION2 {

    DOMAIN_GENERAL_INFORMATION    I1;

    //
    // New fields added for this structure (NT1.0A).
    //

    LARGE_INTEGER               LockoutDuration;          //Must be a Delta time
    LARGE_INTEGER               LockoutObservationWindow; //Must be a Delta time
    USHORT                      LockoutThreshold;
} DOMAIN_GENERAL_INFORMATION2, *PDOMAIN_GENERAL_INFORMATION2;
#include "poppack.h"

typedef struct _DOMAIN_UAS_INFORMATION {
    BOOLEAN UasCompatibilityRequired;
} DOMAIN_UAS_INFORMATION;

//
// This needs to be guarded, because ntsecapi.h is a generated
// public file, and ntsam.h is an internal file, but people like
// to mix and match them anyway.
//

// begin_ntsecapi
#ifndef _DOMAIN_PASSWORD_INFORMATION_DEFINED
#define _DOMAIN_PASSWORD_INFORMATION_DEFINED
typedef struct _DOMAIN_PASSWORD_INFORMATION {
    USHORT MinPasswordLength;
    USHORT PasswordHistoryLength;
    ULONG PasswordProperties;
#if defined(MIDL_PASS)
    OLD_LARGE_INTEGER MaxPasswordAge;
    OLD_LARGE_INTEGER MinPasswordAge;
#else
    LARGE_INTEGER MaxPasswordAge;
    LARGE_INTEGER MinPasswordAge;
#endif
} DOMAIN_PASSWORD_INFORMATION, *PDOMAIN_PASSWORD_INFORMATION;
#endif

#if (_WIN32_WINNT >= 0x0501)
//
// PasswordProperties flags
//

#define DOMAIN_PASSWORD_COMPLEX             0x00000001L
#define DOMAIN_PASSWORD_NO_ANON_CHANGE      0x00000002L
#define DOMAIN_PASSWORD_NO_CLEAR_CHANGE     0x00000004L
#define DOMAIN_LOCKOUT_ADMINS               0x00000008L
#define DOMAIN_PASSWORD_STORE_CLEARTEXT     0x00000010L
#define DOMAIN_REFUSE_PASSWORD_CHANGE       0x00000020L
#if(_WIN32_WINNT >= 0x0502)
#define DOMAIN_NO_LM_OWF_CHANGE             0x00000040L
#endif
#endif

// end_ntsecapi

typedef enum _DOMAIN_PASSWORD_CONSTRUCTION {
    DomainPasswordSimple = 1,
    DomainPasswordComplex
} DOMAIN_PASSWORD_CONSTRUCTION;

typedef struct _DOMAIN_LOGOFF_INFORMATION {
#if defined(MIDL_PASS)
    OLD_LARGE_INTEGER ForceLogoff;
#else
    LARGE_INTEGER ForceLogoff;
#endif
} DOMAIN_LOGOFF_INFORMATION, *PDOMAIN_LOGOFF_INFORMATION;

typedef struct _DOMAIN_OEM_INFORMATION {
    UNICODE_STRING OemInformation;
} DOMAIN_OEM_INFORMATION, *PDOMAIN_OEM_INFORMATION;

typedef struct _DOMAIN_NAME_INFORMATION {
    UNICODE_STRING DomainName;
} DOMAIN_NAME_INFORMATION, *PDOMAIN_NAME_INFORMATION;

typedef struct _DOMAIN_SERVER_ROLE_INFORMATION {
    DOMAIN_SERVER_ROLE DomainServerRole;
} DOMAIN_SERVER_ROLE_INFORMATION, *PDOMAIN_SERVER_ROLE_INFORMATION;

typedef struct _DOMAIN_REPLICATION_INFORMATION {
    UNICODE_STRING ReplicaSourceNodeName;
} DOMAIN_REPLICATION_INFORMATION, *PDOMAIN_REPLICATION_INFORMATION;

typedef struct _DOMAIN_MODIFIED_INFORMATION {
#if defined(MIDL_PASS)
    OLD_LARGE_INTEGER DomainModifiedCount;
    OLD_LARGE_INTEGER CreationTime;
#else
    LARGE_INTEGER DomainModifiedCount;
    LARGE_INTEGER CreationTime;
#endif
} DOMAIN_MODIFIED_INFORMATION, *PDOMAIN_MODIFIED_INFORMATION;

typedef struct _DOMAIN_MODIFIED_INFORMATION2 {
#if defined(MIDL_PASS)
    OLD_LARGE_INTEGER DomainModifiedCount;
    OLD_LARGE_INTEGER CreationTime;
    OLD_LARGE_INTEGER ModifiedCountAtLastPromotion;
#else
    LARGE_INTEGER DomainModifiedCount;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER ModifiedCountAtLastPromotion;
#endif
} DOMAIN_MODIFIED_INFORMATION2, *PDOMAIN_MODIFIED_INFORMATION2;

typedef struct _DOMAIN_STATE_INFORMATION {
    DOMAIN_SERVER_ENABLE_STATE DomainServerState;
} DOMAIN_STATE_INFORMATION, *PDOMAIN_STATE_INFORMATION;

typedef struct _DOMAIN_LOCKOUT_INFORMATION {
#if defined(MIDL_PASS)
    OLD_LARGE_INTEGER           LockoutDuration;          //Must be a Delta time
    OLD_LARGE_INTEGER           LockoutObservationWindow; //Must be a Delta time
#else
    LARGE_INTEGER               LockoutDuration;          //Must be a Delta time
    LARGE_INTEGER               LockoutObservationWindow; //Must be a Delta time
#endif
    USHORT                      LockoutThreshold;         //Zero means no lockout
} DOMAIN_LOCKOUT_INFORMATION, *PDOMAIN_LOCKOUT_INFORMATION;


//
// Types used by the SamQueryDisplayInformation API
//

typedef enum _DOMAIN_DISPLAY_INFORMATION {
    DomainDisplayUser = 1,
    DomainDisplayMachine,
    DomainDisplayGroup,         // Added in NT1.0A
    DomainDisplayOemUser,       // Added in NT1.0A
    DomainDisplayOemGroup,      // Added in NT1.0A
    DomainDisplayServer         // Added in NT5 to support query of servers
} DOMAIN_DISPLAY_INFORMATION, *PDOMAIN_DISPLAY_INFORMATION;


typedef struct _DOMAIN_DISPLAY_USER {
    ULONG           Index;
    ULONG           Rid;
    ULONG           AccountControl;
    UNICODE_STRING  LogonName;
    UNICODE_STRING  AdminComment;
    UNICODE_STRING  FullName;
} DOMAIN_DISPLAY_USER, *PDOMAIN_DISPLAY_USER;

typedef struct _DOMAIN_DISPLAY_MACHINE {
    ULONG           Index;
    ULONG           Rid;
    ULONG           AccountControl;
    UNICODE_STRING  Machine;
    UNICODE_STRING  Comment;
} DOMAIN_DISPLAY_MACHINE, *PDOMAIN_DISPLAY_MACHINE;

typedef struct _DOMAIN_DISPLAY_GROUP {      // Added in NT1.0A
    ULONG           Index;
    ULONG           Rid;
    ULONG           Attributes;
    UNICODE_STRING  Group;
    UNICODE_STRING  Comment;
} DOMAIN_DISPLAY_GROUP, *PDOMAIN_DISPLAY_GROUP;

typedef struct _DOMAIN_DISPLAY_OEM_USER {      // Added in NT1.0A
    ULONG           Index;
    OEM_STRING     User;
} DOMAIN_DISPLAY_OEM_USER, *PDOMAIN_DISPLAY_OEM_USER;

typedef struct _DOMAIN_DISPLAY_OEM_GROUP {      // Added in NT1.0A
    ULONG           Index;
    OEM_STRING     Group;
} DOMAIN_DISPLAY_OEM_GROUP, *PDOMAIN_DISPLAY_OEM_GROUP;

//
// Types for SamQueryLocalizableAccountsInDomain
//

typedef enum _DOMAIN_LOCALIZABLE_ACCOUNTS_INFORMATION {
    DomainLocalizableAccountsBasic = 1,
} DOMAIN_LOCALIZABLE_ACCOUNTS_INFORMATION, *PDOMAIN_LOCALIZABLE_ACCOUNTS_INFORMATION;

typedef struct _DOMAIN_LOCALIZABLE_ACCOUNTS_ENTRY {
    ULONG           Rid;
    SID_NAME_USE    Use;
    UNICODE_STRING  Name;
    UNICODE_STRING  AdminComment;
} DOMAIN_LOCALIZABLE_ACCOUNT_ENTRY, *PDOMAIN_LOCALIZABLE_ACCOUNT_ENTRY;

typedef struct _DOMAIN_LOCALIZABLE_ACCOUNTS {
                    ULONG                             Count;
#ifdef MIDL_PASS
    [size_is(Count)]
#endif
    __ecount(Count) DOMAIN_LOCALIZABLE_ACCOUNT_ENTRY  *Entries;
} DOMAIN_LOCALIZABLE_ACCOUNTS_BASIC, *PDOMAIN_LOCALIZABLE_ACCOUNTS_BASIC;


typedef
#ifdef MIDL_PASS
    [switch_type(DOMAIN_LOCALIZABLE_ACCOUNTS_INFORMATION)]
#endif
    union _DOMAIN_LOCALIZABLE_INFO_BUFFER {
#ifdef MIDL_PASS
        [case(DomainLocalizableAccountsBasic)]
#endif
        DOMAIN_LOCALIZABLE_ACCOUNTS_BASIC        Basic;
} DOMAIN_LOCALIZABLE_ACCOUNTS_INFO_BUFFER, *PDOMAIN_LOCALIZABLE_ACCOUNTS_INFO_BUFFER;


///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//   Group Object Related Definitions                                        //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////


//
// Access rights for group object
//

#define GROUP_READ_INFORMATION           0x0001
#define GROUP_WRITE_ACCOUNT              0x0002
#define GROUP_ADD_MEMBER                 0x0004
#define GROUP_REMOVE_MEMBER              0x0008
#define GROUP_LIST_MEMBERS               0x0010

#define GROUP_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED  |\
                          GROUP_LIST_MEMBERS        |\
                          GROUP_WRITE_ACCOUNT       |\
                          GROUP_ADD_MEMBER          |\
                          GROUP_REMOVE_MEMBER       |\
                          GROUP_READ_INFORMATION)


#define GROUP_READ       (STANDARD_RIGHTS_READ      |\
                          GROUP_LIST_MEMBERS)


#define GROUP_WRITE      (STANDARD_RIGHTS_WRITE     |\
                          GROUP_WRITE_ACCOUNT       |\
                          GROUP_ADD_MEMBER          |\
                          GROUP_REMOVE_MEMBER)

#define GROUP_EXECUTE    (STANDARD_RIGHTS_EXECUTE   |\
                          GROUP_READ_INFORMATION)


//
// Group object types
//

typedef struct _GROUP_MEMBERSHIP {
    ULONG RelativeId;
    ULONG Attributes;
} GROUP_MEMBERSHIP, *PGROUP_MEMBERSHIP;


typedef enum _GROUP_INFORMATION_CLASS {
    GroupGeneralInformation = 1,
    GroupNameInformation,
    GroupAttributeInformation,
    GroupAdminCommentInformation,
    GroupReplicationInformation
} GROUP_INFORMATION_CLASS;

typedef struct _GROUP_GENERAL_INFORMATION {
    UNICODE_STRING Name;
    ULONG Attributes;
    ULONG MemberCount;
    UNICODE_STRING AdminComment;
} GROUP_GENERAL_INFORMATION,  *PGROUP_GENERAL_INFORMATION;

typedef struct _GROUP_NAME_INFORMATION {
    UNICODE_STRING Name;
} GROUP_NAME_INFORMATION, *PGROUP_NAME_INFORMATION;

typedef struct _GROUP_ATTRIBUTE_INFORMATION {
    ULONG Attributes;
} GROUP_ATTRIBUTE_INFORMATION, *PGROUP_ATTRIBUTE_INFORMATION;

typedef struct _GROUP_ADM_COMMENT_INFORMATION {
    UNICODE_STRING AdminComment;
} GROUP_ADM_COMMENT_INFORMATION, *PGROUP_ADM_COMMENT_INFORMATION;



///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//   Alias Object Related Definitions                                        //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

//
// Access rights for alias object
//

#define ALIAS_ADD_MEMBER                 0x0001
#define ALIAS_REMOVE_MEMBER              0x0002
#define ALIAS_LIST_MEMBERS               0x0004
#define ALIAS_READ_INFORMATION           0x0008
#define ALIAS_WRITE_ACCOUNT              0x0010

#define ALIAS_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED  |\
                          ALIAS_READ_INFORMATION    |\
                          ALIAS_WRITE_ACCOUNT       |\
                          ALIAS_LIST_MEMBERS        |\
                          ALIAS_ADD_MEMBER          |\
                          ALIAS_REMOVE_MEMBER)


#define ALIAS_READ       (STANDARD_RIGHTS_READ      |\
                          ALIAS_LIST_MEMBERS)


#define ALIAS_WRITE      (STANDARD_RIGHTS_WRITE     |\
                          ALIAS_WRITE_ACCOUNT       |\
                          ALIAS_ADD_MEMBER          |\
                          ALIAS_REMOVE_MEMBER)

#define ALIAS_EXECUTE    (STANDARD_RIGHTS_EXECUTE   |\
                          ALIAS_READ_INFORMATION)

//
// Alias object types
//

typedef enum _ALIAS_INFORMATION_CLASS {
    AliasGeneralInformation = 1,
    AliasNameInformation,
    AliasAdminCommentInformation,
    AliasReplicationInformation,
    AliasExtendedInformation,
} ALIAS_INFORMATION_CLASS;

typedef struct _ALIAS_GENERAL_INFORMATION {
    UNICODE_STRING Name;
    ULONG MemberCount;
    UNICODE_STRING AdminComment;
} ALIAS_GENERAL_INFORMATION,  *PALIAS_GENERAL_INFORMATION;

typedef struct _ALIAS_NAME_INFORMATION {
    UNICODE_STRING Name;
} ALIAS_NAME_INFORMATION, *PALIAS_NAME_INFORMATION;

typedef struct _ALIAS_ADM_COMMENT_INFORMATION {
    UNICODE_STRING AdminComment;
} ALIAS_ADM_COMMENT_INFORMATION, *PALIAS_ADM_COMMENT_INFORMATION;

#define ALIAS_ALL_NAME                              (0x00000001L)
#define ALIAS_ALL_MEMBER_COUNT                      (0x00000002L)
#define ALIAS_ALL_ADMIN_COMMENT                     (0x00000004L)
#define ALIAS_ALL_SHELL_ADMIN_OBJECT_PROPERTIES     (0x00000008L)

typedef struct _ALIAS_EXTENDED_INFORMATION {

    ULONG                       WhichFields;
    SAM_SHELL_OBJECT_PROPERTIES ShellAdminObjectProperties;

} ALIAS_EXTENDED_INFORMATION, *PALIAS_EXTENDED_INFORMATION;


//////////////////////////////////////////////////////////////////////////////
//                                                                          //
//    NT5+ Limited Groups Related Definitions                               //
//                                                                          //
//////////////////////////////////////////////////////////////////////////////

//
// Group Flag Definitions to determine Type of Group
//

#define GROUP_TYPE_BUILTIN_LOCAL_GROUP   0x00000001
#define GROUP_TYPE_ACCOUNT_GROUP         0x00000002
#define GROUP_TYPE_RESOURCE_GROUP        0x00000004
#define GROUP_TYPE_UNIVERSAL_GROUP       0x00000008
#define GROUP_TYPE_APP_BASIC_GROUP       0x00000010
#define GROUP_TYPE_APP_QUERY_GROUP       0x00000020
#define GROUP_TYPE_SECURITY_ENABLED      0x80000000


#define GROUP_TYPE_RESOURCE_BEHAVOIR     (GROUP_TYPE_RESOURCE_GROUP    | \
                                          GROUP_TYPE_APP_BASIC_GROUP   | \
                                          GROUP_TYPE_APP_QUERY_GROUP)



///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//   User  Object Related Definitions                                        //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////



//
// Access rights for user object
//

#define USER_READ_GENERAL                0x0001
#define USER_READ_PREFERENCES            0x0002
#define USER_WRITE_PREFERENCES           0x0004
#define USER_READ_LOGON                  0x0008
#define USER_READ_ACCOUNT                0x0010
#define USER_WRITE_ACCOUNT               0x0020
#define USER_CHANGE_PASSWORD             0x0040
#define USER_FORCE_PASSWORD_CHANGE       0x0080
#define USER_LIST_GROUPS                 0x0100
#define USER_READ_GROUP_INFORMATION      0x0200
#define USER_WRITE_GROUP_INFORMATION     0x0400

#define USER_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED       |\
                         USER_READ_PREFERENCES          |\
                         USER_READ_LOGON                |\
                         USER_LIST_GROUPS               |\
                         USER_READ_GROUP_INFORMATION    |\
                         USER_WRITE_PREFERENCES         |\
                         USER_CHANGE_PASSWORD           |\
                         USER_FORCE_PASSWORD_CHANGE     |\
                         USER_READ_GENERAL              |\
                         USER_READ_ACCOUNT              |\
                         USER_WRITE_ACCOUNT             |\
                         USER_WRITE_GROUP_INFORMATION)



#define USER_READ       (STANDARD_RIGHTS_READ           |\
                         USER_READ_PREFERENCES          |\
                         USER_READ_LOGON                |\
                         USER_READ_ACCOUNT              |\
                         USER_LIST_GROUPS               |\
                         USER_READ_GROUP_INFORMATION)


#define USER_WRITE      (STANDARD_RIGHTS_WRITE          |\
                         USER_WRITE_PREFERENCES         |\
                         USER_CHANGE_PASSWORD)

#define USER_EXECUTE    (STANDARD_RIGHTS_EXECUTE        |\
                         USER_READ_GENERAL              |\
                         USER_CHANGE_PASSWORD)


//
// User object types
//

// begin_ntsubauth
#ifndef _NTSAM_USER_ACCOUNT_FLAGS_

//
// User account control flags...
//

#define USER_ACCOUNT_DISABLED                       (0x00000001)
#define USER_HOME_DIRECTORY_REQUIRED                (0x00000002)
#define USER_PASSWORD_NOT_REQUIRED                  (0x00000004)
#define USER_TEMP_DUPLICATE_ACCOUNT                 (0x00000008)
#define USER_NORMAL_ACCOUNT                         (0x00000010)
#define USER_MNS_LOGON_ACCOUNT                      (0x00000020)
#define USER_INTERDOMAIN_TRUST_ACCOUNT              (0x00000040)
#define USER_WORKSTATION_TRUST_ACCOUNT              (0x00000080)
#define USER_SERVER_TRUST_ACCOUNT                   (0x00000100)
#define USER_DONT_EXPIRE_PASSWORD                   (0x00000200)
#define USER_ACCOUNT_AUTO_LOCKED                    (0x00000400)
#define USER_ENCRYPTED_TEXT_PASSWORD_ALLOWED        (0x00000800)
#define USER_SMARTCARD_REQUIRED                     (0x00001000)
#define USER_TRUSTED_FOR_DELEGATION                 (0x00002000)
#define USER_NOT_DELEGATED                          (0x00004000)
#define USER_USE_DES_KEY_ONLY                       (0x00008000)
#define USER_DONT_REQUIRE_PREAUTH                   (0x00010000)
#define USER_PASSWORD_EXPIRED                       (0x00020000)
#define USER_TRUSTED_TO_AUTHENTICATE_FOR_DELEGATION (0x00040000)
#define USER_NO_AUTH_DATA_REQUIRED                  (0x00080000)
#define USER_PARTIAL_SECRETS_ACCOUNT                (0x00100000)
#define USER_USE_AES_KEYS                           (0x00200000)
// end_ntsubauth

//
// The previous bit (USER_USE_AES_KEYS) is not used in the product.
//  To be clearer, this bit is stored and retrieved but is never
//  used by any component.
// We've shipped Vista with it, therefore we have to leave it behind.
//
// !!!! <<<<< DO NOT, AGAIN DO NOT INTRODUCE ANY NEW BITS >>>>> !!!!
//  See Windows OS Bug 1962665 and Windows SE Bug 201918
//
// Also see the use of begin_ntsubauth and end_ntsubauth which is used
//  for generating ntsubauth.h file
//

// begin_ntsubauth
#define NEXT_FREE_ACCOUNT_CONTROL_BIT (USER_USE_AES_KEYS << 1)

#define USER_MACHINE_ACCOUNT_MASK      \
            ( USER_INTERDOMAIN_TRUST_ACCOUNT |\
              USER_WORKSTATION_TRUST_ACCOUNT |\
              USER_SERVER_TRUST_ACCOUNT)

#define USER_ACCOUNT_TYPE_MASK         \
            ( USER_TEMP_DUPLICATE_ACCOUNT |\
              USER_NORMAL_ACCOUNT |\
              USER_MACHINE_ACCOUNT_MASK )

#define USER_COMPUTED_ACCOUNT_CONTROL_BITS  \
            (USER_ACCOUNT_AUTO_LOCKED |     \
             USER_PASSWORD_EXPIRED      )



//
// Logon times may be expressed in day, hour, or minute granularity.
//
//              Days per week    = 7
//              Hours per week   = 168
//              Minutes per week = 10080
//

#define SAM_DAYS_PER_WEEK    (7)
#define SAM_HOURS_PER_WEEK   (24 * SAM_DAYS_PER_WEEK)
#define SAM_MINUTES_PER_WEEK (60 * SAM_HOURS_PER_WEEK)

typedef struct _LOGON_HOURS {

    USHORT UnitsPerWeek;

    //
    // UnitsPerWeek is the number of equal length time units the week is
    // divided into.  This value is used to compute the length of the bit
    // string in logon_hours.  Must be less than or equal to
    // SAM_UNITS_PER_WEEK (10080) for this release.
    //
    // LogonHours is a bit map of valid logon times.  Each bit represents
    // a unique division in a week.  The largest bit map supported is 1260
    // bytes (10080 bits), which represents minutes per week.  In this case
    // the first bit (bit 0, byte 0) is Sunday, 00:00:00 - 00-00:59; bit 1,
    // byte 0 is Sunday, 00:01:00 - 00:01:59, etc.  A NULL pointer means
    // DONT_CHANGE for SamSetInformationUser() calls.
    //

    PUCHAR LogonHours;

} LOGON_HOURS, *PLOGON_HOURS;

typedef struct _SR_SECURITY_DESCRIPTOR {
    ULONG Length;
    PUCHAR SecurityDescriptor;
} SR_SECURITY_DESCRIPTOR, *PSR_SECURITY_DESCRIPTOR;

#define _NTSAM_USER_ACCOUNT_FLAG_
#endif
// end_ntsubauth

typedef enum _USER_INFORMATION_CLASS {
    UserGeneralInformation = 1,
    UserPreferencesInformation,
    UserLogonInformation,
    UserLogonHoursInformation,
    UserAccountInformation,
    UserNameInformation,
    UserAccountNameInformation,
    UserFullNameInformation,
    UserPrimaryGroupInformation,
    UserHomeInformation,
    UserScriptInformation,
    UserProfileInformation,
    UserAdminCommentInformation,
    UserWorkStationsInformation,
    UserSetPasswordInformation,
    UserControlInformation,
    UserExpiresInformation,
    UserInternal1Information,
    UserInternal2Information,
    UserParametersInformation,
    UserAllInformation,
    UserInternal3Information,
    UserInternal4Information,
    UserInternal5Information,
    UserInternal4InformationNew,
    UserInternal5InformationNew,
    UserInternal6Information,
    UserExtendedInformation,
    UserLogonUIInformation,
} USER_INFORMATION_CLASS, *PUSER_INFORMATION_CLASS;

// begin_ntsubauth
#ifndef _NTSAM_USER_ALL_INFO_
#include "pshpack4.h"
typedef struct _USER_ALL_INFORMATION {
    LARGE_INTEGER LastLogon;
    LARGE_INTEGER LastLogoff;
    LARGE_INTEGER PasswordLastSet;
    LARGE_INTEGER AccountExpires;
    LARGE_INTEGER PasswordCanChange;
    LARGE_INTEGER PasswordMustChange;
    UNICODE_STRING UserName;
    UNICODE_STRING FullName;
    UNICODE_STRING HomeDirectory;
    UNICODE_STRING HomeDirectoryDrive;
    UNICODE_STRING ScriptPath;
    UNICODE_STRING ProfilePath;
    UNICODE_STRING AdminComment;
    UNICODE_STRING WorkStations;
    UNICODE_STRING UserComment;
    UNICODE_STRING Parameters;
    UNICODE_STRING LmPassword;
    UNICODE_STRING NtPassword;
    UNICODE_STRING PrivateData;
    SR_SECURITY_DESCRIPTOR SecurityDescriptor;
    ULONG UserId;
    ULONG PrimaryGroupId;
    ULONG UserAccountControl;
    ULONG WhichFields;
    LOGON_HOURS LogonHours;
    USHORT BadPasswordCount;
    USHORT LogonCount;
    USHORT CountryCode;
    USHORT CodePage;
    BOOLEAN LmPasswordPresent;
    BOOLEAN NtPasswordPresent;
    BOOLEAN PasswordExpired;
    BOOLEAN PrivateDataSensitive;
} USER_ALL_INFORMATION,  *PUSER_ALL_INFORMATION;
#include "poppack.h"
#define _NTSAM_USER_ALL_INFO_
#endif
// end_ntsubauth

//
// Bits to be used in UserAllInformation's WhichFields field (to indicate
// which items were queried or set).
//

#define USER_ALL_USERNAME           0x00000001
#define USER_ALL_FULLNAME           0x00000002
#define USER_ALL_USERID             0x00000004
#define USER_ALL_PRIMARYGROUPID     0x00000008
#define USER_ALL_ADMINCOMMENT       0x00000010
#define USER_ALL_USERCOMMENT        0x00000020
#define USER_ALL_HOMEDIRECTORY      0x00000040
#define USER_ALL_HOMEDIRECTORYDRIVE 0x00000080
#define USER_ALL_SCRIPTPATH         0x00000100
#define USER_ALL_PROFILEPATH        0x00000200
#define USER_ALL_WORKSTATIONS       0x00000400
#define USER_ALL_LASTLOGON          0x00000800
#define USER_ALL_LASTLOGOFF         0x00001000
#define USER_ALL_LOGONHOURS         0x00002000
#define USER_ALL_BADPASSWORDCOUNT   0x00004000
#define USER_ALL_LOGONCOUNT         0x00008000
#define USER_ALL_PASSWORDCANCHANGE  0x00010000
#define USER_ALL_PASSWORDMUSTCHANGE 0x00020000
#define USER_ALL_PASSWORDLASTSET    0x00040000
#define USER_ALL_ACCOUNTEXPIRES     0x00080000
#define USER_ALL_USERACCOUNTCONTROL 0x00100000
#ifndef _NTSAM_SAM_USER_PARMS_                 // ntsubauth
#define USER_ALL_PARAMETERS         0x00200000 // ntsubauth
#define _NTSAM_SAM_USER_PARMS_                 // ntsubauth
#endif                                         // ntsubauth
#define USER_ALL_COUNTRYCODE        0x00400000
#define USER_ALL_CODEPAGE           0x00800000
#define USER_ALL_NTPASSWORDPRESENT  0x01000000  // field AND boolean
#define USER_ALL_LMPASSWORDPRESENT  0x02000000  // field AND boolean
#define USER_ALL_PRIVATEDATA        0x04000000  // field AND boolean
#define USER_ALL_PASSWORDEXPIRED    0x08000000
#define USER_ALL_SECURITYDESCRIPTOR 0x10000000
#define USER_ALL_OWFPASSWORD        0x20000000  // boolean

#define USER_ALL_UNDEFINED_MASK     0xC0000000

//
// Now define masks for fields that are accessed for read by the same
// access type.
//
// Fields that require READ_GENERAL access to read.
//

#define USER_ALL_READ_GENERAL_MASK  (USER_ALL_USERNAME               | \
                                    USER_ALL_FULLNAME                | \
                                    USER_ALL_USERID                  | \
                                    USER_ALL_PRIMARYGROUPID          | \
                                    USER_ALL_ADMINCOMMENT            | \
                                    USER_ALL_USERCOMMENT)

//
// Fields that require READ_LOGON access to read.
//

#define USER_ALL_READ_LOGON_MASK    (USER_ALL_HOMEDIRECTORY          | \
                                    USER_ALL_HOMEDIRECTORYDRIVE      | \
                                    USER_ALL_SCRIPTPATH              | \
                                    USER_ALL_PROFILEPATH             | \
                                    USER_ALL_WORKSTATIONS            | \
                                    USER_ALL_LASTLOGON               | \
                                    USER_ALL_LASTLOGOFF              | \
                                    USER_ALL_LOGONHOURS              | \
                                    USER_ALL_BADPASSWORDCOUNT        | \
                                    USER_ALL_LOGONCOUNT              | \
                                    USER_ALL_PASSWORDCANCHANGE       | \
                                    USER_ALL_PASSWORDMUSTCHANGE)

//
// Fields that require READ_ACCOUNT access to read.
//

#define USER_ALL_READ_ACCOUNT_MASK  (USER_ALL_PASSWORDLASTSET        | \
                                    USER_ALL_ACCOUNTEXPIRES          | \
                                    USER_ALL_USERACCOUNTCONTROL      | \
                                    USER_ALL_PARAMETERS)

//
// Fields that require READ_PREFERENCES access to read.
//

#define USER_ALL_READ_PREFERENCES_MASK (USER_ALL_COUNTRYCODE         | \
                                    USER_ALL_CODEPAGE)

//
// Fields that can only be read by trusted clients.
//

#define USER_ALL_READ_TRUSTED_MASK  (USER_ALL_NTPASSWORDPRESENT      | \
                                    USER_ALL_LMPASSWORDPRESENT       | \
                                    USER_ALL_PASSWORDEXPIRED         | \
                                    USER_ALL_SECURITYDESCRIPTOR      | \
                                    USER_ALL_PRIVATEDATA)

//
// Fields that can't be read.
//

#define USER_ALL_READ_CANT_MASK     USER_ALL_UNDEFINED_MASK


//
// Now define masks for fields that are accessed for write by the same
// access type.
//
// Fields that require WRITE_ACCOUNT access to write.
//

#define USER_ALL_WRITE_ACCOUNT_MASK     (USER_ALL_USERNAME           | \
                                        USER_ALL_FULLNAME            | \
                                        USER_ALL_PRIMARYGROUPID      | \
                                        USER_ALL_HOMEDIRECTORY       | \
                                        USER_ALL_HOMEDIRECTORYDRIVE  | \
                                        USER_ALL_SCRIPTPATH          | \
                                        USER_ALL_PROFILEPATH         | \
                                        USER_ALL_ADMINCOMMENT        | \
                                        USER_ALL_WORKSTATIONS        | \
                                        USER_ALL_LOGONHOURS          | \
                                        USER_ALL_ACCOUNTEXPIRES      | \
                                        USER_ALL_USERACCOUNTCONTROL  | \
                                        USER_ALL_PARAMETERS)

//
// Fields that require WRITE_PREFERENCES access to write.
//

#define USER_ALL_WRITE_PREFERENCES_MASK (USER_ALL_USERCOMMENT        | \
                                        USER_ALL_COUNTRYCODE         | \
                                        USER_ALL_CODEPAGE)

//
// Fields that require FORCE_PASSWORD_CHANGE access to write.
//
// Note that non-trusted clients only set the NT password as a
// UNICODE string.  The wrapper will convert it to an LM password,
// OWF and encrypt both versions.  Trusted clients can pass in OWF
// versions of either or both.
//

#define USER_ALL_WRITE_FORCE_PASSWORD_CHANGE_MASK                      \
                                        (USER_ALL_NTPASSWORDPRESENT  | \
                                        USER_ALL_LMPASSWORDPRESENT   | \
                                        USER_ALL_PASSWORDEXPIRED)

//
// Fields that can only be written by trusted clients.
//

#define USER_ALL_WRITE_TRUSTED_MASK     (USER_ALL_LASTLOGON          | \
                                        USER_ALL_LASTLOGOFF          | \
                                        USER_ALL_BADPASSWORDCOUNT    | \
                                        USER_ALL_LOGONCOUNT          | \
                                        USER_ALL_PASSWORDLASTSET     | \
                                        USER_ALL_SECURITYDESCRIPTOR  | \
                                        USER_ALL_PRIVATEDATA)

//
// Fields that can't be written.
//

#define USER_ALL_WRITE_CANT_MASK        (USER_ALL_USERID             | \
                                        USER_ALL_PASSWORDCANCHANGE   | \
                                        USER_ALL_PASSWORDMUSTCHANGE  | \
                                        USER_ALL_UNDEFINED_MASK)


typedef struct _USER_GENERAL_INFORMATION {
    UNICODE_STRING UserName;
    UNICODE_STRING FullName;
    ULONG PrimaryGroupId;
    UNICODE_STRING AdminComment;
    UNICODE_STRING UserComment;
} USER_GENERAL_INFORMATION,  *PUSER_GENERAL_INFORMATION;

typedef struct _USER_PREFERENCES_INFORMATION {
    UNICODE_STRING UserComment;
    UNICODE_STRING Reserved1;
    USHORT CountryCode;
    USHORT CodePage;
} USER_PREFERENCES_INFORMATION,  *PUSER_PREFERENCES_INFORMATION;

typedef struct _USER_PARAMETERS_INFORMATION {
    UNICODE_STRING Parameters;
} USER_PARAMETERS_INFORMATION,  *PUSER_PARAMETERS_INFORMATION;

#include "pshpack4.h"
typedef struct _USER_LOGON_INFORMATION {
    UNICODE_STRING UserName;
    UNICODE_STRING FullName;
    ULONG UserId;
    ULONG PrimaryGroupId;
    UNICODE_STRING HomeDirectory;
    UNICODE_STRING HomeDirectoryDrive;
    UNICODE_STRING ScriptPath;
    UNICODE_STRING ProfilePath;
    UNICODE_STRING WorkStations;
    LARGE_INTEGER LastLogon;
    LARGE_INTEGER LastLogoff;
    LARGE_INTEGER PasswordLastSet;
    LARGE_INTEGER PasswordCanChange;
    LARGE_INTEGER PasswordMustChange;
    LOGON_HOURS LogonHours;
    USHORT BadPasswordCount;
    USHORT LogonCount;
    ULONG UserAccountControl;
} USER_LOGON_INFORMATION, *PUSER_LOGON_INFORMATION;
#include "poppack.h"

#include "pshpack4.h"
typedef struct _USER_ACCOUNT_INFORMATION {
    UNICODE_STRING UserName;
    UNICODE_STRING FullName;
    ULONG UserId;
    ULONG PrimaryGroupId;
    UNICODE_STRING HomeDirectory;
    UNICODE_STRING HomeDirectoryDrive;
    UNICODE_STRING ScriptPath;
    UNICODE_STRING ProfilePath;
    UNICODE_STRING AdminComment;
    UNICODE_STRING WorkStations;
    LARGE_INTEGER LastLogon;
    LARGE_INTEGER LastLogoff;
    LOGON_HOURS LogonHours;
    USHORT BadPasswordCount;
    USHORT LogonCount;
    LARGE_INTEGER PasswordLastSet;
    LARGE_INTEGER AccountExpires;
    ULONG UserAccountControl;
} USER_ACCOUNT_INFORMATION,  *PUSER_ACCOUNT_INFORMATION;
#include "poppack.h"

typedef struct _USER_ACCOUNT_NAME_INFORMATION {
    UNICODE_STRING UserName;
} USER_ACCOUNT_NAME_INFORMATION, *PUSER_ACCOUNT_NAME_INFORMATION;

typedef struct _USER_FULL_NAME_INFORMATION {
    UNICODE_STRING FullName;
} USER_FULL_NAME_INFORMATION, *PUSER_FULL_NAME_INFORMATION;

typedef struct _USER_NAME_INFORMATION {
    UNICODE_STRING UserName;
    UNICODE_STRING FullName;
} USER_NAME_INFORMATION, *PUSER_NAME_INFORMATION;

typedef struct _USER_PRIMARY_GROUP_INFORMATION {
    ULONG PrimaryGroupId;
} USER_PRIMARY_GROUP_INFORMATION, *PUSER_PRIMARY_GROUP_INFORMATION;

typedef struct _USER_HOME_INFORMATION {
    UNICODE_STRING HomeDirectory;
    UNICODE_STRING HomeDirectoryDrive;
} USER_HOME_INFORMATION, *PUSER_HOME_INFORMATION;

typedef struct _USER_SCRIPT_INFORMATION {
    UNICODE_STRING ScriptPath;
} USER_SCRIPT_INFORMATION, *PUSER_SCRIPT_INFORMATION;

typedef struct _USER_PROFILE_INFORMATION {
    UNICODE_STRING ProfilePath;
} USER_PROFILE_INFORMATION, *PUSER_PROFILE_INFORMATION;

typedef struct _USER_ADMIN_COMMENT_INFORMATION {
    UNICODE_STRING AdminComment;
} USER_ADMIN_COMMENT_INFORMATION, *PUSER_ADMIN_COMMENT_INFORMATION;

typedef struct _USER_WORKSTATIONS_INFORMATION {
    UNICODE_STRING WorkStations;
} USER_WORKSTATIONS_INFORMATION, *PUSER_WORKSTATIONS_INFORMATION;

typedef struct _USER_SET_PASSWORD_INFORMATION {
    UNICODE_STRING Password;
    BOOLEAN PasswordExpired;
} USER_SET_PASSWORD_INFORMATION, *PUSER_SET_PASSWORD_INFORMATION;

typedef struct _USER_CONTROL_INFORMATION {
    ULONG UserAccountControl;
} USER_CONTROL_INFORMATION, *PUSER_CONTROL_INFORMATION;

typedef struct _USER_EXPIRES_INFORMATION {
#if defined(MIDL_PASS)
    OLD_LARGE_INTEGER AccountExpires;
#else
    LARGE_INTEGER AccountExpires;
#endif
} USER_EXPIRES_INFORMATION, *PUSER_EXPIRES_INFORMATION;

typedef struct _USER_LOGON_HOURS_INFORMATION {
    LOGON_HOURS LogonHours;
} USER_LOGON_HOURS_INFORMATION, *PUSER_LOGON_HOURS_INFORMATION;

typedef SAM_BYTE_ARRAY_32K  SAM_USER_TILE, *PSAM_USER_TILE;

// 0xFF000FFF is reserved for internal callers and implementation

#define USER_EXTENDED_FIELD_USER_TILE                       (0x00001000L)
#define USER_EXTENDED_FIELD_PASSWORD_HINT                   (0x00002000L)
#define USER_EXTENDED_FIELD_DONT_SHOW_IN_LOGON_UI           (0x00004000L)
#define USER_EXTENDED_FIELD_SHELL_ADMIN_OBJECT_PROPERTIES   (0x00008000L)

typedef struct _USER_EXTENDED_INFORMATION {

    ULONG                               ExtendedWhichFields;
    SAM_USER_TILE                       UserTile;
    UNICODE_STRING                      PasswordHint;
    BOOLEAN                             DontShowInLogonUI;
    SAM_SHELL_OBJECT_PROPERTIES         ShellAdminObjectProperties;

} USER_EXTENDED_INFORMATION, *PUSER_EXTENDED_INFORMATION;

// For local callers only
typedef struct _USER_LOGON_UI_INFORMATION {

    BOOLEAN PasswordIsBlank;
    BOOLEAN AccountIsDisabled;

} USER_LOGON_UI_INFORMATION, *PUSER_LOGON_UI_INFORMATION;

///////////////////////////////////////////////////////////////////////////
//                                                                       //
// Data type used by SamChangePasswordUser3 for better error             //
// reporting of password change change failures                          //
//                                                                       //
// The field definitions are as follows:                                 //
//                                                                       //
//        ExtendedFailureReason   -- Indicates the reason                //
//                                   why the new password was not        //
//                                   accepted                            //
//                                                                       //
//        FilterModuleName        -- If the password change was failed   //
//                                   by a password filter , the name of  //
//                                   of the filter DLL is returned in    //
//                                   here                                //
//                                                                       //
// The following error codes are defined                                 //
//                                                                       //
//   SAM_PWD_CHANGE_NO_ERROR                                             //
//           No error, cannot be returned alongwith a failure code for   //
//                                   password change                     //
//                                                                       //
//   SAM_PWD_CHANGE_PASSWORD_TOO_SHORT                                   //
//                                                                       //
//               Supplied password did not meet password length policy   //
//                                                                       //
//   SAM_PWD_CHANGE_PWD_IN_HISTORY                                       //
//                                                                       //
//                History restrictions were not met                      //
//                                                                       //
//   SAM_PWD_CHANGE_USERNAME_IN_PASSWORD                                 //
//                 Complexity check could not  be met because the user   //
//                 name was part of the password                         //
//                                                                       //
//   SAM_PWD_CHANGE_FULLNAME_IN_PASSWORD                                 //
//                                                                       //
//                Complexity check could not be met because the user's   //
//                full name was part of the password                     //
//                                                                       //
//   SAM_PWD_CHANGE_MACHINE_PASSWORD_NOT_DEFAULT                         //
//                                                                       //
//                The  domain has the refuse password change setting     //
//                enabled. This disallows machine accounts from having   //
//                anything other than the default password               //
//                                                                       //
//   SAM_PWD_CHANGE_FAILED_BY_FILTER                                     //
//                                                                       //
//                The supplied new password failed by a password filter  //
//                the name of the filter DLL is indicated                //
//                                                                       //
//                                                                       //
///////////////////////////////////////////////////////////////////////////

typedef struct _USER_PWD_CHANGE_FAILURE_INFORMATION {
    ULONG                       ExtendedFailureReason;
    UNICODE_STRING              FilterModuleName;
} USER_PWD_CHANGE_FAILURE_INFORMATION,*PUSER_PWD_CHANGE_FAILURE_INFORMATION;

//
// Currently defined values for ExtendedFailureReason are as follows
//


#define SAM_PWD_CHANGE_NO_ERROR                     0
#define SAM_PWD_CHANGE_PASSWORD_TOO_SHORT           1
#define SAM_PWD_CHANGE_PWD_IN_HISTORY               2
#define SAM_PWD_CHANGE_USERNAME_IN_PASSWORD         3
#define SAM_PWD_CHANGE_FULLNAME_IN_PASSWORD         4
#define SAM_PWD_CHANGE_NOT_COMPLEX                  5
#define SAM_PWD_CHANGE_MACHINE_PASSWORD_NOT_DEFAULT 6
#define SAM_PWD_CHANGE_FAILED_BY_FILTER             7
#define SAM_PWD_CHANGE_PASSWORD_TOO_LONG            8
#define SAM_PWD_CHANGE_FAILURE_REASON_MAX           8


/////////////////////////////////////////////////////////////////////////////
//                                                                         //
// Data types used by SAM and Netlogon for database replication            //
//                                                                         //
/////////////////////////////////////////////////////////////////////////////


typedef enum _SECURITY_DB_DELTA_TYPE {
    SecurityDbNew = 1,
    SecurityDbRename,
    SecurityDbDelete,
    SecurityDbChangeMemberAdd,
    SecurityDbChangeMemberSet,
    SecurityDbChangeMemberDel,
    SecurityDbChange,
    SecurityDbChangePassword
} SECURITY_DB_DELTA_TYPE, *PSECURITY_DB_DELTA_TYPE;

typedef enum _SECURITY_DB_OBJECT_TYPE {
    SecurityDbObjectSamDomain = 1,
    SecurityDbObjectSamUser,
    SecurityDbObjectSamGroup,
    SecurityDbObjectSamAlias,
    SecurityDbObjectLsaPolicy,
    SecurityDbObjectLsaTDomain,
    SecurityDbObjectLsaAccount,
    SecurityDbObjectLsaSecret
} SECURITY_DB_OBJECT_TYPE, *PSECURITY_DB_OBJECT_TYPE;

//
// Account types
//
//  Both enumerated types and flag definitions are provided.
//  The flag definitions are used in places where more than
//  one type of account may be specified together.
//

typedef enum _SAM_ACCOUNT_TYPE {
    SamObjectUser = 1,
    SamObjectGroup ,
    SamObjectAlias
} SAM_ACCOUNT_TYPE, *PSAM_ACCOUNT_TYPE;


#define SAM_USER_ACCOUNT                (0x00000001)
#define SAM_GLOBAL_GROUP_ACCOUNT        (0x00000002)
#define SAM_LOCAL_GROUP_ACCOUNT         (0x00000004)



//
// Define the data type used to pass netlogon information on the account
// that was added or deleted from a group.
//

typedef struct _SAM_GROUP_MEMBER_ID {
    ULONG   MemberRid;
} SAM_GROUP_MEMBER_ID, *PSAM_GROUP_MEMBER_ID;


//
// Define the data type used to pass netlogon information on the account
// that was added or deleted from an alias.
//

typedef struct _SAM_ALIAS_MEMBER_ID {
    PSID    MemberSid;
} SAM_ALIAS_MEMBER_ID, *PSAM_ALIAS_MEMBER_ID;




//
// Define the data type used to pass netlogon information on a delta
//

typedef union _SAM_DELTA_DATA {

    //
    // Delta type ChangeMember{Add/Del/Set} and account type group
    //

    SAM_GROUP_MEMBER_ID GroupMemberId;

    //
    // Delta type ChangeMember{Add/Del/Set} and account type alias
    //

    SAM_ALIAS_MEMBER_ID AliasMemberId;

    //
    // Delta type AddOrChange and account type User
    //

    ULONG  AccountControl;

} SAM_DELTA_DATA, *PSAM_DELTA_DATA;


//
// Prototype for delta notification routine.
//

typedef NTSTATUS (*PSAM_DELTA_NOTIFICATION_ROUTINE) (
    __in PSID DomainSid,
    __in SECURITY_DB_DELTA_TYPE DeltaType,
    __in SECURITY_DB_OBJECT_TYPE ObjectType,
    __in ULONG ObjectRid,
    __in_opt PUNICODE_STRING ObjectName,
    __in PLARGE_INTEGER ModifiedCount,
    __in_opt PSAM_DELTA_DATA DeltaData 
    );

#define SAM_DELTA_NOTIFY_ROUTINE "DeltaNotify"


//////////////////////////////////////////////////////////////////
//                                                              //
// Structure and ProtoType for RAS User Parameters              //
//                                                              //
//////////////////////////////////////////////////////////////////

// Flags used by SAM UserParms Migration
// indicate UserParmsConvert is called during upgrade.

#define SAM_USERPARMS_DURING_UPGRADE    0x00000001


typedef struct _SAM_USERPARMS_ATTRVALS {
    ULONG   length;     // length of the attribute.
    __field_bcount(length) PVOID   value;      // pointer to the value.
} SAM_USERPARMS_ATTRVALS, *PSAM_USERPARMS_ATTRVALS; // describes one value of the attribute.


typedef enum _SAM_USERPARMS_ATTRSYNTAX {
    Syntax_Attribute = 1,
    Syntax_EncryptedAttribute
} SAM_USERPARMS_ATTRSYNTAX;          // indicates whether attributes are encrypted or not.


typedef struct _SAM_USERPARMS_ATTR {
    UNICODE_STRING AttributeIdentifier;     // This will be the LDAP display name of the attribute.
                                            // SAM will perform the translation to attribute ID.
                                            // unless the specified syntax is type EncryptedAttribute,
                                            // in which case it is packaged as part of supplemental
                                            // credentials blob and the name identifes the package name.
                                            // Encrypted attribute will be supplied in the clear ie decrypted.
    SAM_USERPARMS_ATTRSYNTAX Syntax;
    ULONG CountOfValues;                    // The count of values in the attribute.
    __ecount(CountOfValues) SAM_USERPARMS_ATTRVALS * Values;        
                                            // pointer to an array of values representing the data
                                            // values of the attribute.
} SAM_USERPARMS_ATTR, *PSAM_USERPARMS_ATTR; // describes an attribute and the set of values associated with it.


typedef struct _SAM_USERPARMS_ATTRBLOCK {
    ULONG attCount;
    __ecount(attCount) SAM_USERPARMS_ATTR * UserParmsAttr;
} SAM_USERPARMS_ATTRBLOCK, *PSAM_USERPARMS_ATTRBLOCK;  // describes an array of attributes


typedef NTSTATUS (*PSAM_USERPARMS_CONVERT_NOTIFICATION_ROUTINE) (
    __in                              ULONG    Flags,
    __in                              PSID     DomainSid,
    __in                              ULONG    ObjectRid,  // identifies the object
    __in                              ULONG    UserParmsLengthOrig,
    __in_bcount(UserParmsLengthOrig)  PVOID    UserParmsOrig,
    __in                              ULONG    UserParmsLengthNew,
    __in_bcount(UserParmsLengthNew)   PVOID    UserParmsNew,
    __deref_out                       PSAM_USERPARMS_ATTRBLOCK *  UserParmsAttrBlock
);

#define SAM_USERPARMS_CONVERT_NOTIFICATION_ROUTINE "UserParmsConvert"


typedef VOID (*PSAM_USERPARMS_ATTRBLOCK_FREE_ROUTINE) (
    IN PSAM_USERPARMS_ATTRBLOCK UserParmsAttrBlock
);

#define SAM_USERPARMS_ATTRBLOCK_FREE_ROUTINE    "UserParmsFree"


//////////////////////////////////////////////////////////////////
//                                                              //
// Return Values for Compatiblity Mode                          //
//                                                              //
//////////////////////////////////////////////////////////////////

// All SAM attributes are accessible
#define SAM_SID_COMPATIBILITY_ALL     0

// Rid field can be returned to caller as 0
// No writes to PrimaryGroupId allowed
#define SAM_SID_COMPATIBILITY_LAX     1

// NET API Information levels that ask for RID are to failed
// No writes to PrimaryGroupId allowed
#define SAM_SID_COMPATIBILITY_STRICT  2


//
//    *************************************
//
//    Internal Password Checking API structures
//
//    *************************************
//

//
//    What kind of password checking is to be performed?
//        SamValidateAuthentication : Check if the authentication can be done
//        SamValidatePasswordChange: Check if the password can be changed
//        SamValidatePasswordReset: Reset the password to the given value
//
typedef enum _PASSWORD_POLICY_VALIDATION_TYPE{
    SamValidateAuthentication = 1,
    SamValidatePasswordChange,
    SamValidatePasswordReset
} PASSWORD_POLICY_VALIDATION_TYPE;

//
//    Structure to keep the password hash
//
typedef struct _SAM_VALIDATE_PASSWORD_HASH{
    ULONG Length;
#ifdef MIDL_PASS
    [unique,size_is(Length)]
#endif
    PUCHAR Hash;
} SAM_VALIDATE_PASSWORD_HASH, *PSAM_VALIDATE_PASSWORD_HASH;


// To be used with PresentFields member of SAM_VALIDATE_PERSISTED_FIELDS
#define SAM_VALIDATE_PASSWORD_LAST_SET          0x00000001
#define SAM_VALIDATE_BAD_PASSWORD_TIME          0x00000002
#define SAM_VALIDATE_LOCKOUT_TIME               0x00000004
#define SAM_VALIDATE_BAD_PASSWORD_COUNT         0x00000008
#define SAM_VALIDATE_PASSWORD_HISTORY_LENGTH    0x00000010
#define SAM_VALIDATE_PASSWORD_HISTORY           0x00000020

//
//    Structure to keep information about the password and related things.
//        Present Fields: (used only in output args) which fields are changed.
//            See the constants above.
//        PasswordLastSet: When the password is last set.
//        BadPasswordTime: When the password was incorrect for the last time.
//        LockoutTime: When the account is locked out. If the account is not locked out
//            it is 0.
//        BadPasswordCount: How many times the password has given incorrectly in the
//            Observation Window.
//        PasswordHistoryLength: How many passwords are kept in the history
//        PasswordHistory: Password hashes that are in the history
//
typedef struct _SAM_VALIDATE_PERSISTED_FIELDS{
    ULONG PresentFields;
    LARGE_INTEGER PasswordLastSet;
    LARGE_INTEGER BadPasswordTime;
    LARGE_INTEGER LockoutTime;
    ULONG BadPasswordCount;
    ULONG PasswordHistoryLength;
#ifdef MIDL_PASS
    [unique,size_is(PasswordHistoryLength)]
#endif
    PSAM_VALIDATE_PASSWORD_HASH PasswordHistory;
} SAM_VALIDATE_PERSISTED_FIELDS, *PSAM_VALIDATE_PERSISTED_FIELDS;

//
//    Validation Status of the password check:
//        Names are self-explaining so I think there is no need to explain them here.
//
typedef enum _SAM_VALIDATE_VALIDATION_STATUS{
    SamValidateSuccess = 0,
    SamValidatePasswordMustChange,
    SamValidateAccountLockedOut,
    SamValidatePasswordExpired,
    SamValidatePasswordIncorrect,
    SamValidatePasswordIsInHistory,
    SamValidatePasswordTooShort,
    SamValidatePasswordTooLong,
    SamValidatePasswordNotComplexEnough,
    SamValidatePasswordTooRecent,
    SamValidatePasswordFilterError
}SAM_VALIDATE_VALIDATION_STATUS, *PSAM_VALIDATE_VALIDATION_STATUS;

//
//    Output Arg
//        ChangedPersistedFields: Any changes to the password related info
//        ValidationStatus: Shows the result of the request
//
typedef struct _SAM_VALIDATE_STANDARD_OUTPUT_ARG{
    SAM_VALIDATE_PERSISTED_FIELDS ChangedPersistedFields;
    SAM_VALIDATE_VALIDATION_STATUS ValidationStatus;
} SAM_VALIDATE_STANDARD_OUTPUT_ARG, *PSAM_VALIDATE_STANDARD_OUTPUT_ARG;

//
//    If authentication type of password check is to be made,
//        this kind of input must be used
//
//    InputPersistedFields: Information about the account to be logged into
//    PasswordMatched: Indicates the result of the application's authentication of the supplied password

typedef struct _SAM_VALIDATE_AUTHENTICATION_INPUT_ARG{
    SAM_VALIDATE_PERSISTED_FIELDS InputPersistedFields;
    BOOLEAN PasswordMatched; // indicates the result of the application's authentication of the supplied password
} SAM_VALIDATE_AUTHENTICATION_INPUT_ARG, *PSAM_VALIDATE_AUTHENTICATION_INPUT_ARG;

//
//    If password change type of check is to be made,
//        this kind of input must be used
//
//    InputPersistedFields: Information about the account to be logged into
//    ClearPassword: The string which password is going to be
//    UserAccountName: Name of the user account
//    HashedPassword: Hash of the string that the password is going to be
//    PasswordMatch: denotes if the old password supplied by user matched or not
//
typedef struct  _SAM_VALIDATE_PASSWORD_CHANGE_INPUT_ARG{
    SAM_VALIDATE_PERSISTED_FIELDS InputPersistedFields;
    UNICODE_STRING ClearPassword;
    UNICODE_STRING UserAccountName;
    SAM_VALIDATE_PASSWORD_HASH HashedPassword;
    BOOLEAN PasswordMatch; // denotes if the old password supplied by user matched or not.
} SAM_VALIDATE_PASSWORD_CHANGE_INPUT_ARG, *PSAM_VALIDATE_PASSWORD_CHANGE_INPUT_ARG;

//
//    If password reset type of check is to be made,
//        this kind of input must be used
//
//    InputPersistedFields: Information about the account to be logged into
//    ClearPassword: The string which password is going to be
//    UserAccountName: Name of the user account
//    HashedPassword: Hash of the string that the password is going to be
//    PasswordMustChangeAtNextLogon: Password must change for the user to be logged in
//    ClearLockout: If the account was locked out, this field can be used to clear lockout
//
typedef struct _SAM_VALIDATE_PASSWORD_RESET_INPUT_ARG{
    SAM_VALIDATE_PERSISTED_FIELDS InputPersistedFields;
    UNICODE_STRING ClearPassword;
    UNICODE_STRING UserAccountName;
    SAM_VALIDATE_PASSWORD_HASH HashedPassword;
    BOOLEAN  PasswordMustChangeAtNextLogon; // looked at only for password reset
    BOOLEAN  ClearLockout; // can be used clear user account lockout --
}SAM_VALIDATE_PASSWORD_RESET_INPUT_ARG, *PSAM_VALIDATE_PASSWORD_RESET_INPUT_ARG;


//
//    A union to encapsulate all kinds of inputs
//
typedef
#ifdef MIDL_PASS
    [switch_type(PASSWORD_POLICY_VALIDATION_TYPE)]
#endif
    union _SAM_VALIDATE_INPUT_ARG{
#ifdef MIDL_PASS
        [case(SamValidateAuthentication)]
#endif
        SAM_VALIDATE_AUTHENTICATION_INPUT_ARG ValidateAuthenticationInput;
#ifdef MIDL_PASS
        [case(SamValidatePasswordChange)]
#endif
        SAM_VALIDATE_PASSWORD_CHANGE_INPUT_ARG ValidatePasswordChangeInput;
#ifdef MIDL_PASS
        [case(SamValidatePasswordReset)]
#endif
        SAM_VALIDATE_PASSWORD_RESET_INPUT_ARG ValidatePasswordResetInput;
} SAM_VALIDATE_INPUT_ARG,  *PSAM_VALIDATE_INPUT_ARG;

//
//    A union to encapsulate all kinds of outputs
//        Actually, currently there is only one type of output,
//        but this can be used when there is a need for another
//        type of output.
//
typedef
#ifdef MIDL_PASS
    [switch_type(PASSWORD_POLICY_VALIDATION_TYPE)]
#endif
    union _SAM_VALIDATE_OUTPUT_ARG{
#ifdef MIDL_PASS
        [case(SamValidateAuthentication)]
#endif
        SAM_VALIDATE_STANDARD_OUTPUT_ARG ValidateAuthenticationOutput;
#ifdef MIDL_PASS
        [case(SamValidatePasswordChange)]
#endif
        SAM_VALIDATE_STANDARD_OUTPUT_ARG ValidatePasswordChangeOutput;
#ifdef MIDL_PASS
        [case(SamValidatePasswordReset)]
#endif
        SAM_VALIDATE_STANDARD_OUTPUT_ARG ValidatePasswordResetOutput;
} SAM_VALIDATE_OUTPUT_ARG,  *PSAM_VALIDATE_OUTPUT_ARG;

//
// Internal Password Checking API structures end here!
//

//
// Generic operation API structures start here
//

typedef enum _SAM_GENERIC_OPERATION_TYPE {

    SamObjectChangeNotificationOperation,

} SAM_GENERIC_OPERATION_TYPE, *PSAM_GENERIC_OPERATION_TYPE;

typedef struct _SAM_OPERATION_OBJCHG_INPUT {

    BOOLEAN                     Register;
    ULONG64                     EventHandle;
    SECURITY_DB_OBJECT_TYPE     ObjectType;
    ULONG                       ProcessID;

} SAM_OPERATION_OBJCHG_INPUT, *PSAM_OPERATION_OBJCHG_INPUT;

typedef struct _SAM_OPERATION_OBJCHG_OUTPUT {

    ULONG                       Reserved;

} SAM_OPERATION_OBJCHG_OUTPUT, *PSAM_OPERATION_OBJCHG_OUTPUT;

typedef SWITCH_TYPE( SAM_GENERIC_OPERATION_TYPE ) union _SAM_GENERIC_OPERATION_INPUT {

    CASE( SamObjectChangeNotificationOperation )    SAM_OPERATION_OBJCHG_INPUT ObjChangeIn;

} SAM_GENERIC_OPERATION_INPUT, *PSAM_GENERIC_OPERATION_INPUT;

typedef SWITCH_TYPE( SAM_GENERIC_OPERATION_TYPE ) union _SAM_GENERIC_OPERATION_OUTPUT {

    CASE( SamObjectChangeNotificationOperation )    SAM_OPERATION_OBJCHG_OUTPUT ObjChangeOut;

} SAM_GENERIC_OPERATION_OUTPUT, *PSAM_GENERIC_OPERATION_OUTPUT;

//
// Generic operation API structures end here
//


///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//   APIs Exported By SAM                                                    //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////


NTSTATUS
SamFreeMemory(
    __in PVOID Buffer
    );


__checkReturn
NTSTATUS
SamSetSecurityObject(
    __in SAM_HANDLE ObjectHandle,
    __in SECURITY_INFORMATION SecurityInformation,
    __in PSECURITY_DESCRIPTOR SecurityDescriptor
    );

__checkReturn
NTSTATUS
SamQuerySecurityObject(
    __in        SAM_HANDLE ObjectHandle,
    __in        SECURITY_INFORMATION SecurityInformation,
    __deref_out PSECURITY_DESCRIPTOR *SecurityDescriptor
    );

NTSTATUS
SamCloseHandle(
    __in SAM_HANDLE SamHandle
    );

__checkReturn
NTSTATUS
SamConnect(
    __in_opt  PUNICODE_STRING ServerName,
    __out     PSAM_HANDLE ServerHandle,
    __in      ACCESS_MASK DesiredAccess,
    __in      POBJECT_ATTRIBUTES ObjectAttributes
    );

__checkReturn
NTSTATUS
SamShutdownSamServer(
    __in SAM_HANDLE ServerHandle
    );

__checkReturn
NTSTATUS
SamLookupDomainInSamServer(
    __in        SAM_HANDLE ServerHandle,
    __in        PUNICODE_STRING Name,
    __deref_out PSID * DomainId
    );

__checkReturn
NTSTATUS
SamEnumerateDomainsInSamServer(
    __in        SAM_HANDLE ServerHandle,
    __inout     PSAM_ENUMERATE_HANDLE EnumerationContext,
    __deref_out PVOID *Buffer,
    __in        ULONG PreferedMaximumLength,
    __out       PULONG CountReturned
    );

__checkReturn
NTSTATUS
SamOpenDomain(
    __in  SAM_HANDLE ServerHandle,
    __in  ACCESS_MASK DesiredAccess,
    __in  PSID DomainId,
    __out PSAM_HANDLE DomainHandle
    );

__checkReturn
NTSTATUS
SamQueryInformationDomain(
    __in        SAM_HANDLE DomainHandle,
    __in        DOMAIN_INFORMATION_CLASS DomainInformationClass,
    __deref_out PVOID *Buffer
    );

__checkReturn
NTSTATUS
SamSetInformationDomain(
    __in SAM_HANDLE DomainHandle,
    __in DOMAIN_INFORMATION_CLASS DomainInformationClass,
    __in PVOID DomainInformation
    );

__checkReturn
NTSTATUS
SamCreateGroupInDomain(
    __in  SAM_HANDLE DomainHandle,
    __in  PUNICODE_STRING AccountName,
    __in  ACCESS_MASK DesiredAccess,
    __out PSAM_HANDLE GroupHandle,
    __out PULONG RelativeId
    );


__checkReturn
NTSTATUS
SamEnumerateGroupsInDomain(
    __in        SAM_HANDLE DomainHandle,
    __inout     PSAM_ENUMERATE_HANDLE EnumerationContext,
    __deref_out PVOID *Buffer,
    __in        ULONG PreferedMaximumLength,
    __out       PULONG CountReturned
    );

__checkReturn
NTSTATUS
SamCreateUser2InDomain(
    __in  SAM_HANDLE DomainHandle,
    __in  PUNICODE_STRING AccountName,
    __in  ULONG AccountType,
    __in  ACCESS_MASK DesiredAccess,
    __out PSAM_HANDLE UserHandle,
    __out PULONG GrantedAccess,
    __out PULONG RelativeId
    );

__checkReturn
NTSTATUS
SamCreateUserInDomain(
    __in  SAM_HANDLE DomainHandle,
    __in  PUNICODE_STRING AccountName,
    __in  ACCESS_MASK DesiredAccess,
    __out PSAM_HANDLE UserHandle,
    __out PULONG RelativeId
    );

__checkReturn
NTSTATUS
SamEnumerateUsersInDomain(
    __in        SAM_HANDLE DomainHandle,
    __inout     PSAM_ENUMERATE_HANDLE EnumerationContext,
    __in        ULONG UserAccountControl,
    __deref_out PVOID *Buffer,
    __in        ULONG PreferedMaximumLength,
    __out       PULONG CountReturned
    );

__checkReturn
NTSTATUS
SamCreateAliasInDomain(
    __in  SAM_HANDLE DomainHandle,
    __in  PUNICODE_STRING AccountName,
    __in  ACCESS_MASK DesiredAccess,
    __out PSAM_HANDLE AliasHandle,
    __out PULONG RelativeId
    );

__checkReturn
NTSTATUS
SamEnumerateAliasesInDomain(
    __in        SAM_HANDLE DomainHandle,
    __inout     PSAM_ENUMERATE_HANDLE EnumerationContext,
    __deref_out PVOID *Buffer,
    __in        ULONG PreferedMaximumLength,
    __out       PULONG CountReturned
    );

__checkReturn
NTSTATUS
SamGetAliasMembership(
    __in                                 SAM_HANDLE DomainHandle,
    __in                                 ULONG PassedCount,
    __in_ecount(PassedCount)             PSID *Sids,
    __out                                PULONG MembershipCount,
    __deref_out_ecount(*MembershipCount) PULONG *Aliases
    );

__checkReturn
NTSTATUS
SamLookupNamesInDomain(
    __in SAM_HANDLE               DomainHandle,
    __in                          ULONG Count,
    __in_ecount(Count)            PUNICODE_STRING Names,
    __deref_out_ecount(Count)     PULONG *RelativeIds,
    __deref_out_ecount(Count)     PSID_NAME_USE *Use
    );

__checkReturn
NTSTATUS
SamLookupIdsInDomain(
    __in                      SAM_HANDLE DomainHandle,
    __in                      ULONG Count,
    __in_ecount(Count)        PULONG RelativeIds,
    __deref_out_ecount(Count) PUNICODE_STRING *Names,
    __deref_opt_out_ecount(Count) PSID_NAME_USE *Use
    );

__checkReturn
NTSTATUS
SamOpenGroup(
    __in  SAM_HANDLE DomainHandle,
    __in  ACCESS_MASK DesiredAccess,
    __in  ULONG GroupId,
    __out PSAM_HANDLE GroupHandle
    );

__checkReturn
NTSTATUS
SamQueryInformationGroup(
    __in        SAM_HANDLE GroupHandle,
    __in        GROUP_INFORMATION_CLASS GroupInformationClass,
    __deref_out PVOID *Buffer
    );

__checkReturn
NTSTATUS
SamSetInformationGroup(
    __in SAM_HANDLE GroupHandle,
    __in GROUP_INFORMATION_CLASS GroupInformationClass,
    __in PVOID Buffer
    );

__checkReturn
NTSTATUS
SamAddMemberToGroup(
    __in SAM_HANDLE GroupHandle,
    __in ULONG MemberId,
    __in ULONG Attributes
    );

__checkReturn
NTSTATUS
SamDeleteGroup(
    __in SAM_HANDLE GroupHandle
    );

__checkReturn
NTSTATUS
SamRemoveMemberFromGroup(
    __in SAM_HANDLE GroupHandle,
    __in ULONG MemberId
    );

__checkReturn
NTSTATUS
SamGetMembersInGroup(
    __in                             SAM_HANDLE GroupHandle,
    __deref_out_ecount(*MemberCount) PULONG * MemberIds,
    __deref_out_ecount(*MemberCount) PULONG * Attributes,
    __out                            PULONG MemberCount
    );

__checkReturn
NTSTATUS
SamSetMemberAttributesOfGroup(
    __in SAM_HANDLE GroupHandle,
    __in ULONG MemberId,
    __in ULONG Attributes
    );

__checkReturn
NTSTATUS
SamOpenAlias(
    __in  SAM_HANDLE DomainHandle,
    __in  ACCESS_MASK DesiredAccess,
    __in  ULONG AliasId,
    __out PSAM_HANDLE AliasHandle
    );

__checkReturn
NTSTATUS
SamQueryInformationAlias(
    __in        SAM_HANDLE AliasHandle,
    __in        ALIAS_INFORMATION_CLASS AliasInformationClass,
    __deref_out PVOID *Buffer
    );

__checkReturn
NTSTATUS
SamSetInformationAlias(
    __in SAM_HANDLE AliasHandle,
    __in ALIAS_INFORMATION_CLASS AliasInformationClass,
    __in PVOID Buffer
    );

__checkReturn
NTSTATUS
SamDeleteAlias(
    __in SAM_HANDLE AliasHandle
    );

__checkReturn
NTSTATUS
SamAddMemberToAlias(
    __in SAM_HANDLE AliasHandle,
    __in PSID MemberId
    );

__checkReturn
NTSTATUS
SamAddMultipleMembersToAlias(
    __in                     SAM_HANDLE   AliasHandle,
    __in_ecount(MemberCount) PSID         *MemberIds,
    __in                     ULONG        MemberCount
    );

__checkReturn
NTSTATUS
SamRemoveMemberFromAlias(
    __in SAM_HANDLE AliasHandle,
    __in PSID MemberId
    );

__checkReturn
NTSTATUS
SamRemoveMultipleMembersFromAlias(
    __in                     SAM_HANDLE   AliasHandle,
    __in_ecount(MemberCount) PSID         *MemberIds,
    __in ULONG               MemberCount
    );

__checkReturn
NTSTATUS
SamRemoveMemberFromForeignDomain(
    __in SAM_HANDLE DomainHandle,
    __in PSID MemberId
    );

__checkReturn
NTSTATUS
SamGetMembersInAlias(
    __in                             SAM_HANDLE AliasHandle,
    __deref_out_ecount(*MemberCount) PSID **MemberIds,
    __out                            PULONG MemberCount
    );

__checkReturn
NTSTATUS
SamOpenUser(
    __in  SAM_HANDLE DomainHandle,
    __in  ACCESS_MASK DesiredAccess,
    __in  ULONG UserId,
    __out PSAM_HANDLE UserHandle
    );

__checkReturn
NTSTATUS
SamDeleteUser(
    __in SAM_HANDLE UserHandle
    );

__checkReturn
NTSTATUS
SamQueryInformationUser(
    __in        SAM_HANDLE UserHandle,
    __in        USER_INFORMATION_CLASS UserInformationClass,
    __deref_out PVOID * Buffer
    );

__checkReturn
NTSTATUS
SamSetInformationUser(
    __in SAM_HANDLE UserHandle,
    __in USER_INFORMATION_CLASS UserInformationClass,
    __in PVOID Buffer
    );

__checkReturn
NTSTATUS
SamChangePasswordUser(
    __in SAM_HANDLE UserHandle,
    __in PUNICODE_STRING OldPassword,
    __in PUNICODE_STRING NewPassword
    );

__checkReturn
NTSTATUS
SamChangePasswordUser2(
    __in PUNICODE_STRING ServerName,
    __in PUNICODE_STRING UserName,
    __in PUNICODE_STRING OldPassword,
    __in PUNICODE_STRING NewPassword
    );


__checkReturn
NTSTATUS
SamChangePasswordUser3(
    __in        PUNICODE_STRING ServerName,
    __in        PUNICODE_STRING UserName,
    __in        PUNICODE_STRING OldPassword,
    __in        PUNICODE_STRING NewPassword,
    __deref_out PDOMAIN_PASSWORD_INFORMATION * EffectivePasswordPolicy,
    __deref_out PUSER_PWD_CHANGE_FAILURE_INFORMATION *PasswordChangeFailureInfo
    );


__checkReturn
NTSTATUS
SamGetGroupsForUser(
    __in                                 SAM_HANDLE UserHandle,
    __deref_out_ecount(*MembershipCount) PGROUP_MEMBERSHIP * Groups,
    __out                                PULONG MembershipCount
    );

__checkReturn
NTSTATUS
SamQueryDisplayInformation (
      __in        SAM_HANDLE DomainHandle,
      __in        DOMAIN_DISPLAY_INFORMATION DisplayInformation,
      __in        ULONG      Index,
      __in        ULONG      EntryCount,
      __in        ULONG      PreferredMaximumLength,
      __in        PULONG     TotalAvailable,
      __out       PULONG     TotalReturned,
      __out       PULONG     ReturnedEntryCount,
      __deref_out PVOID      *SortedBuffer
      );

__checkReturn
NTSTATUS
SamGetDisplayEnumerationIndex (
      __in    SAM_HANDLE        DomainHandle,
      __in    DOMAIN_DISPLAY_INFORMATION DisplayInformation,
      __in    PUNICODE_STRING   Prefix,
      __out   PULONG            Index
      );

__checkReturn
NTSTATUS
SamRidToSid(
    __in        SAM_HANDLE ObjectHandle,
    __in        ULONG      Rid,
    __deref_out PSID*      Sid
    );

__checkReturn
NTSTATUS
SamGetCompatibilityMode(
    __in  SAM_HANDLE ObjectHandle,
    __out ULONG*     Mode
    );

__checkReturn
NTSTATUS
SamValidatePassword(
    __in_opt PUNICODE_STRING ServerName,
    __in     PASSWORD_POLICY_VALIDATION_TYPE ValidationType,
    __in     PSAM_VALIDATE_INPUT_ARG InputArg,
    __out    PSAM_VALIDATE_OUTPUT_ARG *OutputArg
    );

__checkReturn
NTSTATUS
SamQueryLocalizableAccountsInDomain(
    __in         SAM_HANDLE                              Domain,
    __in         ULONG                                   Flags,
    __in         ULONG                                   LanguageId,
    __in         DOMAIN_LOCALIZABLE_ACCOUNTS_INFORMATION Class,
    __deref_out  PVOID                                  *Buffer
    );

__checkReturn
NTSTATUS
SamRegisterObjectChangeNotification(
    __in    SECURITY_DB_OBJECT_TYPE     ObjectType,
    __in    HANDLE                      NotificationEventHandle
    );

NTSTATUS
SamUnregisterObjectChangeNotification(
    __in    SECURITY_DB_OBJECT_TYPE     ObjectType,
    __in    HANDLE                      NotificationEventHandle
    );

__checkReturn
NTSTATUS
SamPerformGenericOperation(
    __in_opt    PWSTR                           ServerName,
    __in        SAM_GENERIC_OPERATION_TYPE      OperationType,
    __in        PSAM_GENERIC_OPERATION_INPUT    OperationIn,
    __out       PSAM_GENERIC_OPERATION_OUTPUT   *OperationOut
    );


////////////////////////////////////////////////////////////////////////////
//                                                                        //
// Interface definitions of services provided by a password filter DLL    //
//                                                                        //
////////////////////////////////////////////////////////////////////////////




//
// Routine names
//
// The routines provided by the DLL must be assigned the following names
// so that their addresses can be retrieved when the DLL is loaded.
//


//
// routine templates
//


//
// These guards are in place to allow ntsam.h and ntsecapi.h
// to be included in the same file.
//

// begin_ntsecapi

#ifndef _PASSWORD_NOTIFICATION_DEFINED
#define _PASSWORD_NOTIFICATION_DEFINED
typedef NTSTATUS (*PSAM_PASSWORD_NOTIFICATION_ROUTINE) (
    PUNICODE_STRING UserName,
    ULONG RelativeId,
    PUNICODE_STRING NewPassword
);

#define SAM_PASSWORD_CHANGE_NOTIFY_ROUTINE  "PasswordChangeNotify"

typedef BOOLEAN (*PSAM_INIT_NOTIFICATION_ROUTINE) (
);

#define SAM_INIT_NOTIFICATION_ROUTINE  "InitializeChangeNotify"

#define SAM_PASSWORD_FILTER_ROUTINE  "PasswordFilter"

typedef BOOLEAN (*PSAM_PASSWORD_FILTER_ROUTINE) (
    __in PUNICODE_STRING  AccountName,
    __in PUNICODE_STRING  FullName,
    __in PUNICODE_STRING Password,
    __in BOOLEAN SetOperation
    );


#endif // _PASSWORD_NOTIFICATION_DEFINED

// end_ntsecapi

// begin_ntsecpkg

#ifndef _SAM_CREDENTIAL_UPDATE_DEFINED
#define _SAM_CREDENTIAL_UPDATE_DEFINED

typedef NTSTATUS (*PSAM_CREDENTIAL_UPDATE_NOTIFY_ROUTINE) (
    __in                                   PUNICODE_STRING ClearPassword,
    __in_bcount(OldCredentialSize)         PVOID OldCredentials,
    __in                                   ULONG OldCredentialSize,
    __in                                   ULONG UserAccountControl,
    __in_opt                               PUNICODE_STRING UPN,  
    __in                                   PUNICODE_STRING UserName,
    __in                                   PUNICODE_STRING NetbiosDomainName,
    __in                                   PUNICODE_STRING DnsDomainName,
    __deref_out_bcount(*NewCredentialSize) PVOID * NewCredentials,
    __out                                  ULONG * NewCredentialSize
    );

#define SAM_CREDENTIAL_UPDATE_NOTIFY_ROUTINE "CredentialUpdateNotify"

typedef BOOLEAN (*PSAM_CREDENTIAL_UPDATE_REGISTER_ROUTINE) (
    __out PUNICODE_STRING CredentialName
    );

#define SAM_CREDENTIAL_UPDATE_REGISTER_ROUTINE "CredentialUpdateRegister"

typedef VOID (*PSAM_CREDENTIAL_UPDATE_FREE_ROUTINE) (
    __in PVOID p
    );

#define SAM_CREDENTIAL_UPDATE_FREE_ROUTINE "CredentialUpdateFree"

typedef struct {
    PSTR   Original;
    PSTR   Mapped;
    BOOLEAN Continuable;  // only honored for some operations
} SAM_REGISTER_MAPPING_ELEMENT, *PSAM_REGISTER_MAPPING_ELEMENT;

typedef struct {
                    ULONG                           Count;
    __ecount(Count) PSAM_REGISTER_MAPPING_ELEMENT   Elements;
} SAM_REGISTER_MAPPING_LIST, *PSAM_REGISTER_MAPPING_LIST;

typedef struct {
                    ULONG                          Count;
    __ecount(Count) PSAM_REGISTER_MAPPING_LIST     Lists;
} SAM_REGISTER_MAPPING_TABLE, *PSAM_REGISTER_MAPPING_TABLE;

typedef NTSTATUS (*PSAM_CREDENTIAL_UPDATE_REGISTER_MAPPED_ENTRYPOINTS_ROUTINE) (
    __out SAM_REGISTER_MAPPING_TABLE *Table
    );

#define SAM_CREDENTIAL_UPDATE_REGISTER_MAPPED_ENTRYPOINTS_ROUTINE "RegisterMappedEntrypoints"

#endif // _SAM_CREDENTIAL_UPDATE_DEFINED

// end_ntsecpkg

#undef SIZE_IS
#undef SWITCH_IS
#undef SWITCH_TYPE
#undef CASE
#undef VAR_SIZE_ARRAY
#undef RANGE

#ifdef __cplusplus
}
#endif

#endif // _NTSAM_

