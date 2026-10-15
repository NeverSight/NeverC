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
const unsigned long long s0[]={1067705018ULL,3205136400ULL,1056477585ULL,1065708954ULL,3194352654ULL,1063748526ULL,3185236356ULL,1068880114ULL,1071852269ULL,1045329691ULL,1051515614ULL,1060994296ULL,1068625150ULL,1076610354ULL,3209226370ULL,1057804593ULL};
if(int r=check<float,G>(0,s0,0ULL,1065353216ULL,4286578688ULL,2139095040ULL,1860345957ULL))return 0+r;
const unsigned long long s1[]={3220049571ULL,3224457218ULL,3222304487ULL,3221047603ULL,3223793568ULL,3221426058ULL,3223546726ULL,3219462023ULL,3217975946ULL,3222899783ULL,3222614610ULL,3221770337ULL,3219589505ULL,3214162638ULL,3224968464ULL,3222169050ULL};
if(int r=check<float,G>(1,s1,3223322624ULL,1056964608ULL,4286578688ULL,2139095040ULL,1860345957ULL))return 8+r;
const unsigned long long s2[]={1094546694ULL,1087115258ULL,1092046246ULL,1093798170ULL,1089106207ULL,1093363888ULL,1089846734ULL,1094987355ULL,1096101913ULL,1091153301ULL,1091581061ULL,1092847470ULL,1094891743ULL,1098934771ULL,1085581519ULL,1092249401ULL};
if(int r=check<float,G>(2,s2,1090519040ULL,1077936128ULL,4286578688ULL,2139095040ULL,1860345957ULL))return 16+r;
const unsigned long long s3[]={13826420301137147007ULL,4607373404276465745ULL,4606320908096454268ULL,4609075907820116940ULL,4596432367720552236ULL,4604842241020856254ULL,4613226047807590708ULL,4603129781804886489ULL,4595271891694118142ULL,13821230606823366200ULL,4612027269828712398ULL,4602058134573618327ULL,13824313112055026671ULL,13811526553832646524ULL,4577767451768090421ULL,13813351566367344580ULL};
if(int r=check<double,G>(0,s3,0ULL,4607182418800017408ULL,18442240474082181120ULL,9218868437227405312ULL,2013693826ULL))return 24+r;
const unsigned long long s4[]={13836793085781143184ULL,13834962562543939544ULL,13835165744120109104ULL,13834111310772113946ULL,13835956944331818311ULL,13835350577504558856ULL,13831266226088275660ULL,13835564634906555077ULL,13835993209207644376ULL,13836455534866445554ULL,13832465004067153970ULL,13835659798023024311ULL,13836638296144152895ULL,13836246405938553807ULL,13836171037552312904ULL,13836267003764778271ULL};
if(int r=check<double,G>(1,s4,13836183955189006336ULL,4602678819172646912ULL,18442240474082181120ULL,9218868437227405312ULL,2013693826ULL))return 32+r;
const unsigned long long s5[]={4618865825905718352ULL,4622453687096060958ULL,4622220534285474743ULL,4623092125924930156ULL,4621033733967910934ULL,4621943284208800116ULL,4625211378373654132ULL,4621622198105805785ULL,4620979336654171836ULL,4619878478649811243ULL,4624326855953650138ULL,4621479453431101934ULL,4619330194816689219ULL,4620505865433486483ULL,4620712594137169043ULL,4620444071954813091ULL};
if(int r=check<double,G>(2,s5,4620693217682128896ULL,4613937818241073152ULL,18442240474082181120ULL,9218868437227405312ULL,2013693826ULL))return 40+r;
return 0;}
