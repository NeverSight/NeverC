#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}
template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long ea,unsigned long long eb,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::extreme_value_distribution<R>;using P=typename D::param_type;R a[]={R(0),R(-2.5),R(8)},b[]={R(1),R(.5),R(3)};
 P defaults;D initial;P one(R(-2));D first(R(4));if(defaults.a()!=R(0)||defaults.b()!=R(1)||initial.a()!=R(0)||initial.b()!=R(1)||one.b()!=R(1)||first.b()!=R(1))return 1;
 P p(a[k],b[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.a())!=ea||bits(d.b())!=eb||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(a[k])+magnitude(b[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={3202281544ULL,1067397308ULL,3193728686ULL,3213469507ULL,1081876409ULL,1059578411ULL,1067364843ULL,1081179902ULL,1050504141ULL,1063026038ULL,1066510440ULL,1044844704ULL,1072830360ULL,1048048470ULL,1050383056ULL,3207224785ULL};
if(int r=check<float,std::mt19937_64>(0,s0,0ULL,1065353216ULL,4286578688ULL,2139095040ULL,3657840786615683573ULL))return 0+r;
const unsigned long long s1[]={3224235780ULL,3220203426ULL,3223774069ULL,3225577937ULL,3204956302ULL,3221947323ULL,3220219658ULL,3206349316ULL,3222677827ULL,3221516369ULL,3220646860ULL,3222914939ULL,3217486900ULL,3222814821ULL,3222685395ULL,3224718266ULL};
if(int r=check<float,std::mt19937_64>(1,s1,3223322624ULL,1056964608ULL,4286578688ULL,2139095040ULL,3657840786615683573ULL))return 8+r;
const unsigned long long s2[]={1087779570ULL,1094431302ULL,1089164704ULL,1083753102ULL,1100909542ULL,1092581992ULL,1094419128ULL,1100648351ULL,1091486235ULL,1093228422ULL,1094098727ULL,1091130568ULL,1096468697ULL,1091280744ULL,1091474884ULL,1086332114ULL};
if(int r=check<float,std::mt19937_64>(2,s2,1090519040ULL,1077936128ULL,4286578688ULL,2139095040ULL,3657840786615683573ULL))return 16+r;
const unsigned long long s3[]={13824887612037620174ULL,4608279832671071178ULL,13820295828602025017ULL,13830894103717472606ULL,4616053242479402189ULL,4604082093104369530ULL,4608262403650497513ULL,4615679309259140802ULL,4599210383346646227ULL,4605933024536750360ULL,4607803698749485803ULL,4596171997307214940ULL,4611196679313199011ULL,4597892005639991798ULL,4599145376207636079ULL,13827541493008473771ULL};
if(int r=check<double,std::mt19937_64>(0,s3,0ULL,4607182418800017408ULL,18442240474082181120ULL,9218868437227405312ULL,3657840786615683573ULL))return 24+r;
const unsigned long long s4[]={13836674202393064989ULL,13834509348346636827ULL,13836426323047028642ULL,13837394767111518808ULL,13826323607178135142ULL,13835445595994119697ULL,13834518062856923660ULL,13827071473618657916ULL,13835837782474710067ULL,13835214229565072093ULL,13834747415307429514ULL,13835965080907235101ULL,13833050925025572910ULL,13835911330646835824ULL,13835841845420898201ULL,13836933234765059029ULL};
if(int r=check<double,std::mt19937_64>(1,s4,13836183955189006336ULL,4602678819172646912ULL,18442240474082181120ULL,9218868437227405312ULL,3657840786615683573ULL))return 32+r;
const unsigned long long s5[]={4619222476069952938ULL,4622793597744037996ULL,4619966114108061979ULL,4617060781914591482ULL,4626271576375583437ULL,4621800756474458855ULL,4622787061861322872ULL,4626131351417985417ULL,4621212476753573300ULL,4622147806118030260ULL,4622615047523443480ULL,4621021529104785748ULL,4623887415234835933ULL,4621102154495384664ULL,4621206382334291098ULL,4618445378953970816ULL};
if(int r=check<double,std::mt19937_64>(2,s5,4620693217682128896ULL,4613937818241073152ULL,18442240474082181120ULL,9218868437227405312ULL,3657840786615683573ULL))return 40+r;
return 0;}
