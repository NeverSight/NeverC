//===- SupportCOMTests.cpp - COM initialization regressions ---------------===//
//
// Keep the CSupport COM boundary balanced when initialization fails.  In
// particular, RPC_E_CHANGED_MODE does not acquire an initialization reference
// and therefore must not be paired with CoUninitialize.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/COM.h"

#include <gtest/gtest.h>

#ifdef _WIN32
#include <objbase.h>
#include <thread>
#endif

using namespace llvm;

TEST(SupportCOMTest, FailedModeChangeDoesNotUninitializeCallerState) {
#ifdef _WIN32
  HRESULT InitialResult = E_UNEXPECTED;
  HRESULT ProbeResult = E_UNEXPECTED;

  std::thread Worker([&] {
    InitialResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(InitialResult))
      return;

    {
      sys::InitializeCOMRAII COM(sys::COMThreadingMode::MultiThreaded);
    }

    ProbeResult = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    if (ProbeResult == S_FALSE) {
      ::CoUninitialize();
      ::CoUninitialize();
    } else if (ProbeResult == S_OK) {
      ::CoUninitialize();
    } else {
      ::CoUninitialize();
    }
  });
  Worker.join();

  ASSERT_EQ(InitialResult, S_OK);
  EXPECT_EQ(ProbeResult, S_FALSE);
#else
  GTEST_SKIP() << "COM is only available on Windows";
#endif
}
