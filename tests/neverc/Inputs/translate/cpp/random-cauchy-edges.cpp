#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}

struct Constant64{using result_type=unsigned long long;unsigned calls=0;unsigned long long draw=0;static constexpr result_type min(){return 0;}static constexpr result_type max(){return ~0ULL;}result_type operator()(){++calls;return draw;}};
template<class R>int edges(){std::cauchy_distribution<R>d;Constant64 zero{0,0},half{0,1ULL<<63},maximum{0,~0ULL};R z=d(zero),h=d(half),m=d(maximum);if(bits(z)!=0||h<R(1.6e16)||h>R(1.7e16)||m>R(-1.2e-16)||m<R(-1.3e-16))return 1;if(zero.calls!=1||half.calls!=1||maximum.calls!=1)return 2;return 0;}
unsigned effects;float af(){++effects;return -2.5f;}float bf(){++effects;return 3.f;}
int main(){if(int r=edges<float>())return r;if(int r=edges<double>())return 8+r;std::cauchy_distribution<float>d(af(),bf());if(effects!=2||d.a()!=-2.5f||d.b()!=3.f)return 16;return 0;}
