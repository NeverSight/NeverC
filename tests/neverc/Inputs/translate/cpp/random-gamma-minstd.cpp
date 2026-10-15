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
const unsigned long long s0[]={935100555ULL,1035337662ULL,1064005637ULL,1074673091ULL,1079784290ULL,1045914334ULL,1060715265ULL,1057090633ULL,1050422840ULL,1068378779ULL,1036001655ULL,1062364643ULL,1063219548ULL,1070876721ULL,1063613235ULL,1060602764ULL};
if(int r=check<float,std::minstd_rand>(0,s0,1065353216ULL,1065353216ULL,0ULL,2139095040ULL,366270559ULL))return 0+r;
const unsigned long long s1[]={797630687ULL,1044418422ULL,1040705335ULL,1024298651ULL,998465868ULL,1043472674ULL,1043941715ULL,1060650077ULL,1050955070ULL,1026868939ULL,1062801070ULL,1063025580ULL,1012809848ULL,1063642391ULL,1066780092ULL,1027791787ULL};
if(int r=check<float,std::minstd_rand>(1,s1,1056964608ULL,1056964608ULL,0ULL,2139095040ULL,552575312ULL))return 8+r;
const unsigned long long s2[]={1086562608ULL,1103736209ULL,1083668808ULL,1085904316ULL,1086236060ULL,1090995213ULL,1065683745ULL,1097708260ULL,1098289155ULL,1100174856ULL,1067597496ULL,1077235879ULL,1089858664ULL,1086252883ULL,1086847231ULL,1086499897ULL};
if(int r=check<float,std::minstd_rand>(2,s2,1075838976ULL,1077936128ULL,0ULL,2139095040ULL,374001148ULL))return 16+r;
const unsigned long long s3[]={4591067939627806255ULL,4612185988308757324ULL,4596746249252478941ULL,4602746479159889120ULL,4608806755314101244ULL,4605577940684125594ULL,4610147827631365516ULL,4604632039476886036ULL,4617670594848969781ULL,4614837501562828231ULL,4603176146600647218ULL,4607445549850719685ULL,4614518686658839294ULL,4611469120853952877ULL,4599943820601576772ULL,4602998899533906929ULL};
if(int r=check<double,std::minstd_rand>(0,s3,4607182418800017408ULL,4607182418800017408ULL,0ULL,9218868437227405312ULL,1308187679ULL))return 24+r;
const unsigned long long s4[]={4570482641451131630ULL,4580842804307029387ULL,4599933086080534861ULL,4602369032522397108ULL,4616079676108938026ULL,4591204342950420778ULL,4609832715053832504ULL,4586390299221478750ULL,4593948101292065790ULL,4591396340039677083ULL,4600878407001062833ULL,4601152210440442739ULL,4595357380648533416ULL,4590779635761332566ULL,4578898532119621088ULL,4568843990483457172ULL};
if(int r=check<double,std::minstd_rand>(1,s4,4602678819172646912ULL,4602678819172646912ULL,0ULL,9218868437227405312ULL,390948395ULL))return 32+r;
const unsigned long long s5[]={4621169627286292073ULL,4622182281012973378ULL,4614690883569835896ULL,4606969019977071106ULL,4617014070048491863ULL,4617532665964919802ULL,4614915558308084403ULL,4621580186624935792ULL,4621694360729312344ULL,4618159317617944441ULL,4620012461113127875ULL,4613349086131144689ULL,4619981405669123510ULL,4616101497935521786ULL,4618020212881043327ULL,4620537094659090280ULL};
if(int r=check<double,std::minstd_rand>(2,s5,4612811918334230528ULL,4613937818241073152ULL,0ULL,9218868437227405312ULL,1836190148ULL))return 40+r;
return 0;}
