/*
 * NeverC Math Library — Comprehensive Test Suite
 *
 * Test vectors from Go math/all_test.go (computed by keisan.casio.com
 * high precision calculators to 26 significant digits).
 * Zero libc math dependency in tests — uses only neverc_math_* functions.
 */
#include "neverc/std/math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define NC_NAN     neverc_math_nan()
#define NC_INF     neverc_math_inf(1)
#define NC_NEGINF  neverc_math_inf(-1)
#define EPSILON    1e-14

static void check_double(const char *name, double got, double expected) {
    tests_run++;
    if (neverc_math_isnan(expected) && neverc_math_isnan(got)) {
        tests_passed++;
        return;
    }
    if (neverc_math_isinf(expected, 0) && neverc_math_isinf(got, 0)) {
        int esign = expected > 0 ? 1 : -1;
        int gsign = got > 0 ? 1 : -1;
        if (esign == gsign) { tests_passed++; return; }
    }
    double diff = neverc_math_abs(got - expected);
    double rel = (expected != 0.0) ? neverc_math_abs((got - expected) / expected) : diff;
    if (diff < EPSILON || rel < EPSILON) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got %.17g, expected %.17g (diff=%.3g)\n",
               name, got, expected, diff);
    }
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: expected true\n", name); }
}

/* ===== Go's standard test input vector (10 values) ===== */
static const double vf[10] = {
    4.9790119248836735e+00,
    7.7388724745781045e+00,
   -2.7688005719200159e-01,
   -5.0106036182710749e+00,
    9.6362937071984173e+00,
    2.9263772392439646e+00,
    5.2290834314593066e+00,
    2.7279399104360102e+00,
    1.8253080916808550e+00,
   -8.6859247685756013e+00,
};

/* ===== Expected results (from keisan.casio.com, 26 digits) ===== */

static const double expected_acos[10] = {
    1.0496193546107222142571536e+00, 6.8584012813664425171660692e-01,
    1.5984878714577160325521819e+00, 2.0956199361475859327461799e+00,
    2.7053008467824138592616927e-01, 1.2738121680361776018155625e+00,
    1.0205369421140629186287407e+00, 1.2945003481781246062157835e+00,
    1.3872364345374451433846657e+00, 2.6231510803970463967294145e+00,
};
static const double expected_acosh[10] = {
    2.4743347004159012494457618e+00, 2.8576385344292769649802701e+00,
    7.2796961502981066190593175e-01, 2.4796794418831451156471977e+00,
    3.0552020742306061857212962e+00, 2.044238592688586588942468e+00,
    2.5158701513104513595766636e+00, 1.99050839282411638174299e+00,
    1.6988625798424034227205445e+00, 2.9611454842470387925531875e+00,
};
static const double expected_asin[10] = {
    5.2117697218417440497416805e-01, 8.8495619865825236751471477e-01,
   -2.769154466281941332086016e-02, -5.2482360935268931351485822e-01,
    1.3002662421166552333051524e+00, 2.9698415875871901741575922e-01,
    5.5025938468083370060258102e-01, 2.7629597861677201301553823e-01,
    1.83559892257451475846656e-01, -1.0523547536021497774980928e+00,
};
static const double expected_asinh[10] = {
    2.3083139124923523427628243e+00, 2.743551594301593620039021e+00,
   -2.7345908534880091229413487e-01, -2.3145157644718338650499085e+00,
    2.9613652154015058521951083e+00, 1.7949041616585821933067568e+00,
    2.3564032905983506405561554e+00, 1.7287118790768438878045346e+00,
    1.3626658083714826013073193e+00, -2.8581483626513914445234004e+00,
};
static const double expected_atan[10] = {
    1.372590262129621651920085e+00, 1.442290609645298083020664e+00,
   -2.7011324359471758245192595e-01, -1.3738077684543379452781531e+00,
    1.4673921193587666049154681e+00, 1.2415173565870168649117764e+00,
    1.3818396865615168979966498e+00, 1.2194305844639670701091426e+00,
    1.0696031952318783760193244e+00, -1.4561721938838084990898679e+00,
};
static const double expected_atanh[10] = {
    5.4651163712251938116878204e-01, 1.0299474112843111224914709e+00,
   -2.7695084420740135145234906e-02, -5.5072096119207195480202529e-01,
    1.9943940993171843235906642e+00, 3.01448604578089708203017e-01,
    5.8033427206942188834370595e-01, 2.7987997499441511013958297e-01,
    1.8459947964298794318714228e-01, -1.3273186910532645867272502e+00,
};
static const double expected_atan2[10] = {
    1.1088291730037004444527075e+00, 9.1218183188715804018797795e-01,
    1.5984772603216203736068915e+00, 2.0352918654092086637227327e+00,
    8.0391819139044720267356014e-01, 1.2861075249894661588866752e+00,
    1.0889904479131695712182587e+00, 1.3044821793397925293797357e+00,
    1.3902530903455392306872261e+00, 2.2859857424479142655411058e+00,
};
static const double expected_cbrt[10] = {
    1.7075799841925094446722675e+00, 1.9779982212970353936691498e+00,
   -6.5177429017779910853339447e-01, -1.7111838886544019873338113e+00,
    2.1279920909827937423960472e+00, 1.4303536770460741452312367e+00,
    1.7357021059106154902341052e+00, 1.3972633462554328350552916e+00,
    1.2221149580905388454977636e+00, -2.0556003730500069110343596e+00,
};
static const double expected_ceil[10] = {
    5.0, 8.0, -0.0, -5.0, 10.0, 3.0, 6.0, 3.0, 2.0, -8.0,
};
static const double expected_cos[10] = {
    2.634752140995199110787593e-01, 1.148551260848219865642039e-01,
    9.6191297325640768154550453e-01, 2.938141150061714816890637e-01,
   -9.777138189897924126294461e-01, -9.7693041344303219127199518e-01,
    4.940088096948647263961162e-01, -9.1565869021018925545016502e-01,
   -2.517729313893103197176091e-01, -7.39241351595676573201918e-01,
};
static const double expected_cosh[10] = {
    7.2668796942212842775517446e+01, 1.1479413465659254502011135e+03,
    1.0385767908766418550935495e+00, 7.5000957789658051428857788e+01,
    7.655246669605357888468613e+03, 9.3567491758321272072888257e+00,
    9.331351599270605471131735e+01, 7.6833430994624643209296404e+00,
    3.1829371625150718153881164e+00, 2.9595059261916188501640911e+03,
};
static const double expected_erf[10] = {
    5.1865354817738701906913566e-01, 7.2623875834137295116929844e-01,
   -3.123458688281309990629839e-02, -5.2143121110253302920437013e-01,
    8.2704742671312902508629582e-01, 3.2101767558376376743993945e-01,
    5.403990312223245516066252e-01, 3.0034702916738588551174831e-01,
    2.0369924417882241241559589e-01, -7.8069386968009226729944677e-01,
};
static const double expected_erfc[10] = {
    4.8134645182261298093086434e-01, 2.7376124165862704883070156e-01,
    1.0312345868828130999062984e+00, 1.5214312111025330292043701e+00,
    1.7295257328687097491370418e-01, 6.7898232441623623256006055e-01,
    4.596009687776754483933748e-01, 6.9965297083261411448825169e-01,
    7.9630075582117758758440411e-01, 1.7806938696800922672994468e+00,
};
static const double expected_erfinv[10] = {
    4.746037673358033586786350696e-01, 8.559054432692110956388764172e-01,
   -2.45427830571707336251331946e-02, -4.78116683518973366268905506e-01,
    1.479804430319470983648120853e+00, 2.654485787128896161882650211e-01,
    5.027444534221520197823192493e-01, 2.466703532707627818954585670e-01,
    1.632011465103005426240343116e-01, -1.06672334642196900710000389e+00,
};
static const double expected_exp[10] = {
    1.4533071302642137507696589e+02, 2.2958822575694449002537581e+03,
    7.5814542574851666582042306e-01, 6.6668778421791005061482264e-03,
    1.5310493273896033740861206e+04, 1.8659907517999328638667732e+01,
    1.8662167355098714543942057e+02, 1.5301332413189378961665788e+01,
    6.2047063430646876349125085e+00, 1.6894712385826521111610438e-04,
};
static const double expected_exp2[10] = {
    3.1537839463286288034313104e+01, 2.1361549283756232296144849e+02,
    8.2537402562185562902577219e-01, 3.1021158628740294833424229e-02,
    7.9581744110252191462569661e+02, 7.6019905892596359262696423e+00,
    3.7506882048388096973183084e+01, 6.6250893439173561733216375e+00,
    3.5438267900243941544605339e+00, 2.4281533133513300984289196e-03,
};
static const double expected_expm1[10] = {
    5.105047796122957327384770212e-02, 8.046199708567344080562675439e-02,
   -2.764970978891639815187418703e-03, -4.8871434888875355394330300273e-02,
    1.0115864277221467777117227494e-01, 2.969616407795910726014621657e-02,
    5.368214487944892300914037972e-02, 2.765488851131274068067445335e-02,
    1.842068661871398836913874273e-02, -8.3193870863553801814961137573e-02,
};
static const double expected_fabs[10] = {
    4.9790119248836735e+00, 7.7388724745781045e+00,
    2.7688005719200159e-01, 5.0106036182710749e+00,
    9.6362937071984173e+00, 2.9263772392439646e+00,
    5.2290834314593066e+00, 2.7279399104360102e+00,
    1.8253080916808550e+00, 8.6859247685756013e+00,
};
static const double expected_floor[10] = {
    4.0, 7.0, -1.0, -6.0, 9.0, 2.0, 5.0, 2.0, 1.0, -9.0,
};
static const double expected_gamma[10] = {
    2.3254348370739963835386613898e+01, 2.991153837155317076427529816e+03,
   -4.561154336726758060575129109e+00, 7.719403468842639065959210984e-01,
    1.6111876618855418534325755566e+05, 1.8706575145216421164173224946e+00,
    3.4082787447257502836734201635e+01, 1.579733951448952054898583387e+00,
    9.3834586598354592860187267089e-01, -2.093995902923148389186189429e-05,
};
static const double expected_log[10] = {
    1.605231462693062999102599e+00, 2.0462560018708770653153909e+00,
   -1.2841708730962657801275038e+00, 1.6115563905281545116286206e+00,
    2.2655365644872016636317461e+00, 1.0737652208918379856272735e+00,
    1.6542360106073546632707956e+00, 1.0035467127723465801264487e+00,
    6.0174879014578057187016475e-01, 2.161703872847352815363655e+00,
};
static const double expected_log10[10] = {
    6.9714316642508290997617083e-01, 8.886776901739320576279124e-01,
   -5.5770832400658929815908236e-01, 6.998900476822994346229723e-01,
    9.8391002850684232013281033e-01, 4.6633031029295153334285302e-01,
    7.1842557117242328821552533e-01, 4.3583479968917773161304553e-01,
    2.6133617905227038228626834e-01, 9.3881606348649405716214241e-01,
};
static const double expected_log1p[10] = {
    4.8590257759797794104158205e-02, 7.4540265965225865330849141e-02,
   -2.7726407903942672823234024e-03, -5.1404917651627649094953380e-02,
    9.1998280672258624681335010e-02, 2.8843762576593352865894824e-02,
    5.0969534581863707268992645e-02, 2.6913947602193238458458594e-02,
    1.8088493239630770262045333e-02, -9.0865245631588989681559268e-02,
};
static const double expected_log2[10] = {
    2.3158594707062190618898251e+00, 2.9521233862883917703341018e+00,
   -1.8526669502700329984917062e+00, 2.3249844127278861543568029e+00,
    3.268478366538305087466309e+00, 1.5491157592596970278166492e+00,
    2.3865580889631732407886495e+00, 1.447811865817085365540347e+00,
    8.6813999540425116282815557e-01, 3.118679457227342224364709e+00,
};
static const double expected_logb[10] = {
    2.0, 2.0, -2.0, 2.0, 3.0, 1.0, 2.0, 1.0, 0.0, 3.0,
};
static const double expected_pow[10] = {
    9.5282232631648411840742957e+04, 5.4811599352999901232411871e+07,
    5.2859121715894396531132279e-01, 9.7587991957286474464259698e-06,
    4.328064329346044846740467e+09, 8.4406761805034547437659092e+02,
    1.6946633276191194947742146e+05, 5.3449040147551939075312879e+02,
    6.688182138451414936380374e+01, 2.0609869004248742886827439e-09,
};
static const double expected_sin[10] = {
   -9.6466616586009283766724726e-01, 9.9338225271646545763467022e-01,
   -2.7335587039794393342449301e-01, 9.5586257685042792878173752e-01,
   -2.099421066779969164496634e-01, 2.135578780799860532750616e-01,
   -8.694568971167362743327708e-01, 4.019566681155577786649878e-01,
    9.6778633541687993721617774e-01, -6.734405869050344734943028e-01,
};
static const double expected_sinh[10] = {
    7.2661916084208532301448439e+01, 1.1479409110035194500526446e+03,
   -2.8043136512812518927312641e-01, -7.499429091181587232835164e+01,
    7.6552466042906758523925934e+03, 9.3031583421672014313789064e+00,
    9.330815755828109072810322e+01, 7.6179893137269146407361477e+00,
    3.021769180549615819524392e+00, -2.95950575724449499189888e+03,
};
static const double expected_sqrt[10] = {
    2.2313699659365484748756904e+00, 2.7818829009464263511285458e+00,
    5.2619393496314796848143251e-01, 2.2384377628763938724244104e+00,
    3.1042380236055381099288487e+00, 1.7106657298385224403917771e+00,
    2.286718922705479046148059e+00, 1.6516476350711159636222979e+00,
    1.3510396336454586262419247e+00, 2.9471892997524949215723329e+00,
};
static const double expected_tan[10] = {
   -3.661316565040227801781974e+00, 8.64900232648597589369854e+00,
   -2.8417941955033612725238097e-01, 3.253290185974728640827156e+00,
    2.147275640380293804770778e-01, -2.18600910711067004921551e-01,
   -1.760002817872367935518928e+00, -4.389808914752818126249079e-01,
   -3.843885560201130679995041e+00, 9.10988793377685105753416e-01,
};
static const double expected_tanh[10] = {
    9.9990531206936338549262119e-01, 9.9999962057085294197613294e-01,
   -2.7001505097318677233756845e-01, -9.9991110943061718603541401e-01,
    9.9999999146798465745022007e-01, 9.9427249436125236705001048e-01,
    9.9994257600983138572705076e-01, 9.9149409509772875982054701e-01,
    9.4936501296239685514466577e-01, -9.9999994291374030946055701e-01,
};
static const double expected_trunc[10] = {
    4.0, 7.0, -0.0, -5.0, 9.0, 2.0, 5.0, 2.0, 1.0, -8.0,
};
static const double expected_j0[10] = {
   -1.8444682230601672018219338e-01, 2.27353668906331975435892e-01,
    9.809259936157051116270273e-01, -1.741170131426226587841181e-01,
   -2.1389448451144143352039069e-01, -2.340905848928038763337414e-01,
   -1.0029099691890912094586326e-01, -1.5466726714884328135358907e-01,
    3.252650187653420388714693e-01, -8.72218484409407250005360235e-03,
};
static const double expected_j1[10] = {
   -3.251526395295203422162967e-01, 1.893581711430515718062564e-01,
   -1.3711761352467242914491514e-01, 3.287486536269617297529617e-01,
    1.3133899188830978473849215e-01, 3.660243417832986825301766e-01,
   -3.4436769271848174665420672e-01, 4.329481396640773768835036e-01,
    5.8181350531954794639333955e-01, -2.7030574577733036112996607e-01,
};
static const double expected_y0[10] = {
   -3.053399153780788357534855e-01, 1.7437227649515231515503649e-01,
   -8.6221781263678836910392572e-01, -3.100664880987498407872839e-01,
    1.422200649300982280645377e-01, 4.000004067997901144239363e-01,
   -3.3340749753099352392332536e-01, 4.5399790746668954555205502e-01,
    4.8290004112497761007536522e-01, 2.7036697826604756229601611e-01,
};
static const double expected_y1[10] = {
    0.15494213737457922210218611, -0.2165955142081145245075746,
   -2.4644949631241895201032829, 0.1442740489541836405154505,
    0.2215379960518984777080163, 0.3038800915160754150565448,
    0.0691107642452362383808547, 0.2380116417809914424860165,
   -0.20849492979459761009678934, 0.0242503179793232308250804,
};
static const double expected_copysign[10] = {
   -4.9790119248836735e+00, -7.7388724745781045e+00,
   -2.7688005719200159e-01, -5.0106036182710749e+00,
   -9.6362937071984173e+00, -2.9263772392439646e+00,
   -5.2290834314593066e+00, -2.7279399104360102e+00,
   -1.8253080916808550e+00, -8.6859247685756013e+00,
};
static const double expected_dim[10] = {
    4.9790119248836735e+00, 7.7388724745781045e+00,
    0.0, 0.0,
    9.6362937071984173e+00, 2.9263772392439646e+00,
    5.2290834314593066e+00, 2.7279399104360102e+00,
    1.8253080916808550e+00, 0.0,
};

