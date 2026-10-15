#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}

struct Zero{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;return 0;}};
template<class R>int zero(){std::exponential_distribution<R>d(R(2));Zero a,b;typename std::exponential_distribution<R>::param_type p(R(4));R x=d(a),y=d(b,p);if(bits(x)!=(sizeof(R)==4?0x80000000ULL:0x8000000000000000ULL)||bits(y)!=bits(x))return 1;if(a.calls!=(sizeof(R)==4?1u:2u)||b.calls!=a.calls)return 2;return 0;}
struct Maximum{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;return ~0u;}};
unsigned effects;
float ratef(){++effects;return 2.f;}
double rated(){++effects;return .5;}
template<class R>int maximum(){std::exponential_distribution<R>d;Maximum g;R x=d(g);if(bits(x)!=(sizeof(R)==4?0x7f800000ULL:0x7ff0000000000000ULL))return 1;if(g.calls!=(sizeof(R)==4?1u:2u))return 2;return 0;}
int main(){if(int r=zero<float>())return r;if(int r=zero<double>())return 8+r;if(int r=maximum<float>())return 16+r;if(int r=maximum<double>())return 24+r;std::exponential_distribution<float>d(ratef());std::exponential_distribution<double>::param_type p(rated());if(effects!=2||d.lambda()!=2.f||p.lambda()!=.5)return 32;return 0;}
