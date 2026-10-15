#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}
template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long ea,unsigned long long eb,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::gamma_distribution<R>;using P=typename D::param_type;R a[]={R(1),R(.5),R(2.5)},b[]={R(1),R(.5),R(3)};
 P defaults;D initial;P one(R(2));D first(R(4));if(defaults.alpha()!=R(1)||defaults.beta()!=R(1)||initial.alpha()!=R(1)||initial.beta()!=R(1)||one.beta()!=R(1)||first.beta()!=R(1))return 1;
 P p(a[k],b[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.alpha())!=ea||bits(d.beta())!=eb||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(a[k])+magnitude(b[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={1049513449ULL,1071374158ULL,1058900670ULL,1052018847ULL,1067465271ULL,1053770350ULL,1066299272ULL,1048012818ULL,1043175498ULL,1062289703ULL,1060546131ULL,1056193667ULL,1048513194ULL,1032576649ULL,1074546978ULL,1058254963ULL};
if(int r=check<float,G>(0,s0,1065353216ULL,1065353216ULL,0ULL,2139095040ULL,1860345957ULL))return 0+r;
const unsigned long long s1[]={1022439006ULL,1037584147ULL,1050376722ULL,1048640397ULL,1011298820ULL,1040531911ULL,1019680807ULL,1061670793ULL,1028032852ULL,1037607332ULL,1025049155ULL,1055138723ULL,940328786ULL,1014149130ULL,1019695786ULL,1029847785ULL};
if(int r=check<float,G>(1,s1,1056964608ULL,1056964608ULL,0ULL,2139095040ULL,1128622408ULL))return 8+r;
const unsigned long long s2[]={1081575767ULL,1090719496ULL,1089141964ULL,1083507998ULL,1096923951ULL,1068076419ULL,1081600572ULL,1057566718ULL,1092798546ULL,1071445196ULL,1091496292ULL,1091481846ULL,1089396115ULL,1073877066ULL,1094598475ULL,1074543030ULL};
if(int r=check<float,G>(2,s2,1075838976ULL,1077936128ULL,0ULL,2139095040ULL,2811007460ULL))return 16+r;
const unsigned long long s3[]={4610414887890065355ULL,4600023583944835037ULL,4600963914523687726ULL,4597872863090695126ULL,4605537708364567961ULL,4602264923790743754ULL,4589585634849244319ULL,4603371573748935625ULL,4605778712267801436ULL,4608410954982366575ULL,4593045021838926170ULL,4603829874498119656ULL,4609421198143982664ULL,4607439279648783647ULL,4607079668270533597ULL,4607527171626462253ULL};
if(int r=check<double,G>(0,s3,4607182418800017408ULL,4607182418800017408ULL,0ULL,9218868437227405312ULL,2013693826ULL))return 24+r;
const unsigned long long s4[]={4602790312513037691ULL,4588146044394045781ULL,4594860324492410664ULL,4567170926244467206ULL,4595134498168728558ULL,4573599301883861435ULL,4600999602049936660ULL,4596725863527471552ULL,4598495850831426384ULL,4577328035527418143ULL,4581955036865391607ULL,4577577023441111337ULL,4594395510254465844ULL,4599747321078439363ULL,4569398993021029460ULL,4592465022540363812ULL};
if(int r=check<double,G>(1,s4,4602678819172646912ULL,4602678819172646912ULL,0ULL,9218868437227405312ULL,4166708416ULL))return 32+r;
const unsigned long long s5[]={4622387863963756029ULL,4610497586226255174ULL,4617785005799642654ULL,4617994816202509592ULL,4621630954570382081ULL,4619074599269992130ULL,4620258950210851268ULL,4617407784701282367ULL,4621085404018387084ULL,4611660902725842857ULL,4616094763663358900ULL,4617397194508751184ULL,4618739845935533592ULL,4622839002999989829ULL,4618975630628737436ULL,4617786513464922408ULL};
if(int r=check<double,G>(2,s5,4612811918334230528ULL,4613937818241073152ULL,0ULL,9218868437227405312ULL,657801464ULL))return 40+r;
return 0;}
