#include <memory>
#include <functional>
#include <cstddef>
struct Undestroyable{Undestroyable()=delete;~Undestroyable()=delete;};
std::size_t scalar_hash(const std::unique_ptr<Undestroyable>&p){return std::hash<std::unique_ptr<Undestroyable>>{}(p);}
std::size_t array_hash(const std::unique_ptr<Undestroyable[]>&p){return std::hash<std::unique_ptr<Undestroyable[]>>{}(p);}
