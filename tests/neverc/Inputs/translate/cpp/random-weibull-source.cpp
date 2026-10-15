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
const unsigned long long s0[]={1049513449ULL,1071374158ULL,1058900670ULL,1052018847ULL,1067465271ULL,1053770350ULL,1066299272ULL,1048012818ULL,1043175498ULL,1062289703ULL,1060546131ULL,1056193667ULL,1048513194ULL,1032576649ULL,1074546978ULL,1058254963ULL};
if(int r=check<float,G>(0,s0,1065353216ULL,1065353216ULL,0ULL,2139095040ULL,1860345957ULL))return 0+r;
const unsigned long long s1[]={1025389836ULL,1069340626ULL,1044506352ULL,1031708881ULL,1061720484ULL,1034407039ULL,1058963415ULL,1022302717ULL,1013673565ULL,1051396978ULL,1048727786ULL,1038680936ULL,1023284799ULL,991483604ULL,1075429412ULL,1042966587ULL};
if(int r=check<float,G>(1,s1,1056964608ULL,1056964608ULL,0ULL,2139095040ULL,1860345957ULL))return 8+r;
const unsigned long long s2[]={1076075188ULL,1078816506ULL,1077195234ULL,1076398889ULL,1078294340ULL,1076591156ULL,1078105332ULL,1075889078ULL,1075432642ULL,1077622960ULL,1077416166ULL,1076824148ULL,1075929182ULL,1074349853ULL,1079233084ULL,1077100021ULL};
if(int r=check<float,G>(2,s2,1090519040ULL,1077936128ULL,0ULL,2139095040ULL,1860345957ULL))return 16+r;
const unsigned long long s3[]={4610414887890065355ULL,4600023583944835037ULL,4600963914523687726ULL,4597872863090695126ULL,4605537708364567961ULL,4602264923790743754ULL,4589585634849244319ULL,4603371573748935625ULL,4605778712267801436ULL,4608410954982366575ULL,4593045021838926170ULL,4603829874498119656ULL,4609421198143982664ULL,4607439279648783647ULL,4607079668270533597ULL,4607527171626462253ULL};
if(int r=check<double,G>(0,s3,4607182418800017408ULL,4607182418800017408ULL,0ULL,9218868437227405312ULL,2013693826ULL))return 24+r;
const unsigned long long s4[]={4609323144174019513ULL,4589119753964510192ULL,4590568315882261375ULL,4584069857349125255ULL,4599689721669753059ULL,4592862848316695634ULL,4567523976276446614ULL,4595163690253243093ULL,4600090163567982349ULL,4605471023715207668ULL,4574447615398908688ULL,4596267923763612095ULL,4607725856870761849ULL,4603207190817130255ULL,4602474490250656680ULL,4603394715828551030ULL};
if(int r=check<double,G>(1,s4,4602678819172646912ULL,4602678819172646912ULL,0ULL,9218868437227405312ULL,2013693826ULL))return 32+r;
const unsigned long long s5[]={4614410468025930004ULL,4613112519477280706ULL,4613215741969902658ULL,4612838816408426465ULL,4613769687717193396ULL,4613340828588303474ULL,4612012451793015499ULL,4613488937032450904ULL,4613796262908014485ULL,4614144604785485992ULL,4612344804637138736ULL,4613555920059766862ULL,4614287314497315172ULL,4613984819053273892ULL,4613928136957398435ULL,4614000392705517922ULL};
if(int r=check<double,G>(2,s5,4620693217682128896ULL,4613937818241073152ULL,0ULL,9218868437227405312ULL,2013693826ULL))return 40+r;
return 0;}
