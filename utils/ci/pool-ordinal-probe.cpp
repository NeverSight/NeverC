// Test the actual Pool header, including its caller and worker dispatch paths.
#include "FastLinkSupport.h"
#include <array>
#include <iostream>
#include <stdexcept>

struct Counts {
  std::atomic<unsigned> Visits{0}, Wrong{0}, Bounds{0}, Restores{0}, Errors{0};
  void check(unsigned Expected, unsigned Size) {
    ++Visits;
    if (fl::Pool::self() != Expected)
      ++Wrong;
    if (fl::Pool::self() >= Size)
      ++Bounds;
  }
  void restore(unsigned Expected) {
    if (fl::Pool::self() != Expected)
      ++Restores;
  }
};

int main(int argc, char **argv) {
  const bool ExpectBroken = argc == 2 && std::string(argv[1]) == "--expect-original";
  const char *Names[] = {"serial-run", "serial-for-each", "small-for-each", "parallel-run",
                         "parallel-for-each", "serial-throw", "small-throw", "parallel-throw",
                         "zero-for-each"};
  std::array<Counts, 9> Results;
  fl::Pool Outer(8);
  Outer.run([&](unsigned Worker) {
    if (Worker != 7)
      return;
    for (unsigned Round = 0; Round != 8; ++Round) {
      {
        fl::Pool Inner(1);
        Inner.run([&](unsigned Index) { Results[0].check(Index, Inner.size()); });
        Results[0].restore(Worker);
        Inner.forEach(5, [&](size_t) { Results[1].check(0, Inner.size()); });
        Results[1].restore(Worker);
      }
      {
        fl::Pool Inner(3);
        Inner.forEach(2, [&](size_t) { Results[2].check(0, Inner.size()); }, 4);
        Results[2].restore(Worker);
        Inner.run([&](unsigned Index) { Results[3].check(Index, Inner.size()); });
        Results[3].restore(Worker);
        Inner.forEach(1024, [&](size_t) {
          const unsigned Index = fl::Pool::self();
          // The real consumer indexes slots by self(); inspect the invariant
          // without ever indexing an invalid slot in this reproducer.
          Results[4].check(Index < Inner.size() ? Index : 0, Inner.size());
        }, 1);
        Results[4].restore(Worker);
      }
      {
        fl::Pool Inner(1);
        bool Caught = false;
        try {
          Inner.run([&](unsigned Index) {
            Results[5].check(Index, Inner.size());
            throw std::runtime_error("serial-pool-marker");
          });
        } catch (const std::runtime_error &Error) {
          Caught = std::string(Error.what()) == "serial-pool-marker";
        }
        if (!Caught)
          ++Results[5].Errors;
        Results[5].restore(Worker);
      }
      {
        fl::Pool Inner(3);
        bool Caught = false;
        try {
          Inner.forEach(2, [&](size_t) {
            Results[6].check(0, Inner.size());
            throw std::runtime_error("small-pool-marker");
          }, 4);
        } catch (const std::runtime_error &Error) {
          Caught = std::string(Error.what()) == "small-pool-marker";
        }
        if (!Caught)
          ++Results[6].Errors;
        Results[6].restore(Worker);
        Caught = false;
        try {
          Inner.run([&](unsigned Index) {
            Results[7].check(Index, Inner.size());
            if (Index == 0)
              throw std::runtime_error("parallel-pool-marker");
          });
        } catch (const std::runtime_error &Error) {
          Caught = std::string(Error.what()) == "parallel-pool-marker";
        }
        if (!Caught || Inner.failed())
          ++Results[7].Errors;
        Results[7].restore(Worker);
        Inner.forEach(0, [&](size_t) { ++Results[8].Errors; });
        Results[8].restore(Worker);
      }
    }
  });
  unsigned Bad = 0;
  for (unsigned Index = 0; Index != Results.size(); ++Index) {
    const auto &Row = Results[Index];
    std::cout << Names[Index] << " " << Row.Visits << " " << Row.Wrong << " "
              << Row.Bounds << " " << Row.Restores << " " << Row.Errors << "\n";
    if (Row.Restores || Row.Errors || (Index != 8 && !Row.Visits))
      ++Bad;
    if (ExpectBroken) {
      if (Index != 4 && Index != 8 && (!Row.Wrong || !Row.Bounds))
        ++Bad;
    } else if (Row.Wrong || Row.Bounds) {
      ++Bad;
    }
  }
  // The outer caller's prior ordinal is restored after its complete run too.
  if (fl::Pool::self() != 0)
    ++Bad;
  return Bad ? 1 : 0;
}
