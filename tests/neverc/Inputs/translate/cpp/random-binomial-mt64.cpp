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
if(int r=check<long long,std::mt19937_64>(0,s0,14734379229272918391ULL,6597103971274460346ULL))return r;
const unsigned long long s1[]={1ULL,1ULL,0ULL,0ULL,0ULL,1ULL,1ULL,0ULL,1ULL,1ULL,1ULL,1ULL,1ULL,0ULL,0ULL,1ULL};
if(int r=check<long long,std::mt19937_64>(1,s1,13192380205651515791ULL,14270278433167030962ULL))return r;
const unsigned long long s2[]={1ULL,1ULL,3ULL,1ULL,3ULL,2ULL,2ULL,3ULL,1ULL,2ULL,2ULL,2ULL,1ULL,3ULL,4ULL,1ULL};
if(int r=check<long long,std::mt19937_64>(2,s2,5723630240783139273ULL,14270278433167030962ULL))return r;
const unsigned long long s3[]={14ULL,14ULL,10ULL,11ULL,11ULL,12ULL,13ULL,10ULL,11ULL,12ULL,13ULL,13ULL,14ULL,15ULL,17ULL,14ULL};
if(int r=check<long long,std::mt19937_64>(3,s3,4702056192834347926ULL,14270278433167030962ULL))return r;
const unsigned long long s4[]={91ULL,88ULL,93ULL,92ULL,92ULL,89ULL,90ULL,93ULL,88ULL,89ULL,90ULL,90ULL,88ULL,87ULL,85ULL,88ULL};
if(int r=check<long long,std::mt19937_64>(4,s4,5620450998464210075ULL,14270278433167030962ULL))return r;
const unsigned long long s5[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<long long,std::mt19937_64>(5,s5,14734379229272918391ULL,6597103971274460346ULL))return r;
const unsigned long long s6[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<long long,std::mt19937_64>(6,s6,10552691409469378443ULL,6597103971274460346ULL))return r;
const unsigned long long s7[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<long long,std::mt19937_64>(7,s7,9212616690067620630ULL,14270278433167030962ULL))return r;
const unsigned long long s8[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long long,std::mt19937_64>(0,s8,14734379229272918391ULL,6597103971274460346ULL))return r;
const unsigned long long s9[]={1ULL,1ULL,0ULL,0ULL,0ULL,1ULL,1ULL,0ULL,1ULL,1ULL,1ULL,1ULL,1ULL,0ULL,0ULL,1ULL};
if(int r=check<unsigned long long,std::mt19937_64>(1,s9,13192380205651515791ULL,14270278433167030962ULL))return r;
const unsigned long long s10[]={1ULL,1ULL,3ULL,1ULL,3ULL,2ULL,2ULL,3ULL,1ULL,2ULL,2ULL,2ULL,1ULL,3ULL,4ULL,1ULL};
if(int r=check<unsigned long long,std::mt19937_64>(2,s10,5723630240783139273ULL,14270278433167030962ULL))return r;
const unsigned long long s11[]={14ULL,14ULL,10ULL,11ULL,11ULL,12ULL,13ULL,10ULL,11ULL,12ULL,13ULL,13ULL,14ULL,15ULL,17ULL,14ULL};
if(int r=check<unsigned long long,std::mt19937_64>(3,s11,4702056192834347926ULL,14270278433167030962ULL))return r;
const unsigned long long s12[]={91ULL,88ULL,93ULL,92ULL,92ULL,89ULL,90ULL,93ULL,88ULL,89ULL,90ULL,90ULL,88ULL,87ULL,85ULL,88ULL};
if(int r=check<unsigned long long,std::mt19937_64>(4,s12,5620450998464210075ULL,14270278433167030962ULL))return r;
const unsigned long long s13[]={0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL,0ULL};
if(int r=check<unsigned long long,std::mt19937_64>(5,s13,14734379229272918391ULL,6597103971274460346ULL))return r;
const unsigned long long s14[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<unsigned long long,std::mt19937_64>(6,s14,10552691409469378443ULL,6597103971274460346ULL))return r;
const unsigned long long s15[]={12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL,12ULL};
if(int r=check<unsigned long long,std::mt19937_64>(7,s15,9212616690067620630ULL,14270278433167030962ULL))return r;
return 0;}
