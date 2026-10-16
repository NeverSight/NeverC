#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}

struct Steps{using result_type=unsigned;unsigned calls=0,mode=0,width=1;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){unsigned group=(calls++/width)%2;return group==mode?0:0x80000000u;}};
template<class R>int edges(){std::fisher_f_distribution<R>d(R(2),R(2));Steps a{0,0,sizeof(R)==4?1u:2u},b{0,1,sizeof(R)==4?1u:2u};R low=d(a),high=d(b);if(bits(low)!=(sizeof(R)==4?0x80000000ULL:0x8000000000000000ULL)||bits(high)!=(sizeof(R)==4?0xff800000ULL:0xfff0000000000000ULL))return 1;if(a.calls!=(sizeof(R)==4?2u:4u)||b.calls!=a.calls)return 2;return 0;}
struct Mutates{using result_type=unsigned;unsigned calls=0;std::fisher_f_distribution<float>::param_type*target;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;*target=std::fisher_f_distribution<float>::param_type(8.f,10.f);return 0x80000000u;}};
unsigned effects;float mf(){++effects;return 2.5f;}float nf(){++effects;return 3.f;}
int main(){using D=std::fisher_f_distribution<float>;using P=D::param_type;if(int r=edges<float>())return r;if(int r=edges<double>())return 8+r;D constructed(mf(),nf());if(effects!=2||constructed.m()!=2.5f||constructed.n()!=3.f)return 16;
 P changing(2.f,2.f);Mutates g{0,&changing};D stored(2.f,2.f);float x=stored(g,changing);bool allowed=(x>.24999f&&x<.25001f)||(x>.99999f&&x<1.00001f)||(x>1.24999f&&x<1.25001f)||(x>4.99999f&&x<5.00001f);if(g.calls!=2||!allowed||changing.m()!=8.f||changing.n()!=10.f||stored.m()!=2.f||stored.n()!=2.f)return 17;
 return 0;}
