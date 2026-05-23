//===-- Windows/ProcessCmdline.inc - Win32 argv + OS version -------------===//
//
// Split from Program.inc: command-line UTF-8 conversion and OS version queries
// used by InitLLVM and Path.inc.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/Windows/WindowsSupport.h"
#include "llvm/Support/WindowsError.h"

#include <cassert>
#include <cstring>
#include <cwchar>
#include <system_error>

namespace llvm {
namespace sys {
namespace windows {

static std::error_code WildcardExpand(StringRef Arg,
                                      SmallVectorImpl<const char *> &Args,
                                      StringSaver &Saver) {
  std::error_code EC;

  if (Arg.find_first_of("*?") == StringRef::npos || Arg == "/?" ||
      Arg == "-?" || Arg.starts_with("\\\\?\\")) {
    Args.push_back(Arg.data());
    return EC;
  }

  SmallVector<wchar_t, 256> ArgW;
  EC = UTF8ToUTF16(Arg, ArgW);
  if (EC)
    return EC;

  WIN32_FIND_DATAW FileData;
  HANDLE FindHandle = FindFirstFileW(ArgW.data(), &FileData);
  if (FindHandle == INVALID_HANDLE_VALUE) {
    Args.push_back(Arg.data());
    return EC;
  }

  SmallString<256> Dir = Arg;
  sys::path::remove_filename(Dir);
  const int DirSize = Dir.size();

  do {
    SmallString<128> FileName;
    EC = UTF16ToUTF8(FileData.cFileName, wcslen(FileData.cFileName), FileName);
    if (EC)
      break;

    sys::path::append(Dir, FileName);
    Args.push_back(Saver.save(Dir.str()).data());
    Dir.resize(DirSize);
  } while (FindNextFileW(FindHandle, &FileData));

  FindClose(FindHandle);
  return EC;
}

static std::error_code GetExecutableName(SmallVectorImpl<char> &Filename) {
  wchar_t ModuleName[MAX_PATH];
  size_t Length = ::GetModuleFileNameW(NULL, ModuleName, MAX_PATH);
  if (Length == 0 || Length == MAX_PATH)
    return mapWindowsError(GetLastError());

  Length = GetLongPathNameW(ModuleName, ModuleName, MAX_PATH);
  if (Length == 0)
    return mapWindowsError(GetLastError());
  if (Length > MAX_PATH)
    return mapWindowsError(ERROR_INSUFFICIENT_BUFFER);

  std::error_code EC = UTF16ToUTF8(ModuleName, Length, Filename);
  if (EC)
    return EC;

  std::string Base = sys::path::filename(Filename.data()).str();
  Filename.assign(Base.begin(), Base.end());
  return std::error_code();
}

std::error_code GetCommandLineArguments(SmallVectorImpl<const char *> &Args,
                                      BumpPtrAllocator &Alloc) {
  const wchar_t *CmdW = GetCommandLineW();
  assert(CmdW);
  std::error_code EC;
  SmallString<256> Cmd;
  EC = UTF16ToUTF8(CmdW, wcslen(CmdW), Cmd);
  if (EC)
    return EC;

  SmallVector<const char *, 32> TmpArgs;
  StringSaver Saver(Alloc);
  cl::TokenizeWindowsCommandLineFull(Cmd, Saver, TmpArgs, /*MarkEOLs=*/false);

  for (const char *Arg : TmpArgs) {
    EC = WildcardExpand(Arg, Args, Saver);
    if (EC)
      return EC;
  }

  if (Args.empty())
    return std::make_error_code(std::errc::invalid_argument);

  SmallString<256> Arg0(Args[0], Args[0] + strlen(Args[0]));
  SmallString<256> Filename;
  sys::path::remove_filename(Arg0);
  EC = GetExecutableName(Filename);
  if (EC)
    return EC;
  sys::path::make_preferred(Arg0);
  sys::path::append(Arg0, Filename);
  Args[0] = Saver.save(Arg0).data();
  return std::error_code();
}

} // namespace windows
} // namespace sys

typedef NTSTATUS(WINAPI *RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

static RTL_OSVERSIONINFOEXW GetWindowsVer() {
  auto getVer = []() -> RTL_OSVERSIONINFOEXW {
    HMODULE hMod = ::GetModuleHandleW(L"ntdll.dll");
    assert(hMod);

    auto Fn = (RtlGetVersionPtr)(void *)::GetProcAddress(hMod, "RtlGetVersion");
    assert(Fn);

    RTL_OSVERSIONINFOEXW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    NTSTATUS r = Fn((PRTL_OSVERSIONINFOW)&info);
    (void)r;
    assert(r == STATUS_SUCCESS);

    return info;
  };
  static RTL_OSVERSIONINFOEXW info = getVer();
  return info;
}

VersionTuple GetWindowsOSVersion() {
  RTL_OSVERSIONINFOEXW info = GetWindowsVer();
  return VersionTuple(info.dwMajorVersion, info.dwMinorVersion, 0,
                      info.dwBuildNumber);
}

bool RunningWindows8OrGreater() {
  return GetWindowsOSVersion() >= VersionTuple(6, 2, 0, 0);
}

bool RunningWindows11OrGreater() {
  RTL_OSVERSIONINFOEXW info = GetWindowsVer();
  auto ver = VersionTuple(info.dwMajorVersion, info.dwMinorVersion, 0,
                          info.dwBuildNumber);

  if (info.wProductType == VER_NT_SERVER)
    return ver >= VersionTuple(10, 0, 0, 20348);

  return ver >= VersionTuple(10, 0, 0, 22000);
}

} // namespace llvm
