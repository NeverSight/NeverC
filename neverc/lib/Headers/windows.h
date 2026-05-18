/*===---- windows.h - NeverC shellcode-oriented Win32 shim ----------------===*\
|*
|* This header is placed in NeverC's resource include directory, which appears
|* before the bundled MSVC SDK include paths.  In normal compilation modes we
|* forward to the next windows.h in the include chain.  In shellcode mode
|* (`__NEVERC_SHELLCODE__`) we expose a compact, dependency-free subset that
|* matches the WinPEBImport whitelist and avoids pulling in the full Windows
|* SDK header graph.
|*
|* Goal: let users keep writing ordinary `#include <windows.h>` code in
|* shellcode mode without carrying thousands of SDK declarations, parser-heavy
|* macros, or unrelated subsystem baggage.
|*
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_WINDOWS_SHIM_H_
#define _NEVERC_WINDOWS_SHIM_H_

#if defined(__NEVERC_SHELLCODE_KERNEL__)
#error                                                                         \
    "<windows.h> is a user-mode header and cannot be used in -mshellcode-context=kernel builds. Include <neverc/kernel.h> (or a driver-specific header) instead; ring-0 Windows payloads reach kernel APIs through the KernelImportPass resolver shim, not the Win32 user-mode surface."
#endif

#if !defined(__NEVERC_SHELLCODE__)
#include_next <windows.h>
#else

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Calling convention marker.  On Win64 (x86_64) and ARM64 Windows the
 * Microsoft ABI uses a single calling convention for all user-mode APIs,
 * so WINAPI/APIENTRY/CALLBACK/WINBASEAPI are semantically no-ops.
 * Defining them keeps hand-authored forward declarations that copy the
 * real SDK signatures compilable unchanged. */
#ifndef WINAPI
#define WINAPI
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef WINBASEAPI
#define WINBASEAPI
#endif
#ifndef WINUSERAPI
#define WINUSERAPI
#endif

/* Microsoft SAL annotation macros reduce to nothing — they exist only so
 * copy-pasted SDK signatures compile unmodified. */
#ifndef _In_
#define _In_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Inout_
#define _Inout_
#endif
#ifndef _Inout_opt_
#define _Inout_opt_
#endif

/* Basic Win32 scalar and pointer aliases used by common shellcode payloads. */
typedef int BOOL;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef uint64_t DWORD64;
typedef uint64_t ULONGLONG;
typedef int32_t LONG;
typedef int64_t LONGLONG;
typedef uint32_t UINT;
typedef uint16_t USHORT;
typedef uint8_t UCHAR;
typedef int64_t LONG_PTR;
typedef uint64_t ULONG_PTR;
typedef size_t SIZE_T;
typedef void *PVOID;
typedef const void *LPCVOID;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef wchar_t WCHAR;
typedef wchar_t *LPWSTR;
typedef const wchar_t *LPCWSTR;

typedef void *HANDLE;
typedef HANDLE HMODULE;
typedef HANDLE HINSTANCE;
typedef HANDLE HGLOBAL;
typedef HANDLE HLOCAL;
typedef HANDLE HRSRC;
typedef HANDLE HFILE;
/* user32-style handles.  All of them are plain opaque pointers in the
 * Windows ABI, so aliasing them to HANDLE keeps call sites type-correct
 * without pulling in the full SDK window-manager header graph. */
typedef HANDLE HWND;
typedef HANDLE HICON;
typedef HANDLE HCURSOR;
typedef HANDLE HBRUSH;
typedef HANDLE HMENU;
typedef HANDLE HDC;

typedef DWORD *LPDWORD;
typedef BYTE *LPBYTE;
typedef void *LPVOID;

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Common constants used by shellcode-style payloads. */
#define STD_INPUT_HANDLE ((DWORD) - 10)
#define STD_OUTPUT_HANDLE ((DWORD) - 11)
#define STD_ERROR_HANDLE ((DWORD) - 12)

#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000

#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_READ 0x20
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_GUARD 0x100

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define FILE_SHARE_DELETE 0x00000004u

#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u

#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_ATTRIBUTE_HIDDEN 0x00000002u

#ifndef INFINITE
#define INFINITE 0xFFFFFFFFu
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR) - 1)
#endif

