
/*=============================================================================

	 Copyright (c) 1998  Microsoft Corporation

	 Module Name:

		 clfsmsg.mc

	 Abstract:

		 Common log file system (CLFS) driver message file.

	 Author:

		 Dexter Bradshaw [DexterB] 17-Dec-1998

	 Environment:

		 Kernel mode

	 Revision History:


=============================================================================*/

//
//  Values are 32 bit values laid out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +---+-+-+-----------------------+-------------------------------+
//  |Sev|C|R|     Facility          |               Code            |
//  +---+-+-+-----------------------+-------------------------------+
//
//  where
//
//      Sev - is the severity code
//
//          00 - Success
//          01 - Informational
//          10 - Warning
//          11 - Error
//
//      C - is the Customer code flag
//
//      R - is a reserved bit
//
//      Facility - is the facility code
//
//      Code - is the facility's status code
//
//
// Define the facility codes
//
#define FACILITY_RPC_STUBS               0x3
#define FACILITY_RPC_RUNTIME             0x2
#define FACILITY_IO_ERROR_CODE           0x4
#define FACILITY_CLFS_ERROR_CODE         0x9


//
// Define the severity codes
//
#define STATUS_SEVERITY_WARNING          0x2
#define STATUS_SEVERITY_SUCCESS          0x0
#define STATUS_SEVERITY_INFORMATIONAL    0x1
#define STATUS_SEVERITY_ERROR            0x3


//
// MessageId: CLFS_MSG_LOGGING_ENABLED
//
// MessageText:
//
// Event logging enabled for Common Log File System.
//
#define CLFS_MSG_LOGGING_ENABLED         ((NTSTATUS)0x400919FAL)

//
// MessageId: CLFS_MSG_DRIVER_STARTING
//
// MessageText:
//
// Common Log File System driver has successfully initialized.
//
#define CLFS_MSG_DRIVER_STARTING         ((NTSTATUS)0x400919FBL)

//
// MessageId: CLFS_MSG_DRIVER_STOPPING
//
// MessageText:
//
// Common Log File System driver has unloaded.
//
#define CLFS_MSG_DRIVER_STOPPING         ((NTSTATUS)0x400919FCL)

//
// MessageId: CLFS_MSG_OPENING_HANDLE
//
// MessageText:
//
// Opening handle to %1.
//
#define CLFS_MSG_OPENING_HANDLE          ((NTSTATUS)0x400919FDL)

//
// MessageId: CLFS_MSG_CLOSING_HANDLE
//
// MessageText:
//
// Closing handle to %1.
//
#define CLFS_MSG_CLOSING_HANDLE          ((NTSTATUS)0x400919FEL)

//
// MessageId: CLFS_MSG_FLUSH_FAILED
//
// MessageText:
//
// %1 log flush failed because of media write error.
//
#define CLFS_MSG_FLUSH_FAILED            ((NTSTATUS)0x400919FFL)

//
// MessageId: CLFS_MSG_METADATA_READ_FAILED
//
// MessageText:
//
// %1 log metadata read failed because of media write error.
//
#define CLFS_MSG_METADATA_READ_FAILED    ((NTSTATUS)0x40091A00L)

//
// MessageId: CLFS_MSG_METADATA_FLUSH_FAILED
//
// MessageText:
//
// %1 log metadata flush failed because of media write error.
//
#define CLFS_MSG_METADATA_FLUSH_FAILED   ((NTSTATUS)0x40091A01L)

//
// MessageId: CLFS_MSG_OWNERPAGE_READ_FAILED
//
// MessageText:
//
// %1 log owner page read failed because of media error.
//
#define CLFS_MSG_OWNERPAGE_READ_FAILED   ((NTSTATUS)0x40091A02L)

//
// MessageId: CLFS_MSG_OWNERPAGE_WRITE_FAILED
//
// MessageText:
//
// %1 log owner page write failed because of media error.
//
#define CLFS_MSG_OWNERPAGE_WRITE_FAILED  ((NTSTATUS)0x40091A03L)

//-----------------------------------------------------------------------------
//                                   End of File
//-----------------------------------------------------------------------------

