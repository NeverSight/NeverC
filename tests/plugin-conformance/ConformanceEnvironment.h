//===- ConformanceEnvironment.h - plugin conformance harness ------------===//
//
// Shared harness for the standalone NeverC plugin conformance suite. It builds
// fixture plugins with the *system* C compiler against the staged/installed
// public SDK and drives the compiler-under-test through its public CLI only.
// It never includes host-private headers, so a green run proves the shipped ABI
// and SDK are self-sufficient for an independent toolchain.
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_PLUGIN_CONFORMANCE_ENVIRONMENT_H
#define NEVERC_PLUGIN_CONFORMANCE_ENVIRONMENT_H

#include <string>
#include <utility>
#include <vector>

namespace neverc::conformance {

struct RunResult {
  int exitCode = -1;
  std::string out;
  std::string err;
  bool outContains(const std::string &Needle) const;
  bool errContains(const std::string &Needle) const;
};

using EnvVars = std::vector<std::pair<std::string, std::string>>;

/// Process-wide configuration resolved from CMake compile definitions and, when
/// present, environment overrides (NEVERC_UNDER_TEST, NEVERC_PLUGIN_SDK_ROOT).
class Environment {
public:
  static const Environment &get();

  const std::string &neverc() const { return Neverc; }
  const std::string &cc() const { return CC; }
  const std::string &sdkInclude() const { return SDKInclude; }
  const std::string &fixturesDir() const { return FixturesDir; }
  bool usable() const { return !Neverc.empty() && !CC.empty() &&
                               !SDKInclude.empty() && !FixturesDir.empty(); }
  std::string whyUnusable() const;

  /// A fresh, empty, unique temporary directory for one test.
  std::string makeTempDir(const std::string &Label) const;

  static std::string pluginExtension();

  /// Build a fixture plugin (<fixture>.c) into a shared library using the system
  /// C compiler and the staged SDK include path. \p defines are passed as -D
  /// tokens (e.g. "NCF_WRONG_MAJOR" or "NCF_ID=\"com.x\""). Returns the plugin
  /// path, or "" on failure with the compiler output in \p error.
  std::string buildPlugin(const std::string &Dir, const std::string &Fixture,
                          const std::vector<std::string> &Defines,
                          std::string &Error) const;

  /// Run the compiler-under-test with \p args (argv[0] is supplied) and optional
  /// extra environment variables.
  RunResult runNeverc(const std::vector<std::string> &Args,
                      const EnvVars &Extra = {}) const;

  /// Run an arbitrary program, capturing stdout/stderr and the exit code.
  RunResult runProgram(const std::vector<std::string> &Args,
                       const EnvVars &Extra = {}) const;

private:
  Environment();
  std::string Neverc;
  std::string CC;
  std::string SDKInclude;
  std::string FixturesDir;
};

} // namespace neverc::conformance

#endif
