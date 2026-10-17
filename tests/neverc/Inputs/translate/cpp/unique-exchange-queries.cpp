#include <memory>
#include <utility>
struct Undestroyable {
  Undestroyable() = delete;
  ~Undestroyable() = delete;
};
using Owner = std::unique_ptr<Undestroyable>;
using ArrayOwner = std::unique_ptr<Undestroyable[]>;
static_assert(__is_same(decltype(std::exchange(std::declval<Owner&>(), std::declval<Owner&&>())), Owner));
static_assert(__is_same(decltype(std::exchange(std::declval<Owner&>(), nullptr)), Owner));
static_assert(noexcept(std::exchange(std::declval<Owner&>(), std::declval<Owner&&>())));
static_assert(noexcept(std::exchange(std::declval<Owner&>(), nullptr)));
static_assert(sizeof(std::exchange(std::declval<Owner&>(), nullptr)) == sizeof(Owner));
static_assert(__is_same(decltype(std::exchange(std::declval<ArrayOwner&>(), nullptr)), ArrayOwner));
static_assert(noexcept(std::exchange(std::declval<ArrayOwner&>(), nullptr)));
int main() { return 0; }
