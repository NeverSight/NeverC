#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}

template<class R>bool close(R actual,unsigned long long expected){R reference=value<R>(expected);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+R(5));return magnitude(actual-reference)<=tolerance;}
template<class R,class Engine>int check(const unsigned long long* expected,unsigned long long changed,unsigned long long cached,unsigned long long reset,unsigned long long next){using D=std::normal_distribution<R>;using P=typename D::param_type;
 D cold,coldCopy(cold),coldMoved(static_cast<D&&>(cold));P initial;D location(R(2));P one(R(4));if(cold!=coldCopy||coldMoved!=cold||initial.mean()!=R(0)||initial.stddev()!=R(1)||location.stddev()!=R(1)||one.stddev()!=R(1))return 1;
 P p(R(2.5),R(.75)),pc(p),pm(static_cast<P&&>(p));if(p!=pc||pm!=p)return 2;
 D d(p);if(d.mean()!=R(2.5)||d.stddev()!=R(.75)||bits(d.min())!=(sizeof(R)==4?0xff800000ULL:0xfff0000000000000ULL)||bits(d.max())!=(sizeof(R)==4?0x7f800000ULL:0x7ff0000000000000ULL))return 3;
 Engine e;for(unsigned i=0;i<10003;++i){R x=d(e);if(!(x==x))return 4;if(i<16&&!close(x,expected[i]))return 5;}
 D copied(d),moved(static_cast<D&&>(d)),assigned,resetter(d);D*result=&(assigned=d);D*self=&(assigned=assigned);if(result!=&assigned||self!=&assigned||assigned!=copied||moved!=copied||d!=copied)return 6;
 d.param(P(R(-1),R(2)));R x=d(e),c=copied(e),m=moved(e),a=assigned(e);if(!close(x,changed)||!close(c,cached)||bits(c)!=bits(m)||bits(m)!=bits(a)||d.mean()!=R(-1)||d.stddev()!=R(2))return 7;
 resetter.reset();R r=resetter(e);if(!close(r,reset)||static_cast<unsigned long long>(e())!=next)return 8;return 0;}
int main(){
const unsigned long long s0[]={1078323617ULL,1073613106ULL,1076137384ULL,1076471664ULL,1074963720ULL,1075665833ULL,1076731782ULL,1063647336ULL,1067348795ULL,1077269788ULL,1070856661ULL,1076270242ULL,1076061976ULL,1079472364ULL,1075815194ULL,1056084384ULL};
if(int r=check<float,std::mt19937_64>(s0,3212981891ULL,1075811783ULL,1080495262ULL,16775822657467161507ULL))return 0+r;
const unsigned long long s1[]={4614145849555026799ULL,4611616913452843178ULL,4612972124850005238ULL,4613151590212766723ULL,4612342018664360148ULL,4612718962726880251ULL,4613291239601884486ULL,4606266579012600972ULL,4608253786973607650ULL,4613580079564164186ULL,4610137058396854207ULL,4613043452572251648ULL,4612931640630835870ULL,4614762578947669336ULL,4612799149870887456ULL,4602206254191902688ULL};
if(int r=check<double,std::mt19937_64>(s1,13830632316338028703ULL,4612797319456123874ULL,4615311742680415278ULL,16775822657467161507ULL))return 16+r;
return 0;}