/* ========== Test groups using Go's vf[] vector ========== */

static void test_trig_vectors(void) {
    char buf[128];
    printf("[trig vectors - sin/cos/tan]\n");
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "sin(vf[%d])", i);
        check_double(buf, neverc_math_sin(vf[i]), expected_sin[i]);
        snprintf(buf, sizeof(buf), "cos(vf[%d])", i);
        check_double(buf, neverc_math_cos(vf[i]), expected_cos[i]);
        snprintf(buf, sizeof(buf), "tan(vf[%d])", i);
        check_double(buf, neverc_math_tan(vf[i]), expected_tan[i]);
    }
}

static void test_inv_trig_vectors(void) {
    char buf[128];
    printf("[inv trig vectors - asin/acos/atan]\n");
    for (int i = 0; i < 10; i++) {
        double x = vf[i] / 10.0;
        snprintf(buf, sizeof(buf), "asin(vf[%d]/10)", i);
        check_double(buf, neverc_math_asin(x), expected_asin[i]);
        snprintf(buf, sizeof(buf), "acos(vf[%d]/10)", i);
        check_double(buf, neverc_math_acos(x), expected_acos[i]);
        snprintf(buf, sizeof(buf), "atan(vf[%d])", i);
        check_double(buf, neverc_math_atan(vf[i]), expected_atan[i]);
    }
}

static void test_hyp_vectors(void) {
    char buf[128];
    printf("[hyperbolic vectors - sinh/cosh/tanh]\n");
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "sinh(vf[%d])", i);
        check_double(buf, neverc_math_sinh(vf[i]), expected_sinh[i]);
        snprintf(buf, sizeof(buf), "cosh(vf[%d])", i);
        check_double(buf, neverc_math_cosh(vf[i]), expected_cosh[i]);
        snprintf(buf, sizeof(buf), "tanh(vf[%d])", i);
        check_double(buf, neverc_math_tanh(vf[i]), expected_tanh[i]);
    }
}

static void test_inv_hyp_vectors(void) {
    char buf[128];
    printf("[inv hyperbolic vectors - asinh/acosh/atanh]\n");
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "asinh(vf[%d])", i);
        check_double(buf, neverc_math_asinh(vf[i]), expected_asinh[i]);
        double a = 1.0 + neverc_math_abs(vf[i]);
        snprintf(buf, sizeof(buf), "acosh(1+|vf[%d]|)", i);
        check_double(buf, neverc_math_acosh(a), expected_acosh[i]);
        double tx = vf[i] / 10.0;
        snprintf(buf, sizeof(buf), "atanh(vf[%d]/10)", i);
        check_double(buf, neverc_math_atanh(tx), expected_atanh[i]);
    }
}

static void test_exp_vectors(void) {
    char buf[128];
    printf("[exp vectors - exp/exp2/expm1]\n");
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "exp(vf[%d])", i);
        check_double(buf, neverc_math_exp(vf[i]), expected_exp[i]);
        snprintf(buf, sizeof(buf), "exp2(vf[%d])", i);
        check_double(buf, neverc_math_exp2(vf[i]), expected_exp2[i]);
    }
    printf("[expm1 vectors]\n");
    for (int i = 0; i < 10; i++) {
        double x = vf[i] / 100.0;
        snprintf(buf, sizeof(buf), "expm1(vf[%d]/100)", i);
        check_double(buf, neverc_math_expm1(x), expected_expm1[i]);
    }
}

static void test_log_vectors(void) {
    char buf[128];
    printf("[log vectors - log/log2/log10/log1p]\n");
    for (int i = 0; i < 10; i++) {
        double ax = neverc_math_abs(vf[i]);
        snprintf(buf, sizeof(buf), "log(|vf[%d]|)", i);
        check_double(buf, neverc_math_log(ax), expected_log[i]);
        snprintf(buf, sizeof(buf), "log2(|vf[%d]|)", i);
        check_double(buf, neverc_math_log2(ax), expected_log2[i]);
        snprintf(buf, sizeof(buf), "log10(|vf[%d]|)", i);
        check_double(buf, neverc_math_log10(ax), expected_log10[i]);
        snprintf(buf, sizeof(buf), "logb(|vf[%d]|)", i);
        check_double(buf, neverc_math_logb(ax), expected_logb[i]);
    }
    printf("[log1p vectors]\n");
    for (int i = 0; i < 10; i++) {
        double x = vf[i] / 100.0;
        snprintf(buf, sizeof(buf), "log1p(vf[%d]/100)", i);
        check_double(buf, neverc_math_log1p(x), expected_log1p[i]);
    }
}

static void test_pow_vectors(void) {
    char buf[128];
    printf("[pow vectors]\n");
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "pow(10,vf[%d])", i);
        check_double(buf, neverc_math_pow(10.0, vf[i]), expected_pow[i]);
    }
}

static void test_sqrt_cbrt_vectors(void) {
    char buf[128];
    printf("[sqrt/cbrt vectors]\n");
    for (int i = 0; i < 10; i++) {
        double ax = neverc_math_abs(vf[i]);
        snprintf(buf, sizeof(buf), "sqrt(|vf[%d]|)", i);
        check_double(buf, neverc_math_sqrt(ax), expected_sqrt[i]);
        snprintf(buf, sizeof(buf), "cbrt(vf[%d])", i);
        check_double(buf, neverc_math_cbrt(vf[i]), expected_cbrt[i]);
    }
}

static void test_rounding_vectors(void) {
    char buf[128];
    printf("[rounding vectors - ceil/floor/trunc]\n");
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "ceil(vf[%d])", i);
        check_double(buf, neverc_math_ceil(vf[i]), expected_ceil[i]);
        snprintf(buf, sizeof(buf), "floor(vf[%d])", i);
        check_double(buf, neverc_math_floor(vf[i]), expected_floor[i]);
        snprintf(buf, sizeof(buf), "trunc(vf[%d])", i);
        check_double(buf, neverc_math_trunc(vf[i]), expected_trunc[i]);
    }
}

static void test_abs_dim_vectors(void) {
    char buf[128];
    printf("[abs/dim/copysign vectors]\n");
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "abs(vf[%d])", i);
        check_double(buf, neverc_math_abs(vf[i]), expected_fabs[i]);
        snprintf(buf, sizeof(buf), "dim(vf[%d],0)", i);
        check_double(buf, neverc_math_dim(vf[i], 0.0), expected_dim[i]);
        snprintf(buf, sizeof(buf), "copysign(|vf[%d]|,-1)", i);
        check_double(buf, neverc_math_copysign(neverc_math_abs(vf[i]), -1.0),
                     expected_copysign[i]);
    }
}

static void test_erf_vectors(void) {
    char buf[128];
    printf("[erf/erfc/erfinv vectors]\n");
    for (int i = 0; i < 10; i++) {
        double x = vf[i] / 10.0;
        snprintf(buf, sizeof(buf), "erf(vf[%d]/10)", i);
        check_double(buf, neverc_math_erf(x), expected_erf[i]);
        snprintf(buf, sizeof(buf), "erfc(vf[%d]/10)", i);
        check_double(buf, neverc_math_erfc(x), expected_erfc[i]);
        snprintf(buf, sizeof(buf), "erfinv(vf[%d]/10)", i);
        check_double(buf, neverc_math_erfinv(x), expected_erfinv[i]);
    }
}

static void test_gamma_vectors(void) {
    char buf[128];
    printf("[gamma vectors]\n");
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "gamma(vf[%d])", i);
        check_double(buf, neverc_math_gamma(vf[i]), expected_gamma[i]);
    }
}

static void test_bessel_vectors(void) {
    char buf[128];
    printf("[bessel vectors - j0/j1/y0/y1]\n");
    for (int i = 0; i < 10; i++) {
        double ax = neverc_math_abs(vf[i]);
        snprintf(buf, sizeof(buf), "j0(vf[%d])", i);
        check_double(buf, neverc_math_j0(vf[i]), expected_j0[i]);
        snprintf(buf, sizeof(buf), "j1(vf[%d])", i);
        check_double(buf, neverc_math_j1(vf[i]), expected_j1[i]);
        snprintf(buf, sizeof(buf), "y0(|vf[%d]|)", i);
        check_double(buf, neverc_math_y0(ax), expected_y0[i]);
        snprintf(buf, sizeof(buf), "y1(|vf[%d]|)", i);
        check_double(buf, neverc_math_y1(ax), expected_y1[i]);
    }
}

static void test_atan2_vectors(void) {
    char buf[128];
    printf("[atan2 vectors]\n");
    for (int i = 0; i < 10; i++) {
        snprintf(buf, sizeof(buf), "atan2(10,vf[%d])", i);
        check_double(buf, neverc_math_atan2(10.0, vf[i]), expected_atan2[i]);
    }
}

/* ========== Special value tests ========== */

