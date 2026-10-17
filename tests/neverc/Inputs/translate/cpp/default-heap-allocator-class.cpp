#include <memory>
#include <new>
unsigned long long arena[16];
int custom = 0;
struct Object {
  int value;
  static void* operator new(std::size_t) { ++custom; return arena; }
  static void operator delete(void*) noexcept { ++custom; }
};
int main() {
  std::allocator<Object> allocator;
  Object* pointer = allocator.allocate(2);
  allocator.construct(pointer, Object{17});
  allocator.construct(pointer+1, Object{19});
  if (!pointer || pointer[0].value != 17 || pointer[1].value != 19 || custom) return 1;
  allocator.destroy(pointer+1); allocator.destroy(pointer);
  allocator.deallocate(pointer, 2);
  return custom ? 2 : 0;
}
