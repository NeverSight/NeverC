#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}
template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long ea,unsigned long long eb,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::weibull_distribution<R>;using P=typename D::param_type;R a[]={R(1),R(.5),R(8)},b[]={R(1),R(.5),R(3)};
 P defaults;D initial;P one(R(2));D first(R(4));if(defaults.a()!=R(1)||defaults.b()!=R(1)||initial.a()!=R(1)||initial.b()!=R(1)||one.b()!=R(1)||first.b()!=R(1))return 1;
 P p(a[k],b[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.a())!=ea||bits(d.b())!=eb||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(a[k])+magnitude(b[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={1069930232ULL,1049861898ULL,1067368090ULL,1077647622ULL,1017080030ULL,1057283868ULL,1049899412ULL,1018967231ULL,1060912455ULL,1054368059ULL,1050940740ULL,1062389169ULL,1041923415ULL,1061745224ULL,1060957053ULL,1073284237ULL};
if(int r=check<float,std::mt19937_64>(0,s0,1065353216ULL,1065353216ULL,0ULL,2139095040ULL,3657840786615683573ULL))return 0+r;
const unsigned long long s1[]={1066984590ULL,1026179089ULL,1061478312ULL,1082751210ULL,960923357ULL,1040838063ULL,1026265785ULL,965369219ULL,1049258511ULL,1035396152ULL,1028806274ULL,1051560175ULL,1010464276ULL,1050524534ULL,1049324216ULL,1072839130ULL};
if(int r=check<float,std::mt19937_64>(1,s1,1056964608ULL,1056964608ULL,0ULL,2139095040ULL,3657840786615683573ULL))return 8+r;
const unsigned long long s2[]={1078639976ULL,1076124464ULL,1078279308ULL,1079746574ULL,1072344446ULL,1076945797ULL,1076129676ULL,1072667028ULL,1077461706ULL,1076651813ULL,1076267754ULL,1077634049ULL,1075286802ULL,1077560982ULL,1077467169ULL,1079027630ULL};
if(int r=check<float,std::mt19937_64>(2,s2,1090519040ULL,1077936128ULL,0ULL,2139095040ULL,3657840786615683573ULL))return 16+r;
const unsigned long long s3[]={4609639685794086531ULL,4598865580509351323ULL,4608264145696983550ULL,4613782928237412756ULL,4581265944042058253ULL,4602850220990507513ULL,4598885720185994049ULL,4582279123855602893ULL,4604798303001805401ULL,4601284807738974831ULL,4599444779360669108ULL,4605591107807809626ULL,4594603640598554064ULL,4605245392478329082ULL,4604822246375977386ULL,4611440352648756912ULL};
if(int r=check<double,std::mt19937_64>(0,s3,4607182418800017408ULL,4607182418800017408ULL,0ULL,9218868437227405312ULL,3657840786615683573ULL))return 24+r;
const unsigned long long s4[]={4608058256526158143ULL,4586150968663823924ULL,4605102094703953129ULL,4616522896529114526ULL,4551117052035275573ULL,4594020946909519456ULL,4586197512539286318ULL,4553503906312298279ULL,4598541639247061895ULL,4591099343124590890ULL,4587561427819090062ULL,4599777335708908399ULL,4577714144602260701ULL,4599221330087463833ULL,4598576914498216511ULL,4611201387252018616ULL};
if(int r=check<double,std::mt19937_64>(1,s4,4602678819172646912ULL,4602678819172646912ULL,0ULL,9218868437227405312ULL,3657840786615683573ULL))return 32+r;
const unsigned long long s5[]={4614315693942175530ULL,4612965188464585340ULL,4614122061853326262ULL,4614909793884265556ULL,4610935805322590802ULL,4613406138586770000ULL,4612967986570599291ULL,4611108990637125816ULL,4613683114555795502ULL,4613248306710606495ULL,4613042117112186758ULL,4613775640958003191ULL,4612515472447503278ULL,4613736413213773289ULL,4613686047535750306ULL,4614523813557544890ULL};
if(int r=check<double,std::mt19937_64>(2,s5,4620693217682128896ULL,4613937818241073152ULL,0ULL,9218868437227405312ULL,3657840786615683573ULL))return 40+r;
return 0;}
