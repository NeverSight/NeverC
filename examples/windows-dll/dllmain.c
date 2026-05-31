#include <windows.h>
#include <tlhelp32.h>

#define EXPORT __declspec(dllexport)

EXPORT DWORD nc_get_pid(void) { return GetCurrentProcessId(); }

EXPORT DWORD nc_get_tid(void) { return GetCurrentThreadId(); }

EXPORT BOOL nc_read_memory(HANDLE hProcess, LPCVOID addr, LPVOID buf,
                           SIZE_T size) {
  SIZE_T bytes_read;
  return ReadProcessMemory(hProcess, addr, buf, size, &bytes_read);
}

EXPORT LPVOID nc_alloc_remote(HANDLE hProcess, SIZE_T size, DWORD protect) {
  return VirtualAllocEx(hProcess, NULL, size, MEM_COMMIT | MEM_RESERVE,
                        protect);
}

EXPORT BOOL nc_free_remote(HANDLE hProcess, LPVOID addr) {
  return VirtualFreeEx(hProcess, addr, 0, MEM_RELEASE);
}

EXPORT void nc_xor_buffer(unsigned char *buf, DWORD len, unsigned char key) {
  for (DWORD i = 0; i < len; ++i)
    buf[i] ^= key;
}

EXPORT HANDLE nc_open_process(DWORD pid, DWORD access) {
  return OpenProcess(access, FALSE, pid);
}

EXPORT BOOL nc_enum_modules(DWORD pid, char *out_buf, DWORD buf_size) {
  HANDLE snap =
      CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
  if (snap == INVALID_HANDLE_VALUE)
    return FALSE;

  MODULEENTRY32 me;
  me.dwSize = sizeof(me);
  DWORD offset = 0;
  if (Module32First(snap, &me)) {
    do {
      int len = lstrlenA(me.szModule);
      if (offset + len + 1 >= buf_size)
        break;
      lstrcpyA(out_buf + offset, me.szModule);
      offset += len;
      out_buf[offset++] = '\n';
    } while (Module32Next(snap, &me));
  }
  out_buf[offset] = '\0';
  CloseHandle(snap);
  return TRUE;
}

BOOL __stdcall DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
  (void)hinstDLL;
  (void)lpvReserved;
  (void)fdwReason;
  return TRUE;
}
