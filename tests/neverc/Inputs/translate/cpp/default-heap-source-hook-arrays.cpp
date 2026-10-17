#include <memory>
using Size = decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
int allocations = 0, releases = 0;
void* operator new(Size bytes) { ++allocations; return malloc(bytes ? bytes : 1); }
void operator delete(void* pointer) noexcept { ++releases; free(pointer); }
int main() {
  {
    std::unique_ptr<int> owners[2];
    if (owners[0] || owners[1] || allocations || releases) return 1;
    owners[0].reset(new int(17));
    owners[1].reset(new int(19));
    if (*owners[0] != 17 || *owners[1] != 19 || allocations != 2 || releases) return 2;
    owners[0].reset(new int(23));
    if (*owners[0] != 23 || allocations != 3 || releases != 1) return 3;
  }
  return allocations == 3 && releases == 3 ? 0 : 4;
}
