#include <random>
struct G{using result_type=unsigned;unsigned state=17,calls=0;static constexpr unsigned min(){return 0;}static constexpr unsigned max(){return ~0u;}unsigned operator()(){++calls;state=state*1664525u+1013904223u;return state;}};
template<class R>unsigned long long bits(R v){const auto*p=static_cast<const unsigned char*>(static_cast<const void*>(&v));unsigned long long n=0;for(unsigned i=0;i<sizeof(R);++i)n|=static_cast<unsigned long long>(p[i])<<(8*i);return n;}
template<class R>R value(unsigned long long raw){R v{};auto*p=static_cast<unsigned char*>(static_cast<void*>(&v));for(unsigned i=0;i<sizeof(R);++i)p[i]=static_cast<unsigned char>(raw>>(8*i));return v;}
template<class R>R magnitude(R x){return x<R(0)?-x:x;}

template<class R>bool close(R x,unsigned long long raw){R expected=value<R>(raw);return magnitude(x-expected)<=(sizeof(R)==4?R(.000002):R(.00000000000002))*(magnitude(expected)+R(5));}
template<class R,class Engine>int check(unsigned k,const unsigned long long*expected,unsigned long long degree,unsigned long long changed,unsigned long long cached,unsigned long long reset,unsigned long long next,unsigned long long nextCopy,unsigned long long nextReset){using D=std::student_t_distribution<R>;using P=typename D::param_type;R ns[]={R(1),R(2),R(7)};
 P defaults,p(ns[k]),pc(p),pm(static_cast<P&&>(p));D initial,d(p);if(defaults.n()!=R(1)||initial.n()!=R(1)||p!=pc||pm!=p||bits(d.n())!=degree||bits(d.min())!=(sizeof(R)==4?0xff800000ULL:0xfff0000000000000ULL)||bits(d.max())!=(sizeof(R)==4?0x7f800000ULL:0x7ff0000000000000ULL))return 1;
 Engine e;for(unsigned i=0;i<10003;++i){R x=d(e);if(!(x==x))return 2;if(i<16&&!close(x,expected[i]))return 3;}
 D copied(d),moved(static_cast<D&&>(d)),assigned,resetter(d),cold(p);D*result=&(assigned=d);D*self=&(assigned=assigned);if(result!=&assigned||self!=&assigned||d!=copied||moved!=copied||assigned!=copied||cold!=copied)return 4;
 Engine ec=e,em=e,ea=e,er=e;d.param(P(R(9)));R x=d(e),c=copied(ec),m=moved(em),a=assigned(ea);auto en=e(),cn=ec(),mn=em(),an=ea();if(!close(x,changed)||!close(c,cached)||bits(c)!=bits(m)||bits(m)!=bits(a)||cn!=mn||mn!=an||static_cast<unsigned long long>(en)!=next||static_cast<unsigned long long>(cn)!=nextCopy||d.n()!=R(9))return 5;
 resetter.reset();R r=resetter(er);if(resetter!=copied||!close(r,reset)||static_cast<unsigned long long>(er())!=nextReset)return 6;return 0;}
int main(){
const unsigned long long s0[]={3210364740ULL,1059083274ULL,1077844441ULL,3217164854ULL,1074578278ULL,3195140801ULL,3195454451ULL,3258943755ULL,3226715005ULL,1065533938ULL,1064250118ULL,1079772182ULL,1082574787ULL,3221809063ULL,3228413622ULL,1055621098ULL};
if(int r=check<float,G>(0,s0,1065353216ULL,1059011349ULL,1082472934ULL,3220388779ULL,405894280ULL,1197826150ULL,405894280ULL))return 0+r;
const unsigned long long s1[]={3207911422ULL,1066715488ULL,1066759650ULL,3220862858ULL,3216190412ULL,1049940947ULL,1070266037ULL,3190509662ULL,3205603450ULL,1084597309ULL,1098766347ULL,3188331459ULL,3221509436ULL,1056391439ULL,3213559022ULL,1066399054ULL};
if(int r=check<float,G>(1,s1,1073741824ULL,3192878265ULL,3193051036ULL,1084061064ULL,331565535ULL,1638043776ULL,1133481394ULL))return 16+r;
const unsigned long long s2[]={3207664023ULL,1059437228ULL,1061452486ULL,3226499502ULL,1070680467ULL,3192150568ULL,3196915142ULL,3210810675ULL,1065391001ULL,1072596714ULL,1065047510ULL,3210243813ULL,3224983941ULL,1056993553ULL,1050242325ULL,1075861627ULL};
if(int r=check<float,G>(2,s2,1088421888ULL,1050168662ULL,1050296346ULL,1058910307ULL,3894433708ULL,3894433708ULL,3318471102ULL))return 32+r;
const unsigned long long s3[]={4611175346998895581ULL,13827916040195937264ULL,13829497399181115550ULL,13827465056324635189ULL,4608519268744907139ULL,4603883409258318112ULL,13839782027958790789ULL,13829560734590520836ULL,4623643264300903372ULL,4607113647811724939ULL,4604347873717814488ULL,4609401228154281870ULL,4612505317035408829ULL,13839643966785930721ULL,4588619464992015637ULL,13828012217403175893ULL};
if(int r=check<double,G>(0,s3,4607182418800017408ULL,4602853104800192007ULL,4605139405359673108ULL,13835026058373428177ULL,2089246268ULL,2089246268ULL,644912ULL))return 48+r;
const unsigned long long s4[]={4608965738935114903ULL,13831201008541018326ULL,4615801359460732137ULL,13836677481486437392ULL,4609727668862106591ULL,4611943506065341099ULL,4608059555361035929ULL,4603869191849807316ULL,4613535936822745481ULL,13840407192023708155ULL,13835515137015517689ULL,13837663311624906641ULL,4595526462023449863ULL,4609762791410599992ULL,13831649683065613270ULL,13822112295791618458ULL};
if(int r=check<double,G>(1,s4,4611686018427387904ULL,13818021692421525330ULL,13817428827984216571ULL,4611214229675220027ULL,423261342ULL,2994145164ULL,2774527360ULL))return 64+r;
const unsigned long long s5[]={4609019836554325389ULL,13827140947658567723ULL,13828604751499126684ULL,13814470005131495723ULL,4607836689195639731ULL,4610198091613580524ULL,4612297182992815016ULL,4605458279626209280ULL,4603872854580490442ULL,4610599369610595947ULL,4599611414034946476ULL,13827110163619944229ULL,13815343338813956882ULL,13829263153155334187ULL,13826258313514876411ULL,13825204729458369904ULL};
if(int r=check<double,G>(2,s5,4619567317775286272ULL,4607177787009814707ULL,4607316515766216190ULL,4596705258669541114ULL,2654196300ULL,2654196300ULL,453116992ULL))return 80+r;
return 0;}