static void test_special_nan_inf(void) {
    printf("[special: NaN/Inf handling]\n");
    check_true("nan is NaN", neverc_math_isnan(NC_NAN));
    check_true("+inf is +Inf", neverc_math_isinf(NC_INF, 1));
    check_true("-inf is -Inf", neverc_math_isinf(NC_NEGINF, -1));
    check_true("isinf(inf,0)", neverc_math_isinf(NC_INF, 0));
    check_true("isinf(-inf,0)", neverc_math_isinf(NC_NEGINF, 0));
    check_int("isnan(1)", neverc_math_isnan(1.0), 0);
    check_int("isinf(1,0)", neverc_math_isinf(1.0, 0), 0);
    check_int("signbit(-0)", neverc_math_signbit(-0.0), 1);
    check_int("signbit(0)", neverc_math_signbit(0.0), 0);
    check_int("signbit(-1)", neverc_math_signbit(-1.0), 1);
    check_int("signbit(1)", neverc_math_signbit(1.0), 0);

    check_double("abs(NaN)", neverc_math_abs(NC_NAN), NC_NAN);
    check_double("abs(+Inf)", neverc_math_abs(NC_INF), NC_INF);
    check_double("abs(-Inf)", neverc_math_abs(NC_NEGINF), NC_INF);

    check_double("sin(NaN)", neverc_math_sin(NC_NAN), NC_NAN);
    check_double("sin(+Inf)", neverc_math_sin(NC_INF), NC_NAN);
    check_double("cos(NaN)", neverc_math_cos(NC_NAN), NC_NAN);
    check_double("cos(+Inf)", neverc_math_cos(NC_INF), NC_NAN);
    check_double("tan(NaN)", neverc_math_tan(NC_NAN), NC_NAN);

    check_double("exp(NaN)", neverc_math_exp(NC_NAN), NC_NAN);
    check_double("exp(+Inf)", neverc_math_exp(NC_INF), NC_INF);
    check_double("exp(-Inf)", neverc_math_exp(NC_NEGINF), 0.0);
    check_double("log(NaN)", neverc_math_log(NC_NAN), NC_NAN);
    check_double("log(+Inf)", neverc_math_log(NC_INF), NC_INF);
    check_double("log(0)", neverc_math_log(0.0), NC_NEGINF);
    check_double("log(-1)", neverc_math_log(-1.0), NC_NAN);

    check_double("sqrt(-1)", neverc_math_sqrt(-1.0), NC_NAN);
    check_double("sqrt(NaN)", neverc_math_sqrt(NC_NAN), NC_NAN);
    check_double("sqrt(+Inf)", neverc_math_sqrt(NC_INF), NC_INF);

    check_double("asin(2)", neverc_math_asin(2.0), NC_NAN);
    check_double("acos(-2)", neverc_math_acos(-2.0), NC_NAN);
    check_double("acosh(0.5)", neverc_math_acosh(0.5), NC_NAN);

    check_double("pow(NaN,1)", neverc_math_pow(NC_NAN, 1.0), NC_NAN);
    check_double("pow(x,0)=1", neverc_math_pow(42.0, 0.0), 1.0);
    check_double("pow(1,y)=1", neverc_math_pow(1.0, 999.0), 1.0);
    check_double("pow(+Inf,1)", neverc_math_pow(NC_INF, 1.0), NC_INF);

    check_double("j0(NaN)", neverc_math_j0(NC_NAN), NC_NAN);
    check_double("j0(+Inf)", neverc_math_j0(NC_INF), 0.0);
    check_double("j1(0)", neverc_math_j1(0.0), 0.0);
    check_double("y0(0)", neverc_math_y0(0.0), NC_NEGINF);
    check_double("y0(-1)", neverc_math_y0(-1.0), NC_NAN);
    check_double("y1(0)", neverc_math_y1(0.0), NC_NEGINF);

    check_double("erf(0)", neverc_math_erf(0.0), 0.0);
    check_double("erf(+Inf)", neverc_math_erf(NC_INF), 1.0);
    check_double("erf(-Inf)", neverc_math_erf(NC_NEGINF), -1.0);
    check_double("erfc(0)", neverc_math_erfc(0.0), 1.0);
    check_double("erfinv(0)", neverc_math_erfinv(0.0), 0.0);
    check_double("erfinv(1)", neverc_math_erfinv(1.0), NC_INF);
    check_double("erfinv(-1)", neverc_math_erfinv(-1.0), NC_NEGINF);
    check_double("erfinv(2)", neverc_math_erfinv(2.0), NC_NAN);

    check_double("gamma(1)", neverc_math_gamma(1.0), 1.0);
    check_double("gamma(5)=24", neverc_math_gamma(5.0), 24.0);
    check_double("gamma(NaN)", neverc_math_gamma(NC_NAN), NC_NAN);
    check_double("lgamma(1)", neverc_math_lgamma(1.0), 0.0);
    check_double("lgamma(2)", neverc_math_lgamma(2.0), 0.0);

    check_double("dim(+Inf,+Inf)", neverc_math_dim(NC_INF, NC_INF), NC_NAN);
    check_double("dim(NaN,1)", neverc_math_dim(NC_NAN, 1.0), NC_NAN);
    check_double("max(NaN,1)", neverc_math_max(NC_NAN, 1.0), NC_NAN);
    check_double("min(NaN,1)", neverc_math_min(NC_NAN, 1.0), NC_NAN);

    check_double("tanh(+Inf)", neverc_math_tanh(NC_INF), 1.0);
    check_double("tanh(-Inf)", neverc_math_tanh(NC_NEGINF), -1.0);
    check_double("atanh(1)", neverc_math_atanh(1.0), NC_INF);
    check_double("atanh(-1)", neverc_math_atanh(-1.0), NC_NEGINF);
    check_double("log1p(-1)", neverc_math_log1p(-1.0), NC_NEGINF);

    check_double("hypot(+Inf,1)", neverc_math_hypot(NC_INF, 1.0), NC_INF);
    check_double("hypot(NaN,1)", neverc_math_hypot(NC_NAN, 1.0), NC_NAN);
    check_double("fmod(NaN,1)", neverc_math_fmod(NC_NAN, 1.0), NC_NAN);
    check_double("fmod(1,0)", neverc_math_fmod(1.0, 0.0), NC_NAN);
    check_double("remainder(NaN,1)", neverc_math_remainder(NC_NAN, 1.0), NC_NAN);

    check_double("atan2(NaN,1)", neverc_math_atan2(NC_NAN, 1.0), NC_NAN);
    check_double("atan2(0,1)", neverc_math_atan2(0.0, 1.0), 0.0);
    check_double("atan2(1,0)", neverc_math_atan2(1.0, 0.0), NEVERC_MATH_PI / 2.0);
}

/* ========== Mathematical identity tests ========== */

static void test_identities(void) {
    printf("[identities]\n");
    char buf[128];

    for (int i = 0; i < 10; i++) {
        /* sin²(x) + cos²(x) = 1 */
        double s = neverc_math_sin(vf[i]);
        double c = neverc_math_cos(vf[i]);
        snprintf(buf, sizeof(buf), "sin²+cos²=1 (vf[%d])", i);
        check_double(buf, s * s + c * c, 1.0);

        /* sincos consistency */
        double sv, cv;
        neverc_math_sincos(vf[i], &sv, &cv);
        snprintf(buf, sizeof(buf), "sincos.sin==sin (vf[%d])", i);
        check_double(buf, sv, s);
        snprintf(buf, sizeof(buf), "sincos.cos==cos (vf[%d])", i);
        check_double(buf, cv, c);
    }

    for (int i = 0; i < 10; i++) {
        double ax = neverc_math_abs(vf[i]);
        /* exp(log(x)) = x */
        snprintf(buf, sizeof(buf), "exp(log(x))=x (|vf[%d]|)", i);
        check_double(buf, neverc_math_exp(neverc_math_log(ax)), ax);

        /* log(exp(x)) = x (for moderate values) */
        if (ax < 700.0) {
            snprintf(buf, sizeof(buf), "log(exp(x))=x (vf[%d])", i);
            check_double(buf, neverc_math_log(neverc_math_exp(vf[i])), vf[i]);
        }

        /* sqrt(x)² = x */
        snprintf(buf, sizeof(buf), "sqrt(x)²=x (|vf[%d]|)", i);
        double sq = neverc_math_sqrt(ax);
        check_double(buf, sq * sq, ax);

        /* cbrt(x)³ = x */
        snprintf(buf, sizeof(buf), "cbrt(x)³=x (vf[%d])", i);
        double cb = neverc_math_cbrt(vf[i]);
        check_double(buf, cb * cb * cb, vf[i]);
    }

    /* erf(erfinv(x)) = x round-trip */
    double test_erfinv_vals[] = { 0.1, 0.3, 0.5, 0.7, 0.9, -0.1, -0.5, -0.9 };
    for (int i = 0; i < 8; i++) {
        double x = test_erfinv_vals[i];
        snprintf(buf, sizeof(buf), "erf(erfinv(%.1f))=%.1f", x, x);
        check_double(buf, neverc_math_erf(neverc_math_erfinv(x)), x);
    }

    /* erf(x) + erfc(x) = 1 */
    for (int i = 0; i < 10; i++) {
        double x = vf[i] / 10.0;
        snprintf(buf, sizeof(buf), "erf+erfc=1 (vf[%d]/10)", i);
        check_double(buf, neverc_math_erf(x) + neverc_math_erfc(x), 1.0);
    }

    /* cosh²(x) - sinh²(x) = 1 (use small values to avoid cancellation) */
    {
        double small_vals[] = { 0.1, 0.5, 1.0, -0.3, -1.0 };
        for (int i = 0; i < 5; i++) {
            double sh = neverc_math_sinh(small_vals[i]);
            double ch = neverc_math_cosh(small_vals[i]);
            snprintf(buf, sizeof(buf), "cosh²-sinh²=1 (%.1f)", small_vals[i]);
            check_double(buf, ch * ch - sh * sh, 1.0);
        }
    }

    /* pow(x, 1) = x, pow(x, 0) = 1 */
    for (int i = 0; i < 10; i++) {
        double ax = neverc_math_abs(vf[i]);
        snprintf(buf, sizeof(buf), "pow(x,1)=x (|vf[%d]|)", i);
        check_double(buf, neverc_math_pow(ax, 1.0), ax);
        snprintf(buf, sizeof(buf), "pow(x,0)=1 (vf[%d])", i);
        check_double(buf, neverc_math_pow(vf[i], 0.0), 1.0);
    }

    /* log2(2^n) = n */
    for (int n = -10; n <= 10; n++) {
        snprintf(buf, sizeof(buf), "log2(2^%d)=%d", n, n);
        check_double(buf, neverc_math_log2(neverc_math_pow(2.0, (double)n)), (double)n);
    }

    /* floor(x) <= x < floor(x)+1 for finite non-integer x */
    for (int i = 0; i < 10; i++) {
        double fl = neverc_math_floor(vf[i]);
        snprintf(buf, sizeof(buf), "floor<=x (vf[%d])", i);
        check_true(buf, fl <= vf[i]);
        snprintf(buf, sizeof(buf), "x<floor+1 (vf[%d])", i);
        check_true(buf, vf[i] < fl + 1.0);
    }
}

/* ========== Decomposition function tests ========== */

static void test_decomposition(void) {
    char buf[128];
    printf("[decomposition: modf/frexp/ldexp]\n");

    /* Go's expected frexp values */
    static const double frexp_frac[10] = {
        6.2237649061045918750e-01, 9.6735905932226306250e-01,
       -5.5376011438400318000e-01, -6.2632545228388436250e-01,
        6.02268356699901081250e-01, 7.3159430981099115000e-01,
        6.5363542893241332500e-01, 6.8198497760900255000e-01,
        9.1265404584042750000e-01, -5.4287029803597508250e-01,
    };
    static const int frexp_exp[10] = { 3, 3, -1, 3, 4, 2, 3, 2, 1, 4 };

    for (int i = 0; i < 10; i++) {
        int exp;
        double frac = neverc_math_frexp(vf[i], &exp);
        snprintf(buf, sizeof(buf), "frexp(vf[%d]).frac", i);
        check_double(buf, frac, frexp_frac[i]);
        snprintf(buf, sizeof(buf), "frexp(vf[%d]).exp", i);
        check_int(buf, exp, frexp_exp[i]);
    }

    /* modf: integer + fraction */
    for (int i = 0; i < 10; i++) {
        double ipart;
        double fpart = neverc_math_modf(vf[i], &ipart);
        snprintf(buf, sizeof(buf), "modf(vf[%d]) sum", i);
        check_double(buf, ipart + fpart, vf[i]);
        snprintf(buf, sizeof(buf), "modf(vf[%d]) |frac|<1", i);
        check_true(buf, neverc_math_abs(fpart) < 1.0);
    }

    /* ldexp: frexp round-trip */
    for (int i = 0; i < 10; i++) {
        int exp;
        double frac = neverc_math_frexp(vf[i], &exp);
        double back = neverc_math_ldexp(frac, exp);
        snprintf(buf, sizeof(buf), "ldexp(frexp(vf[%d]))", i);
        check_double(buf, back, vf[i]);
    }

    /* nextafter */
    printf("[nextafter]\n");
    double na = neverc_math_nextafter(1.0, 2.0);
    check_true("nextafter(1,2)>1", na > 1.0);
    na = neverc_math_nextafter(1.0, 0.0);
    check_true("nextafter(1,0)<1", na < 1.0);
    check_double("nextafter(x,x)=x", neverc_math_nextafter(1.0, 1.0), 1.0);
    check_double("nextafter(0,1)>0",
                 neverc_math_nextafter(0.0, 1.0) > 0.0 ? 1.0 : 0.0, 1.0);
}

/* ========== Round/mod tests ========== */

static void test_round_mod(void) {
    printf("[round/roundtoeven/fmod/remainder]\n");

    check_double("round(1.5)", neverc_math_round(1.5), 2.0);
    check_double("round(2.5)", neverc_math_round(2.5), 3.0);
    check_double("round(-1.5)", neverc_math_round(-1.5), -2.0);
    check_double("round(0.4)", neverc_math_round(0.4), 0.0);

    check_double("rte(0.5)", neverc_math_roundtoeven(0.5), 0.0);
    check_double("rte(1.5)", neverc_math_roundtoeven(1.5), 2.0);
    check_double("rte(2.5)", neverc_math_roundtoeven(2.5), 2.0);
    check_double("rte(3.5)", neverc_math_roundtoeven(3.5), 4.0);
    check_double("rte(-0.5)", neverc_math_roundtoeven(-0.5), -0.0);
    check_double("rte(-1.5)", neverc_math_roundtoeven(-1.5), -2.0);
    check_double("rte(100.0)", neverc_math_roundtoeven(100.0), 100.0);
    check_double("rte(NaN)", neverc_math_roundtoeven(NC_NAN), NC_NAN);
    check_double("rte(+Inf)", neverc_math_roundtoeven(NC_INF), NC_INF);

    check_double("fmod(5,3)", neverc_math_fmod(5.0, 3.0), 2.0);
    check_double("fmod(7,2)", neverc_math_fmod(7.0, 2.0), 1.0);
    check_double("fmod(-5,3)", neverc_math_fmod(-5.0, 3.0), -2.0);

    check_double("rem(5,3)", neverc_math_remainder(5.0, 3.0), -1.0);
    check_double("rem(7,2)", neverc_math_remainder(7.0, 2.0), -1.0);
}

/* ========== Bit-level helper tests ========== */

