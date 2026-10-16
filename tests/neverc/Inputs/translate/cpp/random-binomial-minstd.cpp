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
if(int r=check<short,std::minstd_rand>(0,s0,14734379229272918391ULL,595905495ULL))return r;
const unsigned long long s1[]={0ULL,0ULL,0ULL,1ULL,0ULL,1ULL,1ULL,1ULL,1ULL,0ULL,0ULL,1ULL,1ULL,0ULL,0ULL,0ULL};
if(int r=check<short,std::minstd_rand>(1,s1,6536411504869695936ULL,1475859482ULL))return r;
const unsigned long long s2[]={3ULL,4ULL,3ULL,1ULL,3ULL,2ULL,2ULL,2ULL,1ULL,0ULL,3ULL,2ULL,2ULL,0ULL,3ULL,0ULL};
if(int r=check<short,std::minstd_rand>(2,s2,5996801420603478129ULL,1475859482ULL))return r;
const unsigned long long s3[]={10ULL,8ULL,10ULL,14ULL,15ULL,13ULL,13ULL,13ULL,14ULL,16ULL,15ULL,12ULL,13ULL,9ULL,10ULL,16ULL};
if(int r=check<short,std::minstd_rand>(3,s3,12833717063890363383ULL,1475859482ULL))return r;
const unsigned long long s4[]={93ULL,84ULL,93ULL,88ULL,87ULL,90ULL,90ULL,90ULL,91ULL,94ULL,87ULL,91ULL,90ULL,94ULL,93ULL,86ULL};
if(int r=check<short,std::minstd_rand>(4,s4,13040683890205776406ULL,1475859482ULL))return r;
const unsigned long long s5[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<short,std::minstd_rand>(5,s5,14734379229272918391ULL,595905495ULL))return r;
const unsigned long long s6[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<short,std::minstd_rand>(6,s6,10552691409469378443ULL,595905495ULL))return r;
const unsigned long long s7[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<short,std::minstd_rand>(7,s7,1710026874217614779ULL,1475859482ULL))return r;
const unsigned long long s8[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned short,std::minstd_rand>(0,s8,14734379229272918391ULL,595905495ULL))return r;
const unsigned long long s9[]={0ULL,0ULL,0ULL,1ULL,0ULL,1ULL,1ULL,1ULL,1ULL,0ULL,0ULL,1ULL,1ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned short,std::minstd_rand>(1,s9,6536411504869695936ULL,1475859482ULL))return r;
const unsigned long long s10[]={3ULL,4ULL,3ULL,1ULL,3ULL,2ULL,2ULL,2ULL,1ULL,0ULL,3ULL,2ULL,2ULL,0ULL,3ULL,0ULL};
if(int r=check<unsigned short,std::minstd_rand>(2,s10,5996801420603478129ULL,1475859482ULL))return r;
const unsigned long long s11[]={10ULL,8ULL,10ULL,14ULL,15ULL,13ULL,13ULL,13ULL,14ULL,16ULL,15ULL,12ULL,13ULL,9ULL,10ULL,16ULL};
if(int r=check<unsigned short,std::minstd_rand>(3,s11,12833717063890363383ULL,1475859482ULL))return r;
const unsigned long long s12[]={93ULL,84ULL,93ULL,88ULL,87ULL,90ULL,90ULL,90ULL,91ULL,94ULL,87ULL,91ULL,90ULL,94ULL,93ULL,86ULL};
if(int r=check<unsigned short,std::minstd_rand>(4,s12,13040683890205776406ULL,1475859482ULL))return r;
const unsigned long long s13[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned short,std::minstd_rand>(5,s13,14734379229272918391ULL,595905495ULL))return r;
const unsigned long long s14[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<unsigned short,std::minstd_rand>(6,s14,10552691409469378443ULL,595905495ULL))return r;
const unsigned long long s15[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<unsigned short,std::minstd_rand>(7,s15,1710026874217614779ULL,1475859482ULL))return r;
const unsigned long long s16[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<int,std::minstd_rand>(0,s16,14734379229272918391ULL,595905495ULL))return r;
const unsigned long long s17[]={0ULL,0ULL,0ULL,1ULL,0ULL,1ULL,1ULL,1ULL,1ULL,0ULL,0ULL,1ULL,1ULL,0ULL,0ULL,0ULL};
if(int r=check<int,std::minstd_rand>(1,s17,6536411504869695936ULL,1475859482ULL))return r;
const unsigned long long s18[]={3ULL,4ULL,3ULL,1ULL,3ULL,2ULL,2ULL,2ULL,1ULL,0ULL,3ULL,2ULL,2ULL,0ULL,3ULL,0ULL};
if(int r=check<int,std::minstd_rand>(2,s18,5996801420603478129ULL,1475859482ULL))return r;
const unsigned long long s19[]={10ULL,8ULL,10ULL,14ULL,15ULL,13ULL,13ULL,13ULL,14ULL,16ULL,15ULL,12ULL,13ULL,9ULL,10ULL,16ULL};
if(int r=check<int,std::minstd_rand>(3,s19,12833717063890363383ULL,1475859482ULL))return r;
const unsigned long long s20[]={93ULL,84ULL,93ULL,88ULL,87ULL,90ULL,90ULL,90ULL,91ULL,94ULL,87ULL,91ULL,90ULL,94ULL,93ULL,86ULL};
if(int r=check<int,std::minstd_rand>(4,s20,13040683890205776406ULL,1475859482ULL))return r;
const unsigned long long s21[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<int,std::minstd_rand>(5,s21,14734379229272918391ULL,595905495ULL))return r;
const unsigned long long s22[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<int,std::minstd_rand>(6,s22,10552691409469378443ULL,595905495ULL))return r;
const unsigned long long s23[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<int,std::minstd_rand>(7,s23,1710026874217614779ULL,1475859482ULL))return r;
return 0;}
