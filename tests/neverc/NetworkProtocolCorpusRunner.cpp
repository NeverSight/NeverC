//===- NetworkProtocolCorpusRunner.cpp - Sanitizer corpus runner --------===//

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " <corpus-directory>\n";
    return 2;
  }

  namespace fs = std::filesystem;
  const fs::path corpus(argv[1]);
  if (!fs::is_directory(corpus)) {
    std::cerr << "corpus directory not found: " << corpus << '\n';
    return 2;
  }

  size_t inputs = 0;
  for (const fs::directory_entry &entry : fs::directory_iterator(corpus)) {
    if (!entry.is_regular_file())
      continue;
    std::ifstream stream(entry.path(), std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    if (stream.bad() || data.empty()) {
      std::cerr << "invalid corpus input: " << entry.path() << '\n';
      return 1;
    }
    LLVMFuzzerTestOneInput(data.data(), data.size());
    ++inputs;
  }

  if (inputs == 0) {
    std::cerr << "corpus is empty: " << corpus << '\n';
    return 1;
  }
  std::cout << "network protocol sanitizer corpus passed: " << inputs
            << " inputs\n";
  return 0;
}
