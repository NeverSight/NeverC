#include <functional>
#include <cstddef>
std::size_t object_hash(const int* pointer) { return std::hash<const int*>{}(pointer); }
using Callback = int (*)(int) noexcept;
std::size_t callback_hash(Callback pointer) { return std::hash<Callback>{}(pointer); }