static void test_float_bits(void) {
    printf("[float bits]\n");
    for (int i = 0; i < 10; i++) {
        char buf[128];
        uint64_t bits = neverc_math_float64bits(vf[i]);
        double back = neverc_math_float64frombits(bits);
        snprintf(buf, sizeof(buf), "f64 roundtrip vf[%d]", i);
        check_double(buf, back, vf[i]);
    }
    check_true("float64bits(1.0)==0x3FF0...",
               neverc_math_float64bits(1.0) == 0x3FF0000000000000ULL);
    check_true("float32bits(1.0f)==0x3F800000",
               neverc_math_float32bits(1.0f) == 0x3F800000U);

    float fv = 3.14f;
    float fback = neverc_math_float32frombits(neverc_math_float32bits(fv));
    check_double("f32 roundtrip", (double)fback, (double)fv);
}

/* ========== FMA test ========== */

static void test_fma(void) {
    printf("[fma]\n");
    check_double("fma(2,3,4)", neverc_math_fma(2.0, 3.0, 4.0), 10.0);
    check_double("fma(0,0,0)", neverc_math_fma(0.0, 0.0, 0.0), 0.0);
    check_double("fma(1,2,3)", neverc_math_fma(1.0, 2.0, 3.0), 5.0);
    check_double("fma(-2,3,4)", neverc_math_fma(-2.0, 3.0, 4.0), -2.0);
    check_double("fma(2,-3,4)", neverc_math_fma(2.0, -3.0, 4.0), -2.0);
    check_double("fma(2,3,-4)", neverc_math_fma(2.0, 3.0, -4.0), 2.0);
    check_double("fma(1e308,2,-1e308)", neverc_math_fma(1e308, 2.0, -1e308), 1e308);
    check_double("fma(NaN,1,1)", neverc_math_fma(NC_NAN, 1.0, 1.0), NC_NAN);
    check_double("fma(1,NaN,1)", neverc_math_fma(1.0, NC_NAN, 1.0), NC_NAN);
    check_double("fma(1,1,NaN)", neverc_math_fma(1.0, 1.0, NC_NAN), NC_NAN);
    check_double("fma(Inf,1,1)", neverc_math_fma(NC_INF, 1.0, 1.0), NC_INF);
    check_double("fma(0,Inf,NaN)", neverc_math_fma(0.0, NC_INF, NC_NAN), NC_NAN);
    check_double("fma(1,1,Inf)", neverc_math_fma(1.0, 1.0, NC_INF), NC_INF);
    check_double("fma(1,1,-Inf)", neverc_math_fma(1.0, 1.0, NC_NEGINF), NC_NEGINF);

    /*
     * Single-rounding test: values where naive x*y+z (two roundings) gives
     * a different answer than correct FMA (one rounding).
     * These test the core correctness property of FMA.
     */
    {
        double a = 1.0 + 0x1p-52;
        double b = 1.0 + 0x1p-52;
        double c = -1.0;
        double fma_result = neverc_math_fma(a, b, c);
        double naive = a * b + c;
        check_true("fma single-round differs from naive",
            neverc_math_float64bits(fma_result) != neverc_math_float64bits(naive) ||
            fma_result == naive);
        check_double("fma((1+ulp)^2-1)",
            fma_result, 0x1p-51 + 0x1p-104);
    }

    /* Cancellation: p == -z should return +0 */
    check_double("fma(2,3,-6)=+0", neverc_math_fma(2.0, 3.0, -6.0), 0.0);

    /* Subnormal results */
    check_true("fma subnormal not zero",
        neverc_math_fma(0x1p-1022, 0.5, 0.0) != 0.0);
}

/* ========== pow10/hypot extra ========== */

static void test_pow10_hypot(void) {
    printf("[pow10/hypot]\n");
    check_double("pow10(0)", neverc_math_pow10(0), 1.0);
    check_double("pow10(1)", neverc_math_pow10(1), 10.0);
    check_double("pow10(3)", neverc_math_pow10(3), 1000.0);
    check_double("pow10(-1)", neverc_math_pow10(-1), 0.1);
    check_double("pow10(22)", neverc_math_pow10(22), 1e22);

    check_double("hypot(3,4)", neverc_math_hypot(3.0, 4.0), 5.0);
    check_double("hypot(5,12)", neverc_math_hypot(5.0, 12.0), 13.0);
    check_double("hypot(0,0)", neverc_math_hypot(0.0, 0.0), 0.0);
}

/* ========== jn/yn tests ========== */

static void test_jn_yn(void) {
    printf("[jn/yn]\n");
    check_double("jn(0,1)==j0(1)", neverc_math_jn(0, 1.0), neverc_math_j0(1.0));
    check_double("jn(1,1)==j1(1)", neverc_math_jn(1, 1.0), neverc_math_j1(1.0));
    check_double("jn(n,NaN)", neverc_math_jn(2, NC_NAN), NC_NAN);
    check_double("jn(n,+Inf)", neverc_math_jn(2, NC_INF), 0.0);
    check_double("yn(0,1)==y0(1)", neverc_math_yn(0, 1.0), neverc_math_y0(1.0));
    check_double("yn(1,1)==y1(1)", neverc_math_yn(1, 1.0), neverc_math_y1(1.0));
    check_double("yn(n,-1)", neverc_math_yn(2, -1.0), NC_NAN);
    check_double("yn(0,0)", neverc_math_yn(0, 0.0), NC_NEGINF);
}

/* ========== Constants test ========== */

static void test_constants(void) {
    printf("[constants]\n");
    check_double("E", NEVERC_MATH_E, 2.718281828459045);
    check_double("PI", NEVERC_MATH_PI, 3.141592653589793);
    check_double("PHI", NEVERC_MATH_PHI, 1.618033988749895);
    check_double("SQRT2", NEVERC_MATH_SQRT2, 1.4142135623730951);
    check_double("LN2", NEVERC_MATH_LN2, 0.6931471805599453);
    check_double("LOG2E", NEVERC_MATH_LOG2E, 1.4426950408889634);
    check_double("LN10", NEVERC_MATH_LN10, 2.302585092994046);
    check_double("LOG10E", NEVERC_MATH_LOG10E, 0.4342944819032518);

    check_true("MAX_INT8", NEVERC_MATH_MAX_INT8 == 127);
    check_true("MIN_INT8", NEVERC_MATH_MIN_INT8 == -128);
    check_true("MAX_UINT8", NEVERC_MATH_MAX_UINT8 == 255U);
}

/* ========== lgamma precision test (regression for overflow bug) ========== */

static void test_lgamma_precision(void) {
    printf("[lgamma precision]\n");

    /* These values MUST NOT be Inf — the old implementation returned Inf
       because it used log(|gamma(x)|) which overflows for x >= ~170 */
    check_double("lgamma(5)=ln(24)", neverc_math_lgamma(5.0), neverc_math_log(24.0));
    check_double("lgamma(3)=ln(2)", neverc_math_lgamma(3.0), neverc_math_log(2.0));
    check_double("lgamma(1)", neverc_math_lgamma(1.0), 0.0);
    check_double("lgamma(2)", neverc_math_lgamma(2.0), 0.0);

    /* Verify against ln(n!) computed by summation for large arguments */
    {
        double sum99 = 0.0;
        for (int k = 1; k <= 99; k++) sum99 += neverc_math_log((double)k);
        check_double("lgamma(100)=ln(99!)", neverc_math_lgamma(100.0), sum99);
    }
    {
        double sum199 = 0.0;
        for (int k = 1; k <= 199; k++) sum199 += neverc_math_log((double)k);
        check_double("lgamma(200)=ln(199!)", neverc_math_lgamma(200.0), sum199);
    }

    /* The critical regression — these were Inf before */
    check_true("lgamma(200) is finite", !neverc_math_isinf(neverc_math_lgamma(200.0), 0));
    check_true("lgamma(500) is finite", !neverc_math_isinf(neverc_math_lgamma(500.0), 0));
    check_true("lgamma(1000) is finite", !neverc_math_isinf(neverc_math_lgamma(1000.0), 0));

    /* Small positive values */
    check_double("lgamma(0.5)", neverc_math_lgamma(0.5), 0.5723649429247001);
    check_double("lgamma(1.5)", neverc_math_lgamma(1.5), -0.12078223763524522);

    /* Special cases */
    check_double("lgamma(+Inf)", neverc_math_lgamma(NC_INF), NC_INF);
    check_double("lgamma(0)", neverc_math_lgamma(0.0), NC_INF);
    check_double("lgamma(NaN)", neverc_math_lgamma(NC_NAN), NC_NAN);
    check_double("lgamma(-1)", neverc_math_lgamma(-1.0), NC_INF);
    check_double("lgamma(-2)", neverc_math_lgamma(-2.0), NC_INF);

    /* Negative non-integer */
    check_true("lgamma(-0.5) finite", !neverc_math_isinf(neverc_math_lgamma(-0.5), 0));
    check_true("lgamma(-1.5) finite", !neverc_math_isinf(neverc_math_lgamma(-1.5), 0));
}

/* ========== expm1 precision test (regression for cancellation bug) ========== */

static void test_expm1_precision(void) {
    printf("[expm1 precision]\n");

    /* The key test: for very small x, expm1(x) ≈ x + x²/2.
       The old implementation returned 0 for exp(1e-16)-1 due to cancellation. */
    double small_vals[] = {1e-8, 1e-10, 1e-14, 1e-16, 1e-20};
    for (int i = 0; i < 5; i++) {
        double x = small_vals[i];
        double result = neverc_math_expm1(x);
        char buf[128];
        snprintf(buf, sizeof(buf), "expm1(%.0e) != 0", x);
        check_true(buf, result != 0.0);
        snprintf(buf, sizeof(buf), "expm1(%.0e) ~ x (rel)", x);
        double rel = neverc_math_abs((result - x) / x);
        check_true(buf, rel < x);
    }

    /* Negative small values */
    check_true("expm1(-1e-16) != 0", neverc_math_expm1(-1e-16) != 0.0);
    check_true("expm1(-1e-20) != 0", neverc_math_expm1(-1e-20) != 0.0);

    /* Known values */
    check_double("expm1(0)", neverc_math_expm1(0.0), 0.0);
    check_double("expm1(+Inf)", neverc_math_expm1(NC_INF), NC_INF);
    check_double("expm1(-Inf)", neverc_math_expm1(NC_NEGINF), -1.0);
    check_double("expm1(NaN)", neverc_math_expm1(NC_NAN), NC_NAN);

    /* Identity: expm1(x) + 1 = exp(x) for moderate values */
    for (int i = 0; i < 10; i++) {
        double x = vf[i] / 10.0;
        char buf[128];
        snprintf(buf, sizeof(buf), "expm1+1=exp (vf[%d]/10)", i);
        check_double(buf, neverc_math_expm1(x) + 1.0, neverc_math_exp(x));
    }

    /* Large negative: expm1(-50) ≈ -1 */
    check_double("expm1(-50)~-1", neverc_math_expm1(-50.0), -1.0);
}

/* ========== log1p precision test (regression for cancellation bug) ========== */

static void test_log1p_precision(void) {
    printf("[log1p precision]\n");

    /* The key test: for very small x, log1p(x) ≈ x - x²/2.
       The old implementation returned 0 for log(1+1e-16) due to cancellation. */
    double small_vals_lp[] = {1e-8, 1e-10, 1e-14, 1e-16, 1e-20};
    for (int i = 0; i < 5; i++) {
        double x = small_vals_lp[i];
        double result = neverc_math_log1p(x);
        char buf[128];
        snprintf(buf, sizeof(buf), "log1p(%.0e) != 0", x);
        check_true(buf, result != 0.0);
        snprintf(buf, sizeof(buf), "log1p(%.0e) ~ x (rel)", x);
        double rel = neverc_math_abs((result - x) / x);
        check_true(buf, rel < x);
    }

    /* Known values */
    check_double("log1p(0)", neverc_math_log1p(0.0), 0.0);
    check_double("log1p(-1)=-Inf", neverc_math_log1p(-1.0), NC_NEGINF);
    check_double("log1p(-2)=NaN", neverc_math_log1p(-2.0), NC_NAN);
    check_double("log1p(+Inf)", neverc_math_log1p(NC_INF), NC_INF);
    check_double("log1p(NaN)", neverc_math_log1p(NC_NAN), NC_NAN);

    /* Identity: log1p(expm1(x)) = x for moderate values */
    double mod_vals[] = {0.01, 0.1, 0.5, 1.0, 2.0, -0.01, -0.1, -0.5};
    for (int i = 0; i < 8; i++) {
        double x = mod_vals[i];
        char buf[128];
        snprintf(buf, sizeof(buf), "log1p(expm1(%.2f))=%.2f", x, x);
        check_double(buf, neverc_math_log1p(neverc_math_expm1(x)), x);
    }

    /* Identity: expm1(log1p(x)) = x */
    for (int i = 0; i < 8; i++) {
        double x = mod_vals[i];
        char buf[128];
        snprintf(buf, sizeof(buf), "expm1(log1p(%.2f))=%.2f", x, x);
        check_double(buf, neverc_math_expm1(neverc_math_log1p(x)), x);
    }
}

/* ========== erfcinv test ========== */

static void test_erfcinv(void) {
    printf("[erfcinv]\n");

    check_double("erfcinv(1)=0", neverc_math_erfcinv(1.0), 0.0);
    check_double("erfcinv(0)=+Inf", neverc_math_erfcinv(0.0), NC_INF);
    check_double("erfcinv(2)=-Inf", neverc_math_erfcinv(2.0), NC_NEGINF);
    check_double("erfcinv(-1)=NaN", neverc_math_erfcinv(-1.0), NC_NAN);
    check_double("erfcinv(3)=NaN", neverc_math_erfcinv(3.0), NC_NAN);
    check_double("erfcinv(NaN)=NaN", neverc_math_erfcinv(NC_NAN), NC_NAN);

    /* erfcinv(x) = erfinv(1-x), verify identity */
    double test_vals[] = {0.1, 0.3, 0.5, 0.7, 0.9, 1.0, 1.5, 1.9};
    for (int i = 0; i < 8; i++) {
        double x = test_vals[i];
        char buf[128];
        snprintf(buf, sizeof(buf), "erfcinv(%.1f)=erfinv(1-%.1f)", x, x);
        double a = neverc_math_erfcinv(x);
        double b = neverc_math_erfinv(1.0 - x);
        if (neverc_math_isnan(a) && neverc_math_isnan(b)) {
            check_true(buf, 1);
        } else {
            check_double(buf, a, b);
        }
    }

    /* Round-trip: erfc(erfcinv(x)) = x */
    double rt_vals[] = {0.1, 0.3, 0.5, 0.8, 1.0, 1.2, 1.5, 1.9};
    for (int i = 0; i < 8; i++) {
        double x = rt_vals[i];
        if (x == 0.0 || x == 2.0) continue;
        char buf[128];
        snprintf(buf, sizeof(buf), "erfc(erfcinv(%.1f))=%.1f", x, x);
        check_double(buf, neverc_math_erfc(neverc_math_erfcinv(x)), x);
    }
}

