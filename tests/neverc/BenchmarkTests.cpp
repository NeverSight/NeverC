#include "NeverCTestFixture.h"
#include <chrono>
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
  ASSERT_EQ(exec("cc", {serialObj.string(), "-o", serialBin.string()}).exitCode,
            0)
      << "serial link";
  ASSERT_EQ(
      exec("cc", {parallelObj.string(), "-o", parallelBin.string()}).exitCode,
      0)
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

    exec("cc", {serObj.string(), "-o", serBin.string()});
    exec("cc", {parObj.string(), "-o", parBin.string()});

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
  ASSERT_EQ(exec("cc", {obj.string(), "-o", bin.string()}).exitCode, 0);

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

  exec("cc", {serObj.string(), "-o", serBin.string()});
  exec("cc", {noparObj.string(), "-o", noparBin.string()});

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
