//===- SupportSignalTests.cpp - Signal helper regressions -----------------===//

#include "csupport/lsignals.h"
#include "llvm/Config/config.h"

#include <gtest/gtest.h>

#include <string>

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void testSignalHandler(int) {}

#if HAVE_DLFCN_H && HAVE_DLADDR
void appendSignalOutput(void *Context, const char *Data, size_t Length) {
  static_cast<std::string *>(Context)->append(Data, Length);
}
#endif

} // namespace

TEST(SupportSignalTest, RegistrationFailureRollsBackEarlierHandlers) {
  const pid_t Child = ::fork();
  ASSERT_GE(Child, 0);
  if (Child == 0) {
    struct sigaction Before;
    struct sigaction After;
    if (::sigaction(SIGUSR1, nullptr, &Before) != 0)
      ::_exit(2);

    const int Signals[] = {SIGUSR1, -1};
    const int Result = csupport_register_signal_handlers(
        nullptr, 0, Signals, 2, nullptr, 0, 0, testSignalHandler, nullptr);
    if (Result != -1)
      ::_exit(3);
    if (::sigaction(SIGUSR1, nullptr, &After) != 0)
      ::_exit(4);
    if (After.sa_handler != Before.sa_handler)
      ::_exit(5);

    // A failed transaction must not leave a published registration count.
    csupport_unregister_signal_handlers();
    ::_exit(0);
  }

  int Status = 0;
  ASSERT_EQ(::waitpid(Child, &Status, 0), Child);
  ASSERT_TRUE(WIFEXITED(Status));
  EXPECT_EQ(WEXITSTATUS(Status), 0);
}

TEST(SupportSignalTest, RegistrationCanRecoverAfterSignalSideUnregister) {
  const pid_t Child = ::fork();
  ASSERT_GE(Child, 0);
  if (Child == 0) {
    struct sigaction Unregistered;
    struct sigaction Registered;

    csupport_unix_unregister_all_handlers();
    csupport_unix_register_all_handlers();
    csupport_unregister_signal_handlers();
    if (::sigaction(SIGUSR1, nullptr, &Unregistered) != 0)
      ::_exit(2);
    csupport_unix_register_all_handlers();

    if (::sigaction(SIGUSR1, nullptr, &Registered) != 0)
      ::_exit(3);
    csupport_unix_unregister_all_handlers();
    if (Registered.sa_handler == Unregistered.sa_handler)
      ::_exit(4);
    ::_exit(0);
  }

  int Status = 0;
  ASSERT_EQ(::waitpid(Child, &Status, 0), Child);
  ASSERT_TRUE(WIFEXITED(Status));
  EXPECT_EQ(WEXITSTATUS(Status), 0);
}

#if HAVE_DLFCN_H && HAVE_DLADDR
TEST(SupportSignalTest, DladdrStackTraceImplementationIsConfigured) {
  int Marker = 0;
  void *Trace[] = {&Marker};
  std::string Output;

  csupport_print_stack_trace_dladdr(&Output, appendSignalOutput, Trace, 1,
                                    nullptr);

  EXPECT_FALSE(Output.empty());
}
#endif
#endif