#define WAIT_OBJECT_0 0x00000000u
#define WAIT_TIMEOUT 0x00000102u
#define WAIT_FAILED 0xFFFFFFFFu

/* Process / thread creation flags. */
#define CREATE_SUSPENDED 0x00000004u
#define CREATE_NEW_CONSOLE 0x00000010u
#define DETACHED_PROCESS 0x00000008u

/* Minimal structs frequently needed by WriteFile/CreateProcess style code. */
typedef struct _SECURITY_ATTRIBUTES {
  DWORD nLength;
  LPVOID lpSecurityDescriptor;
  BOOL bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;

typedef struct _OVERLAPPED {
  ULONG_PTR Internal;
  ULONG_PTR InternalHigh;
  union {
    struct {
      DWORD Offset;
      DWORD OffsetHigh;
    };
    PVOID Pointer;
  };
  HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

/* Win32 APIs supported by WinPEBImportPass's resolver whitelist. */
HMODULE LoadLibraryA(LPCSTR lpLibFileName);
LPVOID GetProcAddress(HMODULE hModule, LPCSTR lpProcName);
HMODULE GetModuleHandleA(LPCSTR lpModuleName);
BOOL FreeLibrary(HMODULE hLibModule);

LPVOID VirtualAlloc(LPVOID lpAddress, size_t dwSize, DWORD flAllocationType,
                    DWORD flProtect);
BOOL VirtualFree(LPVOID lpAddress, size_t dwSize, DWORD dwFreeType);
BOOL VirtualProtect(LPVOID lpAddress, size_t dwSize, DWORD flNewProtect,
                    LPDWORD lpflOldProtect);

BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
               LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped);
BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
              LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped);
HANDLE CreateFileA(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                   LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                   DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                   HANDLE hTemplateFile);
BOOL CloseHandle(HANDLE hObject);

HANDLE GetStdHandle(DWORD nStdHandle);
BOOL SetStdHandle(DWORD nStdHandle, HANDLE hHandle);
void ExitProcess(UINT uExitCode);
DWORD GetLastError(void);

BOOL CreateProcessA(LPCSTR lpApplicationName, LPSTR lpCommandLine,
                    LPSECURITY_ATTRIBUTES lpProcessAttributes,
                    LPSECURITY_ATTRIBUTES lpThreadAttributes,
                    BOOL bInheritHandles, DWORD dwCreationFlags,
                    LPVOID lpEnvironment, LPCSTR lpCurrentDirectory,
                    LPVOID lpStartupInfo, LPVOID lpProcessInformation);
DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);

void Sleep(DWORD dwMilliseconds);
DWORD GetTickCount(void);
HANDLE GetCurrentProcess(void);
HANDLE GetCurrentThread(void);

/* Additional APIs routed through WinPEBImportPass.  Declarations cover
 * every entry in `WinImportTables.cpp` so user code can keep the
 * standard `#include <windows.h>` style without hand-rolled externs.
 * Signatures match the public MSVC SDK; SAL annotations reduce to
 * nothing via the macros above. */
typedef DWORD(WINAPI *LPTHREAD_START_ROUTINE)(LPVOID lpThreadParameter);

/* Thread / process helpers. */
HANDLE CreateThread(LPSECURITY_ATTRIBUTES lpThreadAttributes,
                    SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress,
                    LPVOID lpParameter, DWORD dwCreationFlags,
                    LPDWORD lpThreadId);
HANDLE CreateRemoteThread(HANDLE hProcess, LPSECURITY_ATTRIBUTES lpThreadAttrs,
                          SIZE_T dwStackSize,
                          LPTHREAD_START_ROUTINE lpStartAddress,
                          LPVOID lpParameter, DWORD dwCreationFlags,
                          LPDWORD lpThreadId);
void ExitThread(DWORD dwExitCode);
BOOL TerminateThread(HANDLE hThread, DWORD dwExitCode);
DWORD ResumeThread(HANDLE hThread);
DWORD SuspendThread(HANDLE hThread);
DWORD GetCurrentProcessId(void);
DWORD GetCurrentThreadId(void);
HANDLE OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle,
                   DWORD dwProcessId);
