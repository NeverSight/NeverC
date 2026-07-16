#include "NeverCTestFixture.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <regex>
#include <sstream>

class BenchmarkTest : public NeverCTest {
protected:
  std::string generateBenchSource(int numFuncs) {
    std::ostringstream os;
    os << "#include <stdio.h>\n#include <stdlib.h>\n\n";
    for (int i = 0; i < numFuncs; ++i) {
      os << "unsigned compute_" << i << "(unsigned x) {\n"
         << "    unsigned acc = x;\n"
         << "    for (unsigned j = 0; j < 100; j++) {\n"
         << "        acc = acc * 31u + j + " << i << "u;\n"
         << "        acc ^= (acc >> 3);\n"
         << "    }\n"
         << "    return acc % 10000u;\n"
         << "}\n\n";
    }
    os << "int main(void) {\n"
       << "    unsigned long total = 0;\n";
    for (int i = 0; i < numFuncs; ++i)
      os << "    total += compute_" << i << "(42);\n";
    os << "    printf(\"total=%lu\\n\", total);\n"
       << "    return 0;\n"
       << "}\n";
    return os.str();
  }
};

TEST_F(BenchmarkTest, ParallelCodegenCorrectness) {
  auto src = tmpFile("bench_parallel.c");
  writeFile(src, generateBenchSource(50));

  auto serialObj = tmpFile("serial.o");
  auto parallelObj = tmpFile("parallel.o");
  auto serialBin = tmpFile("serial_bin");
  auto parallelBin = tmpFile("parallel_bin");

  // Serial compile
  ASSERT_EQ(ncc({"-O2", "-fparallel-codegen=1", "-c", src.string(), "-o",
                 serialObj.string()})
                .exitCode,
            0)
      << "serial compile";

  // Parallel compile
  ASSERT_EQ(ncc({"-O2", "-fparallel-codegen=4", "-c", src.string(), "-o",
                 parallelObj.string()})
                .exitCode,
            0)
      << "parallel compile";

  // Link both
  ASSERT_EQ(ncc({serialObj.string(), "-o", serialBin.string()}).exitCode, 0)
      << "serial link";
  ASSERT_EQ(ncc({parallelObj.string(), "-o", parallelBin.string()}).exitCode, 0)
      << "parallel link";

  // Compare outputs
  auto serialOut = exec(serialBin.string(), {});
  auto parallelOut = exec(parallelBin.string(), {});
  EXPECT_EQ(serialOut.exitCode, 0);
  EXPECT_EQ(parallelOut.exitCode, 0);
  EXPECT_EQ(serialOut.out, parallelOut.out)
      << "serial and parallel outputs differ";
}

TEST_F(BenchmarkTest, ParallelCodegenOptLevels) {
  auto src = tmpFile("bench_optlevels.c");
  writeFile(src, generateBenchSource(20));

  for (auto *opt : {"-O0", "-O1", "-O2", "-O3"}) {
    SCOPED_TRACE(opt);
    auto serObj = tmpFile(std::string("opt_ser_") + opt + ".o");
    auto parObj = tmpFile(std::string("opt_par_") + opt + ".o");
    auto serBin = tmpFile(std::string("opt_ser_") + opt);
    auto parBin = tmpFile(std::string("opt_par_") + opt);

    ASSERT_EQ(ncc({opt, "-fparallel-codegen=1", "-c", src.string(), "-o",
                   serObj.string()})
                  .exitCode,
              0);
    ASSERT_EQ(ncc({opt, "-fparallel-codegen=4", "-c", src.string(), "-o",
                   parObj.string()})
                  .exitCode,
              0);

    ASSERT_EQ(ncc({serObj.string(), "-o", serBin.string()}).exitCode, 0)
        << opt << " serial link";
    ASSERT_EQ(ncc({parObj.string(), "-o", parBin.string()}).exitCode, 0)
        << opt << " parallel link";

    auto serR = exec(serBin.string(), {});
    auto parR = exec(parBin.string(), {});
    EXPECT_EQ(serR.out, parR.out) << opt << " outputs differ";
  }
}

