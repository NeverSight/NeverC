#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}
template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long ea,unsigned long long eb,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::cauchy_distribution<R>;using P=typename D::param_type;R a[]={R(0),R(-2.5),R(8)},b[]={R(1),R(.5),R(3)};
 P defaults;D initial;P one(R(-2));D first(R(4));if(defaults.a()!=R(0)||defaults.b()!=R(1)||initial.a()!=R(0)||initial.b()!=R(1)||one.b()!=R(1)||first.b()!=R(1))return 1;
 P p(a[k],b[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.a())!=ea||bits(d.b())!=eb||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(a[k])+magnitude(b[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={949229355ULL,1049370522ULL,3225561364ULL,3199559859ULL,3184451368ULL,1059953897ULL,3249138146ULL,1077992323ULL,1066062616ULL,3213185968ULL,1049884181ULL,3232146427ULL,3228710163ULL,3207494851ULL,3226955526ULL,3252244737ULL};
if(int r=check<float,std::minstd_rand>(0,s0,0ULL,1065353216ULL,4286578688ULL,2139095040ULL,366270559ULL))return 0+r;
const unsigned long long s1[]={3223322476ULL,3222748678ULL,3229649477ULL,3224065675ULL,3223534461ULL,3221900387ULL,3243370978ULL,3212724475ULL,3220870772ULL,3225507052ULL,3222716575ULL,3231928829ULL,3230436677ULL,3224752024ULL,3229998017ULL,3246434433ULL};
if(int r=check<float,std::minstd_rand>(1,s1,3223322624ULL,1056964608ULL,4286578688ULL,2139095040ULL,366270559ULL))return 8+r;
const unsigned long long s2[]={1090519262ULL,1091379958ULL,3213686393ULL,1088289886ULL,1089883530ULL,1092652396ULL,3260997331ULL,1099453009ULL,1093930793ULL,1083965756ULL,1091428114ULL,3237211120ULL,3226902329ULL,1086230839ULL,3221638417ULL,3264412865ULL};
if(int r=check<float,std::minstd_rand>(2,s2,1090519040ULL,1077936128ULL,4286578688ULL,2139095040ULL,366270559ULL))return 16+r;
const unsigned long long s3[]={4598601775570616676ULL,13823426417866686446ULL,4604283681418284053ULL,4613967988315658446ULL,13830741879375088458ULL,13840921198984209514ULL,13827686485137963663ULL,13851711395336790531ULL,13803428273231563927ULL,13815622673698555059ULL,4616444884352907217ULL,13834687548023933413ULL,13816787062910863469ULL,13825660132565795564ULL,4608638985629425237ULL,4615377648934093854ULL};
if(int r=check<double,std::minstd_rand>(0,s3,0ULL,4607182418800017408ULL,18442240474082181120ULL,9218868437227405312ULL,1308187679ULL))return 24+r;
const unsigned long long s4[]={13835875820460711914ULL,13836582877757381631ULL,13835420397454880381ULL,13830494115505622628ULL,13837356711025922770ULL,13840804376900293173ULL,13836951358781245266ULL,13848592012378624258ULL,13836201343108138630ULL,13836302489816828435ULL,13824008725642232184ULL,13838343128188134009ULL,13836320683398270754ULL,13836722484926075951ULL,13834329771867459798ULL,13827674794268751812ULL};
if(int r=check<double,std::minstd_rand>(1,s4,13836183955189006336ULL,4602678819172646912ULL,18442240474082181120ULL,9218868437227405312ULL,1308187679ULL))return 32+r;
const unsigned long long s5[]={4621155419774570529ULL,4619496449977003012ULL,4621838554283317828ULL,4625489606064179533ULL,4617174950171379592ULL,13843640287133560126ULL,4618391006905412106ULL,13858244109438138754ULL,4620641053924732013ULL,4620337613798662600ULL,4626514166939953629ULL,4612241779314733352ULL,4620283033054335643ULL,4619077628470920052ULL,4622928280103420768ULL,4626018228796092811ULL};
if(int r=check<double,std::minstd_rand>(2,s5,4620693217682128896ULL,4613937818241073152ULL,18442240474082181120ULL,9218868437227405312ULL,1308187679ULL))return 40+r;
return 0;}