/* ========== erfc direct computation test ========== */

static void test_erfc_direct(void) {
    printf("[erfc direct computation]\n");

    /* Verify erfc(x) + erf(x) = 1 for all ranges */
    double test_x[] = {0.1, 0.5, 0.84, 0.9, 1.0, 1.2, 1.5, 2.0, 3.0, 5.0};
    for (int i = 0; i < 10; i++) {
        double x = test_x[i];
        char buf[128];
        snprintf(buf, sizeof(buf), "erf(%.1f)+erfc(%.1f)=1", x, x);
        check_double(buf, neverc_math_erf(x) + neverc_math_erfc(x), 1.0);
        snprintf(buf, sizeof(buf), "erf(-%.1f)+erfc(-%.1f)=1", x, x);
        check_double(buf, neverc_math_erf(-x) + neverc_math_erfc(-x), 1.0);
    }

    /* Known precise values */
    check_double("erfc(0)=1", neverc_math_erfc(0.0), 1.0);
    check_double("erfc(+Inf)=0", neverc_math_erfc(NC_INF), 0.0);
    check_double("erfc(-Inf)=2", neverc_math_erfc(NC_NEGINF), 2.0);

    /* Large x: erfc should be very small but non-zero */
    check_true("erfc(5) > 0", neverc_math_erfc(5.0) > 0.0);
    check_true("erfc(10) > 0", neverc_math_erfc(10.0) > 0.0);
    check_true("erfc(27) > 0", neverc_math_erfc(27.0) > 0.0);
    check_double("erfc(28) = 0", neverc_math_erfc(28.0), 0.0);
}

/* ========== Large-argument trig tests (Payne-Hanek reduction) ========== */

static void test_large_trig(void) {
    char buf[128];
    printf("[large trig — Payne-Hanek reduction]\n");

    /*
     * Test with arguments >= 2^29 (536870912) to trigger Payne-Hanek.
     * We verify mathematical properties since exact reference values
     * depend on implementation-specific FP rounding.
     */
    double huge_vals[] = {
        1e9, 1e12, 1e15, 1e18, 1e20, 1e100, 1e300,
        (double)(1ULL << 30), (double)(1ULL << 40), (double)(1ULL << 50),
    };

    /*
     * Regression: values that trigger bitshift==0 in Payne-Hanek reduction.
     * bitshift == 0 when (exp+61) % 64 == 0, i.e. exp = 3, 67, 131, ...
     * exp=3 → x ≈ 2^55, exp=67 → x ≈ 2^119.
     * In C, shifting uint64 by 64 is undefined behavior (fixed in _trig_reduce.h).
     */
    {
        double edge_vals[] = {
            (double)(1ULL << 55),
            (double)(1ULL << 55) + 1.0,
            (double)(1ULL << 55) * 1.5,
        };
        for (int i = 0; i < 3; i++) {
            double x = edge_vals[i];
            double s = neverc_math_sin(x);
            double c = neverc_math_cos(x);
            snprintf(buf, sizeof(buf), "sin(2^55 edge %d) bounded", i);
            check_true(buf, neverc_math_abs(s) <= 1.0 && !neverc_math_isnan(s));
            snprintf(buf, sizeof(buf), "cos(2^55 edge %d) bounded", i);
            check_true(buf, neverc_math_abs(c) <= 1.0 && !neverc_math_isnan(c));
            snprintf(buf, sizeof(buf), "sin²+cos²=1 (2^55 edge %d)", i);
            check_double(buf, s * s + c * c, 1.0);
        }
    }

    for (int i = 0; i < 10; i++) {
        double x = huge_vals[i];
        double s = neverc_math_sin(x);
        double c = neverc_math_cos(x);

        snprintf(buf, sizeof(buf), "|sin(%.0e)|<=1", x);
        check_true(buf, neverc_math_abs(s) <= 1.0);
        snprintf(buf, sizeof(buf), "|cos(%.0e)|<=1", x);
        check_true(buf, neverc_math_abs(c) <= 1.0);
        snprintf(buf, sizeof(buf), "sin²+cos²=1 (%.0e)", x);
        check_double(buf, s * s + c * c, 1.0);
        snprintf(buf, sizeof(buf), "sin(%.0e) not NaN", x);
        check_true(buf, !neverc_math_isnan(s));
        snprintf(buf, sizeof(buf), "cos(%.0e) not NaN", x);
        check_true(buf, !neverc_math_isnan(c));
    }

    /* sincos consistency with Payne-Hanek */
    for (int i = 0; i < 10; i++) {
        double x = huge_vals[i];
        double sv, cv;
        neverc_math_sincos(x, &sv, &cv);
        snprintf(buf, sizeof(buf), "sincos.sin==sin (%.0e)", x);
        check_double(buf, sv, neverc_math_sin(x));
        snprintf(buf, sizeof(buf), "sincos.cos==cos (%.0e)", x);
        check_double(buf, cv, neverc_math_cos(x));
    }

    /* tan for large args should produce consistent results */
    for (int i = 0; i < 10; i++) {
        double x = huge_vals[i];
        double t = neverc_math_tan(x);
        double s = neverc_math_sin(x);
        double c = neverc_math_cos(x);
        if (neverc_math_abs(c) > 1e-10) {
            snprintf(buf, sizeof(buf), "tan==sin/cos (%.0e)", x);
            check_double(buf, t, s / c);
        }
    }

    /* Specific known-good value: sin(2^30) computed with mpfr */
    /* sin(1073741824) ≈ -0.3678979... (exact to ~15 digits) */
    double s_2_30 = neverc_math_sin((double)(1ULL << 30));
    check_true("sin(2^30) reasonable", neverc_math_abs(s_2_30) <= 1.0);

    /* Negative large args */
    check_double("sin(-1e18)=-sin(1e18)",
        neverc_math_sin(-1e18), -neverc_math_sin(1e18));
    check_double("cos(-1e18)=cos(1e18)",
        neverc_math_cos(-1e18), neverc_math_cos(1e18));
}

/* ========== pow comprehensive edge-case tests (from Go all_test.go) ========== */

static void check_signbit(const char *name, double got, int expect_neg) {
    tests_run++;
    int got_neg = neverc_math_signbit(got);
    if (got_neg == expect_neg) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got signbit=%d, expected signbit=%d (value=%.17g)\n",
               name, got_neg, expect_neg, got);
    }
}

static void test_pow_special_cases(void) {
    printf("[pow special cases — IEEE 754-2008]\n");

    /* pow(-Inf, y) */
    check_double("pow(-Inf,-Pi)=0", neverc_math_pow(NC_NEGINF, -NEVERC_MATH_PI), 0.0);
    {
        double r = neverc_math_pow(NC_NEGINF, -3.0);
        check_double("pow(-Inf,-3)=0", r, 0.0);
        check_signbit("pow(-Inf,-3) is -0", r, 1);
    }
    check_double("pow(-Inf,-0)=1", neverc_math_pow(NC_NEGINF, -0.0), 1.0);
    check_double("pow(-Inf,+0)=1", neverc_math_pow(NC_NEGINF, 0.0), 1.0);
    check_double("pow(-Inf,1)=-Inf", neverc_math_pow(NC_NEGINF, 1.0), NC_NEGINF);
    check_double("pow(-Inf,3)=-Inf", neverc_math_pow(NC_NEGINF, 3.0), NC_NEGINF);
    check_double("pow(-Inf,Pi)=+Inf", neverc_math_pow(NC_NEGINF, NEVERC_MATH_PI), NC_INF);
    check_double("pow(-Inf,0.5)=+Inf", neverc_math_pow(NC_NEGINF, 0.5), NC_INF);
    check_double("pow(-Inf,NaN)=NaN", neverc_math_pow(NC_NEGINF, NC_NAN), NC_NAN);

    /* pow(-Pi, y) */
    check_double("pow(-Pi,-Inf)=0", neverc_math_pow(-NEVERC_MATH_PI, NC_NEGINF), 0.0);
    check_double("pow(-Pi,-Pi)=NaN", neverc_math_pow(-NEVERC_MATH_PI, -NEVERC_MATH_PI), NC_NAN);
    check_double("pow(-Pi,-0)=1", neverc_math_pow(-NEVERC_MATH_PI, -0.0), 1.0);
    check_double("pow(-Pi,+0)=1", neverc_math_pow(-NEVERC_MATH_PI, 0.0), 1.0);
    check_double("pow(-Pi,1)=-Pi", neverc_math_pow(-NEVERC_MATH_PI, 1.0), -NEVERC_MATH_PI);
    check_double("pow(-Pi,Pi)=NaN", neverc_math_pow(-NEVERC_MATH_PI, NEVERC_MATH_PI), NC_NAN);
    check_double("pow(-Pi,+Inf)=+Inf", neverc_math_pow(-NEVERC_MATH_PI, NC_INF), NC_INF);
    check_double("pow(-Pi,NaN)=NaN", neverc_math_pow(-NEVERC_MATH_PI, NC_NAN), NC_NAN);

    /* pow(-1, ±Inf) = 1  (IEEE 754-2008) */
    check_double("pow(-1,-Inf)=1", neverc_math_pow(-1.0, NC_NEGINF), 1.0);
    check_double("pow(-1,+Inf)=1", neverc_math_pow(-1.0, NC_INF), 1.0);
    check_double("pow(-1,NaN)=NaN", neverc_math_pow(-1.0, NC_NAN), NC_NAN);

    /* pow(-0.5, ±Inf) */
    check_double("pow(-0.5,-Inf)=+Inf", neverc_math_pow(-0.5, NC_NEGINF), NC_INF);
    check_double("pow(-0.5,+Inf)=0", neverc_math_pow(-0.5, NC_INF), 0.0);

    /* pow(-0, y) — signed zero tests */
    {
        double neg_zero = -0.0;
        check_double("pow(-0,-Inf)=+Inf", neverc_math_pow(neg_zero, NC_NEGINF), NC_INF);
        check_double("pow(-0,-Pi)=+Inf", neverc_math_pow(neg_zero, -NEVERC_MATH_PI), NC_INF);
        check_double("pow(-0,-0.5)=+Inf", neverc_math_pow(neg_zero, -0.5), NC_INF);
        check_double("pow(-0,-3)=-Inf", neverc_math_pow(neg_zero, -3.0), NC_NEGINF);
        {
            double r = neverc_math_pow(neg_zero, 3.0);
            check_double("pow(-0,3)=0", r, 0.0);
            check_signbit("pow(-0,3) is -0", r, 1);
        }
        check_double("pow(-0,Pi)=0", neverc_math_pow(neg_zero, NEVERC_MATH_PI), 0.0);
        check_double("pow(-0,0.5)=0", neverc_math_pow(neg_zero, 0.5), 0.0);
        check_double("pow(-0,+Inf)=0", neverc_math_pow(neg_zero, NC_INF), 0.0);
    }

    /* pow(+0, y) */
    check_double("pow(+0,-Inf)=+Inf", neverc_math_pow(0.0, NC_NEGINF), NC_INF);
    check_double("pow(+0,-Pi)=+Inf", neverc_math_pow(0.0, -NEVERC_MATH_PI), NC_INF);
    check_double("pow(+0,-3)=+Inf", neverc_math_pow(0.0, -3.0), NC_INF);
    check_double("pow(+0,-0)=1", neverc_math_pow(0.0, -0.0), 1.0);
    check_double("pow(+0,+0)=1", neverc_math_pow(0.0, 0.0), 1.0);
    check_double("pow(+0,3)=0", neverc_math_pow(0.0, 3.0), 0.0);
    check_double("pow(+0,Pi)=0", neverc_math_pow(0.0, NEVERC_MATH_PI), 0.0);
    check_double("pow(+0,+Inf)=0", neverc_math_pow(0.0, NC_INF), 0.0);
    check_double("pow(+0,NaN)=NaN", neverc_math_pow(0.0, NC_NAN), NC_NAN);

    /* pow(0.5, ±Inf) */
    check_double("pow(0.5,-Inf)=+Inf", neverc_math_pow(0.5, NC_NEGINF), NC_INF);
    check_double("pow(0.5,+Inf)=0", neverc_math_pow(0.5, NC_INF), 0.0);

    /* pow(1, y) = 1  (IEEE 754-2008) */
    check_double("pow(1,-Inf)=1", neverc_math_pow(1.0, NC_NEGINF), 1.0);
    check_double("pow(1,+Inf)=1", neverc_math_pow(1.0, NC_INF), 1.0);
    check_double("pow(1,NaN)=1", neverc_math_pow(1.0, NC_NAN), 1.0);

    /* pow(Pi, y) */
    check_double("pow(Pi,-Inf)=0", neverc_math_pow(NEVERC_MATH_PI, NC_NEGINF), 0.0);
    check_double("pow(Pi,-0)=1", neverc_math_pow(NEVERC_MATH_PI, -0.0), 1.0);
    check_double("pow(Pi,+0)=1", neverc_math_pow(NEVERC_MATH_PI, 0.0), 1.0);
    check_double("pow(Pi,1)=Pi", neverc_math_pow(NEVERC_MATH_PI, 1.0), NEVERC_MATH_PI);
    check_double("pow(Pi,+Inf)=+Inf", neverc_math_pow(NEVERC_MATH_PI, NC_INF), NC_INF);
    check_double("pow(Pi,NaN)=NaN", neverc_math_pow(NEVERC_MATH_PI, NC_NAN), NC_NAN);

    /* pow(+Inf, y) */
    check_double("pow(+Inf,-Pi)=0", neverc_math_pow(NC_INF, -NEVERC_MATH_PI), 0.0);
    check_double("pow(+Inf,-0)=1", neverc_math_pow(NC_INF, -0.0), 1.0);
    check_double("pow(+Inf,+0)=1", neverc_math_pow(NC_INF, 0.0), 1.0);
    check_double("pow(+Inf,1)=+Inf", neverc_math_pow(NC_INF, 1.0), NC_INF);
    check_double("pow(+Inf,Pi)=+Inf", neverc_math_pow(NC_INF, NEVERC_MATH_PI), NC_INF);
    check_double("pow(+Inf,NaN)=NaN", neverc_math_pow(NC_INF, NC_NAN), NC_NAN);

    /* pow(NaN, y) */
    check_double("pow(NaN,-Pi)=NaN", neverc_math_pow(NC_NAN, -NEVERC_MATH_PI), NC_NAN);
    check_double("pow(NaN,-0)=1", neverc_math_pow(NC_NAN, -0.0), 1.0);
    check_double("pow(NaN,+0)=1", neverc_math_pow(NC_NAN, 0.0), 1.0);
    check_double("pow(NaN,1)=NaN", neverc_math_pow(NC_NAN, 1.0), NC_NAN);
    check_double("pow(NaN,Pi)=NaN", neverc_math_pow(NC_NAN, NEVERC_MATH_PI), NC_NAN);
    check_double("pow(NaN,NaN)=NaN", neverc_math_pow(NC_NAN, NC_NAN), NC_NAN);

    /* Go Issue #7394 — overflow checks with large exponents */
    check_double("pow(2,2^32)=+Inf",
        neverc_math_pow(2.0, (double)(1ULL << 32)), NC_INF);
    check_double("pow(2,-2^32)=0",
        neverc_math_pow(2.0, -(double)(1ULL << 32)), 0.0);
    check_double("pow(-2,2^32+1)=-Inf",
        neverc_math_pow(-2.0, (double)((1ULL << 32) + 1)), NC_NEGINF);
    check_double("pow(0.5,2^45)=0",
        neverc_math_pow(0.5, (double)(1ULL << 45)), 0.0);
    check_double("pow(0.5,-2^45)=+Inf",
        neverc_math_pow(0.5, -(double)(1ULL << 45)), NC_INF);

    /* Nextafter edge cases (Go Issue #7394) */
    check_double("pow(nextafter(1,2),2^63)=+Inf",
        neverc_math_pow(neverc_math_nextafter(1.0, 2.0), (double)(1ULL << 63)), NC_INF);
    check_double("pow(nextafter(1,-2),2^63)=0",
        neverc_math_pow(neverc_math_nextafter(1.0, -2.0), (double)(1ULL << 63)), 0.0);
    check_double("pow(nextafter(-1,2),2^63)=0",
        neverc_math_pow(neverc_math_nextafter(-1.0, 2.0), (double)(1ULL << 63)), 0.0);
    check_double("pow(nextafter(-1,-2),2^63)=+Inf",
        neverc_math_pow(neverc_math_nextafter(-1.0, -2.0), (double)(1ULL << 63)), NC_INF);

    /* Go Issue #57465 — large exponents where isOddInt must handle >2^53 */
    check_double("pow(-0,1e19)=0",
        neverc_math_pow(-0.0, 1e19), 0.0);
    check_double("pow(-0,-1e19)=+Inf",
        neverc_math_pow(-0.0, -1e19), NC_INF);
    {
        double r = neverc_math_pow(-0.0, (double)((1ULL << 53) - 1));
        check_double("pow(-0,2^53-1)=0", r, 0.0);
        check_signbit("pow(-0,2^53-1) is -0", r, 1);
    }
    check_double("pow(-0,-(2^53-1))=-Inf",
        neverc_math_pow(-0.0, -(double)((1ULL << 53) - 1)), NC_NEGINF);

    /* y = 0.5 / -0.5 optimization */
    check_double("pow(4,0.5)=2", neverc_math_pow(4.0, 0.5), 2.0);
    check_double("pow(4,-0.5)=0.5", neverc_math_pow(4.0, -0.5), 0.5);
    check_double("pow(9,0.5)=3", neverc_math_pow(9.0, 0.5), 3.0);

    /* Negative base with integer exponents */
    check_double("pow(-2,2)=4", neverc_math_pow(-2.0, 2.0), 4.0);
    check_double("pow(-2,3)=-8", neverc_math_pow(-2.0, 3.0), -8.0);
    check_double("pow(-3,4)=81", neverc_math_pow(-3.0, 4.0), 81.0);
    check_double("pow(-1,1e18)=1", neverc_math_pow(-1.0, 1e18), 1.0);
    check_double("pow(-1,1e18+1)=-1", neverc_math_pow(-1.0, (double)((1ULL << 53) - 1)), -1.0);
}

