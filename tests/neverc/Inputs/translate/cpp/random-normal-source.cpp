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
const unsigned long long s0[]={1074095714ULL,1078010273ULL,1074683773ULL,1066342560ULL,1079713711ULL,1071884848ULL,1077904955ULL,1071055039ULL,1070497401ULL,1076470521ULL,1076461802ULL,1060650758ULL,1078885472ULL,1075355444ULL,1066683522ULL,1077929107ULL};
if(int r=check<float,G>(s0,1070880080ULL,1080020991ULL,1074755794ULL,2914249248ULL))return 0+r;
const unsigned long long s1[]={4614311918839282150ULL,4611862610765515683ULL,4611714459744416936ULL,4610189635691804298ULL,4614501236332528413ULL,4606741045263651596ULL,4611511373929325073ULL,4612639627738506649ULL,4613713379921805378ULL,4615641196022116725ULL,4610611914829926921ULL,4612668005034424046ULL,4614818152402306471ULL,4613919648103127735ULL,4614813376253598097ULL,4615303665084666444ULL};
if(int r=check<double,G>(s1,13836381965363222540ULL,4611256035820015094ULL,4608358899063024590ULL,1674662908ULL))return 16+r;
return 0;}
