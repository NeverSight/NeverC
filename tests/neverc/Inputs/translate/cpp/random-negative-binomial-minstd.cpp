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
std::minstd_rand engine(12345),clonedEngine;
const unsigned long long s0[]={3ULL,1ULL,0ULL,0ULL,0ULL,2ULL,0ULL,3ULL,1ULL,1ULL,2ULL,5ULL,3ULL,0ULL,0ULL,1ULL};
if(int r=check<short,std::minstd_rand,false>(engine,clonedEngine,0,s0,11630228489970860001ULL,80481031ULL))return r;
const unsigned long long s1[]={51ULL,30ULL,71ULL,78ULL,30ULL,37ULL,18ULL,19ULL,80ULL,33ULL,42ULL,34ULL,96ULL,65ULL,26ULL,25ULL};
if(int r=check<short,std::minstd_rand,false>(engine,clonedEngine,1,s1,3600563393288887699ULL,1347022289ULL))return r;
const unsigned long long s2[]={250ULL,199ULL,114ULL,258ULL,198ULL,216ULL,166ULL,171ULL,83ULL,163ULL,187ULL,281ULL,209ULL,165ULL,172ULL,295ULL};
if(int r=check<short,std::minstd_rand,false>(engine,clonedEngine,2,s2,1691154831264414503ULL,1992663841ULL))return r;
const unsigned long long s3[]={1897ULL,1693ULL,1151ULL,1988ULL,1666ULL,1760ULL,1478ULL,1468ULL,820ULL,2446ULL,1725ULL,1854ULL,1721ULL,2698ULL,968ULL,1129ULL};
if(int r=check<short,std::minstd_rand,false>(engine,clonedEngine,3,s3,6912097119660413101ULL,586094878ULL))return r;
const unsigned long long s4[]={1ULL,0ULL,0ULL,0ULL,0ULL,3ULL,1ULL,0ULL,1ULL,2ULL,0ULL,2ULL,0ULL,1ULL,0ULL,0ULL};
if(int r=check<short,std::minstd_rand,false>(engine,clonedEngine,4,s4,15420027252971923125ULL,2108859696ULL))return r;
const unsigned long long s5[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<short,std::minstd_rand,false>(engine,clonedEngine,5,s5,14734379229272918391ULL,790023215ULL))return r;
const unsigned long long s6[]={3ULL,1ULL,0ULL,0ULL,0ULL,2ULL,0ULL,3ULL,1ULL,1ULL,2ULL,5ULL,3ULL,0ULL,0ULL,1ULL};
if(int r=check<short,std::minstd_rand,true>(engine,clonedEngine,0,s6,11630228489970860001ULL,80481031ULL))return r;
const unsigned long long s7[]={5ULL,12ULL,10ULL,11ULL,32ULL,59ULL,32ULL,9ULL,28ULL,22ULL,12ULL,2ULL,4ULL,15ULL,38ULL,11ULL};
if(int r=check<short,std::minstd_rand,true>(engine,clonedEngine,1,s7,17916391909972320828ULL,200658905ULL))return r;
const unsigned long long s8[]={32ULL,10ULL,8ULL,31ULL,33ULL,80ULL,56ULL,59ULL,44ULL,51ULL,10ULL,54ULL,28ULL,83ULL,24ULL,16ULL};
if(int r=check<short,std::minstd_rand,true>(engine,clonedEngine,2,s8,8773703985153214236ULL,1399710100ULL))return r;
const unsigned long long s9[]={130ULL,48ULL,34ULL,46ULL,112ULL,117ULL,317ULL,238ULL,249ULL,61ULL,45ULL,62ULL,115ULL,350ULL,117ULL,87ULL};
if(int r=check<short,std::minstd_rand,true>(engine,clonedEngine,3,s9,11441549648834365598ULL,1968366323ULL))return r;
const unsigned long long s10[]={0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<short,std::minstd_rand,true>(engine,clonedEngine,4,s10,6351446828215089345ULL,1456303317ULL))return r;
const unsigned long long s11[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<short,std::minstd_rand,true>(engine,clonedEngine,5,s11,14734379229272918391ULL,1475859482ULL))return r;
const unsigned long long s12[]={3ULL,1ULL,0ULL,0ULL,0ULL,2ULL,0ULL,3ULL,1ULL,1ULL,2ULL,5ULL,3ULL,0ULL,0ULL,1ULL};
if(int r=check<unsigned short,std::minstd_rand,false>(engine,clonedEngine,0,s12,11630228489970860001ULL,80481031ULL))return r;
const unsigned long long s13[]={51ULL,30ULL,71ULL,78ULL,30ULL,37ULL,18ULL,19ULL,80ULL,33ULL,42ULL,34ULL,96ULL,65ULL,26ULL,25ULL};
if(int r=check<unsigned short,std::minstd_rand,false>(engine,clonedEngine,1,s13,3600563393288887699ULL,1347022289ULL))return r;
const unsigned long long s14[]={250ULL,199ULL,114ULL,258ULL,198ULL,216ULL,166ULL,171ULL,83ULL,163ULL,187ULL,281ULL,209ULL,165ULL,172ULL,295ULL};
if(int r=check<unsigned short,std::minstd_rand,false>(engine,clonedEngine,2,s14,1691154831264414503ULL,1992663841ULL))return r;
const unsigned long long s15[]={1897ULL,1693ULL,1151ULL,1988ULL,1666ULL,1760ULL,1478ULL,1468ULL,820ULL,2446ULL,1725ULL,1854ULL,1721ULL,2698ULL,968ULL,1129ULL};
if(int r=check<unsigned short,std::minstd_rand,false>(engine,clonedEngine,3,s15,6912097119660413101ULL,586094878ULL))return r;
const unsigned long long s16[]={1ULL,0ULL,0ULL,0ULL,0ULL,3ULL,1ULL,0ULL,1ULL,2ULL,0ULL,2ULL,0ULL,1ULL,0ULL,0ULL};
if(int r=check<unsigned short,std::minstd_rand,false>(engine,clonedEngine,4,s16,15420027252971923125ULL,2108859696ULL))return r;
const unsigned long long s17[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned short,std::minstd_rand,false>(engine,clonedEngine,5,s17,14734379229272918391ULL,790023215ULL))return r;
const unsigned long long s18[]={3ULL,1ULL,0ULL,0ULL,0ULL,2ULL,0ULL,3ULL,1ULL,1ULL,2ULL,5ULL,3ULL,0ULL,0ULL,1ULL};
if(int r=check<unsigned short,std::minstd_rand,true>(engine,clonedEngine,0,s18,11630228489970860001ULL,80481031ULL))return r;
const unsigned long long s19[]={5ULL,12ULL,10ULL,11ULL,32ULL,59ULL,32ULL,9ULL,28ULL,22ULL,12ULL,2ULL,4ULL,15ULL,38ULL,11ULL};
if(int r=check<unsigned short,std::minstd_rand,true>(engine,clonedEngine,1,s19,17916391909972320828ULL,200658905ULL))return r;
const unsigned long long s20[]={32ULL,10ULL,8ULL,31ULL,33ULL,80ULL,56ULL,59ULL,44ULL,51ULL,10ULL,54ULL,28ULL,83ULL,24ULL,16ULL};
if(int r=check<unsigned short,std::minstd_rand,true>(engine,clonedEngine,2,s20,8773703985153214236ULL,1399710100ULL))return r;
const unsigned long long s21[]={130ULL,48ULL,34ULL,46ULL,112ULL,117ULL,317ULL,238ULL,249ULL,61ULL,45ULL,62ULL,115ULL,350ULL,117ULL,87ULL};
if(int r=check<unsigned short,std::minstd_rand,true>(engine,clonedEngine,3,s21,11441549648834365598ULL,1968366323ULL))return r;
const unsigned long long s22[]={0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned short,std::minstd_rand,true>(engine,clonedEngine,4,s22,6351446828215089345ULL,1456303317ULL))return r;
const unsigned long long s23[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned short,std::minstd_rand,true>(engine,clonedEngine,5,s23,14734379229272918391ULL,1475859482ULL))return r;
const unsigned long long s24[]={3ULL,1ULL,0ULL,0ULL,0ULL,2ULL,0ULL,3ULL,1ULL,1ULL,2ULL,5ULL,3ULL,0ULL,0ULL,1ULL};
if(int r=check<int,std::minstd_rand,false>(engine,clonedEngine,0,s24,11630228489970860001ULL,80481031ULL))return r;
const unsigned long long s25[]={51ULL,30ULL,71ULL,78ULL,30ULL,37ULL,18ULL,19ULL,80ULL,33ULL,42ULL,34ULL,96ULL,65ULL,26ULL,25ULL};
if(int r=check<int,std::minstd_rand,false>(engine,clonedEngine,1,s25,3600563393288887699ULL,1347022289ULL))return r;
const unsigned long long s26[]={250ULL,199ULL,114ULL,258ULL,198ULL,216ULL,166ULL,171ULL,83ULL,163ULL,187ULL,281ULL,209ULL,165ULL,172ULL,295ULL};
if(int r=check<int,std::minstd_rand,false>(engine,clonedEngine,2,s26,1691154831264414503ULL,1992663841ULL))return r;
const unsigned long long s27[]={1897ULL,1693ULL,1151ULL,1988ULL,1666ULL,1760ULL,1478ULL,1468ULL,820ULL,2446ULL,1725ULL,1854ULL,1721ULL,2698ULL,968ULL,1129ULL};
if(int r=check<int,std::minstd_rand,false>(engine,clonedEngine,3,s27,6912097119660413101ULL,586094878ULL))return r;
const unsigned long long s28[]={1ULL,0ULL,0ULL,0ULL,0ULL,3ULL,1ULL,0ULL,1ULL,2ULL,0ULL,2ULL,0ULL,1ULL,0ULL,0ULL};
if(int r=check<int,std::minstd_rand,false>(engine,clonedEngine,4,s28,15420027252971923125ULL,2108859696ULL))return r;
const unsigned long long s29[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<int,std::minstd_rand,false>(engine,clonedEngine,5,s29,14734379229272918391ULL,790023215ULL))return r;
const unsigned long long s30[]={3ULL,1ULL,0ULL,0ULL,0ULL,2ULL,0ULL,3ULL,1ULL,1ULL,2ULL,5ULL,3ULL,0ULL,0ULL,1ULL};
if(int r=check<int,std::minstd_rand,true>(engine,clonedEngine,0,s30,11630228489970860001ULL,80481031ULL))return r;
const unsigned long long s31[]={5ULL,12ULL,10ULL,11ULL,32ULL,59ULL,32ULL,9ULL,28ULL,22ULL,12ULL,2ULL,4ULL,15ULL,38ULL,11ULL};
if(int r=check<int,std::minstd_rand,true>(engine,clonedEngine,1,s31,17916391909972320828ULL,200658905ULL))return r;
const unsigned long long s32[]={32ULL,10ULL,8ULL,31ULL,33ULL,80ULL,56ULL,59ULL,44ULL,51ULL,10ULL,54ULL,28ULL,83ULL,24ULL,16ULL};
if(int r=check<int,std::minstd_rand,true>(engine,clonedEngine,2,s32,8773703985153214236ULL,1399710100ULL))return r;
const unsigned long long s33[]={130ULL,48ULL,34ULL,46ULL,112ULL,117ULL,317ULL,238ULL,249ULL,61ULL,45ULL,62ULL,115ULL,350ULL,117ULL,87ULL};
if(int r=check<int,std::minstd_rand,true>(engine,clonedEngine,3,s33,11441549648834365598ULL,1968366323ULL))return r;
const unsigned long long s34[]={0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<int,std::minstd_rand,true>(engine,clonedEngine,4,s34,6351446828215089345ULL,1456303317ULL))return r;
const unsigned long long s35[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<int,std::minstd_rand,true>(engine,clonedEngine,5,s35,14734379229272918391ULL,1475859482ULL))return r;
return 0;}