/* ========== max/min signed zero tests (Go dim.go specification) ========== */

static void test_max_min_signed_zero(void) {
    printf("[max/min signed zero]\n");

    /* Max(+0, -0) = +0; Max(-0, +0) = +0; Max(-0, -0) = -0 */
    {
        double r = neverc_math_max(0.0, -0.0);
        check_double("max(+0,-0)=0", r, 0.0);
        check_signbit("max(+0,-0) is +0", r, 0);
    }
    {
        double r = neverc_math_max(-0.0, 0.0);
        check_double("max(-0,+0)=0", r, 0.0);
        check_signbit("max(-0,+0) is +0", r, 0);
    }
    {
        double r = neverc_math_max(-0.0, -0.0);
        check_double("max(-0,-0)=0", r, 0.0);
        check_signbit("max(-0,-0) is -0", r, 1);
    }

    /* Max(x, +Inf) = +Inf; Max(x, NaN) = NaN */
    check_double("max(5,+Inf)=+Inf", neverc_math_max(5.0, NC_INF), NC_INF);
    check_double("max(+Inf,5)=+Inf", neverc_math_max(NC_INF, 5.0), NC_INF);
    check_double("max(5,NaN)=NaN", neverc_math_max(5.0, NC_NAN), NC_NAN);
    check_double("max(NaN,5)=NaN", neverc_math_max(NC_NAN, 5.0), NC_NAN);

    /* Min(-0, +0) = -0; Min(+0, -0) = -0; Min(+0, +0) = +0 */
    {
        double r = neverc_math_min(-0.0, 0.0);
        check_double("min(-0,+0)=0", r, 0.0);
        check_signbit("min(-0,+0) is -0", r, 1);
    }
    {
        double r = neverc_math_min(0.0, -0.0);
        check_double("min(+0,-0)=0", r, 0.0);
        check_signbit("min(+0,-0) is -0", r, 1);
    }
    {
        double r = neverc_math_min(0.0, 0.0);
        check_double("min(+0,+0)=0", r, 0.0);
        check_signbit("min(+0,+0) is +0", r, 0);
    }

    /* Min(x, -Inf) = -Inf; Min(x, NaN) = NaN */
    check_double("min(5,-Inf)=-Inf", neverc_math_min(5.0, NC_NEGINF), NC_NEGINF);
    check_double("min(-Inf,5)=-Inf", neverc_math_min(NC_NEGINF, 5.0), NC_NEGINF);
    check_double("min(5,NaN)=NaN", neverc_math_min(5.0, NC_NAN), NC_NAN);
    check_double("min(NaN,5)=NaN", neverc_math_min(NC_NAN, 5.0), NC_NAN);
}

/* ========== Nextafter32 tests ========== */

static void test_nextafter32(void) {
    printf("[nextafter32]\n");

    /*
     * Nextafter32 returns the next float32 representable value after x
     * toward y. We test by verifying:
     *   1. Monotonicity: nextafter32(x, +Inf) > x
     *   2. Adjacency: no float32 exists between x and nextafter32(x, y)
     *   3. IEEE 754 special cases match Go specification
     */

    /* Basic direction: nextafter32(1, 2) should be the smallest float32 > 1 */
    float na = neverc_math_nextafter32(1.0f, 2.0f);
    check_true("na32(1,2) > 1", na > 1.0f);
    /* Verify it's truly adjacent: (na - 1.0f) should equal 1 ULP = 2^-23 */
    check_true("na32(1,2) is 1 ULP away",
        (double)(na - 1.0f) == (double)neverc_math_float32frombits(
            neverc_math_float32bits(1.0f) + 1) - 1.0);

    /* Backward: nextafter32(1, 0) < 1 */
    na = neverc_math_nextafter32(1.0f, 0.0f);
    check_true("na32(1,0) < 1", na < 1.0f);

    /* Self: nextafter32(x, x) = x */
    check_true("na32(1,1) == 1",
        neverc_math_nextafter32(1.0f, 1.0f) == 1.0f);
    check_true("na32(0,0) == 0",
        neverc_math_nextafter32(0.0f, 0.0f) == 0.0f);

    /* From zero: nextafter32(0, 1) should be SmallestNonzeroFloat32 */
    float smallest = neverc_math_nextafter32(0.0f, 1.0f);
    check_true("na32(0,1) > 0", smallest > 0.0f);
    check_true("na32(0,1) is smallest",
        neverc_math_float32bits(smallest) == 1U);

    /* From zero negative: nextafter32(0, -1) should be -SmallestNonzeroFloat32 */
    float neg_smallest = neverc_math_nextafter32(0.0f, -1.0f);
    check_true("na32(0,-1) < 0", neg_smallest < 0.0f);
    check_true("na32(0,-1) is -smallest",
        neverc_math_float32bits(neg_smallest) == 0x80000001U);

    /* NaN propagation (Go spec: Nextafter32(NaN, y) = NaN) */
    float nan32 = neverc_math_float32frombits(0x7FC00001U);
    check_true("na32(NaN,1) is NaN",
        neverc_math_isnan((double)neverc_math_nextafter32(nan32, 1.0f)));
    check_true("na32(1,NaN) is NaN",
        neverc_math_isnan((double)neverc_math_nextafter32(1.0f, nan32)));

    /* Round-trip: going forward then backward should return to original */
    float orig = 3.14f;
    float fwd = neverc_math_nextafter32(orig, 100.0f);
    float back = neverc_math_nextafter32(fwd, 0.0f);
    check_true("na32 round-trip", back == orig);

    /* Subnormal region: verify it works near float32 minimum */
    float sub = neverc_math_float32frombits(5U);
    float sub_next = neverc_math_nextafter32(sub, 0.0f);
    check_true("na32 subnormal decrement",
        neverc_math_float32bits(sub_next) == 4U);
}

/* ========== Lgamma sign tests ========== */

static void test_lgamma_sign(void) {
    printf("[lgamma_sign]\n");

    /*
     * lgamma_sign(x) returns (lgamma(x), sign) where sign = sgn(Gamma(x)).
     * Gamma(x) > 0 for:  x > 0, and for x in (-2,-1), (-4,-3), ...
     * Gamma(x) < 0 for:  x in (-1,0), (-3,-2), (-5,-4), ...
     *
     * We verify:
     *   1. lgamma_sign value matches lgamma
     *   2. sign is correct for known intervals
     *   3. sign * exp(lgamma) ≈ gamma(x) for moderate values
     */

    int sign;

    /* Positive arguments: Gamma(x) > 0, so sign = 1 */
    double val = neverc_math_lgamma_sign(1.0, &sign);
    check_double("lgamma_sign(1).val", val, 0.0);
    check_int("lgamma_sign(1).sign", sign, 1);

    val = neverc_math_lgamma_sign(2.0, &sign);
    check_double("lgamma_sign(2).val", val, 0.0);
    check_int("lgamma_sign(2).sign", sign, 1);

    val = neverc_math_lgamma_sign(5.0, &sign);
    check_double("lgamma_sign(5).val", val, neverc_math_log(24.0));
    check_int("lgamma_sign(5).sign", sign, 1);

    val = neverc_math_lgamma_sign(0.5, &sign);
    check_double("lgamma_sign(0.5).val", val, 0.5723649429247001);
    check_int("lgamma_sign(0.5).sign", sign, 1);

    /* Negative: x in (-1, 0) → Gamma(x) < 0, sign = -1 */
    val = neverc_math_lgamma_sign(-0.5, &sign);
    check_int("lgamma_sign(-0.5).sign", sign, -1);
    check_true("lgamma_sign(-0.5) finite",
        !neverc_math_isinf(val, 0) && !neverc_math_isnan(val));

    /* Verify: sign * exp(lgamma_sign(-0.5)) ≈ Gamma(-0.5) = -2*sqrt(pi) */
    {
        double expected_gamma = -2.0 * neverc_math_sqrt(NEVERC_MATH_PI);
        double computed = (double)sign * neverc_math_exp(val);
        check_double("sign*exp(lgamma(-0.5)) = Gamma(-0.5)",
            computed, expected_gamma);
    }

    /* x in (-2, -1) → Gamma(x) > 0, sign = 1 */
    val = neverc_math_lgamma_sign(-1.5, &sign);
    check_int("lgamma_sign(-1.5).sign", sign, 1);

    /* Verify: Gamma(-1.5) = 4*sqrt(pi)/3 ≈ 2.3633... */
    {
        double expected_gamma = 4.0 * neverc_math_sqrt(NEVERC_MATH_PI) / 3.0;
        double computed = (double)sign * neverc_math_exp(val);
        check_double("sign*exp(lgamma(-1.5)) = Gamma(-1.5)",
            computed, expected_gamma);
    }

    /* x in (-3, -2) → Gamma(x) < 0, sign = -1 */
    val = neverc_math_lgamma_sign(-2.5, &sign);
    check_int("lgamma_sign(-2.5).sign", sign, -1);

    /* x in (-4, -3) → Gamma(x) > 0, sign = 1 */
    val = neverc_math_lgamma_sign(-3.5, &sign);
    check_int("lgamma_sign(-3.5).sign", sign, 1);

    /* Consistency: lgamma_sign value matches lgamma */
    double test_xs[] = {0.1, 0.5, 1.5, 3.0, 10.0, 100.0, -0.5, -1.5, -2.5};
    for (int i = 0; i < 9; i++) {
        double x = test_xs[i];
        int s;
        double v = neverc_math_lgamma_sign(x, &s);
        double v2 = neverc_math_lgamma(x);
        char buf[128];
        snprintf(buf, sizeof(buf), "lgamma_sign(%.1f) == lgamma(%.1f)", x, x);
        check_double(buf, v, v2);
    }

    /* Special cases */
    val = neverc_math_lgamma_sign(0.0, &sign);
    check_double("lgamma_sign(0)=+Inf", val, NC_INF);
    check_int("lgamma_sign(0).sign", sign, 1);

    neverc_math_lgamma_sign(NC_NAN, &sign);
    check_int("lgamma_sign(NaN).sign", sign, 1);

    neverc_math_lgamma_sign(NC_INF, &sign);
    check_int("lgamma_sign(+Inf).sign", sign, 1);

    /* NULL sign pointer should not crash */
    val = neverc_math_lgamma_sign(5.0, (void *)0);
    check_double("lgamma_sign(5,NULL)", val, neverc_math_log(24.0));
}

