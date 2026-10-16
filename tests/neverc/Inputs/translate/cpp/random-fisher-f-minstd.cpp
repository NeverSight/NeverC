#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}
template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long ea,unsigned long long eb,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::fisher_f_distribution<R>;using P=typename D::param_type;R a[]={R(2),R(1.5),R(8)},b[]={R(2),R(4),R(3)};
 P defaults;D initial;P one(R(2));D first(R(4));if(defaults.m()!=R(1)||defaults.n()!=R(1)||initial.m()!=R(1)||initial.n()!=R(1)||one.n()!=R(1)||first.n()!=R(1))return 1;
 P p(a[k],b[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.m())!=ea||bits(d.n())!=eb||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(a[k])+magnitude(b[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={964989659ULL,1054075258ULL,1099095158ULL,1068924200ULL,1046843444ULL,1038730753ULL,1057405642ULL,1067453005ULL,1053397385ULL,1052973360ULL,1059229011ULL,1074457979ULL,1060513038ULL,1035786441ULL,1088037884ULL,1084521303ULL};
if(int r=check<float,std::minstd_rand>(0,s0,1073741824ULL,1073741824ULL,0ULL,2139095040ULL,1308187679ULL))return 0+r;
const unsigned long long s1[]={899285014ULL,1064122973ULL,1054107447ULL,1068372207ULL,1096864152ULL,1070761865ULL,1067171286ULL,1061355371ULL,1081829759ULL,1082970582ULL,1078163974ULL,1047714442ULL,1063699324ULL,1024993722ULL,1044263177ULL,1059794993ULL};
if(int r=check<float,std::minstd_rand>(1,s1,1069547520ULL,1082130432ULL,0ULL,2139095040ULL,1511724264ULL))return 8+r;
const unsigned long long s2[]={1048267177ULL,1068817446ULL,1055039924ULL,1060808498ULL,1055865241ULL,1069185477ULL,1077660334ULL,1063965022ULL,1067109680ULL,1084411429ULL,1072852511ULL,1058571493ULL,1069650224ULL,1060025189ULL,1061664645ULL,1071791502ULL};
if(int r=check<float,std::minstd_rand>(2,s2,1090519040ULL,1077936128ULL,0ULL,2139095040ULL,1372229943ULL))return 16+r;
const unsigned long long s3[]={4585924483471710363ULL,4601137682230367406ULL,4610134934405293445ULL,4612392014865541682ULL,4609720450720549343ULL,4602900087540583604ULL,4610196106836515062ULL,4604031221752792827ULL,4602027076862051111ULL,4620686349799952950ULL,4599142940161668995ULL,4619519740251820262ULL,4620215341269557384ULL,4612350804684865262ULL,4592525692964039620ULL,4601581532333802445ULL};
if(int r=check<double,std::minstd_rand>(0,s3,4611686018427387904ULL,4611686018427387904ULL,0ULL,9218868437227405312ULL,1980579336ULL))return 24+r;
const unsigned long long s4[]={4586469879297434660ULL,4618021005339511926ULL,4620901061496602908ULL,4611841645822334312ULL,4608322058651985504ULL,4605984740775225293ULL,4601473865171451910ULL,4602791481788210753ULL,4605255435055914903ULL,4590955865171007820ULL,4594056277304405854ULL,4572641908509272703ULL,4624974816557780355ULL,4573415582286801939ULL,4620700277489796279ULL,4611006430858802482ULL};
if(int r=check<double,std::minstd_rand>(1,s4,4609434218613702656ULL,4616189618054758400ULL,0ULL,9218868437227405312ULL,1242287816ULL))return 32+r;
const unsigned long long s5[]={4606291871390798058ULL,4610244553933118570ULL,4617606598249706862ULL,4607512193887777911ULL,4607578606196115910ULL,4604420775764502078ULL,4608578766067027194ULL,4606990554675169418ULL,4604131140659015288ULL,4607578593010077817ULL,4593691619476936132ULL,4603259467968961468ULL,4594331120946662663ULL,4608728920786408514ULL,4620867564643838762ULL,4607985714518378525ULL};
if(int r=check<double,std::minstd_rand>(2,s5,4620693217682128896ULL,4613937818241073152ULL,0ULL,9218868437227405312ULL,1511036468ULL))return 40+r;
return 0;}
