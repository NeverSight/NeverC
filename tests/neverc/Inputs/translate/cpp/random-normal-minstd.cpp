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
const unsigned long long s0[]={1076564647ULL,1078642858ULL,1076987511ULL,1059231072ULL,1072482234ULL,1078639747ULL,1072557972ULL,1076234621ULL,1076916851ULL,1079896807ULL,1083036126ULL,1076871463ULL,1067103852ULL,1073864982ULL,1078159305ULL,1076727611ULL};
if(int r=check<float,std::minstd_rand>(s0,1066880918ULL,1079271148ULL,1077598496ULL,4342827ULL))return 0+r;
const unsigned long long s1[]={4609750362625756054ULL,4612123759664136769ULL,4615534360241131948ULL,4613487070960395122ULL,4615147262741240275ULL,4612900276832704263ULL,4610883882842165726ULL,4615971170912315110ULL,4608329388544484065ULL,4611613688857615328ULL,4612939801217746068ULL,4615300249597625452ULL,4612976110059884016ULL,4610923660410393594ULL,4612425540519490343ULL,4614779313909335830ULL};
if(int r=check<double,std::minstd_rand>(s1,13837518282321601046ULL,4610403798101231216ULL,4611230611465949534ULL,857226777ULL))return 16+r;
return 0;}
