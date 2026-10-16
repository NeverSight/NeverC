#include <random>
template<class I,class E>int check(unsigned k,const unsigned long long*expected,unsigned long long hashExpected,unsigned long long next){using D=std::binomial_distribution<I>;using P=typename D::param_type;const unsigned trials[]={0,1,10,25,100,12,12,12};const double probabilities[]={.5,.5,.2,.5,.9,0.,1.,.999};
P initial;D defaults;P first(I(4));D outerFirst(I(4));if(initial.t()!=I(1)||initial.p()!=.5||defaults.t()!=I(1)||defaults.p()!=.5||first.p()!=.5||outerFirst.p()!=.5)return 1;
P p(static_cast<I>(trials[k]),probabilities[k]),pc(p),pm(static_cast<P&&>(p)),pa;P*pr=&(pa=p);P*selfP=&(pa=pa);D d(p),copied(d),moved(static_cast<D&&>(copied)),assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.param(p);assigned.reset();
if(pr!=&pa||selfP!=&pa||dr!=&assigned||selfD!=&assigned||pa!=p||pc!=p||pm!=p||moved!=d||assigned.param()!=p||assigned!=d||d.p()!=probabilities[k]||d.t()!=static_cast<I>(trials[k]))return 2;
if(d.min()!=I(0)||d.max()!=static_cast<I>(trials[k]))return 3;E engine(12345);unsigned long long hash=14695981039346656037ULL;for(unsigned i=0;i<10003;++i){I x=(i&1)?assigned(engine,p):assigned(engine);hash=(hash^static_cast<unsigned long long>(x))*1099511628211ULL;if(i<16&&static_cast<unsigned long long>(x)!=expected[i])return 4;}
if(hash!=hashExpected)return 5;D clone(assigned);E clonedEngine(engine);I a=assigned(engine),b=clone(clonedEngine);if(a!=b||engine!=clonedEngine)return 6;if(static_cast<unsigned long long>(engine())!=next)return 7;
P changed(I(2),.75);assigned.param(changed);if(assigned.t()!=I(2)||assigned.p()!=.75||assigned.max()!=I(2)||assigned.param()!=changed||assigned==d)return 8;assigned.reset();return assigned.t()!=I(2)||assigned.p()!=.75?9:0;}
int main(){
const unsigned long long s0[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned,std::mt19937>(0,s0,14734379229272918391ULL,3992670690ULL))return r;
const unsigned long long s1[]={0ULL,1ULL,1ULL,0ULL,0ULL,0ULL,1ULL,0ULL,1ULL,1ULL,0ULL,1ULL,1ULL,1ULL,0ULL,0ULL};
if(int r=check<unsigned,std::mt19937>(1,s1,16389333779705775044ULL,3450120912ULL))return r;
const unsigned long long s2[]={4ULL,2ULL,2ULL,0ULL,1ULL,4ULL,1ULL,4ULL,1ULL,2ULL,4ULL,2ULL,2ULL,1ULL,0ULL,3ULL};
if(int r=check<unsigned,std::mt19937>(2,s2,6219556318406389013ULL,3450120912ULL))return r;
const unsigned long long s3[]={9ULL,13ULL,13ULL,16ULL,11ULL,8ULL,11ULL,17ULL,14ULL,13ULL,17ULL,13ULL,12ULL,14ULL,16ULL,15ULL};
if(int r=check<unsigned,std::mt19937>(3,s3,2140142169299371188ULL,3450120912ULL))return r;
const unsigned long long s4[]={85ULL,90ULL,90ULL,94ULL,92ULL,96ULL,88ULL,95ULL,91ULL,89ULL,85ULL,90ULL,91ULL,88ULL,86ULL,87ULL};
if(int r=check<unsigned,std::mt19937>(4,s4,4859419837213511735ULL,3450120912ULL))return r;
const unsigned long long s5[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned,std::mt19937>(5,s5,14734379229272918391ULL,3992670690ULL))return r;
const unsigned long long s6[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<unsigned,std::mt19937>(6,s6,10552691409469378443ULL,3992670690ULL))return r;
const unsigned long long s7[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<unsigned,std::mt19937>(7,s7,2820603958207141006ULL,3450120912ULL))return r;
const unsigned long long s8[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<long,std::mt19937>(0,s8,14734379229272918391ULL,3992670690ULL))return r;
const unsigned long long s9[]={0ULL,1ULL,1ULL,0ULL,0ULL,0ULL,1ULL,0ULL,1ULL,1ULL,0ULL,1ULL,1ULL,1ULL,0ULL,0ULL};
if(int r=check<long,std::mt19937>(1,s9,16389333779705775044ULL,3450120912ULL))return r;
const unsigned long long s10[]={4ULL,2ULL,2ULL,0ULL,1ULL,4ULL,1ULL,4ULL,1ULL,2ULL,4ULL,2ULL,2ULL,1ULL,0ULL,3ULL};
if(int r=check<long,std::mt19937>(2,s10,6219556318406389013ULL,3450120912ULL))return r;
const unsigned long long s11[]={9ULL,13ULL,13ULL,16ULL,11ULL,8ULL,11ULL,17ULL,14ULL,13ULL,17ULL,13ULL,12ULL,14ULL,16ULL,15ULL};
if(int r=check<long,std::mt19937>(3,s11,2140142169299371188ULL,3450120912ULL))return r;
const unsigned long long s12[]={85ULL,90ULL,90ULL,94ULL,92ULL,96ULL,88ULL,95ULL,91ULL,89ULL,85ULL,90ULL,91ULL,88ULL,86ULL,87ULL};
if(int r=check<long,std::mt19937>(4,s12,4859419837213511735ULL,3450120912ULL))return r;
const unsigned long long s13[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<long,std::mt19937>(5,s13,14734379229272918391ULL,3992670690ULL))return r;
const unsigned long long s14[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<long,std::mt19937>(6,s14,10552691409469378443ULL,3992670690ULL))return r;
const unsigned long long s15[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<long,std::mt19937>(7,s15,2820603958207141006ULL,3450120912ULL))return r;
const unsigned long long s16[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long,std::mt19937>(0,s16,14734379229272918391ULL,3992670690ULL))return r;
const unsigned long long s17[]={0ULL,1ULL,1ULL,0ULL,0ULL,0ULL,1ULL,0ULL,1ULL,1ULL,0ULL,1ULL,1ULL,1ULL,0ULL,0ULL};
if(int r=check<unsigned long,std::mt19937>(1,s17,16389333779705775044ULL,3450120912ULL))return r;
const unsigned long long s18[]={4ULL,2ULL,2ULL,0ULL,1ULL,4ULL,1ULL,4ULL,1ULL,2ULL,4ULL,2ULL,2ULL,1ULL,0ULL,3ULL};
if(int r=check<unsigned long,std::mt19937>(2,s18,6219556318406389013ULL,3450120912ULL))return r;
const unsigned long long s19[]={9ULL,13ULL,13ULL,16ULL,11ULL,8ULL,11ULL,17ULL,14ULL,13ULL,17ULL,13ULL,12ULL,14ULL,16ULL,15ULL};
if(int r=check<unsigned long,std::mt19937>(3,s19,2140142169299371188ULL,3450120912ULL))return r;
const unsigned long long s20[]={85ULL,90ULL,90ULL,94ULL,92ULL,96ULL,88ULL,95ULL,91ULL,89ULL,85ULL,90ULL,91ULL,88ULL,86ULL,87ULL};
if(int r=check<unsigned long,std::mt19937>(4,s20,4859419837213511735ULL,3450120912ULL))return r;
const unsigned long long s21[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long,std::mt19937>(5,s21,14734379229272918391ULL,3992670690ULL))return r;
const unsigned long long s22[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<unsigned long,std::mt19937>(6,s22,10552691409469378443ULL,3992670690ULL))return r;
const unsigned long long s23[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<unsigned long,std::mt19937>(7,s23,2820603958207141006ULL,3450120912ULL))return r;
return 0;}