TEST_F(BenchmarkTest, SmallModuleFallback) {
  auto src = tmpFile("small.c");
  writeFile(src, R"(
#include <stdio.h>
int foo(int x) { return x + 1; }
int main(void) { printf("small=%d\n", foo(41)); return 0; }
)");
  auto obj = tmpFile("small.o");
  auto bin = tmpFile("small_bin");

  ASSERT_EQ(ncc({"-O2", "-c", src.string(), "-o", obj.string()}).exitCode, 0);
  ASSERT_EQ(ncc({obj.string(), "-o", bin.string()}).exitCode, 0);

  auto r = exec(bin.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("small=42"));
}

TEST_F(BenchmarkTest, NoParallelCodegenFlag) {
  auto src = tmpFile("nopar.c");
  writeFile(src, generateBenchSource(20));

  auto serObj = tmpFile("nopar_ser.o");
  auto noparObj = tmpFile("nopar_nopar.o");
  auto serBin = tmpFile("nopar_ser_bin");
  auto noparBin = tmpFile("nopar_nopar_bin");

  ASSERT_EQ(ncc({"-O2", "-fparallel-codegen=1", "-c", src.string(), "-o",
                 serObj.string()})
                .exitCode,
            0);
  ASSERT_EQ(ncc({"-O2", "-fno-parallel-codegen", "-c", src.string(), "-o",
                 noparObj.string()})
                .exitCode,
            0);

  ncc({serObj.string(), "-o", serBin.string()});
  ncc({noparObj.string(), "-o", noparBin.string()});

  auto serR = exec(serBin.string(), {});
  auto noparR = exec(noparBin.string(), {});
  EXPECT_EQ(serR.out, noparR.out);
}

TEST_F(BenchmarkTest, PerformanceMeasurement) {
  auto src = tmpFile("bench_perf.c");
  writeFile(src, generateBenchSource(100));

  auto serialObj = tmpFile("perf_serial.o");
  auto parallelObj = tmpFile("perf_parallel.o");

  auto start = std::chrono::high_resolution_clock::now();
  ncc({"-O2", "-fparallel-codegen=1", "-c", src.string(), "-o",
       serialObj.string()});
  auto serialMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::high_resolution_clock::now() - start)
                      .count();

  start = std::chrono::high_resolution_clock::now();
  ncc({"-O2", "-fparallel-codegen=4", "-c", src.string(), "-o",
       parallelObj.string()});
  auto parallelMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - start)
                        .count();

  RecordProperty("serial_ms", static_cast<int>(serialMs));
  RecordProperty("parallel_ms", static_cast<int>(parallelMs));
  SCOPED_TRACE("serial=" + std::to_string(serialMs) +
               "ms  parallel=" + std::to_string(parallelMs) + "ms");

  // Not a hard assertion — just record the measurement
  if (parallelMs < serialMs) {
    RecordProperty("speedup", "parallel_faster");
  }
}

