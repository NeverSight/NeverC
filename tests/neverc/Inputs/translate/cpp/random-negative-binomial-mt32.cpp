#include <random>
template<class D,class E>typename D::result_type draw_sample(D&d,E&e,const typename D::param_type&p,bool explicitParameter){if(explicitParameter)return d(e,p);return d(e); }
template<class D>unsigned long long degree(const D&d){return static_cast<unsigned long long>(d.k());}
template<class I>unsigned long long degree(const std::geometric_distribution<I>&){return 1;}
template<class P,bool Geometric,class I>P make(I k,double p){if constexpr(Geometric)return P(p);else return P(k,p);}
template<class I,class E,bool Geometric>int check(E& engine,E& clonedEngine,unsigned k,const unsigned long long*expected,unsigned long long hashExpected,unsigned long long next){
using D=typename std::conditional<Geometric,std::geometric_distribution<I>,std::negative_binomial_distribution<I>>::type;using P=typename D::param_type;const double probabilities[]={.5,.05,.04,.01,.9,1.};const unsigned successes[]={1,2,8,16,5,2};
P initial;D defaults;if(initial.p()!=.5||defaults.p()!=.5)return 1;
P p=make<P,Geometric>(static_cast<I>(successes[k]),probabilities[k]),pc(p),pm(static_cast<P&&>(p)),pa;P*pr=&(pa=p);P*selfP=&(pa=pa);D d(p),copied(d),moved(static_cast<D&&>(copied)),assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.param(p);assigned.reset();
if(pr!=&pa||selfP!=&pa||dr!=&assigned||selfD!=&assigned||pa!=p||pc!=p||pm!=p||moved!=d||assigned.param()!=p||assigned!=d||d.p()!=probabilities[k])return 2;
if(!Geometric&&degree(d)!=successes[k])return 3;
unsigned long long maximum=~0ULL>>(64-sizeof(I)*8+(I(-1)<I(0)?1:0));if(d.min()!=I(0)||d.max()!=static_cast<I>(maximum))return 4;
engine.seed(12345);unsigned long long hash=14695981039346656037ULL;for(unsigned i=0;i<10003;++i){I x=draw_sample(assigned,engine,p,(i&1)!=0);hash=(hash^static_cast<unsigned long long>(x))*1099511628211ULL;if(i<16&&static_cast<unsigned long long>(x)!=expected[i])return 5;}
if(hash!=hashExpected)return 6;D clone(assigned);clonedEngine=engine;I a=draw_sample(assigned,engine,p,false),b=draw_sample(clone,clonedEngine,p,false);if(a!=b||engine!=clonedEngine)return 7;if(static_cast<unsigned long long>(engine())!=next)return 8;
P changed=make<P,Geometric>(I(2),.75);assigned.param(changed);if(assigned.p()!=.75||assigned.param()!=changed||assigned==d)return 9;assigned.reset();return assigned.p()!=.75?10:0;}
int main(){
std::mt19937 engine(12345),clonedEngine;
const unsigned long long s0[]={1ULL,0ULL,3ULL,1ULL,0ULL,1ULL,0ULL,0ULL,5ULL,2ULL,1ULL,1ULL,2ULL,9ULL,0ULL,0ULL};
if(int r=check<unsigned,std::mt19937,false>(engine,clonedEngine,0,s0,5822536651935432561ULL,3645260711ULL))return r;
const unsigned long long s1[]={73ULL,14ULL,60ULL,31ULL,96ULL,69ULL,41ULL,9ULL,19ULL,47ULL,43ULL,7ULL,30ULL,8ULL,12ULL,71ULL};
if(int r=check<unsigned,std::mt19937,false>(engine,clonedEngine,1,s1,6107947725103741761ULL,346138443ULL))return r;
const unsigned long long s2[]={313ULL,154ULL,146ULL,189ULL,229ULL,222ULL,265ULL,238ULL,173ULL,196ULL,112ULL,150ULL,380ULL,346ULL,243ULL,136ULL};
if(int r=check<unsigned,std::mt19937,false>(engine,clonedEngine,2,s2,9726046894972803546ULL,3972726754ULL))return r;
const unsigned long long s3[]={2336ULL,1418ULL,1352ULL,1586ULL,1805ULL,1828ULL,2058ULL,1967ULL,1490ULL,1607ULL,1171ULL,1427ULL,2729ULL,1243ULL,1947ULL,1293ULL};
if(int r=check<unsigned,std::mt19937,false>(engine,clonedEngine,3,s3,16418435333018626365ULL,1594734054ULL))return r;
const unsigned long long s4[]={0ULL,2ULL,1ULL,1ULL,0ULL,0ULL,2ULL,0ULL,1ULL,0ULL,0ULL,4ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned,std::mt19937,false>(engine,clonedEngine,4,s4,6440760375602771948ULL,1633651775ULL))return r;
const unsigned long long s5[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned,std::mt19937,false>(engine,clonedEngine,5,s5,14734379229272918391ULL,408891044ULL))return r;
const unsigned long long s6[]={1ULL,0ULL,3ULL,1ULL,0ULL,1ULL,0ULL,0ULL,5ULL,2ULL,1ULL,1ULL,2ULL,9ULL,0ULL,0ULL};
if(int r=check<unsigned,std::mt19937,true>(engine,clonedEngine,0,s6,5822536651935432561ULL,3645260711ULL))return r;
const unsigned long long s7[]={2ULL,8ULL,38ULL,3ULL,4ULL,84ULL,51ULL,8ULL,17ULL,1ULL,36ULL,2ULL,28ULL,4ULL,1ULL,12ULL};
if(int r=check<unsigned,std::mt19937,true>(engine,clonedEngine,1,s7,6218382928632769141ULL,4088107650ULL))return r;
const unsigned long long s8[]={62ULL,74ULL,9ULL,58ULL,2ULL,27ULL,16ULL,15ULL,20ULL,1ULL,5ULL,12ULL,5ULL,15ULL,24ULL,3ULL};
if(int r=check<unsigned,std::mt19937,true>(engine,clonedEngine,2,s8,1499821675373780215ULL,4165579583ULL))return r;
const unsigned long long s9[]={237ULL,308ULL,43ULL,239ULL,7ULL,84ULL,177ULL,6ULL,56ULL,83ULL,3ULL,67ULL,62ULL,10ULL,244ULL,46ULL};
if(int r=check<unsigned,std::mt19937,true>(engine,clonedEngine,3,s9,12959414189069251016ULL,2045184021ULL))return r;
const unsigned long long s10[]={0ULL,0ULL,0ULL,0ULL,0ULL,1ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,1ULL,0ULL};
if(int r=check<unsigned,std::mt19937,true>(engine,clonedEngine,4,s10,14077693768751643482ULL,586344725ULL))return r;
const unsigned long long s11[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned,std::mt19937,true>(engine,clonedEngine,5,s11,14734379229272918391ULL,3450120912ULL))return r;
const unsigned long long s12[]={1ULL,0ULL,3ULL,1ULL,0ULL,1ULL,0ULL,0ULL,5ULL,2ULL,1ULL,1ULL,2ULL,9ULL,0ULL,0ULL};
if(int r=check<long,std::mt19937,false>(engine,clonedEngine,0,s12,5822536651935432561ULL,3645260711ULL))return r;
const unsigned long long s13[]={73ULL,14ULL,60ULL,31ULL,96ULL,69ULL,41ULL,9ULL,19ULL,47ULL,43ULL,7ULL,30ULL,8ULL,12ULL,71ULL};
if(int r=check<long,std::mt19937,false>(engine,clonedEngine,1,s13,6107947725103741761ULL,346138443ULL))return r;
const unsigned long long s14[]={313ULL,154ULL,146ULL,189ULL,229ULL,222ULL,265ULL,238ULL,173ULL,196ULL,112ULL,150ULL,380ULL,346ULL,243ULL,136ULL};
if(int r=check<long,std::mt19937,false>(engine,clonedEngine,2,s14,9726046894972803546ULL,3972726754ULL))return r;
const unsigned long long s15[]={2336ULL,1418ULL,1352ULL,1586ULL,1805ULL,1828ULL,2058ULL,1967ULL,1490ULL,1607ULL,1171ULL,1427ULL,2729ULL,1243ULL,1947ULL,1293ULL};
if(int r=check<long,std::mt19937,false>(engine,clonedEngine,3,s15,16418435333018626365ULL,1594734054ULL))return r;
const unsigned long long s16[]={0ULL,2ULL,1ULL,1ULL,0ULL,0ULL,2ULL,0ULL,1ULL,0ULL,0ULL,4ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<long,std::mt19937,false>(engine,clonedEngine,4,s16,6440760375602771948ULL,1633651775ULL))return r;
const unsigned long long s17[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<long,std::mt19937,false>(engine,clonedEngine,5,s17,14734379229272918391ULL,408891044ULL))return r;
const unsigned long long s18[]={1ULL,0ULL,3ULL,1ULL,0ULL,1ULL,0ULL,0ULL,5ULL,2ULL,1ULL,1ULL,2ULL,9ULL,0ULL,0ULL};
if(int r=check<long,std::mt19937,true>(engine,clonedEngine,0,s18,5822536651935432561ULL,3645260711ULL))return r;
const unsigned long long s19[]={2ULL,8ULL,38ULL,3ULL,4ULL,84ULL,51ULL,8ULL,17ULL,1ULL,36ULL,2ULL,28ULL,4ULL,1ULL,12ULL};
if(int r=check<long,std::mt19937,true>(engine,clonedEngine,1,s19,6218382928632769141ULL,4088107650ULL))return r;
const unsigned long long s20[]={62ULL,74ULL,9ULL,58ULL,2ULL,27ULL,16ULL,15ULL,20ULL,1ULL,5ULL,12ULL,5ULL,15ULL,24ULL,3ULL};
if(int r=check<long,std::mt19937,true>(engine,clonedEngine,2,s20,1499821675373780215ULL,4165579583ULL))return r;
const unsigned long long s21[]={237ULL,308ULL,43ULL,239ULL,7ULL,84ULL,177ULL,6ULL,56ULL,83ULL,3ULL,67ULL,62ULL,10ULL,244ULL,46ULL};
if(int r=check<long,std::mt19937,true>(engine,clonedEngine,3,s21,12959414189069251016ULL,2045184021ULL))return r;
const unsigned long long s22[]={0ULL,0ULL,0ULL,0ULL,0ULL,1ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,1ULL,0ULL};
if(int r=check<long,std::mt19937,true>(engine,clonedEngine,4,s22,14077693768751643482ULL,586344725ULL))return r;
const unsigned long long s23[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<long,std::mt19937,true>(engine,clonedEngine,5,s23,14734379229272918391ULL,3450120912ULL))return r;
const unsigned long long s24[]={1ULL,0ULL,3ULL,1ULL,0ULL,1ULL,0ULL,0ULL,5ULL,2ULL,1ULL,1ULL,2ULL,9ULL,0ULL,0ULL};
if(int r=check<unsigned long,std::mt19937,false>(engine,clonedEngine,0,s24,5822536651935432561ULL,3645260711ULL))return r;
const unsigned long long s25[]={73ULL,14ULL,60ULL,31ULL,96ULL,69ULL,41ULL,9ULL,19ULL,47ULL,43ULL,7ULL,30ULL,8ULL,12ULL,71ULL};
if(int r=check<unsigned long,std::mt19937,false>(engine,clonedEngine,1,s25,6107947725103741761ULL,346138443ULL))return r;
const unsigned long long s26[]={313ULL,154ULL,146ULL,189ULL,229ULL,222ULL,265ULL,238ULL,173ULL,196ULL,112ULL,150ULL,380ULL,346ULL,243ULL,136ULL};
if(int r=check<unsigned long,std::mt19937,false>(engine,clonedEngine,2,s26,9726046894972803546ULL,3972726754ULL))return r;
const unsigned long long s27[]={2336ULL,1418ULL,1352ULL,1586ULL,1805ULL,1828ULL,2058ULL,1967ULL,1490ULL,1607ULL,1171ULL,1427ULL,2729ULL,1243ULL,1947ULL,1293ULL};
if(int r=check<unsigned long,std::mt19937,false>(engine,clonedEngine,3,s27,16418435333018626365ULL,1594734054ULL))return r;
const unsigned long long s28[]={0ULL,2ULL,1ULL,1ULL,0ULL,0ULL,2ULL,0ULL,1ULL,0ULL,0ULL,4ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long,std::mt19937,false>(engine,clonedEngine,4,s28,6440760375602771948ULL,1633651775ULL))return r;
const unsigned long long s29[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long,std::mt19937,false>(engine,clonedEngine,5,s29,14734379229272918391ULL,408891044ULL))return r;
const unsigned long long s30[]={1ULL,0ULL,3ULL,1ULL,0ULL,1ULL,0ULL,0ULL,5ULL,2ULL,1ULL,1ULL,2ULL,9ULL,0ULL,0ULL};
if(int r=check<unsigned long,std::mt19937,true>(engine,clonedEngine,0,s30,5822536651935432561ULL,3645260711ULL))return r;
const unsigned long long s31[]={2ULL,8ULL,38ULL,3ULL,4ULL,84ULL,51ULL,8ULL,17ULL,1ULL,36ULL,2ULL,28ULL,4ULL,1ULL,12ULL};
if(int r=check<unsigned long,std::mt19937,true>(engine,clonedEngine,1,s31,6218382928632769141ULL,4088107650ULL))return r;
const unsigned long long s32[]={62ULL,74ULL,9ULL,58ULL,2ULL,27ULL,16ULL,15ULL,20ULL,1ULL,5ULL,12ULL,5ULL,15ULL,24ULL,3ULL};
if(int r=check<unsigned long,std::mt19937,true>(engine,clonedEngine,2,s32,1499821675373780215ULL,4165579583ULL))return r;
const unsigned long long s33[]={237ULL,308ULL,43ULL,239ULL,7ULL,84ULL,177ULL,6ULL,56ULL,83ULL,3ULL,67ULL,62ULL,10ULL,244ULL,46ULL};
if(int r=check<unsigned long,std::mt19937,true>(engine,clonedEngine,3,s33,12959414189069251016ULL,2045184021ULL))return r;
const unsigned long long s34[]={0ULL,0ULL,0ULL,0ULL,0ULL,1ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,1ULL,0ULL};
if(int r=check<unsigned long,std::mt19937,true>(engine,clonedEngine,4,s34,14077693768751643482ULL,586344725ULL))return r;
const unsigned long long s35[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long,std::mt19937,true>(engine,clonedEngine,5,s35,14734379229272918391ULL,3450120912ULL))return r;
return 0;}
