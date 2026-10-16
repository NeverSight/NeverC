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
const unsigned long long s0[]={0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<short,std::minstd_rand>(0,s0,12554578273821219282ULL,1282012500ULL))return r;
const unsigned long long s1[]={3ULL,1ULL,0ULL,0ULL,0ULL,2ULL,0ULL,3ULL,1ULL,1ULL,2ULL,2ULL,2ULL,3ULL,1ULL,2ULL};
if(int r=check<short,std::minstd_rand>(1,s1,9786349318384526911ULL,946636033ULL))return r;
const unsigned long long s2[]={7ULL,10ULL,13ULL,11ULL,10ULL,14ULL,4ULL,10ULL,11ULL,16ULL,14ULL,12ULL,17ULL,6ULL,10ULL,12ULL};
if(int r=check<short,std::minstd_rand>(2,s2,9824428395814553562ULL,533627121ULL))return r;
const unsigned long long s3[]={10ULL,15ULL,8ULL,12ULL,12ULL,9ULL,5ULL,9ULL,16ULL,10ULL,9ULL,9ULL,9ULL,13ULL,8ULL,6ULL};
if(int r=check<short,std::minstd_rand>(3,s3,11606016384640932294ULL,1441803390ULL))return r;
const unsigned long long s4[]={12ULL,17ULL,10ULL,14ULL,14ULL,10ULL,7ULL,11ULL,18ULL,12ULL,11ULL,11ULL,11ULL,16ULL,10ULL,8ULL};
if(int r=check<short,std::minstd_rand>(4,s4,7013764737522651305ULL,1029697597ULL))return r;
const unsigned long long s5[]={100ULL,116ULL,94ULL,96ULL,106ULL,96ULL,105ULL,105ULL,99ULL,120ULL,102ULL,97ULL,108ULL,99ULL,103ULL,98ULL};
if(int r=check<short,std::minstd_rand>(5,s5,12240483046000366692ULL,137608508ULL))return r;
const unsigned long long s6[]={0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned short,std::minstd_rand>(0,s6,12554578273821219282ULL,1282012500ULL))return r;
const unsigned long long s7[]={3ULL,1ULL,0ULL,0ULL,0ULL,2ULL,0ULL,3ULL,1ULL,1ULL,2ULL,2ULL,2ULL,3ULL,1ULL,2ULL};
if(int r=check<unsigned short,std::minstd_rand>(1,s7,9786349318384526911ULL,946636033ULL))return r;
const unsigned long long s8[]={7ULL,10ULL,13ULL,11ULL,10ULL,14ULL,4ULL,10ULL,11ULL,16ULL,14ULL,12ULL,17ULL,6ULL,10ULL,12ULL};
if(int r=check<unsigned short,std::minstd_rand>(2,s8,9824428395814553562ULL,533627121ULL))return r;
const unsigned long long s9[]={10ULL,15ULL,8ULL,12ULL,12ULL,9ULL,5ULL,9ULL,16ULL,10ULL,9ULL,9ULL,9ULL,13ULL,8ULL,6ULL};
if(int r=check<unsigned short,std::minstd_rand>(3,s9,11606016384640932294ULL,1441803390ULL))return r;
const unsigned long long s10[]={12ULL,17ULL,10ULL,14ULL,14ULL,10ULL,7ULL,11ULL,18ULL,12ULL,11ULL,11ULL,11ULL,16ULL,10ULL,8ULL};
if(int r=check<unsigned short,std::minstd_rand>(4,s10,7013764737522651305ULL,1029697597ULL))return r;
const unsigned long long s11[]={100ULL,116ULL,94ULL,96ULL,106ULL,96ULL,105ULL,105ULL,99ULL,120ULL,102ULL,97ULL,108ULL,99ULL,103ULL,98ULL};
if(int r=check<unsigned short,std::minstd_rand>(5,s11,12240483046000366692ULL,137608508ULL))return r;
const unsigned long long s12[]={0ULL,1ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<int,std::minstd_rand>(0,s12,12554578273821219282ULL,1282012500ULL))return r;
const unsigned long long s13[]={3ULL,1ULL,0ULL,0ULL,0ULL,2ULL,0ULL,3ULL,1ULL,1ULL,2ULL,2ULL,2ULL,3ULL,1ULL,2ULL};
if(int r=check<int,std::minstd_rand>(1,s13,9786349318384526911ULL,946636033ULL))return r;
const unsigned long long s14[]={7ULL,10ULL,13ULL,11ULL,10ULL,14ULL,4ULL,10ULL,11ULL,16ULL,14ULL,12ULL,17ULL,6ULL,10ULL,12ULL};
if(int r=check<int,std::minstd_rand>(2,s14,9824428395814553562ULL,533627121ULL))return r;
const unsigned long long s15[]={10ULL,15ULL,8ULL,12ULL,12ULL,9ULL,5ULL,9ULL,16ULL,10ULL,9ULL,9ULL,9ULL,13ULL,8ULL,6ULL};
if(int r=check<int,std::minstd_rand>(3,s15,11606016384640932294ULL,1441803390ULL))return r;
const unsigned long long s16[]={12ULL,17ULL,10ULL,14ULL,14ULL,10ULL,7ULL,11ULL,18ULL,12ULL,11ULL,11ULL,11ULL,16ULL,10ULL,8ULL};
if(int r=check<int,std::minstd_rand>(4,s16,7013764737522651305ULL,1029697597ULL))return r;
const unsigned long long s17[]={100ULL,116ULL,94ULL,96ULL,106ULL,96ULL,105ULL,105ULL,99ULL,120ULL,102ULL,97ULL,108ULL,99ULL,103ULL,98ULL};
if(int r=check<int,std::minstd_rand>(5,s17,12240483046000366692ULL,137608508ULL))return r;
return 0;}
