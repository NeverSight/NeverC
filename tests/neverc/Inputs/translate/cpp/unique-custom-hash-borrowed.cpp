#include <memory>
#include <functional>
#include <cstddef>
struct Undestroyable{Undestroyable()=delete;~Undestroyable()=delete;};
struct D{void operator()(Undestroyable*)const noexcept{}};
std::size_t scalar_hash(const std::unique_ptr<Undestroyable,D>&p){return std::hash<std::unique_ptr<Undestroyable,D>>{}(p);}
std::size_t array_hash(const std::unique_ptr<Undestroyable[],D>&p){return std::hash<std::unique_ptr<Undestroyable[],D>>{}(p);}
