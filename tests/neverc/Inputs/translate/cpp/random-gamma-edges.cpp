#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}

struct Zero{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;return 0;}};
struct Maximum{using result_type=unsigned;unsigned calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;return ~0u;}};
template<class R>int edges(){std::gamma_distribution<R>d;Zero a;Maximum b;R low=d(a),high=d(b);if(bits(low)!=(sizeof(R)==4?0x80000000ULL:0x8000000000000000ULL)||bits(high)!=(sizeof(R)==4?0x7f800000ULL:0x7ff0000000000000ULL))return 1;if(a.calls!=(sizeof(R)==4?1u:2u)||b.calls!=a.calls)return 2;return 0;}
struct Steps{using result_type=unsigned;unsigned calls=0,mode=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){const unsigned values[]={0,0,0x40000000u,0,0x80000000u,0x80000000u,0xc0000000u,~0u,0xc0000000u,0xc0000000u,0,0,0x80000000u,0,0x40000000u,0x80000000u,0,0,0xc0000000u,0,0xc0000000u,0x80000000u,0,0};return values[mode*6+(calls++%6)];}};
struct Mutates{using result_type=unsigned;unsigned calls=0;std::gamma_distribution<float>::param_type* target;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;*target=std::gamma_distribution<float>::param_type(2.f,3.f);return 0x80000000u;}};
unsigned effects;float af(){++effects;return 2.5f;}float bf(){++effects;return 3.f;}
int main(){using D=std::gamma_distribution<float>;using P=D::param_type;if(int r=edges<float>())return r;if(int r=edges<double>())return 8+r;D constructed(af(),bf());if(effects!=2||constructed.alpha()!=2.5f||constructed.beta()!=3.f)return 16;
 Steps large{0,0};D a(2.f);if(a(large)!=1.f||large.calls!=6)return 17;
 Steps logs{0,1};float accepted=a(logs);if(logs.calls!=4||accepted<2.322f||accepted>2.324f)return 18;
 D small(.5f);Steps lower{0,2};if(small(lower)!=.0625f||lower.calls!=4)return 19;
 Steps upper{0,3};float acceptedUpper=small(upper);if(upper.calls!=4||acceptedUpper<.716f||acceptedUpper>.718f)return 20;
 P changing(1.f,1.f);Mutates mutator{0,&changing};D stored;float changed=stored(mutator,changing);if(mutator.calls!=1||changed<2.079f||changed>2.080f||changing.alpha()!=2.f||changing.beta()!=3.f||stored.alpha()!=1.f||stored.beta()!=1.f)return 21;
 return 0;}
