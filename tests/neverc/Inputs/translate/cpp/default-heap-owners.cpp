#include <memory>
#include <utility>
int live = 0, drops = 0;
struct Object {
  int value;
  Object(int n = 13) noexcept : value(n) { ++live; }
  ~Object() { --live; ++drops; }
};
struct Holder { std::unique_ptr<Object> value; };
std::unique_ptr<Object> create(int n) { return std::make_unique<Object>(n); }
int main() {
  {
    Holder holder{create(29)};
    if (live != 1 || holder.value->value != 29) return 1;
    auto moved = std::move(holder.value);
    if (holder.value || !moved || live != 1) return 2;
    moved.reset(new Object(31));
    if (live != 1 || drops != 1 || moved->value != 31) return 3;
    auto array = std::make_unique<Object[]>(3);
    if (live != 4 || array[2].value != 13) return 4;
    auto values = std::make_unique<int[]>(4);
    for (int i = 0; i < 4; ++i) if (values[i]) return 5;
    Object* released = moved.release();
    if (moved || !released || released->value != 31) return 6;
    std::default_delete<Object>{}(released);
    std::unique_ptr<Object> owners[2];
    owners[0] = create(41); owners[1] = create(43);
    if (live != 5) return 7;
  }
  if (live || drops != 7) return 8;
  return 0;
}
