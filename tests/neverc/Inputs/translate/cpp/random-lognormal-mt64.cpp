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
const unsigned long long s0[]={1102068867ULL,1089001837ULL,1095846652ULL,1096984557ULL,1092498760ULL,1094388110ULL,1097934930ULL,1075652239ULL,1079816582ULL,1099502796ULL,1084728047ULL,1096288079ULL,1095602256ULL,1105707760ULL,1094832477ULL,1070436991ULL};
if(int r=check<float,std::mt19937_64>(s0,1052319802ULL,1094822151ULL,1108599483ULL,16775822657467161507ULL))return 0+r;
const unsigned long long s1[]={4626893983068239238ULL,4619878675272045637ULL,4623553457331019439ULL,4624164366208260213ULL,4621756071236451219ULL,4622770408524538233ULL,4624674592233731216ULL,4612711663211574514ULL,4614947379042387179ULL,4625516334614859323ULL,4617584201885578716ULL,4623790446971542186ULL,4623422248781637062ULL,4628847600913456751ULL,4623008974576339258ULL,4609911750316453933ULL};
if(int r=check<double,std::mt19937_64>(s1,4600185158116586294ULL,4623003433595766274ULL,4630400081252727746ULL,16775822657467161507ULL))return 16+r;
return 0;}
