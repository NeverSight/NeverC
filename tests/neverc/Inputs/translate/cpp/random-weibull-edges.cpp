#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}

struct Zero{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;return 0;}};
struct Maximum{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;return ~0u;}};
template<class R>int edges(){std::weibull_distribution<R>d;Zero a;Maximum b;R low=d(a),high=d(b);if(bits(low)!=(sizeof(R)==4?0x80000000ULL:0x8000000000000000ULL)||bits(high)!=(sizeof(R)==4?0x7f800000ULL:0x7ff0000000000000ULL))return 1;if(a.calls!=(sizeof(R)==4?1u:2u)||b.calls!=a.calls)return 2;return 0;}
unsigned effects;float af(){++effects;return 2.5f;}float bf(){++effects;return 3.f;}
int main(){if(int r=edges<float>())return r;if(int r=edges<double>())return 8+r;std::weibull_distribution<float>d(af(),bf());if(effects!=2||d.a()!=2.5f||d.b()!=3.f)return 16;return 0;}