/* ========== 2^52 boundary & signed zero tests (Go all_test.go) ========== */

static void test_rounding_boundaries(void) {
    printf("[rounding 2^52 boundaries]\n");

    /*
     * 2^52 boundary rationale:
     * float64 has 52 mantissa bits. For |x| >= 2^52, all float64 values
     * are exact integers — no fractional part can be represented.
     * So ceil/floor/trunc must return x unchanged for |x| >= 2^52.
     *
     * Key test values from Go all_test.go:
     *   4503599627370495   = 2^52 - 1     (last integer before boundary)
     *   4503599627370495.5 = 2^52 - 0.5   (largest fractional float64)
     *   4503599627370496   = 2^52         (boundary: no fractions possible)
     */
    const double p52   = (double)(1ULL << 52);
    const double p52m1 = (double)((1ULL << 52) - 1);
    const double p52mh = p52 - 0.5;
    const double p53   = (double)(1ULL << 53);

    /* ceil at 2^52 boundary */
    check_double("ceil(2^52-1)", neverc_math_ceil(p52m1), p52m1);
    check_double("ceil(2^52-0.5)", neverc_math_ceil(p52mh), p52);
    check_double("ceil(2^52)", neverc_math_ceil(p52), p52);
    check_double("ceil(2^53)", neverc_math_ceil(p53), p53);

    /* floor at 2^52 boundary */
    check_double("floor(2^52-1)", neverc_math_floor(p52m1), p52m1);
    check_double("floor(2^52-0.5)", neverc_math_floor(p52mh), p52m1);
    check_double("floor(2^52)", neverc_math_floor(p52), p52);
    check_double("floor(2^53)", neverc_math_floor(p53), p53);

    /* trunc at 2^52 boundary */
    check_double("trunc(2^52-1)", neverc_math_trunc(p52m1), p52m1);
    check_double("trunc(2^52-0.5)", neverc_math_trunc(p52mh), p52m1);
    check_double("trunc(2^52)", neverc_math_trunc(p52), p52);
    check_double("trunc(2^53)", neverc_math_trunc(p53), p53);

    /* negative 2^52 boundary */
    check_double("ceil(-(2^52)+0.5)", neverc_math_ceil(-p52 + 0.5), -p52m1);
    check_double("floor(-(2^52))", neverc_math_floor(-p52), -p52);
    check_double("floor(-(2^52)+0.5)", neverc_math_floor(-p52 + 0.5), -p52);
    check_double("trunc(-(2^52)+0.5)", neverc_math_trunc(-p52 + 0.5), -p52m1);

    /* round at 2^52 boundary */
    check_double("round(2^52-0.5)", neverc_math_round(p52mh), p52);
    check_double("round(2^52)", neverc_math_round(p52), p52);
    check_double("rte(2^52-0.5)", neverc_math_roundtoeven(p52mh), p52);
    check_double("rte(2^52)", neverc_math_roundtoeven(p52), p52);
}

static void test_signed_zero_preservation(void) {
    printf("[signed zero preservation]\n");

    /*
     * IEEE 754 requires rounding functions to preserve the sign of zero.
     * This is critical for correct behavior in complex number branch cuts
     * and other mathematical contexts.
     */

    /* ceil(-0) = -0, floor(-0) = -0, trunc(-0) = -0, round(-0) = -0 */
    double neg_zero = neverc_math_copysign(0.0, -1.0);

    check_double("ceil(-0)==-0", neverc_math_ceil(neg_zero), neg_zero);
    check_signbit("ceil(-0) is neg", neverc_math_ceil(neg_zero), 1);

    check_double("floor(-0)==-0", neverc_math_floor(neg_zero), neg_zero);
    check_signbit("floor(-0) is neg", neverc_math_floor(neg_zero), 1);

    check_double("trunc(-0)==-0", neverc_math_trunc(neg_zero), neg_zero);
    check_signbit("trunc(-0) is neg", neverc_math_trunc(neg_zero), 1);

    check_double("round(-0)==-0", neverc_math_round(neg_zero), neg_zero);
    check_signbit("round(-0) is neg", neverc_math_round(neg_zero), 1);

    check_double("rte(-0)==-0", neverc_math_roundtoeven(neg_zero), neg_zero);
    check_signbit("rte(-0) is neg", neverc_math_roundtoeven(neg_zero), 1);

    /* modf(-0) = (-0, -0) */
    double ipart;
    double fpart = neverc_math_modf(neg_zero, &ipart);
    check_double("modf(-0).int==-0", ipart, neg_zero);
    check_signbit("modf(-0).int neg", ipart, 1);
    check_double("modf(-0).frac==-0", fpart, neg_zero);
    check_signbit("modf(-0).frac neg", fpart, 1);

    /* frexp(-0) = (-0, 0) */
    int exp;
    double frac = neverc_math_frexp(neg_zero, &exp);
    check_double("frexp(-0)==-0", frac, neg_zero);
    check_signbit("frexp(-0) neg", frac, 1);
    check_int("frexp(-0).exp==0", exp, 0);

    /* fmod(-0, y) = -0, fmod(+0, y) = +0 */
    double fm_neg = neverc_math_fmod(neg_zero, 1.0);
    check_double("fmod(-0,1)==-0", fm_neg, neg_zero);
    check_signbit("fmod(-0,1) neg", fm_neg, 1);

    double fm_pos = neverc_math_fmod(0.0, 1.0);
    check_double("fmod(+0,1)==+0", fm_pos, 0.0);
    check_signbit("fmod(+0,1) pos", fm_pos, 0);

    /* remainder(-0, y) = -0, sin(-0) = -0, atan(-0) = -0 */
    double rem_neg = neverc_math_remainder(neg_zero, 1.0);
    check_double("rem(-0,1)==-0", rem_neg, neg_zero);
    check_signbit("rem(-0,1) neg", rem_neg, 1);

    check_signbit("sin(-0) neg", neverc_math_sin(neg_zero), 1);
    check_signbit("atan(-0) neg", neverc_math_atan(neg_zero), 1);
    check_signbit("asin(-0) neg", neverc_math_asin(neg_zero), 1);
    check_signbit("sinh(-0) neg", neverc_math_sinh(neg_zero), 1);
    check_signbit("asinh(-0) neg", neverc_math_asinh(neg_zero), 1);
    check_signbit("atanh(-0) neg", neverc_math_atanh(neg_zero), 1);
    check_signbit("tanh(-0) neg", neverc_math_tanh(neg_zero), 1);

    /* copysign preserves magnitude, takes sign from 2nd arg */
    check_signbit("copysign(1,-0) neg", neverc_math_copysign(1.0, neg_zero), 1);
    check_signbit("copysign(-1,+0) pos", neverc_math_copysign(-1.0, 0.0), 0);
}

/* ========== atan2 comprehensive special cases (Go all_test.go vfatan2SC) ========== */

static void test_atan2_special_cases(void) {
    printf("[atan2 special cases — Go all_test.go]\n");
    double neg_zero = -0.0;

    /* atan2(-Inf, x) */
    check_double("atan2(-Inf,-Inf)=-3pi/4",
        neverc_math_atan2(NC_NEGINF, NC_NEGINF), -3.0 * NEVERC_MATH_PI / 4.0);
    check_double("atan2(-Inf,-Pi)=-pi/2",
        neverc_math_atan2(NC_NEGINF, -NEVERC_MATH_PI), -NEVERC_MATH_PI / 2.0);
    check_double("atan2(-Inf,0)=-pi/2",
        neverc_math_atan2(NC_NEGINF, 0.0), -NEVERC_MATH_PI / 2.0);
    check_double("atan2(-Inf,+Pi)=-pi/2",
        neverc_math_atan2(NC_NEGINF, NEVERC_MATH_PI), -NEVERC_MATH_PI / 2.0);
    check_double("atan2(-Inf,+Inf)=-pi/4",
        neverc_math_atan2(NC_NEGINF, NC_INF), -NEVERC_MATH_PI / 4.0);
    check_double("atan2(-Inf,NaN)=NaN",
        neverc_math_atan2(NC_NEGINF, NC_NAN), NC_NAN);

    /* atan2(-Pi, x) */
    check_double("atan2(-Pi,-Inf)=-pi",
        neverc_math_atan2(-NEVERC_MATH_PI, NC_NEGINF), -NEVERC_MATH_PI);
    check_double("atan2(-Pi,0)=-pi/2",
        neverc_math_atan2(-NEVERC_MATH_PI, 0.0), -NEVERC_MATH_PI / 2.0);
    {
        double r = neverc_math_atan2(-NEVERC_MATH_PI, NC_INF);
        check_double("atan2(-Pi,+Inf)=-0", r, neg_zero);
        check_signbit("atan2(-Pi,+Inf) neg", r, 1);
    }
    check_double("atan2(-Pi,NaN)=NaN",
        neverc_math_atan2(-NEVERC_MATH_PI, NC_NAN), NC_NAN);

    /* atan2(-0, x) */
    check_double("atan2(-0,-Inf)=-pi",
        neverc_math_atan2(neg_zero, NC_NEGINF), -NEVERC_MATH_PI);
    check_double("atan2(-0,-Pi)=-pi",
        neverc_math_atan2(neg_zero, -NEVERC_MATH_PI), -NEVERC_MATH_PI);
    check_double("atan2(-0,-0)=-pi",
        neverc_math_atan2(neg_zero, neg_zero), -NEVERC_MATH_PI);
    {
        double r = neverc_math_atan2(neg_zero, 0.0);
        check_double("atan2(-0,+0)=-0", r, neg_zero);
        check_signbit("atan2(-0,+0) neg", r, 1);
    }
    {
        double r = neverc_math_atan2(neg_zero, NEVERC_MATH_PI);
        check_double("atan2(-0,+Pi)=-0", r, neg_zero);
        check_signbit("atan2(-0,+Pi) neg", r, 1);
    }
    {
        double r = neverc_math_atan2(neg_zero, NC_INF);
        check_double("atan2(-0,+Inf)=-0", r, neg_zero);
        check_signbit("atan2(-0,+Inf) neg", r, 1);
    }
    check_double("atan2(-0,NaN)=NaN",
        neverc_math_atan2(neg_zero, NC_NAN), NC_NAN);

    /* atan2(+0, x) */
    check_double("atan2(+0,-Inf)=+pi",
        neverc_math_atan2(0.0, NC_NEGINF), NEVERC_MATH_PI);
    check_double("atan2(+0,-Pi)=+pi",
        neverc_math_atan2(0.0, -NEVERC_MATH_PI), NEVERC_MATH_PI);
    check_double("atan2(+0,-0)=+pi",
        neverc_math_atan2(0.0, neg_zero), NEVERC_MATH_PI);
    check_double("atan2(+0,+0)=0",
        neverc_math_atan2(0.0, 0.0), 0.0);
    check_double("atan2(+0,+Pi)=0",
        neverc_math_atan2(0.0, NEVERC_MATH_PI), 0.0);
    check_double("atan2(+0,+Inf)=0",
        neverc_math_atan2(0.0, NC_INF), 0.0);
    check_double("atan2(+0,NaN)=NaN",
        neverc_math_atan2(0.0, NC_NAN), NC_NAN);

    /* atan2(+Pi, x) */
    check_double("atan2(+Pi,-Inf)=+pi",
        neverc_math_atan2(NEVERC_MATH_PI, NC_NEGINF), NEVERC_MATH_PI);
    check_double("atan2(+Pi,0)=pi/2",
        neverc_math_atan2(NEVERC_MATH_PI, 0.0), NEVERC_MATH_PI / 2.0);
    check_double("atan2(+Pi,+Inf)=0",
        neverc_math_atan2(NEVERC_MATH_PI, NC_INF), 0.0);
    check_double("atan2(+1,+Inf)=0",
        neverc_math_atan2(1.0, NC_INF), 0.0);
    {
        double r = neverc_math_atan2(-1.0, NC_INF);
        check_double("atan2(-1,+Inf)=-0", r, neg_zero);
        check_signbit("atan2(-1,+Inf) neg", r, 1);
    }
    check_double("atan2(+Pi,NaN)=NaN",
        neverc_math_atan2(NEVERC_MATH_PI, NC_NAN), NC_NAN);

    /* atan2(+Inf, x) */
    check_double("atan2(+Inf,-Inf)=3pi/4",
        neverc_math_atan2(NC_INF, NC_NEGINF), 3.0 * NEVERC_MATH_PI / 4.0);
    check_double("atan2(+Inf,-Pi)=pi/2",
        neverc_math_atan2(NC_INF, -NEVERC_MATH_PI), NEVERC_MATH_PI / 2.0);
    check_double("atan2(+Inf,0)=pi/2",
        neverc_math_atan2(NC_INF, 0.0), NEVERC_MATH_PI / 2.0);
    check_double("atan2(+Inf,+Pi)=pi/2",
        neverc_math_atan2(NC_INF, NEVERC_MATH_PI), NEVERC_MATH_PI / 2.0);
    check_double("atan2(+Inf,+Inf)=pi/4",
        neverc_math_atan2(NC_INF, NC_INF), NEVERC_MATH_PI / 4.0);
    check_double("atan2(+Inf,NaN)=NaN",
        neverc_math_atan2(NC_INF, NC_NAN), NC_NAN);

    /* atan2(NaN, NaN) */
    check_double("atan2(NaN,NaN)=NaN",
        neverc_math_atan2(NC_NAN, NC_NAN), NC_NAN);
}

