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
const unsigned long long s0[]={1084984107ULL,1054384212ULL,1025085613ULL,1095340832ULL,1071559928ULL,1053248207ULL,1044697275ULL,1052915598ULL,1067653184ULL,1083721223ULL,1060470313ULL,1096180598ULL,1067656690ULL,1052474399ULL,1099660244ULL,1074041133ULL};
if(int r=check<float,std::mt19937_64>(0,s0,1073741824ULL,1073741824ULL,0ULL,2139095040ULL,15464774641942756594ULL))return 0+r;
const unsigned long long s1[]={1070681238ULL,1011758509ULL,1054698714ULL,1057487482ULL,1035789337ULL,1042957698ULL,1054110442ULL,1090202984ULL,1039512383ULL,1050652837ULL,1082021953ULL,1032034271ULL,1038351392ULL,1082810124ULL,1059546932ULL,1011491138ULL};
if(int r=check<float,std::mt19937_64>(1,s1,1069547520ULL,1082130432ULL,0ULL,2139095040ULL,14396980667872580351ULL))return 8+r;
const unsigned long long s2[]={1079858256ULL,1062582363ULL,1060483281ULL,1037971457ULL,1046517755ULL,1058178045ULL,1067412118ULL,1068222236ULL,1065614464ULL,1064276938ULL,1065797213ULL,1073041288ULL,1065891483ULL,1057761088ULL,1052979234ULL,1058244911ULL};
if(int r=check<float,std::mt19937_64>(2,s2,1090519040ULL,1077936128ULL,0ULL,2139095040ULL,15697686084972334337ULL))return 16+r;
const unsigned long long s3[]={4617721673833423096ULL,4601293478409989578ULL,4585563908384275741ULL,4623281906173390509ULL,4610514621175146240ULL,4600683591456510887ULL,4596092845984480836ULL,4600505023599919254ULL,4608417205383679229ULL,4617043668492715171ULL,4604560929736696322ULL,4623732742862212815ULL,4608419086690881810ULL,4600268158020319127ULL,4625600865632520901ULL,4611846709378186940ULL};
if(int r=check<double,std::mt19937_64>(0,s3,4611686018427387904ULL,4611686018427387904ULL,0ULL,9218868437227405312ULL,15464774641942756594ULL))return 24+r;
const unsigned long long s4[]={4610042879165166739ULL,4578408980569111877ULL,4601462327151134285ULL,4602959535906921094ULL,4591310431392506500ULL,4595158917096498974ULL,4601146500354179821ULL,4620523536940431236ULL,4593309228219201134ULL,4599290211939646466ULL,4616131377448195430ULL,4589294447360470751ULL,4592685924851198227ULL,4616554524910337925ULL,4604065194160964368ULL,4578265437488358933ULL};
if(int r=check<double,std::mt19937_64>(1,s4,4609434218613702656ULL,4616189618054758400ULL,0ULL,9218868437227405312ULL,14396980667872580351ULL))return 32+r;
const unsigned long long s5[]={4614969754340063911ULL,4605694829223037413ULL,4604567891552913079ULL,4592481948201147808ULL,4597070208367287025ULL,4603330279056534738ULL,4608287784174880142ULL,4608722711747909568ULL,4607322675464313536ULL,4606604596897154821ULL,4607420787543150378ULL,4611309920278914770ULL,4607471398393476523ULL,4603106426366374386ULL,4600539187936072410ULL,4603366176827324037ULL};
if(int r=check<double,std::mt19937_64>(2,s5,4620693217682128896ULL,4613937818241073152ULL,0ULL,9218868437227405312ULL,15697686084972334337ULL))return 40+r;
return 0;}
