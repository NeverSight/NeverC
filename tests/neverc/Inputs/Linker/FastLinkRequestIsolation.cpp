// Exercise the actual FastLink public entrypoint with invocation-owned inputs.
#include "FastLink.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

struct Probe {
  fs::path Output;
  std::vector<unsigned char> Entry, Wrong;
  std::array<std::vector<unsigned char>, 4> Values;
  std::array<int, 4> Expected{{13, 29, 47, 61}};
  std::mutex Print;
  std::atomic<unsigned> Errors{0}, Ready{0};
  std::atomic<bool> Start{false};

  static std::vector<unsigned char> read(const fs::path &Path) {
    std::ifstream Input(Path, std::ios::binary);
    if (!Input)
      throw std::runtime_error("cannot read fixture " + Path.string());
    return {std::istreambuf_iterator<char>(Input), {}};
  }

  Probe(const fs::path &Input, const fs::path &Output) : Output(Output) {
    fs::create_directories(Output);
    Entry = read(Input / "entry.o");
    Wrong = read(Input / "wrong.o");
    for (unsigned I = 0; I != Values.size(); ++I)
      Values[I] = read(Input / ("value-" + std::to_string(I) + ".o"));
  }

  void link(const std::string &Name, unsigned Value, unsigned Threads,
            bool Mmap, unsigned Icf, bool Bad = false,
            const std::function<void()> &Nested = {}) {
    const std::string EntryName = "memory-" + Name + "-entry.o";
    const std::string ValueName = "memory-" + Name + "-value.o";
    std::vector<std::string> Loaded;
    bool CalledNested = false;
    fastlink::Request Request;
    Request.output = (Output / (Name + ".elf")).string();
    Request.noDynamicLinker = true;
    Request.gcSections = true;
    Request.icf = Icf;
    Request.mmapOutput = Mmap;
    Request.threads = Threads;
    Request.buildIdSize = 20;
    Request.loadedPaths = &Loaded;
    Request.inputs = {{EntryName}, {ValueName}};
    Request.openFile = [&](const std::string &Path, const unsigned char *&Data,
                           std::size_t &Size) {
      const std::vector<unsigned char> *Bytes = nullptr;
      if (Path == EntryName) {
        if (Nested && !CalledNested) {
          CalledNested = true;
          Nested();
        }
        Bytes = &Entry;
      } else if (Path == ValueName) {
        Bytes = Bad ? &Wrong : &Values[Value];
      } else {
        ++Errors;
        return false;
      }
      Data = Bytes->data();
      Size = Bytes->size();
      return true;
    };
    std::string Reason;
    const fastlink::Status Status = fastlink::link(Request, Reason);
    const bool Linked = Status == fastlink::Status::Linked;
    bool Passed = Linked != Bad && Loaded == std::vector<std::string>{EntryName, ValueName};
    Passed &= Bad ? Reason.find("not an x86-64 ELF object") != std::string::npos
                  : Reason.empty() && fs::is_regular_file(Request.output);
    if (!Passed)
      ++Errors;
    std::lock_guard<std::mutex> Lock(Print);
    std::cout << "RESULT\t" << Name << '\t' << (Linked ? "linked" : "declined")
              << '\t' << (Passed ? "pass" : "fail") << '\t'
              << (Bad ? 0 : Expected[Value]) << '\t' << Request.output << '\t'
              << Reason << '\n';
  }

  void serial() {
    for (unsigned I = 0; I != 4; ++I)
      link("serial-" + std::to_string(I), I, I == 0 || I == 3 ? 1 : 4,
           I % 2 == 0, I == 0 || I == 3 ? 0 : 2);
    link("serial-declined", 0, 4, true, 2, true);
  }

  void nested() {
    link("nested-outer", 0, 4, true, 2, false, [&] {
      link("nested-child-declined", 1, 4, false, 2, true);
      link("nested-child", 1, 4, false, 2);
    });
  }

  void concurrent() {
    std::vector<std::thread> Workers;
    for (unsigned Worker = 0; Worker != 4; ++Worker)
      Workers.emplace_back([&, Worker] {
        ++Ready;
        while (!Start.load(std::memory_order_acquire))
          std::this_thread::yield();
        for (unsigned Round = 0; Round != 3; ++Round) {
          const std::string Name = "parallel-" + std::to_string(Worker) + "-" +
                                   std::to_string(Round);
          if (Round == 0)
            link(Name + "-declined", Worker, 4, true, 2, true);
          link(Name, Worker, Round % 2 == 0 ? 4 : 1, Round % 2 == 0, 2);
        }
      });
    while (Ready.load(std::memory_order_acquire) != 4)
      std::this_thread::yield();
    Start.store(true, std::memory_order_release);
    for (auto &Worker : Workers)
      Worker.join();
  }
};

int main(int Count, char **Args) {
  if (Count != 4)
    return 2;
  try {
    Probe P(Args[1], Args[2]);
    P.serial();
    if (std::string(Args[3]) == "all") {
      P.nested();
      P.concurrent();
    } else if (std::string(Args[3]) != "serial") {
      return 2;
    }
    std::cout << "ERRORS\t" << P.Errors.load() << '\n';
    return P.Errors.load() == 0 ? 0 : 1;
  } catch (const std::exception &Error) {
    std::cerr << Error.what() << '\n';
    return 3;
  }
}