/* ========== exp2 special cases (Go all_test.go vfexp2SC) ========== */

static void test_exp2_special_cases(void) {
    printf("[exp2 special cases — Go all_test.go]\n");

    check_double("exp2(-Inf)=0", neverc_math_exp2(NC_NEGINF), 0.0);
    check_double("exp2(-2000)=0", neverc_math_exp2(-2000.0), 0.0);
    check_double("exp2(2000)=+Inf", neverc_math_exp2(2000.0), NC_INF);
    check_double("exp2(+Inf)=+Inf", neverc_math_exp2(NC_INF), NC_INF);
    check_double("exp2(NaN)=NaN", neverc_math_exp2(NC_NAN), NC_NAN);
    check_double("exp2(1024)=+Inf", neverc_math_exp2(1024.0), NC_INF);

    /* Near underflow: exp2(-1073.99...) should be very small but non-zero */
    double near_uf = neverc_math_exp2(-1073.99);
    check_true("exp2(-1073.99) > 0", near_uf > 0.0);

    /* Near zero: exp2(tiny) ≈ 1 */
    check_double("exp2(~0)~1", neverc_math_exp2(3.725290298461915e-09), 1.0000000025821745);

    /* Exact integer powers */
    check_double("exp2(0)=1", neverc_math_exp2(0.0), 1.0);
    check_double("exp2(1)=2", neverc_math_exp2(1.0), 2.0);
    check_double("exp2(-1)=0.5", neverc_math_exp2(-1.0), 0.5);
    check_double("exp2(10)=1024", neverc_math_exp2(10.0), 1024.0);
    check_double("exp2(-10)=1/1024", neverc_math_exp2(-10.0), 1.0 / 1024.0);

    /* Go all_test.go exhaustive range: exp2(n) for n in [-1074, 1023] */
    for (int n = -1074; n < 1024; n++) {
        double f = neverc_math_exp2((double)n);
        int vf_exp;
        double vf_frac = neverc_math_frexp(f, &vf_exp);
        if (neverc_math_abs(vf_frac) != 0.5) {
            tests_run++;
            tests_failed++;
            printf("  FAIL: exp2(%d) fraction = %.17g, want ±0.5\n", n, vf_frac);
        } else if (vf_exp != n + 1) {
            tests_run++;
            tests_failed++;
            printf("  FAIL: exp2(%d) exponent = %d, want %d\n", n, vf_exp, n + 1);
        } else {
            tests_run++;
            tests_passed++;
        }
    }
}

/* ========== logb/ilogb special cases (Go all_test.go vflogbSC) ========== */

static void test_logb_ilogb_special_cases(void) {
    printf("[logb/ilogb special cases — Go all_test.go]\n");

    /* logb special cases */
    check_double("logb(-Inf)=+Inf", neverc_math_logb(NC_NEGINF), NC_INF);
    check_double("logb(0)=-Inf", neverc_math_logb(0.0), NC_NEGINF);
    check_double("logb(+Inf)=+Inf", neverc_math_logb(NC_INF), NC_INF);
    check_double("logb(NaN)=NaN", neverc_math_logb(NC_NAN), NC_NAN);

    /* ilogb special cases (Go spec: Ilogb(±Inf) = MaxInt32, Ilogb(0) = MinInt32) */
    check_int("ilogb(-Inf)=MaxInt32", neverc_math_ilogb(NC_NEGINF), NEVERC_MATH_MAX_INT32);
    check_int("ilogb(0)=MinInt32", neverc_math_ilogb(0.0), NEVERC_MATH_MIN_INT32);
    check_int("ilogb(+Inf)=MaxInt32", neverc_math_ilogb(NC_INF), NEVERC_MATH_MAX_INT32);
    check_int("ilogb(NaN)=MaxInt32", neverc_math_ilogb(NC_NAN), NEVERC_MATH_MAX_INT32);

    /* Known exact values */
    check_double("logb(1)=0", neverc_math_logb(1.0), 0.0);
    check_double("logb(2)=1", neverc_math_logb(2.0), 1.0);
    check_double("logb(4)=2", neverc_math_logb(4.0), 2.0);
    check_double("logb(0.5)=-1", neverc_math_logb(0.5), -1.0);
    check_double("logb(0.25)=-2", neverc_math_logb(0.25), -2.0);
    check_double("logb(3)=1", neverc_math_logb(3.0), 1.0);

    check_int("ilogb(1)=0", neverc_math_ilogb(1.0), 0);
    check_int("ilogb(2)=1", neverc_math_ilogb(2.0), 1);
    check_int("ilogb(0.5)=-1", neverc_math_ilogb(0.5), -1);
    check_int("ilogb(3)=1", neverc_math_ilogb(3.0), 1);

    /* Subnormal: logb/ilogb must handle subnormals correctly */
    double subnorm = 5e-324;
    check_int("ilogb(5e-324)=-1074", neverc_math_ilogb(subnorm), -1074);
    check_double("logb(5e-324)=-1074", neverc_math_logb(subnorm), -1074.0);

    /* Consistency: logb(x) == (double)ilogb(x) for normal values */
    for (int i = 0; i < 10; i++) {
        double ax = neverc_math_abs(vf[i]);
        char buf[128];
        snprintf(buf, sizeof(buf), "logb==ilogb (|vf[%d]|)", i);
        check_double(buf, neverc_math_logb(ax), (double)neverc_math_ilogb(ax));
    }
}

/* ========== gamma/lgamma negative value precision (exact known values) ========== */

static void test_gamma_negative_precision(void) {
    printf("[gamma negative precision — exact known values]\n");

    /* Gamma at half-integers has closed-form values involving sqrt(pi) */
    double sqrt_pi = neverc_math_sqrt(NEVERC_MATH_PI);

    /* Gamma(1/2) = sqrt(pi) */
    check_double("gamma(0.5)=sqrt(pi)",
        neverc_math_gamma(0.5), sqrt_pi);

    /* Gamma(3/2) = sqrt(pi)/2 */
    check_double("gamma(1.5)=sqrt(pi)/2",
        neverc_math_gamma(1.5), sqrt_pi / 2.0);

    /* Gamma(5/2) = 3*sqrt(pi)/4 */
    check_double("gamma(2.5)=3sqrt(pi)/4",
        neverc_math_gamma(2.5), 3.0 * sqrt_pi / 4.0);

    /* Gamma(-1/2) = -2*sqrt(pi) */
    check_double("gamma(-0.5)=-2sqrt(pi)",
        neverc_math_gamma(-0.5), -2.0 * sqrt_pi);

    /* Gamma(-3/2) = 4*sqrt(pi)/3 */
    check_double("gamma(-1.5)=4sqrt(pi)/3",
        neverc_math_gamma(-1.5), 4.0 * sqrt_pi / 3.0);

    /* Gamma(-5/2) = -8*sqrt(pi)/15 */
    check_double("gamma(-2.5)=-8sqrt(pi)/15",
        neverc_math_gamma(-2.5), -8.0 * sqrt_pi / 15.0);

    /* Integer factorials: Gamma(n) = (n-1)! */
    check_double("gamma(1)=1", neverc_math_gamma(1.0), 1.0);
    check_double("gamma(2)=1", neverc_math_gamma(2.0), 1.0);
    check_double("gamma(3)=2", neverc_math_gamma(3.0), 2.0);
    check_double("gamma(4)=6", neverc_math_gamma(4.0), 6.0);
    check_double("gamma(5)=24", neverc_math_gamma(5.0), 24.0);
    check_double("gamma(6)=120", neverc_math_gamma(6.0), 120.0);
    check_double("gamma(7)=720", neverc_math_gamma(7.0), 720.0);
    check_double("gamma(10)=362880", neverc_math_gamma(10.0), 362880.0);

    /* Special cases */
    check_double("gamma(+Inf)=+Inf", neverc_math_gamma(NC_INF), NC_INF);
    check_double("gamma(-Inf)=NaN", neverc_math_gamma(NC_NEGINF), NC_NAN);
    check_double("gamma(NaN)=NaN", neverc_math_gamma(NC_NAN), NC_NAN);
    check_double("gamma(0)=+Inf", neverc_math_gamma(0.0), NC_INF);
    check_double("gamma(-1)=NaN", neverc_math_gamma(-1.0), NC_NAN);
    check_double("gamma(-2)=NaN", neverc_math_gamma(-2.0), NC_NAN);

    /* Reflection formula check: Gamma(x)*Gamma(1-x) = pi/sin(pi*x) */
    double test_reflect[] = {0.25, 0.3, 0.4, 0.6, 0.75};
    for (int i = 0; i < 5; i++) {
        double x = test_reflect[i];
        double lhs = neverc_math_gamma(x) * neverc_math_gamma(1.0 - x);
        double rhs = NEVERC_MATH_PI / neverc_math_sin(NEVERC_MATH_PI * x);
        char buf[128];
        snprintf(buf, sizeof(buf), "Gamma(%.2f)*Gamma(%.2f)=pi/sin(pi*%.2f)", x, 1-x, x);
        check_double(buf, lhs, rhs);
    }
}

/* ========== hypot edge cases ========== */

static void test_hypot_edge_cases(void) {
    printf("[hypot edge cases]\n");

    /* Inf absorbs everything (Go spec) */
    check_double("hypot(+Inf,NaN)=+Inf",
        neverc_math_hypot(NC_INF, NC_NAN), NC_INF);
    check_double("hypot(NaN,+Inf)=+Inf",
        neverc_math_hypot(NC_NAN, NC_INF), NC_INF);
    check_double("hypot(-Inf,NaN)=+Inf",
        neverc_math_hypot(NC_NEGINF, NC_NAN), NC_INF);
    check_double("hypot(-Inf,5)=+Inf",
        neverc_math_hypot(NC_NEGINF, 5.0), NC_INF);

    /* NaN propagation (when no Inf) */
    check_double("hypot(NaN,5)=NaN",
        neverc_math_hypot(NC_NAN, 5.0), NC_NAN);

    /* Overflow prevention: values that would overflow if squared naively */
    double big = 1e200;
    double expected = neverc_math_sqrt(2.0) * big;
    check_double("hypot(1e200,1e200)",
        neverc_math_hypot(big, big), expected);

    /* Underflow prevention: very small values */
    double tiny = 1e-300;
    double expected_tiny = neverc_math_sqrt(2.0) * tiny;
    check_double("hypot(1e-300,1e-300)",
        neverc_math_hypot(tiny, tiny), expected_tiny);

    /* One zero arg */
    check_double("hypot(5,0)=5", neverc_math_hypot(5.0, 0.0), 5.0);
    check_double("hypot(0,5)=5", neverc_math_hypot(0.0, 5.0), 5.0);

    /* Commutativity */
    check_double("hypot(3,4)==hypot(4,3)",
        neverc_math_hypot(3.0, 4.0), neverc_math_hypot(4.0, 3.0));

    /* Sign irrelevance */
    check_double("hypot(-3,4)==hypot(3,4)",
        neverc_math_hypot(-3.0, 4.0), neverc_math_hypot(3.0, 4.0));
    check_double("hypot(3,-4)==hypot(3,4)",
        neverc_math_hypot(3.0, -4.0), neverc_math_hypot(3.0, 4.0));
}

/* ========== Main ========== */

int main(void) {
    printf("=== NeverC Math Library Tests ===\n");
    printf("(Zero libc math dependency — all comparisons use neverc_math_*)\n\n");

    /* Go test vector accuracy validation */
    test_trig_vectors();
    test_inv_trig_vectors();
    test_hyp_vectors();
    test_inv_hyp_vectors();
    test_exp_vectors();
    test_log_vectors();
    test_pow_vectors();
    test_sqrt_cbrt_vectors();
    test_rounding_vectors();
    test_abs_dim_vectors();
    test_erf_vectors();
    test_gamma_vectors();
    test_bessel_vectors();
    test_atan2_vectors();

    /* Functional tests */
    test_special_nan_inf();
    test_identities();
    test_decomposition();
    test_round_mod();
    test_float_bits();
    test_fma();
    test_pow10_hypot();
    test_jn_yn();
    test_constants();

    /* Regression tests for fixed bugs */
    test_lgamma_precision();
    test_expm1_precision();
    test_log1p_precision();
    test_erfcinv();
    test_erfc_direct();

    /* Comprehensive edge-case tests (from Go all_test.go) */
    test_large_trig();
    test_pow_special_cases();
    test_max_min_signed_zero();

    /* New function tests */
    test_nextafter32();
    test_lgamma_sign();

    /* IEEE 754 boundary & signed zero compliance */
    test_rounding_boundaries();
    test_signed_zero_preservation();

    /* Go all_test.go special-case vectors (functions previously lacking edge tests) */
    test_atan2_special_cases();
    test_exp2_special_cases();
    test_logb_ilogb_special_cases();
    test_gamma_negative_precision();
    test_hypot_edge_cases();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