HANDLE OpenThread(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId);
BOOL TerminateProcess(HANDLE hProcess, UINT uExitCode);
DWORD GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize);
DWORD GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
HMODULE GetModuleHandleW(LPCWSTR lpModuleName);
HMODULE LoadLibraryW(LPCWSTR lpLibFileName);
HMODULE LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
HMODULE LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
void SetLastError(DWORD dwErrCode);

/* Heap / allocation helpers. */
LPVOID HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes);
BOOL HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);
HANDLE HeapCreate(DWORD flOptions, SIZE_T dwInitialSize, SIZE_T dwMaximumSize);
BOOL HeapDestroy(HANDLE hHeap);
HANDLE GetProcessHeap(void);
HGLOBAL GlobalAlloc(UINT uFlags, SIZE_T dwBytes);
HGLOBAL GlobalFree(HGLOBAL hMem);
HLOCAL LocalAlloc(UINT uFlags, SIZE_T uBytes);
HLOCAL LocalFree(HLOCAL hMem);

/* Remote memory. */
BOOL WriteProcessMemory(HANDLE hProcess, LPVOID lpBaseAddress, LPCVOID lpBuffer,
                        SIZE_T nSize, SIZE_T *lpNumberOfBytesWritten);
BOOL ReadProcessMemory(HANDLE hProcess, LPCVOID lpBaseAddress, LPVOID lpBuffer,
                       SIZE_T nSize, SIZE_T *lpNumberOfBytesRead);
LPVOID VirtualAllocEx(HANDLE hProcess, LPVOID lpAddress, SIZE_T dwSize,
                      DWORD flAllocationType, DWORD flProtect);
BOOL VirtualFreeEx(HANDLE hProcess, LPVOID lpAddress, SIZE_T dwSize,
                   DWORD dwFreeType);
BOOL VirtualProtectEx(HANDLE hProcess, LPVOID lpAddress, SIZE_T dwSize,
                      DWORD flNewProtect, LPDWORD lpflOldProtect);
SIZE_T VirtualQuery(LPCVOID lpAddress, LPVOID lpBuffer, SIZE_T dwLength);

/* Timing. */
ULONGLONG GetTickCount64(void);
BOOL QueryPerformanceCounter(LONGLONG *lpPerformanceCount);
BOOL QueryPerformanceFrequency(LONGLONG *lpFrequency);
DWORD SleepEx(DWORD dwMilliseconds, BOOL bAlertable);

/* Synchronisation. */
HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES lpMutexAttrs, BOOL bInitialOwner,
                    LPCSTR lpName);
HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES lpMutexAttrs, BOOL bInitialOwner,
                    LPCWSTR lpName);
BOOL ReleaseMutex(HANDLE hMutex);
HANDLE CreateEventA(LPSECURITY_ATTRIBUTES lpEventAttrs, BOOL bManualReset,
                    BOOL bInitialState, LPCSTR lpName);
HANDLE CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttrs, BOOL bManualReset,
                    BOOL bInitialState, LPCWSTR lpName);
BOOL SetEvent(HANDLE hEvent);
BOOL ResetEvent(HANDLE hEvent);
DWORD WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles,
                             BOOL bWaitAll, DWORD dwMilliseconds);

/* Console / stdio. */
BOOL WriteConsoleA(HANDLE hConsoleOutput, const void *lpBuffer,
                   DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten,
                   LPVOID lpReserved);
BOOL WriteConsoleW(HANDLE hConsoleOutput, const void *lpBuffer,
                   DWORD nNumberOfCharsToWrite, LPDWORD lpNumberOfCharsWritten,
                   LPVOID lpReserved);
BOOL ReadConsoleA(HANDLE hConsoleInput, LPVOID lpBuffer,
                  DWORD nNumberOfCharsToRead, LPDWORD lpNumberOfCharsRead,
                  LPVOID pInputControl);
BOOL ReadConsoleW(HANDLE hConsoleInput, LPVOID lpBuffer,
                  DWORD nNumberOfCharsToRead, LPDWORD lpNumberOfCharsRead,
                  LPVOID pInputControl);
BOOL AllocConsole(void);
BOOL FreeConsole(void);

