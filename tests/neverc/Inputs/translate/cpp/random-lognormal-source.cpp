#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}

template<class R>bool close(R actual,unsigned long long expected){R reference=value<R>(expected);R tolerance=(sizeof(R)==4?R(.000003):R(.00000000000003))*(magnitude(reference)+R(5));return magnitude(actual-reference)<=tolerance;}
template<class R,class Engine>int check(const unsigned long long* expected,unsigned long long changed,unsigned long long cached,unsigned long long reset,unsigned long long next){using D=std::lognormal_distribution<R>;using P=typename D::param_type;
 D cold,coldCopy(cold),coldMoved(static_cast<D&&>(cold));P initial;D location(R(2));P one(R(4));if(cold!=coldCopy||coldMoved!=cold||initial.m()!=R(0)||initial.s()!=R(1)||location.s()!=R(1)||one.s()!=R(1))return 1;
 P p(R(2.5),R(.75)),pc(p),pm(static_cast<P&&>(p));if(p!=pc||pm!=p)return 2;
 D d(p);P returned=d.param();if(returned!=p)return 11;if(d.m()!=R(2.5)||d.s()!=R(.75)||bits(d.min())!=0ULL||bits(d.max())!=(sizeof(R)==4?0x7f800000ULL:0x7ff0000000000000ULL))return 3;
 Engine e;for(unsigned i=0;i<10003;++i){R x=d(e);if(!(x==x))return 4;if(i<16&&!close(x,expected[i]))return 5;}
 D copied(d),moved(static_cast<D&&>(d)),assigned,resetter(d);D*result=&(assigned=d);D*self=&(assigned=assigned);if(result!=&assigned||self!=&assigned||assigned!=copied||moved!=copied||d!=copied)return 6;
 d.param(P(R(-1),R(2)));R x=d(e),c=copied(e),m=moved(e),a=assigned(e);if(!close(x,changed)||!close(c,cached)||bits(c)!=bits(m)||bits(m)!=bits(a)||d.m()!=R(-1)||d.s()!=R(2))return 7;
 resetter.reset();R r=resetter(e);if(!close(r,reset)||static_cast<unsigned long long>(e())!=next)return 8;return 0;}
int main(){
const unsigned long long s0[]={1090560518ULL,1101237456ULL,1091829320ULL,1078181679ULL,1106607378ULL,1086160624ULL,1100971670ULL,1084990951ULL,1084267471ULL,1096980510ULL,1096949672ULL,1073967649ULL,1103724457ULL,1093513762ULL,1078713845ULL,1101032033ULL};
if(int r=check<float,G>(s0,1084758760ULL,1107563268ULL,1091997299ULL,2914249248ULL))return 0+r;
const unsigned long long s1[]={4627368572179054404ULL,4620684045912471798ULL,4620111099635129896ULL,4617653463768420041ULL,4627954062346445063ULL,4613010731300278233ULL,4619688916734929218ULL,4622542592107397223ULL,4625810469988583920ULL,4631219861339024760ULL,4618240071999106120ULL,4622623159198960846ULL,4629051351494839308ULL,4626301357659731643ULL,4629033642378720020ULL,4630381449681522299ULL};
if(int r=check<double,G>(s1,4590081363123882782ULL,4619247803741013991ULL,4615130741369454581ULL,1674662908ULL))return 16+r;
return 0;}
