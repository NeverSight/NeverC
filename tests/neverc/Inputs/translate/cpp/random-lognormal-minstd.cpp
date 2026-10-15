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
const unsigned long long s0[]={1097317534ULL,1102982279ULL,1098918085ULL,1072795550ULL,1087077267ULL,1102973038ULL,1087198214ULL,1096168351ULL,1098647900ULL,1107310751ULL,1118317890ULL,1098470122ULL,1079400365ULL,1089699555ULL,1101625150ULL,1097919221ULL};
if(int r=check<float,std::minstd_rand>(s0,1079031968ULL,1104996304ULL,1100235177ULL,4342827ULL))return 0+r;
const unsigned long long s1[]={4617098895215526843ULL,4621241857565575851ULL,4630940773281674754ULL,4625321178154165063ULL,4630033564099189901ULL,4623322208667982394ULL,4618648059952920251ULL,4632170413695849640ULL,4615078829049616111ULL,4619872811396721600ULL,4623448507594798390ULL,4630373591690884575ULL,4623566501309742964ULL,4618709823809505389ULL,4621966414230057294ULL,4628908428538323769ULL};
if(int r=check<double,std::minstd_rand>(s1,4586701576119382721ULL,4617944092785958799ULL,4619205235157099673ULL,857226777ULL))return 16+r;
return 0;}
