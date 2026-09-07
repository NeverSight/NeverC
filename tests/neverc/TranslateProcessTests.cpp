#include "../../neverc/lib/Translate/FrontendProcess.h"
#include "NeverCTestFixture.h"
#include <chrono>
#include <cstdlib>

#ifndef _WIN32
#include <cerrno>
#include <sys/wait.h>
#endif

namespace {
class TranslateProcessTest : public NeverCTest {};

TEST_F(TranslateProcessTest, PreservesExitStatusAndCapturesOutput) {
#ifdef _WIN32
  GTEST_SKIP() << "POSIX shell fixture; Windows uses the same runner contract.";
#else
  neverc::translate::TranslationCancellation Cancellation;
  const auto Out = tmpFile("stdout"), Err = tmpFile("stderr");
  auto Result = neverc::translate::runProcess(
      {"/bin/sh", "-c", "printf 'output'; printf 'error' >&2; exit 7"},
      Out.string(), Err.string(), 5);
  EXPECT_EQ(Result.ExitCode, 7);
  EXPECT_FALSE(Result.Cancelled);
  EXPECT_EQ(readFile(Out), "output");
  EXPECT_EQ(readFile(Err), "error");
#endif
}

TEST_F(TranslateProcessTest, TimeoutReturnsPromptlyAndReapsOrdinaryChild) {
#ifdef _WIN32
  GTEST_SKIP() << "POSIX shell fixture; Windows uses the same runner contract.";
#else
  neverc::translate::TranslationCancellation Cancellation;
  const auto Out = tmpFile("stdout"), Err = tmpFile("stderr");
  const auto Start = std::chrono::steady_clock::now();
  auto Result = neverc::translate::runProcess(
      {"/bin/sh", "-c", "trap '' TERM; printf '%s' $$; exec /bin/sleep 300"},
      Out.string(), Err.string(), 1);
  const auto Elapsed = std::chrono::steady_clock::now() - Start;
  EXPECT_EQ(Result.ExitCode, -2);
  EXPECT_FALSE(Result.Cancelled);
  EXPECT_NE(Result.Error.find("process timed out"), std::string::npos);
  EXPECT_LT(Elapsed, std::chrono::seconds(5));
  const auto PID =
      static_cast<pid_t>(std::strtol(readFile(Out).c_str(), nullptr, 10));
  ASSERT_GT(PID, 0);
  int Status = 0;
  errno = 0;
  EXPECT_EQ(waitpid(PID, &Status, WNOHANG), -1);
  EXPECT_EQ(errno, ECHILD) << "normal timeout cleanup must not leave a zombie";
#endif
}
} // namespace