/* File / directory surface. */
HANDLE CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                   LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                   DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                   HANDLE hTemplateFile);
BOOL DeleteFileA(LPCSTR lpFileName);
BOOL DeleteFileW(LPCWSTR lpFileName);
DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove,
                     LONG *lpDistanceToMoveHigh, DWORD dwMoveMethod);
BOOL SetFilePointerEx(HANDLE hFile, LONGLONG liDistanceToMove,
                      LONGLONG *lpNewFilePointer, DWORD dwMoveMethod);
DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh);
BOOL GetFileSizeEx(HANDLE hFile, LONGLONG *lpFileSize);
BOOL FlushFileBuffers(HANDLE hFile);
BOOL CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName,
               BOOL bFailIfExists);
BOOL CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName,
               BOOL bFailIfExists);
BOOL MoveFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName);
BOOL MoveFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName);
HANDLE FindFirstFileA(LPCSTR lpFileName, LPVOID lpFindFileData);
HANDLE FindFirstFileW(LPCWSTR lpFileName, LPVOID lpFindFileData);
BOOL FindNextFileA(HANDLE hFindFile, LPVOID lpFindFileData);
BOOL FindNextFileW(HANDLE hFindFile, LPVOID lpFindFileData);
BOOL FindClose(HANDLE hFindFile);
BOOL CreateDirectoryA(LPCSTR lpPathName,
                      LPSECURITY_ATTRIBUTES lpSecurityAttributes);
BOOL CreateDirectoryW(LPCWSTR lpPathName,
                      LPSECURITY_ATTRIBUTES lpSecurityAttributes);
BOOL RemoveDirectoryA(LPCSTR lpPathName);
BOOL RemoveDirectoryW(LPCWSTR lpPathName);
BOOL SetCurrentDirectoryA(LPCSTR lpPathName);
BOOL SetCurrentDirectoryW(LPCWSTR lpPathName);
DWORD GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer);
DWORD GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer);
DWORD GetSystemDirectoryA(LPSTR lpBuffer, UINT uSize);
DWORD GetSystemDirectoryW(LPWSTR lpBuffer, UINT uSize);
DWORD GetWindowsDirectoryA(LPSTR lpBuffer, UINT uSize);
DWORD GetWindowsDirectoryW(LPWSTR lpBuffer, UINT uSize);
DWORD GetTempPathA(DWORD nBufferLength, LPSTR lpBuffer);
DWORD GetTempPathW(DWORD nBufferLength, LPWSTR lpBuffer);

/* Environment. */
DWORD GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize);
DWORD GetEnvironmentVariableW(LPCWSTR lpName, LPWSTR lpBuffer, DWORD nSize);
BOOL SetEnvironmentVariableA(LPCSTR lpName, LPCSTR lpValue);
BOOL SetEnvironmentVariableW(LPCWSTR lpName, LPCWSTR lpValue);
LPSTR GetCommandLineA(void);
LPWSTR GetCommandLineW(void);

/* Debug / misc. */
void OutputDebugStringA(LPCSTR lpOutputString);
void OutputDebugStringW(LPCWSTR lpOutputString);
BOOL IsDebuggerPresent(void);
void DebugBreak(void);
BOOL DuplicateHandle(HANDLE hSourceProcessHandle, HANDLE hSourceHandle,
                     HANDLE hTargetProcessHandle, HANDLE *lpTargetHandle,
                     DWORD dwDesiredAccess, BOOL bInheritHandle,
                     DWORD dwOptions);
BOOL FlushInstructionCache(HANDLE hProcess, LPCVOID lpBaseAddress,
                           SIZE_T dwSize);

/* Code page conversion. */
int MultiByteToWideChar(UINT CodePage, DWORD dwFlags, LPCSTR lpMultiByteStr,
                        int cbMultiByte, LPWSTR lpWideCharStr, int cchWideChar);
int WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWSTR lpWideCharStr,
                        int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte,
                        LPCSTR lpDefaultChar, BOOL *lpUsedDefaultChar);

/* Pipes. */
BOOL CreatePipe(HANDLE *hReadPipe, HANDLE *hWritePipe,
                LPSECURITY_ATTRIBUTES lpPipeAttributes, DWORD nSize);
BOOL PeekNamedPipe(HANDLE hNamedPipe, LPVOID lpBuffer, DWORD nBufferSize,
                   LPDWORD lpBytesRead, LPDWORD lpTotalBytesAvail,
                   LPDWORD lpBytesLeftThisMessage);
