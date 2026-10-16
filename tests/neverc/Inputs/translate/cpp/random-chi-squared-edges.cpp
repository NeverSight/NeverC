#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}

struct Zero{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;return 0;}};
struct Maximum{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;return ~0u;}};
template<class R>int edges(){std::chi_squared_distribution<R>d(R(2));Zero a;Maximum b;R low=d(a),high=d(b);if(bits(low)!=(sizeof(R)==4?0x80000000ULL:0x8000000000000000ULL)||bits(high)!=(sizeof(R)==4?0x7f800000ULL:0x7ff0000000000000ULL))return 1;if(a.calls!=(sizeof(R)==4?1u:2u)||b.calls!=a.calls)return 2;return 0;}
struct Mutates{using result_type=unsigned;unsigned calls=0;std::chi_squared_distribution<float>::param_type*target;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;*target=std::chi_squared_distribution<float>::param_type(8.f);return 0x80000000u;}};
struct MutatesDistribution{using result_type=unsigned;unsigned calls=0;std::chi_squared_distribution<float>*target;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;target->param(std::chi_squared_distribution<float>::param_type(8.f));return 0x80000000u;}};
unsigned effects;float degree(){++effects;return 2.5f;}
int main(){using D=std::chi_squared_distribution<float>;using P=D::param_type;if(int r=edges<float>())return r;if(int r=edges<double>())return 8+r;D constructed(degree());if(effects!=1||constructed.n()!=2.5f)return 16;
 P changing(2.f);Mutates g{0,&changing};D stored(2.f);float first=stored(g,changing);if(g.calls!=1||first<1.386f||first>1.387f||changing.n()!=8.f||stored.n()!=2.f)return 17;
 D live(2.f);MutatesDistribution own{0,&live};float second=live(own);if(own.calls!=1||second<1.386f||second>1.387f||live.n()!=8.f)return 18;
 return 0;}
