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
const unsigned long long s0[]={1ULL,0ULL,0ULL,0ULL,1ULL,1ULL,0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,1ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned,std::mt19937>(0,s0,6849162771071353920ULL,2399119966ULL))return r;
const unsigned long long s1[]={1ULL,0ULL,3ULL,1ULL,0ULL,1ULL,0ULL,1ULL,2ULL,1ULL,2ULL,1ULL,1ULL,2ULL,4ULL,4ULL};
if(int r=check<unsigned,std::mt19937>(1,s1,11804522084672340534ULL,638263793ULL))return r;
const unsigned long long s2[]={10ULL,8ULL,19ULL,11ULL,6ULL,9ULL,13ULL,5ULL,6ULL,10ULL,7ULL,18ULL,5ULL,4ULL,13ULL,16ULL};
if(int r=check<unsigned,std::mt19937>(2,s2,6837659149451351297ULL,551250884ULL))return r;
const unsigned long long s3[]={10ULL,9ULL,8ULL,5ULL,10ULL,10ULL,11ULL,11ULL,12ULL,14ULL,8ULL,11ULL,11ULL,8ULL,7ULL,6ULL};
if(int r=check<unsigned,std::mt19937>(3,s3,13070173361686489450ULL,3249598122ULL))return r;
const unsigned long long s4[]={12ULL,11ULL,10ULL,6ULL,12ULL,12ULL,14ULL,13ULL,15ULL,17ULL,10ULL,13ULL,13ULL,10ULL,8ULL,8ULL};
if(int r=check<unsigned,std::mt19937>(4,s4,12113209144918415959ULL,3594886183ULL))return r;
const unsigned long long s5[]={100ULL,99ULL,96ULL,95ULL,108ULL,89ULL,109ULL,115ULL,94ULL,100ULL,86ULL,95ULL,115ULL,102ULL,123ULL,115ULL};
if(int r=check<unsigned,std::mt19937>(5,s5,10936349034543489569ULL,3735243047ULL))return r;
const unsigned long long s6[]={1ULL,0ULL,0ULL,0ULL,1ULL,1ULL,0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,1ULL,0ULL,0ULL,0ULL};
if(int r=check<long,std::mt19937>(0,s6,6849162771071353920ULL,2399119966ULL))return r;
const unsigned long long s7[]={1ULL,0ULL,3ULL,1ULL,0ULL,1ULL,0ULL,1ULL,2ULL,1ULL,2ULL,1ULL,1ULL,2ULL,4ULL,4ULL};
if(int r=check<long,std::mt19937>(1,s7,11804522084672340534ULL,638263793ULL))return r;
const unsigned long long s8[]={10ULL,8ULL,19ULL,11ULL,6ULL,9ULL,13ULL,5ULL,6ULL,10ULL,7ULL,18ULL,5ULL,4ULL,13ULL,16ULL};
if(int r=check<long,std::mt19937>(2,s8,6837659149451351297ULL,551250884ULL))return r;
const unsigned long long s9[]={10ULL,9ULL,8ULL,5ULL,10ULL,10ULL,11ULL,11ULL,12ULL,14ULL,8ULL,11ULL,11ULL,8ULL,7ULL,6ULL};
if(int r=check<long,std::mt19937>(3,s9,13070173361686489450ULL,3249598122ULL))return r;
const unsigned long long s10[]={12ULL,11ULL,10ULL,6ULL,12ULL,12ULL,14ULL,13ULL,15ULL,17ULL,10ULL,13ULL,13ULL,10ULL,8ULL,8ULL};
if(int r=check<long,std::mt19937>(4,s10,12113209144918415959ULL,3594886183ULL))return r;
const unsigned long long s11[]={100ULL,99ULL,96ULL,95ULL,108ULL,89ULL,109ULL,115ULL,94ULL,100ULL,86ULL,95ULL,115ULL,102ULL,123ULL,115ULL};
if(int r=check<long,std::mt19937>(5,s11,10936349034543489569ULL,3735243047ULL))return r;
const unsigned long long s12[]={1ULL,0ULL,0ULL,0ULL,1ULL,1ULL,0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,1ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long,std::mt19937>(0,s12,6849162771071353920ULL,2399119966ULL))return r;
const unsigned long long s13[]={1ULL,0ULL,3ULL,1ULL,0ULL,1ULL,0ULL,1ULL,2ULL,1ULL,2ULL,1ULL,1ULL,2ULL,4ULL,4ULL};
if(int r=check<unsigned long,std::mt19937>(1,s13,11804522084672340534ULL,638263793ULL))return r;
const unsigned long long s14[]={10ULL,8ULL,19ULL,11ULL,6ULL,9ULL,13ULL,5ULL,6ULL,10ULL,7ULL,18ULL,5ULL,4ULL,13ULL,16ULL};
if(int r=check<unsigned long,std::mt19937>(2,s14,6837659149451351297ULL,551250884ULL))return r;
const unsigned long long s15[]={10ULL,9ULL,8ULL,5ULL,10ULL,10ULL,11ULL,11ULL,12ULL,14ULL,8ULL,11ULL,11ULL,8ULL,7ULL,6ULL};
if(int r=check<unsigned long,std::mt19937>(3,s15,13070173361686489450ULL,3249598122ULL))return r;
const unsigned long long s16[]={12ULL,11ULL,10ULL,6ULL,12ULL,12ULL,14ULL,13ULL,15ULL,17ULL,10ULL,13ULL,13ULL,10ULL,8ULL,8ULL};
if(int r=check<unsigned long,std::mt19937>(4,s16,12113209144918415959ULL,3594886183ULL))return r;
const unsigned long long s17[]={100ULL,99ULL,96ULL,95ULL,108ULL,89ULL,109ULL,115ULL,94ULL,100ULL,86ULL,95ULL,115ULL,102ULL,123ULL,115ULL};
if(int r=check<unsigned long,std::mt19937>(5,s17,10936349034543489569ULL,3735243047ULL))return r;
return 0;}