BOOL ConnectNamedPipe(HANDLE hNamedPipe, LPOVERLAPPED lpOverlapped);

/* ntdll.dll */
typedef LONG NTSTATUS;
typedef struct _RTL_OSVERSIONINFOW {
  ULONG_PTR dwOSVersionInfoSize;
  ULONG_PTR dwMajorVersion;
  ULONG_PTR dwMinorVersion;
  ULONG_PTR dwBuildNumber;
  ULONG_PTR dwPlatformId;
  WCHAR szCSDVersion[128];
} RTL_OSVERSIONINFOW, *PRTL_OSVERSIONINFOW;
NTSTATUS RtlGetVersion(PRTL_OSVERSIONINFOW lpVersionInformation);
NTSTATUS NtAllocateVirtualMemory(HANDLE ProcessHandle, LPVOID *BaseAddress,
                                 ULONG_PTR ZeroBits, SIZE_T *RegionSize,
                                 DWORD AllocationType, DWORD Protect);
NTSTATUS NtProtectVirtualMemory(HANDLE ProcessHandle, LPVOID *BaseAddress,
                                SIZE_T *RegionSize, DWORD NewProtect,
                                DWORD *OldProtect);
NTSTATUS NtFreeVirtualMemory(HANDLE ProcessHandle, LPVOID *BaseAddress,
                             SIZE_T *RegionSize, DWORD FreeType);
NTSTATUS NtClose(HANDLE Handle);
NTSTATUS NtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus);
NTSTATUS NtDelayExecution(BOOL Alertable, LONGLONG *DelayInterval);

/* user32.dll */
int MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);
int MessageBoxW(HWND hWnd, LPCWSTR lpText, LPCWSTR lpCaption, UINT uType);
HWND FindWindowA(LPCSTR lpClassName, LPCSTR lpWindowName);
HWND FindWindowW(LPCWSTR lpClassName, LPCWSTR lpWindowName);
int GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount);
int GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount);
BOOL SetWindowTextA(HWND hWnd, LPCSTR lpString);
BOOL SetWindowTextW(HWND hWnd, LPCWSTR lpString);
HWND GetForegroundWindow(void);
BOOL SetForegroundWindow(HWND hWnd);
int GetSystemMetrics(int nIndex);
typedef struct tagPOINT {
  LONG x;
  LONG y;
} POINT, *PPOINT, *LPPOINT;
BOOL GetCursorPos(LPPOINT lpPoint);
BOOL SetCursorPos(int X, int Y);

/* advapi32.dll */
BOOL OpenProcessToken(HANDLE ProcessHandle, DWORD DesiredAccess,
                      HANDLE *TokenHandle);
BOOL AdjustTokenPrivileges(HANDLE TokenHandle, BOOL DisableAllPrivileges,
                           LPVOID NewState, DWORD BufferLength,
                           LPVOID PreviousState, DWORD *ReturnLength);
BOOL CryptAcquireContextA(ULONG_PTR *phProv, LPCSTR szContainer,
                          LPCSTR szProvider, DWORD dwProvType, DWORD dwFlags);
BOOL CryptGenRandom(ULONG_PTR hProv, DWORD dwLen, BYTE *pbBuffer);
BOOL CryptReleaseContext(ULONG_PTR hProv, DWORD dwFlags);

/* ws2_32.dll (minimal — users include <winsock2.h> manually for the rest). */
int WSAStartup(WORD wVersionRequested, LPVOID lpWSAData);
int WSACleanup(void);
int WSAGetLastError(void);

/* shell32.dll */
HANDLE ShellExecuteA(HANDLE hwnd, LPCSTR lpOperation, LPCSTR lpFile,
                     LPCSTR lpParameters, LPCSTR lpDirectory, int nShowCmd);
HANDLE ShellExecuteW(HANDLE hwnd, LPCWSTR lpOperation, LPCWSTR lpFile,
                     LPCWSTR lpParameters, LPCWSTR lpDirectory, int nShowCmd);

#ifdef __cplusplus
}
#endif

#endif /* __NEVERC_SHELLCODE__ */
#endif /* _NEVERC_WINDOWS_SHIM_H_ */
