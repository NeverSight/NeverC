//===- SupportProgramTests.cpp - Process helper regressions ---------------===//

#include "csupport/lprogram.h"

#include <gtest/gtest.h>

#include <cerrno>
#include <limits>

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void testAlarmHandler(int) {}

[[noreturn]] void failWithLiveWorker(pid_t Worker, int ExitCode) {
  ::kill(Worker, SIGKILL);
  while (::waitpid(Worker, nullptr, 0) == -1 && errno == EINTR) {
  }
  ::_exit(ExitCode);
}

[[noreturn]] void runWaitSignalStateChecks() {
  struct sigaction Original;
  struct sigaction Custom = {};
  struct sigaction Current;
  Custom.sa_handler = testAlarmHandler;
  sigemptyset(&Custom.sa_mask);
  if (::sigaction(SIGALRM, &Custom, &Original) != 0)
    ::_exit(2);

  const pid_t Worker = ::fork();
  if (Worker < 0)
    ::_exit(3);
  if (Worker == 0) {
    for (;;)
      ::pause();
  }

  int WaitedPid = -1;
  int ReturnCode = -1;
  int Result = csupport_wait_process(
      Worker, /*seconds_to_wait=*/0, /*polling=*/0, &WaitedPid, &ReturnCode,
      nullptr, nullptr, nullptr, nullptr, 0);
  if (Result != 0 || WaitedPid != 0 || ReturnCode != 0)
    failWithLiveWorker(Worker, 4);
  if (::sigaction(SIGALRM, nullptr, &Current) != 0 ||
      Current.sa_handler != testAlarmHandler)
    failWithLiveWorker(Worker, 5);

  Result = csupport_wait_process(
      Worker, /*seconds_to_wait=*/1, /*polling=*/1, &WaitedPid, &ReturnCode,
      nullptr, nullptr, nullptr, nullptr, 0);
  if (Result != 0 || WaitedPid != 0 || ReturnCode != 0)
    failWithLiveWorker(Worker, 6);
  if (::sigaction(SIGALRM, nullptr, &Current) != 0 ||
      Current.sa_handler != testAlarmHandler || ::kill(Worker, 0) != 0)
    failWithLiveWorker(Worker, 7);

  Result = csupport_wait_process(
      Worker, /*seconds_to_wait=*/1, /*polling=*/0, &WaitedPid, &ReturnCode,
      nullptr, nullptr, nullptr, nullptr, 0);
  if (Result != 1 || WaitedPid != Worker || ReturnCode != -2)
    failWithLiveWorker(Worker, 8);
  if (::sigaction(SIGALRM, nullptr, &Current) != 0 ||
      Current.sa_handler != testAlarmHandler)
    failWithLiveWorker(Worker, 9);

  // An immediate wait4 error must not disturb the caller's alarm state either.
  Result = csupport_wait_process(
      Worker, /*seconds_to_wait=*/1, /*polling=*/0, &WaitedPid, &ReturnCode,
      nullptr, nullptr, nullptr, nullptr, 0);
  if (Result != -1 || ReturnCode != -1)
    ::_exit(10);
  if (::sigaction(SIGALRM, nullptr, &Current) != 0 ||
      Current.sa_handler != testAlarmHandler)
    ::_exit(11);

  if (::sigaction(SIGALRM, &Original, nullptr) != 0)
    ::_exit(12);
  ::_exit(0);
}

} // namespace

TEST(SupportProgramTest, WaitingPreservesTheCallersAlarmHandler) {
  const pid_t Child = ::fork();
  ASSERT_GE(Child, 0);
  if (Child == 0)
    runWaitSignalStateChecks();

  int Status = 0;
  ASSERT_EQ(::waitpid(Child, &Status, 0), Child);
  ASSERT_TRUE(WIFEXITED(Status));
  EXPECT_EQ(WEXITSTATUS(Status), 0);
}

TEST(SupportProgramTest, CommandSizeChecksRejectOverflowAndMissingArguments) {
  EXPECT_FALSE(csupport_cmd_args_fit(std::numeric_limits<size_t>::max(),
                                    nullptr, 0));
  EXPECT_FALSE(csupport_cmd_args_fit(0, nullptr, 1));

  const char *MissingArgument[] = {nullptr};
  EXPECT_FALSE(csupport_cmd_args_fit(0, MissingArgument, 1));
}
#endif
