// Include-free cpp-core-v1 executable. Translation preserves main explicitly.
namespace demo {
struct Pair { int first; unsigned int second; };
int adjust(int value) { return value + 2; }
unsigned int adjust(unsigned int value) { return value + 3u; }
int total(Pair pair) { return pair.first + static_cast<int>(pair.second); }
}

int main() {
  demo::Pair pair{demo::adjust(5), demo::adjust(8u)};
  int result = demo::total(pair);
  int value = 0;
  value = ++value + 1;
  return result == 18 && value == 2 ? 0 : 1;
}
