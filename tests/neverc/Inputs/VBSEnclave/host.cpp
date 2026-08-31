#include <windows.h>

#include <cstdio>
#include <cwchar>

namespace {

constexpr SIZE_T kEnclaveSize = 0x20000000ULL;

void Report(const wchar_t *stage, const wchar_t *status, DWORD error) {
  std::wprintf(L"VBS_STAGE=%ls STATUS=%ls ERROR=%lu\n", stage, status,
               static_cast<unsigned long>(error));
  std::fflush(stdout);
}

int Fail(const wchar_t *stage, DWORD error, PVOID enclave) {
  Report(stage, L"FAIL", error);
  if (enclave != nullptr) {
    TerminateEnclave(enclave, TRUE);
    DeleteEnclave(enclave);
  }
  return 1;
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc != 2) {
    std::fwprintf(stderr, L"usage: vbs-enclave-host.exe <enclave.dll>\n");
    return 2;
  }

  SetLastError(ERROR_SUCCESS);
  if (!IsEnclaveTypeSupported(ENCLAVE_TYPE_VBS))
    return Fail(L"IsEnclaveTypeSupported", ERROR_NOT_SUPPORTED, nullptr);
  Report(L"IsEnclaveTypeSupported", L"PASS", ERROR_SUCCESS);

  constexpr ENCLAVE_CREATE_INFO_VBS createInfo = {
      ENCLAVE_VBS_FLAG_DEBUG,
      {0x10, 0x20, 0x30, 0x40, 0x41, 0x31, 0x21, 0x11},
  };
  SetLastError(ERROR_SUCCESS);
  PVOID enclave =
      CreateEnclave(GetCurrentProcess(), nullptr, kEnclaveSize, 0,
                    ENCLAVE_TYPE_VBS, &createInfo, sizeof(createInfo), nullptr);
  if (enclave == nullptr)
    return Fail(L"CreateEnclave", GetLastError(), nullptr);
  Report(L"CreateEnclave", L"PASS", ERROR_SUCCESS);

  const DWORD previousMode = GetThreadErrorMode();
  SetThreadErrorMode(previousMode | SEM_FAILCRITICALERRORS, nullptr);
  SetLastError(ERROR_SUCCESS);
  const BOOL loaded = LoadEnclaveImageW(enclave, argv[1]);
  const DWORD loadError = loaded ? ERROR_SUCCESS : GetLastError();
  SetThreadErrorMode(previousMode, nullptr);
  if (!loaded)
    return Fail(L"LoadEnclaveImage", loadError, enclave);
  Report(L"LoadEnclaveImage", L"PASS", ERROR_SUCCESS);

  ENCLAVE_INIT_INFO_VBS initInfo = {};
  initInfo.Length = sizeof(initInfo);
  initInfo.ThreadCount = 1;
  SetLastError(ERROR_SUCCESS);
  if (!InitializeEnclave(GetCurrentProcess(), enclave, &initInfo,
                         initInfo.Length, nullptr))
    return Fail(L"InitializeEnclave", GetLastError(), enclave);
  Report(L"InitializeEnclave", L"PASS", ERROR_SUCCESS);

  Report(L"Complete", L"PASS", ERROR_SUCCESS);
  if (!TerminateEnclave(enclave, TRUE))
    return Fail(L"TerminateEnclave", GetLastError(), enclave);
  if (!DeleteEnclave(enclave))
    return Fail(L"DeleteEnclave", GetLastError(), nullptr);
  return 0;
}
