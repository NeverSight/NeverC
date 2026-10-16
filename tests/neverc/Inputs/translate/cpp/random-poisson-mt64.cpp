#include <random>
template<class I,class E>int check(unsigned k,const unsigned long long*expected,unsigned long long hashExpected,unsigned long long next){
using D=std::poisson_distribution<I>;using P=typename D::param_type;const double means[]={.125,1.,9.999,10.,12.,100.};
P initial;D defaults;if(initial.mean()!=1.||defaults.mean()!=1.)return 1;
P p(means[k]),pc(p),pm(static_cast<P&&>(p)),pa;P*pr=&(pa=p);P*selfP=&(pa=pa);D d(p),direct(means[k]),copied(d),moved(static_cast<D&&>(copied)),assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.param(p);assigned.reset();
if(pr!=&pa||selfP!=&pa||dr!=&assigned||selfD!=&assigned||pa!=p||pc!=p||pm!=p||d!=direct||moved!=d||assigned.param()!=p||assigned!=d||d.mean()!=means[k])return 2;
unsigned long long maximum=~0ULL>>(64-sizeof(I)*8+(I(-1)<I(0)?1:0));if(d.min()!=I(0)||d.max()!=static_cast<I>(maximum))return 3;
E engine(12345);unsigned long long hash=14695981039346656037ULL;for(unsigned i=0;i<10003;++i){I x=(i&1)?assigned(engine,p):assigned(engine);hash=(hash^static_cast<unsigned long long>(x))*1099511628211ULL;if(i<16&&static_cast<unsigned long long>(x)!=expected[i])return 4;}
if(hash!=hashExpected)return 5;D clone(assigned);E clonedEngine(engine);I a=assigned(engine),b=clone(clonedEngine);if(a!=b||engine!=clonedEngine)return 6;if(static_cast<unsigned long long>(engine())!=next)return 7;
P changed(2.5);assigned.param(changed);if(assigned.mean()!=2.5||assigned.param()!=changed||assigned==d)return 8;assigned.reset();if(assigned.mean()!=2.5)return 9;return 0;}
int main(){
const unsigned long long s0[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,1ULL,0ULL};
if(int r=check<long long,std::mt19937_64>(0,s0,12729303156372098147ULL,7033949753266590983ULL))return r;
const unsigned long long s1[]={0ULL,1ULL,1ULL,0ULL,0ULL,1ULL,0ULL,0ULL,0ULL,1ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<long long,std::mt19937_64>(1,s1,15429556188659185149ULL,6495065152994363606ULL))return r;
const unsigned long long s2[]={9ULL,2ULL,7ULL,12ULL,11ULL,11ULL,9ULL,14ULL,5ULL,10ULL,8ULL,11ULL,6ULL,14ULL,9ULL,15ULL};
if(int r=check<long long,std::mt19937_64>(2,s2,5538711151355656014ULL,5386154764115179515ULL))return r;
const unsigned long long s3[]={4ULL,15ULL,16ULL,9ULL,10ULL,8ULL,11ULL,12ULL,14ULL,9ULL,10ULL,13ULL,14ULL,9ULL,8ULL,10ULL};
if(int r=check<long long,std::mt19937_64>(3,s3,14887479624542355613ULL,819805261416319501ULL))return r;
const unsigned long long s4[]={6ULL,17ULL,18ULL,11ULL,12ULL,10ULL,13ULL,14ULL,16ULL,11ULL,12ULL,15ULL,16ULL,11ULL,10ULL,12ULL};
if(int r=check<long long,std::mt19937_64>(4,s4,17624252272589175667ULL,14988786739000880095ULL))return r;
const unsigned long long s5[]={83ULL,116ULL,119ULL,97ULL,108ULL,134ULL,98ULL,102ULL,110ULL,114ULL,97ULL,87ULL,96ULL,99ULL,97ULL,96ULL};
if(int r=check<long long,std::mt19937_64>(5,s5,17642097373820460498ULL,1405890961538963210ULL))return r;
const unsigned long long s6[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,1ULL,0ULL};
if(int r=check<unsigned long long,std::mt19937_64>(0,s6,12729303156372098147ULL,7033949753266590983ULL))return r;
const unsigned long long s7[]={0ULL,1ULL,1ULL,0ULL,0ULL,1ULL,0ULL,0ULL,0ULL,1ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long long,std::mt19937_64>(1,s7,15429556188659185149ULL,6495065152994363606ULL))return r;
const unsigned long long s8[]={9ULL,2ULL,7ULL,12ULL,11ULL,11ULL,9ULL,14ULL,5ULL,10ULL,8ULL,11ULL,6ULL,14ULL,9ULL,15ULL};
if(int r=check<unsigned long long,std::mt19937_64>(2,s8,5538711151355656014ULL,5386154764115179515ULL))return r;
const unsigned long long s9[]={4ULL,15ULL,16ULL,9ULL,10ULL,8ULL,11ULL,12ULL,14ULL,9ULL,10ULL,13ULL,14ULL,9ULL,8ULL,10ULL};
if(int r=check<unsigned long long,std::mt19937_64>(3,s9,14887479624542355613ULL,819805261416319501ULL))return r;
const unsigned long long s10[]={6ULL,17ULL,18ULL,11ULL,12ULL,10ULL,13ULL,14ULL,16ULL,11ULL,12ULL,15ULL,16ULL,11ULL,10ULL,12ULL};
if(int r=check<unsigned long long,std::mt19937_64>(4,s10,17624252272589175667ULL,14988786739000880095ULL))return r;
const unsigned long long s11[]={83ULL,116ULL,119ULL,97ULL,108ULL,134ULL,98ULL,102ULL,110ULL,114ULL,97ULL,87ULL,96ULL,99ULL,97ULL,96ULL};
if(int r=check<unsigned long long,std::mt19937_64>(5,s11,17642097373820460498ULL,1405890961538963210ULL))return r;
return 0;}
