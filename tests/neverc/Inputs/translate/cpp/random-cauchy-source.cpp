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
const unsigned long long s0[]={1064596417ULL,3206667583ULL,1090165857ULL,1068295421ULL,3214986375ULL,1071447717ULL,3218495745ULL,1061985610ULL,1057522299ULL,3232523146ULL,3254583757ULL,1075886104ULL,1062497097ULL,1045919816ULL,3199953868ULL,1084437537ULL};
if(int r=check<float,G>(0,s0,0ULL,1065353216ULL,4286578688ULL,2139095040ULL,1860345957ULL))return 0+r;
const unsigned long long s1[]={3221320072ULL,3224648616ULL,1068841154ULL,3219754370ULL,3225957154ULL,3218178222ULL,3226834496ULL,3221646423ULL,3222204337ULL,3232117189ULL,3247603943ULL,3214886888ULL,3221582487ULL,3222881342ULL,3224090301ULL,1028458567ULL};
if(int r=check<float,G>(1,s1,3223322624ULL,1056964608ULL,4286578688ULL,2139095040ULL,1860345957ULL))return 8+r;
const unsigned long long s2[]={1093522868ULL,1086541064ULL,1107031369ULL,1094768095ULL,1082615451ULL,1095950206ULL,1077836414ULL,1093033342ULL,1092196471ULL,3238171982ULL,3266167130ULL,1098418706ULL,1093129246ULL,1091180963ULL,1088216010ULL,1102735129ULL};
if(int r=check<float,G>(2,s2,1090519040ULL,1077936128ULL,4286578688ULL,2139095040ULL,1860345957ULL))return 16+r;
const unsigned long long s3[]={13827242347297679887ULL,4608762002505842668ULL,4610454378423462224ULL,4605374449323018520ULL,13841123444729682907ULL,4612837220377651639ULL,4596749191138805431ULL,4617428236430093842ULL,13840089065989001398ULL,13831496487786971344ULL,4600144139739021204ULL,4621476175231806820ULL,13829180435934465103ULL,13834719637569171736ULL,13835853709535323209ULL,13834289959026597175ULL};
if(int r=check<double,G>(0,s3,0ULL,4607182418800017408ULL,18442240474082181120ULL,9218868437227405312ULL,2013693826ULL))return 24+r;
const unsigned long long s4[]={13836895841551209794ULL,13834268263429251082ULL,13833422075470441304ULL,13835284051466788573ULL,13840905499773029870ULL,13831655053518214729ULL,13835947043599997898ULL,4587374803019334784ULL,13840388310402689115ULL,13837545363128893492ULL,13835779422700186631ULL,4612126033619901128ULL,13837138102630807946ULL,13838351150574443590ULL,13838833582129271332ULL,13838243730938799950ULL};
if(int r=check<double,G>(1,s4,13836183955189006336ULL,4602678819172646912ULL,18442240474082181120ULL,9218868437227405312ULL,2013693826ULL))return 32+r;
const unsigned long long s5[]={4618557558595518522ULL,4622974411432077304ULL,4623609052401184638ULL,4622043073265455540ULL,13844156139453442504ULL,4624934318865354569ULL,4621048585065641553ULL,4627251680997843598ULL,13841143888147935778ULL,4616608993862467428ULL,4621300016415358454ULL,4630287635099128331ULL,4617830775356724066ULL,4612193644996875868ULL,4606641767016844432ULL,4612838162810737710ULL};
if(int r=check<double,G>(2,s5,4620693217682128896ULL,4613937818241073152ULL,18442240474082181120ULL,9218868437227405312ULL,2013693826ULL))return 40+r;
return 0;}
