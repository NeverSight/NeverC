#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}

template<class R,class Engine>int check(unsigned k,const unsigned long long* expected,unsigned long long degree,unsigned long long minimum,unsigned long long maximum,unsigned long long next){using D=std::chi_squared_distribution<R>;using P=typename D::param_type;R ns[]={R(1),R(2),R(7)};
 P defaults;D initial;if(defaults.n()!=R(1)||initial.n()!=R(1)||initial==D(R(2))||!(defaults!=P(R(2))))return 1;
 P p(ns[k]),pc(p),pm(static_cast<P&&>(p));D d(p),dc(d),dm(static_cast<D&&>(d));if(p!=pc||pm!=p||d!=dc||dm!=d)return 2;
 P assignedP;P*pr=&(assignedP=p);P*selfP=&(assignedP=assignedP);D assigned;D*dr=&(assigned=d);D*selfD=&(assigned=assigned);assigned.reset();assigned.param(p);if(pr!=&assignedP||selfP!=&assignedP||assignedP!=p||dr!=&assigned||selfD!=&assigned||assigned.param()!=p||assigned!=d)return 3;
 if(bits(d.n())!=degree||bits(d.min())!=minimum||bits(d.max())!=maximum)return 4;
 Engine e;for(unsigned i=0;i<10003;++i){R x=assigned(e,p);if(!(x==x))return 5;if(i<16){R reference=value<R>(expected[i]);R tolerance=(sizeof(R)==4?R(.0000005):R(.000000000000004))*(magnitude(reference)+magnitude(ns[k])+R(1));if(magnitude(x-reference)>tolerance)return 6;}}
 if(static_cast<unsigned long long>(e())!=next)return 7;return 0;}
int main(){
const unsigned long long s0[]={1071358058ULL,1067003032ULL,977450224ULL,1057686897ULL,1041889707ULL,1025550170ULL,1057733434ULL,1056949469ULL,1068669486ULL,1038778992ULL,1080873105ULL,1039593165ULL,1067240766ULL,1070165305ULL,1054382990ULL,1070669766ULL};
if(int r=check<float,std::mt19937_64>(0,s0,1065353216ULL,0ULL,2139095040ULL,14012012891671831484ULL))return 0+r;
const unsigned long long s1[]={1078318840ULL,1058250506ULL,1075756698ULL,1086036230ULL,1025468638ULL,1065672476ULL,1058288020ULL,1027355839ULL,1069301063ULL,1062756667ULL,1059329348ULL,1070777777ULL,1050312023ULL,1070133832ULL,1069345661ULL,1081672845ULL};
if(int r=check<float,std::mt19937_64>(1,s1,1073741824ULL,0ULL,2139095040ULL,3657840786615683573ULL))return 16+r;
const unsigned long long s2[]={1091959300ULL,1090312258ULL,1068874112ULL,1084768761ULL,1072391250ULL,1084802184ULL,1084221674ULL,1091039973ULL,1096789965ULL,1067934938ULL,1090497116ULL,1091562173ULL,1083174491ULL,1091731963ULL,1099924640ULL,1076364673ULL};
if(int r=check<float,std::mt19937_64>(2,s2,1088421888ULL,0ULL,2139095040ULL,13424315022268716298ULL))return 32+r;
const unsigned long long s3[]={4610406244511521148ULL,4608068156380299377ULL,4559989858575780023ULL,4603066593915533286ULL,4594585542886809964ULL,4585813321177097978ULL,4603091578632982175ULL,4602670691469417896ULL,4608962828141423656ULL,4592915490942999286ULL,4615514598122436672ULL,4593352596686862928ULL,4608195789383884479ULL,4609765889674454233ULL,4601292823177860253ULL,4610036719478335530ULL};
if(int r=check<double,std::mt19937_64>(0,s3,4607182418800017408ULL,0ULL,9218868437227405312ULL,14012012891671831484ULL))return 48+r;
const unsigned long long s4[]={4614143285421457027ULL,4603369180136721819ULL,4612767745324354046ULL,4618286527864783252ULL,4585769543669428749ULL,4607353820617878009ULL,4603389319813364545ULL,4586782723482973389ULL,4609301902629175897ULL,4605788407366345327ULL,4603948378988039604ULL,4610094707435180122ULL,4599107240225924560ULL,4609748992105699578ULL,4609325846003347882ULL,4615943952276127408ULL};
if(int r=check<double,std::mt19937_64>(1,s4,4611686018427387904ULL,0ULL,9218868437227405312ULL,3657840786615683573ULL))return 64+r;
const unsigned long long s5[]={4621466451183245020ULL,4620582202052704127ULL,4609072684798086766ULL,4617606059720211027ULL,4610960933635664346ULL,4617624003785892991ULL,4617312344902159079ULL,4620972891732380876ULL,4624059895880172222ULL,4608568470926417280ULL,4620681446833400750ULL,4621253245488065230ULL,4616750143064413007ULL,4621344400368517900ULL,4625742810964337450ULL,4613094149713347672ULL};
if(int r=check<double,std::mt19937_64>(2,s5,4619567317775286272ULL,0ULL,9218868437227405312ULL,13424315022268716298ULL))return 80+r;
return 0;}
