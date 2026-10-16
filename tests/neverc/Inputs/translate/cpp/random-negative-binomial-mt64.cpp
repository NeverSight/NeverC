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
std::mt19937_64 engine(12345),clonedEngine;
const unsigned long long s0[]={0ULL,0ULL,3ULL,0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,2ULL,0ULL,0ULL,0ULL,0ULL,0ULL,3ULL};
if(int r=check<long long,std::mt19937_64,false>(engine,clonedEngine,0,s0,17354658625810395033ULL,5535228877224050495ULL))return r;
const unsigned long long s1[]={4ULL,35ULL,26ULL,34ULL,56ULL,11ULL,31ULL,3ULL,30ULL,103ULL,15ULL,79ULL,13ULL,83ULL,62ULL,144ULL};
if(int r=check<long long,std::mt19937_64,false>(engine,clonedEngine,1,s1,3499811628774607603ULL,16658965049324372747ULL))return r;
const unsigned long long s2[]={154ULL,183ULL,187ULL,210ULL,264ULL,145ULL,136ULL,300ULL,152ULL,199ULL,254ULL,131ULL,205ULL,151ULL,343ULL,285ULL};
if(int r=check<long long,std::mt19937_64,false>(engine,clonedEngine,2,s2,9480114974650032575ULL,17485866292593886566ULL))return r;
const unsigned long long s3[]={1348ULL,1581ULL,1624ULL,1752ULL,1986ULL,686ULL,1285ULL,2269ULL,1758ULL,2198ULL,1314ULL,1308ULL,1722ULL,1437ULL,2545ULL,2145ULL};
if(int r=check<long long,std::mt19937_64,false>(engine,clonedEngine,3,s3,15393524052231181355ULL,2965101985354176365ULL))return r;
const unsigned long long s4[]={0ULL,0ULL,0ULL,0ULL,2ULL,2ULL,0ULL,1ULL,1ULL,0ULL,0ULL,0ULL,2ULL,2ULL,0ULL,1ULL};
if(int r=check<long long,std::mt19937_64,false>(engine,clonedEngine,4,s4,1563820074432342525ULL,14561100528736045758ULL))return r;
const unsigned long long s5[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<long long,std::mt19937_64,false>(engine,clonedEngine,5,s5,14734379229272918391ULL,2576532358858198986ULL))return r;
const unsigned long long s6[]={0ULL,0ULL,3ULL,0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,2ULL,0ULL,0ULL,0ULL,0ULL,0ULL,3ULL};
if(int r=check<long long,std::mt19937_64,true>(engine,clonedEngine,0,s6,17354658625810395033ULL,5535228877224050495ULL))return r;
const unsigned long long s7[]={6ULL,3ULL,0ULL,5ULL,2ULL,6ULL,55ULL,4ULL,16ULL,5ULL,15ULL,43ULL,5ULL,34ULL,83ULL,20ULL};
if(int r=check<long long,std::mt19937_64,true>(engine,clonedEngine,1,s7,7035563507870564763ULL,9522731665029181270ULL))return r;
const unsigned long long s8[]={7ULL,29ULL,1ULL,13ULL,24ULL,0ULL,49ULL,43ULL,22ULL,8ULL,16ULL,8ULL,5ULL,17ULL,88ULL,29ULL};
if(int r=check<long long,std::mt19937_64,true>(engine,clonedEngine,2,s8,17450953216364172833ULL,17055971420440782719ULL))return r;
const unsigned long long s9[]={38ULL,102ULL,16ULL,77ULL,0ULL,208ULL,162ULL,97ULL,27ULL,109ULL,196ULL,65ULL,44ULL,14ULL,55ULL,67ULL};
if(int r=check<long long,std::mt19937_64,true>(engine,clonedEngine,3,s9,14875083886188323363ULL,234368890247493811ULL))return r;
const unsigned long long s10[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<long long,std::mt19937_64,true>(engine,clonedEngine,4,s10,989609055247632689ULL,1965820711352110893ULL))return r;
const unsigned long long s11[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<long long,std::mt19937_64,true>(engine,clonedEngine,5,s11,14734379229272918391ULL,14270278433167030962ULL))return r;
const unsigned long long s12[]={0ULL,0ULL,3ULL,0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,2ULL,0ULL,0ULL,0ULL,0ULL,0ULL,3ULL};
if(int r=check<unsigned long long,std::mt19937_64,false>(engine,clonedEngine,0,s12,17354658625810395033ULL,5535228877224050495ULL))return r;
const unsigned long long s13[]={4ULL,35ULL,26ULL,34ULL,56ULL,11ULL,31ULL,3ULL,30ULL,103ULL,15ULL,79ULL,13ULL,83ULL,62ULL,144ULL};
if(int r=check<unsigned long long,std::mt19937_64,false>(engine,clonedEngine,1,s13,3499811628774607603ULL,16658965049324372747ULL))return r;
const unsigned long long s14[]={154ULL,183ULL,187ULL,210ULL,264ULL,145ULL,136ULL,300ULL,152ULL,199ULL,254ULL,131ULL,205ULL,151ULL,343ULL,285ULL};
if(int r=check<unsigned long long,std::mt19937_64,false>(engine,clonedEngine,2,s14,9480114974650032575ULL,17485866292593886566ULL))return r;
const unsigned long long s15[]={1348ULL,1581ULL,1624ULL,1752ULL,1986ULL,686ULL,1285ULL,2269ULL,1758ULL,2198ULL,1314ULL,1308ULL,1722ULL,1437ULL,2545ULL,2145ULL};
if(int r=check<unsigned long long,std::mt19937_64,false>(engine,clonedEngine,3,s15,15393524052231181355ULL,2965101985354176365ULL))return r;
const unsigned long long s16[]={0ULL,0ULL,0ULL,0ULL,2ULL,2ULL,0ULL,1ULL,1ULL,0ULL,0ULL,0ULL,2ULL,2ULL,0ULL,1ULL};
if(int r=check<unsigned long long,std::mt19937_64,false>(engine,clonedEngine,4,s16,1563820074432342525ULL,14561100528736045758ULL))return r;
const unsigned long long s17[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long long,std::mt19937_64,false>(engine,clonedEngine,5,s17,14734379229272918391ULL,2576532358858198986ULL))return r;
const unsigned long long s18[]={0ULL,0ULL,3ULL,0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,2ULL,0ULL,0ULL,0ULL,0ULL,0ULL,3ULL};
if(int r=check<unsigned long long,std::mt19937_64,true>(engine,clonedEngine,0,s18,17354658625810395033ULL,5535228877224050495ULL))return r;
const unsigned long long s19[]={6ULL,3ULL,0ULL,5ULL,2ULL,6ULL,55ULL,4ULL,16ULL,5ULL,15ULL,43ULL,5ULL,34ULL,83ULL,20ULL};
if(int r=check<unsigned long long,std::mt19937_64,true>(engine,clonedEngine,1,s19,7035563507870564763ULL,9522731665029181270ULL))return r;
const unsigned long long s20[]={7ULL,29ULL,1ULL,13ULL,24ULL,0ULL,49ULL,43ULL,22ULL,8ULL,16ULL,8ULL,5ULL,17ULL,88ULL,29ULL};
if(int r=check<unsigned long long,std::mt19937_64,true>(engine,clonedEngine,2,s20,17450953216364172833ULL,17055971420440782719ULL))return r;
const unsigned long long s21[]={38ULL,102ULL,16ULL,77ULL,0ULL,208ULL,162ULL,97ULL,27ULL,109ULL,196ULL,65ULL,44ULL,14ULL,55ULL,67ULL};
if(int r=check<unsigned long long,std::mt19937_64,true>(engine,clonedEngine,3,s21,14875083886188323363ULL,234368890247493811ULL))return r;
const unsigned long long s22[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long long,std::mt19937_64,true>(engine,clonedEngine,4,s22,989609055247632689ULL,1965820711352110893ULL))return r;
const unsigned long long s23[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long long,std::mt19937_64,true>(engine,clonedEngine,5,s23,14734379229272918391ULL,14270278433167030962ULL))return r;
return 0;}