TEST_F(BenchmarkTest, PluginFastPathMedianCompileTime) {
  using Clock = std::chrono::steady_clock;
  using Microseconds = std::chrono::microseconds;
  struct BenchmarkCase {
    const char *Name;
    const char *Plugin;
  };
  const std::array<BenchmarkCase, 3> Cases = {{
      {"no_plugin", nullptr},
      {"empty_plugin", NEVERC_TEST_EMPTY_PLUGIN},
      {"argument_observer", NEVERC_TEST_ARGUMENT_OBSERVER_PLUGIN},
  }};
  constexpr size_t SampleCount = 7;
  const fs::path Source = tmpFile("plugin_fast_path.c");
  writeFile(Source, "int plugin_fast_path(int value) { return value + 1; }\n");

  auto ArgumentsFor = [&](const BenchmarkCase &Current) {
    std::vector<std::string> Arguments = {
        "--no-default-config", "-fsyntax-only", Source.string()};
    if (Current.Plugin != nullptr)
      Arguments.insert(Arguments.begin(),
                       std::string("-fplugin=") + Current.Plugin);
    return Arguments;
  };
  auto Run = [&](const BenchmarkCase &Current) {
    std::vector<std::string> Arguments = ArgumentsFor(Current);
    const auto Start = Clock::now();
    CmdResult Result = ncc(Arguments);
    const auto Elapsed =
        std::chrono::duration_cast<Microseconds>(Clock::now() - Start).count();
    EXPECT_EQ(Result.exitCode, 0) << Current.Name << ": " << Result.err;
    return static_cast<int64_t>(Elapsed);
  };

  for (const BenchmarkCase &Current : Cases)
    (void)Run(Current);

  std::array<std::vector<int64_t>, Cases.size()> Samples;
  for (size_t Round = 0; Round != SampleCount; ++Round)
    for (size_t Slot = 0; Slot != Cases.size(); ++Slot) {
      const size_t CaseIndex = (Round + Slot) % Cases.size();
      Samples[CaseIndex].push_back(Run(Cases[CaseIndex]));
    }

  std::array<int64_t, Cases.size()> Medians{};
  for (size_t Index = 0; Index != Cases.size(); ++Index) {
    for (size_t Sample = 0; Sample != Samples[Index].size(); ++Sample)
      RecordProperty(std::string(Cases[Index].Name) + "_sample_" +
                         std::to_string(Sample) + "_us",
                     static_cast<int>(Samples[Index][Sample]));
    std::sort(Samples[Index].begin(), Samples[Index].end());
    Medians[Index] = Samples[Index][SampleCount / 2];
    RecordProperty(std::string(Cases[Index].Name) + "_median_us",
                   static_cast<int>(Medians[Index]));
  }

  ASSERT_GT(Medians[0], 0);
  RecordProperty(
      "empty_plugin_overhead_basis_points",
      static_cast<int>((Medians[1] - Medians[0]) * 10000 / Medians[0]));
  RecordProperty(
      "argument_observer_overhead_basis_points",
      static_cast<int>((Medians[2] - Medians[0]) * 10000 / Medians[0]));

  if ((isDarwin() || isLinux()) && fs::exists("/usr/bin/time")) {
    constexpr size_t MemorySampleCount = 5;
    std::array<std::vector<uint64_t>, Cases.size()> PeakMemorySamples;
    const std::regex DarwinPeak(
        R"(([0-9]+)[[:space:]]+maximum resident set size)");
    const std::regex LinuxPeak(
        R"(Maximum resident set size \(kbytes\):[[:space:]]*([0-9]+))");
    for (size_t Round = 0; Round != MemorySampleCount; ++Round)
      for (size_t Slot = 0; Slot != Cases.size(); ++Slot) {
        const size_t CaseIndex = (Round + Slot) % Cases.size();
        std::vector<std::string> Arguments;
        Arguments.push_back(isDarwin() ? "-l" : "-v");
        Arguments.push_back(neverc().string());
        std::vector<std::string> CompilerArguments =
            ArgumentsFor(Cases[CaseIndex]);
        Arguments.insert(Arguments.end(), CompilerArguments.begin(),
                         CompilerArguments.end());
        CmdResult Result = exec("/usr/bin/time", Arguments);
        ASSERT_EQ(Result.exitCode, 0)
            << Cases[CaseIndex].Name << ": " << Result.err;
        std::smatch Match;
        ASSERT_TRUE(std::regex_search(
            Result.err, Match, isDarwin() ? DarwinPeak : LinuxPeak))
            << Result.err;
        uint64_t Peak = std::stoull(Match[1].str());
        if (isLinux())
          Peak *= 1024;
        PeakMemorySamples[CaseIndex].push_back(Peak);
      }

    for (size_t Index = 0; Index != Cases.size(); ++Index) {
      for (size_t Sample = 0;
           Sample != PeakMemorySamples[Index].size(); ++Sample)
        RecordProperty(std::string(Cases[Index].Name) + "_sample_" +
                           std::to_string(Sample) + "_peak_bytes",
                       std::to_string(PeakMemorySamples[Index][Sample]));
      std::sort(PeakMemorySamples[Index].begin(),
                PeakMemorySamples[Index].end());
      RecordProperty(
          std::string(Cases[Index].Name) + "_median_peak_bytes",
          std::to_string(PeakMemorySamples[Index][MemorySampleCount / 2]));
    }
  }
}
