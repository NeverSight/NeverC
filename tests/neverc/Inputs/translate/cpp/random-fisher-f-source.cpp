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
const unsigned long long s0[]={1042657227ULL,1071605176ULL,1078323284ULL,1083400720ULL,1045716950ULL,1069511279ULL,1080649245ULL,1081289400ULL,1055014920ULL,1056429410ULL,1076867770ULL,1075917144ULL,1001635605ULL,1043790744ULL,1048646541ULL,1052779698ULL};
if(int r=check<float,G>(0,s0,1073741824ULL,1073741824ULL,0ULL,2139095040ULL,2013693826ULL))return 0+r;
const unsigned long long s1[]={1056821879ULL,1068713841ULL,1046156803ULL,1087411857ULL,1069629254ULL,1069943804ULL,1056989355ULL,1096125891ULL,1052275898ULL,1054646608ULL,1047315459ULL,1046640606ULL,1006723267ULL,1062481117ULL,1042476757ULL,1032833159ULL};
if(int r=check<float,G>(1,s1,1069547520ULL,1082130432ULL,0ULL,2139095040ULL,3976596914ULL))return 8+r;
const unsigned long long s2[]={1060687273ULL,1077678679ULL,1036282783ULL,1071885792ULL,1048714296ULL,1054383084ULL,1069433297ULL,1050625951ULL,1085298900ULL,1079464333ULL,1074119378ULL,1050840944ULL,1078660117ULL,1080089605ULL,1063663521ULL,1078609175ULL};
if(int r=check<float,G>(2,s2,1090519040ULL,1077936128ULL,0ULL,2139095040ULL,4203507052ULL))return 16+r;
const unsigned long long s3[]={4617170960729544150ULL,4610224406387311443ULL,4610395921324215752ULL,4593194700742621600ULL,4604149098763876552ULL,4595842675534233613ULL,4609057398634678388ULL,4606446497746070210ULL,4614147329709468168ULL,4613401778254366335ULL,4604677354442731636ULL,4615081035592129640ULL,4602808451450422628ULL,4609168813822797329ULL,4594094664292866523ULL,4616417164196136670ULL};
if(int r=check<double,G>(0,s3,4611686018427387904ULL,4611686018427387904ULL,0ULL,9218868437227405312ULL,778298904ULL))return 24+r;
const unsigned long long s4[]={4626504937600805468ULL,4607756392678959076ULL,4588183162795235949ULL,4607112077867099045ULL,4598760901616831676ULL,4622163865345605754ULL,4606416468266183107ULL,4603291644663184653ULL,4609300277889552740ULL,4606147404811354296ULL,4602131302476839427ULL,4614584414240335513ULL,4614274828257089322ULL,4617058674422685486ULL,4601065962973744984ULL,4597972755379267455ULL};
if(int r=check<double,G>(1,s4,4609434218613702656ULL,4616189618054758400ULL,0ULL,9218868437227405312ULL,325134696ULL))return 32+r;
const unsigned long long s5[]={4613913411868242098ULL,4604756220306948055ULL,4607833207686577420ULL,4605292091871116545ULL,4611563325164153915ULL,4608520552888755117ULL,4612422080223436949ULL,4599825982132480502ULL,4623884788566413963ULL,4607522134153031276ULL,4610273856708816552ULL,4612366930709852876ULL,4611977973828162710ULL,4615935735641210869ULL,4625642004103189219ULL,4624205266095779428ULL};
if(int r=check<double,G>(2,s5,4620693217682128896ULL,4613937818241073152ULL,0ULL,9218868437227405312ULL,424179120ULL))return 40+r;
return 0;}
