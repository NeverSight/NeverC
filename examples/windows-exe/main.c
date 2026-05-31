#include <windows.h>
#include <tlhelp32.h>
#include <string.h>

int printf(const char *, ...);

int main(void) {
  printf("NeverC Windows Ring3 EXE\n");
  printf("========================\n");

  SYSTEM_INFO si;
  GetSystemInfo(&si);
  printf("\nSystem Info:\n");
  printf("  Processors:    %lu\n", si.dwNumberOfProcessors);
  printf("  Page Size:     %lu\n", si.dwPageSize);
  printf("  Arch:          %u\n", si.wProcessorArchitecture);
  printf("  Alloc Gran:    %lu\n", si.dwAllocationGranularity);

  printf("\nProcess Info:\n");
  printf("  PID:           %lu\n", GetCurrentProcessId());
  printf("  TID:           %lu\n", GetCurrentThreadId());

  char path[MAX_PATH];
  GetModuleFileNameA(NULL, path, MAX_PATH);
  printf("  Path:          %s\n", path);

  MEMORYSTATUSEX mem;
  mem.dwLength = sizeof(mem);
  if (GlobalMemoryStatusEx(&mem)) {
    printf("  Total Phys:    %llu MB\n", mem.ullTotalPhys / (1024 * 1024));
    printf("  Memory Load:   %lu%%\n", mem.dwMemoryLoad);
  }

  printf("\nRunning Processes (first 10):\n");
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    int count = 0;
    if (Process32First(snap, &pe)) {
      do {
        printf("  [%5lu] %s\n", pe.th32ProcessID, pe.szExeFile);
        if (++count >= 10)
          break;
      } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
  }

  printf("\nVirtualAlloc Demo:\n");
  LPVOID p = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE,
                          PAGE_READWRITE);
  if (p) {
    memset(p, 0xCC, 4096);
    printf("  Allocated 4096 bytes at %p\n", p);

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(p, &mbi, sizeof(mbi))) {
      printf("  Base:    %p\n", mbi.BaseAddress);
      printf("  Size:    %llu\n", (ULONGLONG)mbi.RegionSize);
      printf("  Protect: 0x%lX\n", mbi.Protect);
    }
    VirtualFree(p, 0, MEM_RELEASE);
    printf("  Freed.\n");
  }

  printf("\nDone.\n");
  return 0;
}
