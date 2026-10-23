**语言**: [English](../translate.md) | [简体中文](translate.md) | [繁體中文](../zh-TW/translate.md) | [日本語](../ja/translate.md) | [한국어](../ko/translate.md) | [Français](../fr/translate.md) | [Deutsch](../de/translate.md) | [Español](../es/translate.md) | [Italiano](../it/translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← 文档](README.md)

# 将 C++ 转译为 NeverC

在 Linux 上，使用浮点分布采样的生成 C23 程序需要用 `-lm` 链接。原生对照程序和生成程序均须使用 `-fno-fast-math -ffp-contract=off` 编译。

Core v2 现可直接降低原始指针或经认证的包装迭代器标量范围上的 `std::shuffle` 和 C++17 `std::sample`。生成器须为源码自有、标准布局、可平凡复制且析构平凡的记录，直接声明最多 64 位的非 bool 无符号 `result_type`、返回相同类型的零参数调用运算符，以及各含一个常量返回语句且满足 `min() < max()` 的静态 constexpr `min()`/`max()`。生成器按引用绑定，保留状态、libc++ 的精确拒绝重抽次数及独立位拼接。Sample 接受最多 64 位的非负内置整数数量，保留输入顺序并返回可写输出的结束指针。空／单元素 shuffle 和零数量／空输入 sample 不调用生成器。所选 SDK 的源码替换仍被拒绝；其他 `<random>` 功能、自定义迭代器及记录元素尚不在此支持范围内。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#algorithm-header-from-algorithm).

Core v2 还支持经认证的 `std::linear_congruential_engine` 和 `std::mersenne_twister_engine` 实例，无符号结果类型最多 64 位，包含 `minstd_rand0`、`minstd_rand`、`default_random_engine`、`mt19937`、`mt19937_64`。默认及标量种子构造、复制、赋值、`seed`、`operator()`、`discard`、`min`/`max` 和相等比较保留固定 SDK 的状态语义与目标布局；这些引擎可直接用于 `shuffle`/`sample`，包括包装迭代器范围。所选 SDK 的源码替换仍被拒绝。其他分布、流序列化及 `random_device` 仍待支持。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#standard-random-engines-from-random).

其余确定性系列 `subtract_with_carry_engine`、`discard_block_engine`、`independent_bits_engine` 和 `shuffle_order_engine` 也已支持，包括 `ranlux24_base`、`ranlux48_base`、`ranlux24`、`ranlux48`、`knuth_b`。适配器支持嵌套已认证引擎、基础引擎构造及 `base()`。`seed_seq` 支持默认、整数初始化列表、原始指针或包装迭代器整数范围构造，以及 `size`、`param`、32/64 位无符号范围生成和已认证引擎播种；内部 vector 的扩容、分配与析构次数保留可观察语义。固定 SDK 用种子序列播种零模数 LCG 时会除以零，该路径明确拒绝。源码定义的种子序列类仍不在此子集中。

`uniform_int_distribution<T>` 及其 `param_type` 现支持固定 SDK 接受的最多 64 位整数类型，包括有符号和无符号的 8/16 位类型。构造、默认边界、复制、赋值、`a`/`b`、`min`/`max`、参数读取与设置、相等比较及 `reset` 保留 SDK 的布局和语义。两种调用形式均支持已认证引擎或与 shuffle/sample 相同的源码生成器；负区间、完整位宽范围、拒绝抽样和单点范围保留精确的生成器状态。显式参数调用不改变存储参数。

`uniform_real_distribution<float/double>` 及 `param_type` 现保留相同的构造、参数、属性、复制与赋值、reset 和相等比较接口。`generate_canonical<float/double, Bits>` 支持已认证引擎或已接入的源码生成器，保留 SDK 的取数顺序、位数上限及浮点公式；Bits 为零仍取一次数，实数单点区间也会执行 canonical 取数。运算遵循现有 Core v2 的 IEEE 最近舍入契约，关闭 fast math 和 FMA 收缩；原生对照使用相同选项。long double、其他分布、序列化和 `random_device` 仍待支持。

`bernoulli_distribution` 及 `param_type` 现支持默认、概率和参数对象构造、复制与赋值、`p`、`min`/`max`、参数读取与设置、相等比较、reset 和两种生成器调用形式。已认证 SDK 引擎及已接入的源码生成器保留所选双精度 canonical 取数和概率比较，包括概率 0 和 1；显式参数不改变存储概率。沿用严格浮点契约及所选 SDK 替换拦截。概率须满足 SDK 前置条件 `0 <= p <= 1`。

`discrete_distribution<IntType>` 及 `param_type` 现支持默认、初始化列表、原始指针或封装迭代器算术范围、参数对象、复制与移动构造，复制与移动赋值，以及 `probabilities`、边界、参数读写、相等比较、reset 和两种生成器调用。归一化、累计阈值、概率重建和 canonical 取数沿用固定 SDK 的运算顺序，包括空范围及单权重。内部及返回的 `vector<double>` 保留独立副本、容量复用和经检查的分配释放。沿用严格浮点契约与所选 SDK 替换拦截。

离散分布及参数对象现支持四参数函数权重构造：接受参数及返回值按值传递的核心算术类型一元函数指针。中点计算、参数和结果转换、回调次数及向量分配顺序沿用固定 SDK。数量为 0/1 时不调用回调，各实参表达式仍求值一次。源码别名及工厂返回的函数指针保留已检查的源码函数体；类外模板调用运算符和成员模板回调仍待接入。

离散权重构造支持源码定义、标准布局且具有受支持源码复制构造和析构的普通函数对象。SDK 实际选择的复制构造及一元调用运算符均连同源码函数体检查。分布构造保留独立的外层和内部参数；直接构造 param_type 时仅保留自身参数。可变状态、self 身份、const 运算符及算术值/引用转换均跟随实际对象。零/单权重仍执行所选复制和析构；内部参数在权重初始化完成后销毁，外层或直接参数存活至完整表达式结束。基类、联合体、lambda、成员模板回调及类模板的类外调用运算符仍待支持。 现也支持经过检查的类模板主模板、偏特化和完整特化，所选非模板调用运算符须在类内定义。类型/值实参、默认参数、不同实例的方法身份及源码复制/析构体仍遵循普通类模板检查。

`piecewise_constant_distribution<float/double>` 及 `param_type` 现支持默认、算术边界/权重指针区间、参数、复制及保留源对象的移动构造，并支持赋值、intervals/densities、边界、参数获取/设置、相等比较、reset 和两种生成调用。三个独立拥有的向量保留 SDK 的归一化、累计面积、末项修正、分配顺序和容量复用；边界不足两个时使用单位区间且不读取权重。SDK 参数类型没有移动操作，因此右值参数及分布构造会复制并保留源对象。区间数量及初始化列表回调构造现支持算术值/引用签名的函数指针和普通源码标准布局函数对象，包括已接入的类内类模板调用运算符。所选复制、默认实参临时对象、中点转换和析构时序得到保留；区间数量为零仍调用一次，边界少于两个的列表不调用。long double 和流序列化仍待接入；严格浮点及 SDK 替换检查继续适用。

`piecewise_linear_distribution<float/double>` 及 `param_type` 同样支持默认、区间、数量和列表构造，算术值/引用回调及已接入的源码函数对象，以及保留源对象的移动构造、赋值、向量查询、参数、相等比较、reset 和两种采样形式。线性回调访问实际保存的边界左值，并保留匹配引用的身份；数量为零时访问两个端点，边界不足两个的列表不调用。端点密度、梯形归一化、累计面积和逆变换采样保留固定 SDK 的运算顺序及严格 float/double 舍入。采样仅通过已验证的所选 SDK 数学声明和函数体接入有类型的平方根原语；普通核心配置中的数学调用仍受限制。所选复制、默认实参临时对象、析构、分配顺序和容量复用均得到保留。long double 和流序列化仍待接入。

`exponential_distribution<float/double>` 及 `param_type` 支持默认、速率、参数、复制和移动构造、赋值、lambda、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。采样在现有严格浮点环境下保留 RealType 的规范随机值减法、对数、取负和除法，包括负零及固定 SDK 规范随机值上端点产生的无穷值。所选 SDK 对数和无穷值的声明、函数体及显式重声明均经过验证；源码替换和普通核心配置中的数学调用仍受限制。有类型的对数使用 C23 后端及宿主 CRT/libm 的 log/logf，末位可能因平台而异；独立原生采样对照采用较小 ULP 容差，生成器状态和参数位模式则精确比较。这些分布不需要堆存储。long double 和流序列化仍待接入。

`extreme_value_distribution<float/double>` 及 `param_type` 支持默认、位置/尺度、参数、复制和移动构造、赋值、a/b、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。两个 RealType 参数保留独立值存储，默认值为零和一。采样保留一次规范随机值生成及固定的 RealType 表达式 a-b*log(-log(1-u))，包括两个对数函数体、带符号的无穷边界和规范随机值端点结果。默认实参、所选 SDK 声明/函数体及所有显式对数/无穷值重声明均经过检查；源码替换和普通核心数学调用仍受限制。宿主 CRT/libm 的末位可能不同，因此独立原生采样对照采用较小的绝对/相对容差，参数位模式、实参副作用和生成器状态则精确比较。long double 和流序列化仍待接入。

`normal_distribution<float/double>` 及 `param_type` 支持默认、均值/标准差、参数、复制和移动构造、赋值、mean/stddev、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。参数默认为零和一；分布还保留缓存备用值及有效标志。极坐标采样保留成对规范随机值、拒绝顺序和所选 log/sqrt 因子；缓存有效时不消耗生成器调用。复制/移动/赋值保留缓存状态，参数替换保留缓存，reset 清除缓存，冷缓存复制/比较不读取未使用的备用值。所选 SDK 布局、默认值、状态操作、数学函数体和显式重声明均经过验证；源码替换和普通核心数学调用仍受限制。独立原生采样对照允许较小的平台数学库容差，缓存/参数行为和生成器状态则精确比较。long double 和流序列化仍待接入。

`lognormal_distribution<float/double>` 及 `param_type` 支持构造、复制/移动、赋值、m/s、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。内嵌正态分布在复制/移动及参数替换时保留缓存，reset 清除缓存，冷缓存操作不读取未使用的备用值。采样对所选正态结果执行已验证的 RealType exp，保留临时参数和生成器消耗。布局、嵌套操作、所选 log/sqrt/exp 函数体及显式重声明均经过验证。原生对照允许较小的平台数学库容差；参数、缓存行为和生成器状态精确比较。核心 v2 的 exp 生成 C23 内建调用，其宿主 CRT/libm 导入由后端处理。long double 和流序列化仍待接入。

`weibull_distribution<float/double>` 及 `param_type` 支持默认、形状/尺度、参数、复制/移动构造、赋值、a/b、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。形状和尺度均默认为一，边界为正零和正无穷；采样保留 b*pow(exponential(1),1/a)、RealType 运算和生成器消耗。所选 Weibull/指数分布函数体、默认值及 log/pow 声明均经过验证。核心 v2 的 pow 要求两个匹配的 float/double 参数并生成 C23 内建调用，宿主 CRT/libm 导入由后端处理。原生数值对照使用较小数学容差，参数和生成器状态精确比较。long double 和流序列化仍待接入。

`gamma_distribution<float/double>` 及 `param_type` 支持默认、alpha/beta、参数、复制/移动构造、赋值、alpha/beta 访问器、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。alpha 和 beta 均默认为一，边界为正零和正无穷。采样保留固定 SDK 中 alpha 小于、等于、大于一的三条分支、成对随机值、每次拒绝重试及所选 sqrt/log/pow 运算；调用生成器前复制 alpha，返回时读取 beta，因此保留采样期间的参数修改。所选 SDK 布局、默认值、参数访问器、指数分布依赖、数学函数体和显式重声明均经过验证。原生对照使用较小数学容差，参数效果和生成器状态精确比较；复用现有幂运算，不新增原语或 SDK/目录版本。long double 和流序列化仍待接入。

`chi_squared_distribution<float/double>` 及 `param_type` 支持默认、自由度、参数、复制/移动构造、赋值、n、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。精确布局只保存一个自由度字段，默认为一，没有持久 Gamma 或缓存，边界为正零和正无穷。采样在调用生成器前复制临时 Gamma 参数 n/2 和 2，再复用已验证的 Gamma 拒绝算法；采样中对显式或存储自由度的修改在之后可见，当前 Gamma 则保留已复制参数。SDK 构造、访问器、Gamma/指数分布函数体、数学辅助函数和显式重声明均经过验证。原生对照使用较小数学容差，参数效果和生成器状态精确比较；现有数学运算及 SDK/目录版本不变。long double 和流序列化仍待接入。

`fisher_f_distribution<float/double>` 及 `param_type` 支持默认、m/n、参数、复制/移动构造、赋值、访问器、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。两个自由度均默认为一，边界为正零和正无穷，不保存缓存。采样在任何取样前构造形状 m*RealType(.5)、n*RealType(.5) 且尺度为一的两个 Gamma，再保留 SDK 表达式 n*x/(m*y) 及一种合法的操作数求值顺序；之后的自由度读取与已复制形状分开处理。共享 Gamma 采样器及所选 SDK 构造、访问器、指数/数学函数体和显式重声明均经过验证。原生数值对照使用较小数学容差，生成器状态和参数效果精确比较；现有数学运算及 SDK/目录版本不变。long double 和流序列化仍待接入。

`student_t_distribution<float/double>` 及 `param_type` 支持默认、自由度、参数、复制/移动构造、赋值、n、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。自由度默认为一，边界为负无穷和正无穷。精确布局将自由度与持久正态状态分开保存：构造时正态缓存无效，复制/移动/赋值保留缓存，参数替换保留缓存，reset 清除缓存；固定 SDK 的 Student 相等比较只比较自由度参数，即使正态缓存不同也是如此。采样在任何取样前复制临时 Gamma 形状，保留 SDK 中 double 字面量 .5 的提升、正态缓存行为及 normal*sqrt(n/gamma) 的一种合法求值顺序。正态、Gamma、指数及 sqrt/log/pow 函数体和显式重声明均经过验证。原生数值对照使用较小数学容差，缓存效果和引擎状态精确比较；不新增原语或 SDK/目录版本。long double 和流序列化仍待接入。

`cauchy_distribution<float/double>` 及 `param_type` 支持默认、位置/尺度、参数、复制/移动构造、赋值、a/b、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。默认值为零和一，边界为负无穷和正无穷，不保存缓存。固定 SDK 使用无后缀的 double 圆周率字面量：即使规范随机值为 float，乘圆周率、tan、乘尺度和加位置也使用 double，最后才缩窄结果；实现保留这条路径及一种合法参数读取顺序，不改成 float 圆周率/tanf 或平移后的随机值。所选 SDK 函数体、实际 double 正切签名和显式重声明均经过验证。严格核心 v2 的 tan 要求一个匹配的 float/double 操作数并生成 C23 内建调用，宿主 CRT/libm 导入由后端处理。原生数值对照使用较小数学容差，参数和生成器状态精确比较。long double 和流序列化仍待接入。

`poisson_distribution<IntType>` 及 `param_type` 支持 short、int、long、long long 的有符号和无符号共八种标准结果类型，支持默认、均值、参数、复制/移动构造、赋值、mean、边界、参数接口、相等比较、reset，以及使用已验证源码或 SDK 生成器的两种采样形式。均值默认为一；与固定 SDK 一致，正负无穷均替换为最大的 double。十个 double 参数字段全部初始化并复制。采样保留小均值乘积循环，以及大均值的临时无缓存正态、快速判定、指数候选和接受表达式，精确保持生成器消耗。结果在转换前按 SDK 阈值钳制，包括 64 位整数边界。严格 core-v2 trunc 只接受一个同类型 float/double 参数并生成 C23 builtin。正态、指数、数学及钳制辅助函数体和显式重声明均经过验证；宿主数学导入由 C23 后端负责。流序列化仍待接入。

`negative_binomial_distribution<IntType>`、`geometric_distribution<IntType>` 及各自的 `param_type` 支持八种标准整数结果类型，提供默认、数值、参数、复制/移动构造、赋值、属性、边界、参数接口、相等比较、reset 和两种采样形式。负二项分布在任何取样前保存 k/p；k <= 21*p 时保留 SDK 的重复 Bernoulli 路径，否则使用 Gamma<double>→Poisson<IntType> 混合。几何分布以 k=1 走相同路径，概率阈值及引擎消耗与 SDK 一致。嵌套分布均为临时对象，不新增持久缓存；整数提升、参数变更、嵌套 SDK 函数体和数学/钳制重声明均经过验证。不新增原语或 SDK/目录版本，流序列化仍待接入。

`binomial_distribution<IntType>` 及 `param_type` 支持八种标准整数结果类型，提供默认、试验次数、概率、参数、复制/移动构造、赋值、t/p、边界、参数接口、相等比较、reset 和两种采样形式。默认试验一次、概率 .5，max 返回当前试验次数。仅在内部概率区间缓存 SDK 的众数概率、比值和众数；端点复制及参数操作避免读取未初始化缓存，端点采样不消耗引擎。内部区间保留 SDK 单次取样的众数递推、先下后上顺序、整数提升和实时参数读取。严格 double log-gamma 保留目标 ABI：非 Windows 使用带局部符号变量的 lgamma_r，Windows 使用 lgamma。SDK 辅助函数、exp/log/log-gamma 声明、导入和完整函数体均经过验证。不修改 SDK/目录版本；流序列化仍待接入。

离散权重函数指针还支持算术类型的 const 左值引用、右值引用参数和引用返回值。转换后的实参临时对象在回调期间保留引用身份；引用结果沿用已检查的间接调用路径。double 引用在 SDK 元素构造时复制，即扩容分配后、旧缓冲释放前；其他算术结果在插入前转换为 double。O0/O2 对照覆盖参数修改、全局引用返回及可观察的分配器副作用。

两个算法也接纳经校验的 `std::__wrap_iter<T*>` 范围，包括已支持的 vector 和 string 迭代器。Sample 的输入和输出可分别使用原始指针或包装迭代器，保留 const 输入及经校验的标量转换，并返回原输出迭代器类型。所选 SDK 迭代器操作仍须通过认证。

Core v2 现支持内置且固定版本的 `<type_traits>`，用于编译期类型别名以及整数／枚举常量。`std::remove_cv_t`、`std::is_same_v`、已支持的构造／析构 traits 和 `std::integral_constant::value` 会降为现有核心类型与字面量。驱动会验证并记录 101 个 libc++／resource 头文件的哈希，覆盖受支持的 macOS、Linux 与 Windows 目标。运行时 trait 支持仅限下文列出的、经过验证的函数指针 `integral_constant` 形式；其他 trait 对象与 trait 存储身份仍不受支持。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#compile-time-type_traits).

Core v2 也支持内置且固定版本的 `<cstdint>`。固定、least、fast、指针和最大宽度类型别名，以及标准上下界与常量宏，都会保留所选目标的 C++17 类型和值。驱动会记录独立使用时的 9 个头文件闭包，拒绝引号形式和 C 头文件形式，并允许 `<cstdint>` 与 `<type_traits>` 按任意顺序组合。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#fixed-width-integers-from-cstdint).

Core v2 也支持固定版本的 `<limits>`。已支持整数、`float` 和 `double` 的 `numeric_limits` 整数／枚举数据成员与标准零参数查询会折叠为精确字面量，包括无穷值和 NaN。16 个文件的闭包会在所有目标上经过认证；运行时对象、存储或方法身份、对象限定调用及 `long double` 仍不受支持。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#numeric-bounds-from-limits).

Core v2 也支持固定版本的 `<cstddef>`。目标相关的 `size_t`、`ptrdiff_t` 和 `nullptr_t` 别名、`NULL`、受检查的 `offsetof` 以及 `max_align_t` 布局查询无需 libc++ 即可降低；`std::byte` 存储、位／移位运算和 `to_integer` 会直接降低为标量运算。29 个文件的闭包会在所有目标上经过认证；引号／C 头文件形式、原始内建表达式和标准函数地址仍会被拒绝。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#fundamental-types-and-bytes-from-cstddef).

Core v2 也支持固定版本的 `<new>`。其 37 个文件的闭包在所有受支持目标上完全一致，且不含平台相关内容。`std::nothrow_t`、`std::align_val_t` 和 C++17 干扰尺寸常量保持为编译期或标量元数据；对已接纳的非 volatile 对象指针进行的精确 `std::launder` 调用会直接降低，且实参只求值一次。精确的标准单对象及常量长度数组 placement new 表达式也会复用已捕获的存储，无需运行时调用。直接调用分配函数、nothrow 对象、函数地址和分配失败处理操作仍不在支持范围内。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#new-header-from-new).

纯 `std::launder` 类型与 `noexcept` 查询现在可准备精确的限定、嵌套及源记录 `std::reference_wrapper` 布局，也支持源函数的指针参数；查询不读取指针，也不选择 SDK 函数体或对象生命周期。原始操作数、别名、模板实参、默认实参及源函数体仍会检查；用户替换声明与独立 SDK 函数地址仍被拒绝。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#new-header-from-new).

Core v2 固定版本的 `<memory>` 接口现可将精确的原始指针 `pointer_traits`、`std::allocator<T>` 与 `std::allocator_traits<std::allocator<T>>` 的身份、嵌套别名、rebind 及 trait 常量解析为编译期元数据。精确的 `uses_allocator<T, std::allocator<U>>` 身份、继承的别名及兼容值同样可以解析。精确的单对象 `std::default_delete<T>` 与无界数组 `std::default_delete<T[]>`（包括完整的有界内层维度）使用经过检查的单字节无状态载体，支持默认构造、复制／移动构造及已接纳的 cv 转换构造。其调用运算符只求值一次接收对象和指针，析构完整对象或逆序析构数组元素，并调用经 Clang 选定和检查的标量或数组类／全局 delete。精确的单对象 `std::unique_ptr<T, std::default_delete<T>>` 与无界数组 `std::unique_ptr<T[], std::default_delete<T[]>>`（包括多维所有者）使用指针大小、经过认证的载体。若源码自有的按值自定义删除器是空的、标准布局的平凡单字节记录，其特殊成员均为平凡，且恰好有一个源码定义的 `void operator()(pointer) noexcept`（可带 `const`，不带引用限定符），这些所有者也接纳该删除器。默认构造、空指针构造、原始指针构造、匹配的原始指针／空指针加上绑定到精确删除器左值或右值形参的删除器实参的构造、同类型移动构造及增加 const 的默认删除器转换移动构造，对应的移动赋值、`nullptr` 赋值、`get`、可变／const `get_deleter`、显式布尔转换、`release`、`reset`、成员及自由 `swap`，标量／数组类别相同且原始指针限定兼容的已接纳所有者之间的全部六种比较（与删除器特化无关）、全部六种双向 `nullptr` 比较以及自动析构均直接降低。单对象所有者还支持 `operator->` 和解引用；数组所有者支持 `operator[]`。降低过程保留接收对象与实参的求值顺序。默认删除器调用 Clang 选定的、经过检查的精确源码定义标量或数组类 delete；查找结果仍为全局时，则调用匹配且经过检查的全局 delete/delete[]。已接纳的自定义删除器对非空指针只调用一次，且不要求全局 delete 定义。每个已接纳的非静态成员既可接受对象接收者，也可接受指向该所有者的精确原始指针；指针接收者保留被指对象的 `const`，且只执行一次。精确的单对象 `std::make_unique<T>(args...)` 和无界数组 `std::make_unique<T[]>(count)` 也直接降低，其中 `T` 可带完整的有界内层维度，数量须为 0 到 65536 之间的整数常量表达式。它们会认证固定版本的工厂函数及选定的分配。标量工厂调用 Clang 选定的经过检查的全局或源码类专属 `operator new`，并使用所有者匹配的标量 delete 路径；数组工厂调用 Clang 选定的经过检查的全局或源码类专属 `operator new[]`，并使用匹配的数组 delete[] 路径。工厂对标量元素执行值初始化，或调用精确的源码自有非模板 `noexcept` 记录构造函数，并将所得指针安装到该所有者中。多维构造使用外层数量和固定的内层维度，经过检查的 new[]/delete[] cookie 路径则跟踪展平后的基础元素；运行时数组数量仍被拒绝。提供的构造实参和经过认证的源码自有尾随默认参数均只求值一次。数组工厂为每个展平后的基础元素分别求值这些默认参数。现有的取址操作、标量与源码记录析构、标量或平凡源码记录的未初始化构造，以及不抛异常的零参数源码记录默认／值构造操作均直接降低。对于完整的源码自有记录，未初始化复制、填充和移动也会调用经过认证的 libc++ 辅助函数所选定的精确源码自有非模板 `noexcept` 复制或移动构造函数；精确的运行时分配器对象使用经过检查的单字节无状态载体，支持默认／复制／移动／转换构造、同类型赋值、异构比较、C++17 `address` 和 `max_size`。精确的 C++17 分配器及 allocator-traits `construct` 调用会对可写标量执行值初始化，或调用经过检查的源码自有 `noexcept` 记录构造函数，包括精确的多实参、复制和移动选择，以及经过认证的源码自有尾随默认参数。提供的实参和默认参数均只求值一次。当分配数量已证明不超过 `max_size`，且匹配的全局 new/delete 定义属于源码自有时，精确的分配器及 allocator-traits `allocate`/`deallocate` 转发函数也会针对完整且采用默认 new 对齐的元素降低。单字节元素接受转换为目标 `size_t` 后的运行时数量；更大的元素接受不溢出的整数常量表达式，或来自整个取值范围都不超过 `max_size` 的无符号内置类型的隐式转换。数量、提示参数、接收对象和指针仍只求值一次。精确的 C++17 分配器 `destroy`，以及 `allocator_traits` 的析构、`max_size` 和复制选择转发，都复用经过检查的生命周期与无状态分配器路径，不调用 libc++。经过认证的 267 个文件闭包在所有受支持目标上完全一致，且不含平台相关内容。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

Core v2 也支持固定版本的 `<utility>`。标量 `move`、`forward`、`move_if_noexcept`、`as_const`、`exchange` 和 `swap` 会直接降低。对源码自有记录执行的精确泛型 swap 会调用选定且受支持的移动构造、赋值和临时对象析构。标量或经递归接纳的复合 `std::pair` 的构造、赋值、交换、比较、`make_pair` 和 `get` 也同样支持。元组元数据及 `integer_sequence::size()` 保持为编译期值。经过认证的 87 个文件闭包不会增加 libc++ 运行时依赖；标准函数地址仍会被拒绝。当选定的固定版本 pair 初始化器通过源码自有的复制或移动构造函数构造每个此类字段时，逐元素直接构造 pair 和 `std::make_pair` 也接纳源码自有的非平凡标准布局记录字段。工厂函数的转发链经过认证。pair 字段按逆序析构。同类型整 pair 复制／移动也会认证默认化 pair 构造函数的成员初始化器及选定的源码自有元素构造函数；引用字段保留其绑定。从另一个已接纳 pair 进行的 pair 转换构造同样会为每个自有字段选定并证明其源码自有的复制或移动构造函数。值源和引用源保留其经过检查的 cv 限定和值类别；作为目标的引用字段保留其绑定。同类型及兼容的异构 pair 赋值也会证明固定版本的 pair 函数体，并按顺序调用每个选定的源码自有字段赋值，包括通过引用字段进行的写入。当固定版本的 swap 选定受支持的移动构造和赋值时，成员及自由 pair swap 也接受非平凡的源码自有字段。它们会析构每个 swap 临时对象；引用字段保留其绑定，自交换时亦然。引用值 pair 还支持精确兼容的直接构造、同类型复制／移动构造以及按索引或唯一类型的 `get`；同类型赋值通过其存储的绑定写入，不会重新绑定任一 pair。全部六种比较读取所引用的值，并沿用普通的异构标量比较规则。成员及自由 swap 交换所引用的值，不改变 pair 绑定。兼容转换构造绑定源 pair 的字段，异构赋值则先转换字段值，再通过目标引用写入。`make_pair` 会将经过认证的 `ref`/`cref` 实参解包为对应的引用字段，包括一个引用加一个值的结果。引用与值混合的 pair 也支持精确兼容的直接构造、同类型复制／移动构造、公有字段访问、按索引或唯一类型的 `get`，以及逐字段采用引用或值行为的同类型或兼容异构赋值。全部六种比较使用相同的递归异构值规则。成员及自由 swap 逐字段交换值，不改变引用绑定。兼容转换构造绑定引用字段并初始化值字段。普通值 pair 也支持从值、引用或混合 pair 进行异构构造和赋值，将每个字段复制或转换到独立的存储中。赋值先写入 `first` 再写入 `second`，并以引用返回目标。经过认证的 `std::reference_wrapper` 值也被接纳为 pair 字段，包括嵌套 pair 中的字段。构造和复制保留被包装的绑定；赋值只复制绑定，不对所引用的对象赋值。成员／自由 swap 只有在认证了两个选定的元素交换及其嵌套的 pair 或数组操作之后，才会交换包装器绑定。所有受支持的 pair 和数组 swap（包括源码记录叶节点）都需要这一递归证明。同级的数组字段也受支持；零长度数组跳过元素交换，但两个操作数仍各求值一次。用户 ADL swap 和源码特化仍被拒绝。optional 的成员／自由 swap 也会认证含值判断、值投影以及仅一方含值时的两条转移路径，包括选定的平凡构造和重置操作。支持嵌套的包装器 pair 和元组；用户 ADL swap 和源码操作特化仍被拒绝。含有包装器值叶节点的比较仍被拒绝。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#scalar-utilities-and-pairs-from-utility).

直接 `std::replace`、`std::remove_copy` 和 `std::replace_copy` 也接纳在当前完整表达式中物化的精确经校验包装器值，包括受支持的 `ref`/`cref` 工厂和包装器构造。临时包装器借用被引用对象，并保持自己的绑定直到调用结束；因此将输出包装器复制到临时对象后，即使后续写入重新绑定原包装器，该临时绑定仍固定不变。原始实参、构造和清理保持源码行为。已有的 SDK 循环、转换和赋值证明仍然适用。未物化的 xvalue、SDK 的源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::count` 接纳精确经校验的同一 `std::reference_wrapper<T>` 承载类型的原始指针范围，包括只读输入，以及同类型左值或受支持的完整表达式临时比较包装器。内建标量相等比较读取当前被引用对象的值，并以经校验的指针差类型返回匹配数量。所选 SDK 证明检查原始指针循环、从零开始的计数器、恒等投影、invoke/forward 链、两次引用转换及差值类型别名。每个原始实参只求值一次，构造和清理保持源码行为；绑定和被引用对象均不被赋值。源码比较、枚举或嵌套包装器引用对象、异构比较值、SDK 替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::find` 接纳精确经校验的同一 `std::reference_wrapper<T>` 承载类型的原始指针范围，包括只读输入，以及同类型左值或受支持的完整表达式临时比较包装器。内建标量相等比较读取当前被引用对象的值，并返回第一个匹配的原始迭代器；无匹配时返回原始终点。所选 SDK 证明检查两次迭代器解包、结果重新包装、指针循环、identity/invoke/forward 投影和两次引用转换。每个原始实参只求值一次，构造与清理保持源码行为；绑定和被引用对象均不被赋值。源码比较、枚举或嵌套包装器引用对象、异构比较值、SDK 替换、间接或转换调用及不受支持的原始依赖仍被拒绝。






























`cpp-core-v2`: `std::is_heap_until`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用支持已检查包装器的原始指针区间以及直接构造的透明比较对象，引用类型可为受支持的算术值或对象指针，也支持 const 区间和 const 引用。SDK 证明核验借用比较器、引用转换、迭代器差值元数据及堆扫描中的两个子节点比较。比较读取引用对象的当前值，不修改绑定或对象值；按迭代器顺序返回首个违反堆关系的子节点，有效堆、空区间或单元素区间返回原结束指针。实参只求值一次，保留结果使用和临时对象清理。仍拒绝存储、返回或指定类型的比较器、volatile 或不支持的引用、源代码替换及间接或强制转换的调用入口。

`cpp-core-v2`: `std::is_heap`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用支持已检查包装器的原始指针区间以及直接构造的透明比较对象，引用类型可为受支持的算术值或对象指针，也支持 const 区间和 const 引用。SDK 证明核验借用比较器、引用转换、迭代器差值元数据及堆扫描中的两个子节点比较。比较读取引用对象的当前值，不修改绑定或对象值；扫描到达原结束指针时结果为 true，首个子节点违反堆关系时为 false；空区间和单元素区间返回 true。实参只求值一次，保留结果使用和临时对象清理。仍拒绝存储、返回或指定类型的比较器、volatile 或不支持的引用、源代码替换及间接或强制转换的调用入口。

显式传入透明 multiplies 的 `std::accumulate` 支持经校验 SDK `std::reference_wrapper<T>` 的原始指针范围；引用对象为内建标量，可含 const 输入和 const 引用对象，累加器须为支持的算术类型。它按顺序读取当前引用值，执行通常算术转换，并将每次乘法结果转换回累加器类型；空范围返回初始值。包装器绑定和引用对象保持不变，操作数只求值一次，清理行为得到保留。所选 SDK 模板、完整循环和包装器转换均经过校验。仍拒绝源码替换、自定义运算、枚举或嵌套包装器引用对象、不支持的累加器类型，以及间接或强转后的被调用函数。 四参数形式接受直接初始化的透明 `std::multiplies<>`，或对该 SDK 类型的可变或 const 变量的直接引用。所选 C++17 SDK 运算以左值使用累加器，构造、运算及转发均经过校验；仍拒绝带类型或动态生成的运算对象。

四参数直接调用 `std::accumulate`、`std::reduce`，以及带初值的五参数 `std::inclusive_scan`、`std::exclusive_scan`，支持在已验证的引用包装器指针范围上直接初始化透明比较运算 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::less_equal<>`, `std::greater<>`, `std::greater_equal<>`，并保留八种算术归约。const 包装器及被引用对象、bool、最多 64 位的支持整数、float 和 double 保留现有访问规则；累加器仍使用支持的算术类型，可写扫描输出也支持 bool。每一步以累加器为左操作数、实时被引用值为右操作数，按通常算术公共类型比较，再将 bool 结果转回累加器后处理下一元素。正负零相等；NaN 的比较除不等外均为 false。accumulate 与扫描保留左值累加器参数，reduce 保留已验证的 move。扫描先完成比较与转换再写入，包含扫描写入新值，排他扫描写入保存的前值，保留别名读写和输出末尾。空输入、参数求值与清理保持不变，并验证 SDK 签名、声明来源、构造、精确运算码、forward、转换及完整循环。仍拒绝有类型或函数返回的运算对象、不支持的类型及源代码替换。这四个接口也支持 `std::logical_and<>` 和 `std::logical_or<>`。每步先将累加器转为 bool，仅在短路规则要求时才读取并转换当前被引用值。真值判断使用各自的标量转换，不先转为算术公共类型。bool 结果在扫描输出或下一步之前转回累加器类型。

显式四参数包装器 `std::accumulate` 和 `std::reduce` 也支持直接引用可变或 const 的透明 SDK 运算对象变量，包括参数。现有十六种运算保留原标量值域。参数必须从同一已校验空 SDK 类型的变量，通过已认证、隐式、平凡且不抛出的 SDK 复制／移动构造传入。带类型对象、函数返回对象及成员表达式仍被排除。签名、来源、精确运算体、wrapper 转换、转发、移动与完整循环均须认证。累加器精度、每步 bool 转换、短路、空输入参数求值与清理保持原顺序。带初值的五参数 `std::inclusive_scan` 和 `std::exclusive_scan` 也接受这些运算对象变量。运算及累加器转换在输出写入前完成；包含式扫描写入更新值，排除式扫描写入保存的前值。输出转换、实时别名、返回的输出末端和空输入清理均保持 SDK 顺序。

直接调用 `std::adjacent_difference(first, last, output, operation)` 时，已认证的 `std::reference_wrapper<T>` 原始指针范围还支持直接初始化的透明算术对象：`std::plus<>`, `std::minus<>`, `std::multiplies<>`, `std::divides<>`, `std::modulus<>`, `std::bit_and<>`, `std::bit_or<>`, `std::bit_xor<>`。引用目标可以为 const，类型限 bool、不超过 64 位的整数、float 或 double；取模和位运算要求整型提升后的公共运算类型为整数。输出必须是可写的 bool、整数（不超过 64 位）、float 或 double。首个输出转换首个引用目标的当前值；后续输出按 `operation(当前引用目标, 前一引用目标)` 计算并逐项转换为输出类型。保存的 wrapper 保留绑定，因此输出与引用目标重叠时，写入会影响后续读取。空范围返回原输出指针，单元素范围不执行二元运算；参数求值和临时对象清理保持一致。精确 SDK 运算、转发、wrapper 复制、转换、移动和完整循环均须通过认证。带类型、成员、返回或带副作用的操作表达式及源码替换仍不支持。同一四参数包装器接口也支持 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::less_equal<>`, `std::greater<>`, `std::greater_equal<>`, `std::logical_and<>`, `std::logical_or<>`。比较按通常算术公共操作数类型计算并返回 bool；逻辑运算独立判断当前引用目标，仅在需要右侧短路分支时读取并转换前一引用目标。两者均把 bool 转换为输出类型，首项仍转换原始引用值。保存的包装器绑定、实时输出别名以及所有复制／移动均保留 SDK 顺序。

上述十六种透明运算还支持可变或 const 对象变量及引用参数。实参必须直接引用该变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。复制保留运算顺序、包装器绑定和输出重叠时的实时读取。带类型的对象、成员表达式、返回的对象和带副作用的表达式仍不支持。

包装器数值扫描也支持可写、无限定的 `bool*` 输出：三／四参数 `std::adjacent_difference`、带初值的五参数 `std::inclusive_scan`、四／五参数 `std::exclusive_scan`，以及带初值的六参数 `std::transform_inclusive_scan`／`std::transform_exclusive_scan`。已有透明运算和一元变换保留其类型与有效值域。每次在 SDK 写入位置将原始首值、当前累加器或保存的前值转换为 bool，工作累加器不会因输出为 bool 而被缩窄。零与正负零转换为 false，非零和 NaN 转换为 true。实时包装器绑定、短路、输出别名、末尾、空输入参数求值和清理保留 SDK 顺序。volatile 或不支持的输出、被排除的引用类型以及源代码替换仍保留原边界。

`cpp-core-v2` 还支持十四种已认证包装器数值接口的 bool 累加器：默认及显式 `std::accumulate`、`std::reduce`、`std::inner_product`、二元 `std::transform_reduce`，显式一元 `std::transform_reduce`，带初值的 `std::inclusive_scan`／`std::transform_inclusive_scan`／`std::transform_exclusive_scan`，以及默认和显式 `std::exclusive_scan`。每次 SDK 赋值都将完整算术、比较或逻辑结果转回 bool，再处理下一元素；输出转换独立，排他扫描先计算下一值再写出保存的旧 bool。算术按通常规则提升 bool，比较保留引用值和项的完整精度，逻辑归约保留短路，一元项始终先求值。const 与支持的 bool、窄／宽整数、float、double 引用值、实时别名、末尾、空输入参数和清理保留原有规则。SDK 来源、签名、构造、转换、转发／移动及完整循环均须认证，仍拒绝不符合相应算法构造规则的运算表达式、源码替换及不支持的累加器、引用值和输出。

对两个已验证的引用包装器指针范围，直接调用六参数 `std::inner_product` 与二元 `std::transform_reduce`，项运算和归约运算均支持直接初始化的八种透明算术对象及六种比较对象 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::less_equal<>`, `std::greater<>`, `std::greater_equal<>` 的全部配对。const 包装器及被引用对象、bool、最多 64 位的支持整数、float 和 double 保留访问规则，累加器仍为支持的算术类型。二元项按通常算术公共类型读取第一范围的左操作数和第二范围的右操作数；算术项保留该类型，比较项返回 bool。随后根据累加器和实际项类型确定归约公共类型，因此浮点输入比较为 bool 后可进行整数取余或按位归约。比较归约也返回 bool，每一步均转回累加器。inner_product 保留左值参数，transform_reduce 保留 move／右值 forward。提升、符号与位宽、NaN／正负零、实时绑定、空输入、求值与清理遵循 SDK。验证精确签名、来源、构造、独立输入／项／归约类型、运算码、forward、转换及循环。仍拒绝有类型／成员／函数返回／带副作用的运算表达式、指针／不支持的类型和源代码替换。归约位置也支持 `std::logical_and<>` / `std::logical_or<>`，可搭配全部十四种算术／比较项运算。每步先计算二元项，再判断累加器真值，并按短路规则决定是否将已算出的项转换为 bool。真值判断采用各自的标量转换，不经过算术公共类型，bool 结果随后转回累加器。二元项位置现也支持 `std::logical_and<>` 和 `std::logical_or<>`，可搭配全部十六种归约。项先判断左侧被引用值，仅在需要时读取并转换右侧；两个 bool 转换彼此独立，不经过算术公共类型。既有标量项也按短路规则读取右侧；输入与参数类型不同时，有类型标量参数的转换及临时量构造仍发生在调用前，匹配类型则绑定引用。完整项仍先于外层归约计算。

六参数 `std::inner_product` 与二元 `std::transform_reduce` 的两个运算位置，还支持全部十六种透明运算的可变或 const 变量及引用参数。每个实参必须直接引用变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。项运算与归约运算分别检查。数值转换、累加器转发、包装器绑定、空区间参数求值和清理保留原有规则。

对已验证的引用包装器指针范围，直接调用五参数一元 `std::transform_reduce` 与六参数 `std::transform_inclusive_scan`／`std::transform_exclusive_scan`，支持直接初始化的透明 `std::negate<>`、整数 `std::bit_not<>` 或 `std::logical_not<>`，搭配比较运算 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::less_equal<>`, `std::greater<>`, `std::greater_equal<>`，并保留八种算术归约。支持 const 包装器及被引用对象；值可为 bool、最多 64 位整数、float 或 double，累加器仍使用支持的算术类型，可写扫描输出也支持 bool。取负与按位取反保留提升后的项精度，逻辑非产生 bool。每步以累加器为左操作数、转换后的项为右操作数，按通常公共类型比较，再将 bool 结果转回累加器。NaN 和正负零的比较保持 SDK 行为。transform_reduce 保留移动后的累加器，扫描保留左值参数，并在写入更新值或前值之前完成转换、比较与累加器转换。保留实时别名、输出末尾、空输入、参数求值与清理。验证 SDK 来源、签名、构造、精确一元及二元运算码、forward、转换和完整循环。仍拒绝有类型、成员、函数返回或带副作用的运算表达式、不支持的类型和源代码替换。这三个接口也支持 `std::logical_and<>` / `std::logical_or<>`。每步先求值一元变换，再将累加器转为 bool，并按短路规则决定是否转换已算出的项。两个真值判断之前均不进行算术公共类型转换。bool 结果在输出或下一步之前转回累加器类型。

一元 `std::transform_reduce` 和带初值的 `std::transform_inclusive_scan`／`std::transform_exclusive_scan`，在两个运算位置还支持可变或 const 变量及引用参数。归约保留全部十六种透明运算，变换保留 `std::negate<>`、整数 `std::bit_not<>` 和 `std::logical_not<>`。实参必须直接引用变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。项精度、累加器转发、变换先求值、扫描写入、实时绑定及参数清理均保持 SDK 顺序。

`cpp-core-v2`: `std::rotate`, `std::reference_wrapper<T>`. 直接调用支持已检查包装器的可变原始指针区间，引用可为受支持的算术值或对象指针，也可为 const。SDK 证明认证迭代器类别分派、平凡移动特征、左右移位、前向和最大公约数循环、等长区间交换、返回指针及所选移动适配器。旋转重排包装器绑定，保留引用对象值，并返回原首元素的新位置；空区间或任一半区间为空时保持标准端点语义。实参仅求值一次，保留结果使用和临时对象清理。仍拒绝 volatile 或不支持的引用、源代码替换及间接或强制转换的调用入口。

`cpp-core-v2`: `std::partition`, `std::reference_wrapper<T>`, `std::logical_not<>`. 直接调用并直接初始化透明 logical_not，支持已检查包装器的可变原始指针区间，引用可为 bool、最多 64 位整数、float 或 double，也可为 const。完整认证 SDK 的迭代器类别、借用谓词、两层委托、端点选择、双向扫描、指针 pair 及迭代器交换。转换读取实时引用值，选择转换为 false 的值，按 SDK 顺序交换包装器绑定并返回原区间的分界指针；原对象值不变，空区间不读取或交换元素。保留实参求值、结果使用及清理。仍拒绝带类型、存储或返回的操作、volatile 或不支持的引用及源代码替换。

`cpp-core-v2`: `std::minmax`, `std::initializer_list`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用并直接初始化透明 less 或 greater，支持非空、同类型已检查包装器的初始化列表，包括 const 包装器和引用。完整认证 SDK 路径，检查列表端点、借用比较器、可调用断言、identity/invoke、比较适配器及成对扫描，再将两个所选包装器的绑定复制到值 pair；相等时选择首个最小值和最后一个最大值。保留实时绑定、实参求值、引用身份、结果使用及清理。仍拒绝存储、返回或带类型的比较器、volatile 或不兼容引用及源代码替换。

`cpp-core-v2`: `std::max`, `std::initializer_list`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用并直接初始化透明 less 或 greater，支持非空、同类型已检查包装器的初始化列表，包括 const 包装器和引用。完整认证 SDK 路径，检查列表端点、比较器借用、私有 __max_element、已检查的前向迭代器断言 及完整扫描，再将所选包装器绑定复制到结果值；相等时选择首个列表元素。保留实时绑定、实参求值、引用身份、结果使用及清理，并检查比较器、转发和包装器转换。仍拒绝存储、返回或带类型的比较器、volatile 或不兼容引用及源代码替换。

`cpp-core-v2`: `std::min`, `std::initializer_list`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用并直接初始化透明 less 或 greater，支持非空、同类型已检查包装器的初始化列表，包括 const 包装器和引用。完整认证 SDK 路径，检查列表端点、比较器借用、私有 __min_element、identity/invoke 及完整扫描，再将所选包装器绑定复制到结果值；相等时选择首个列表元素。保留实时绑定、实参求值、引用身份、结果使用及清理，并检查比较器、转发和包装器转换。仍拒绝存储、返回或带类型的比较器、volatile 或不兼容引用及源代码替换。

`cpp-core-v2`: `std::minmax`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用并直接初始化透明 less 或 greater，支持两个相同类型的已检查包装器，包括 const 包装器和引用。完整认证的 SDK 条件表达式比较第二个引用与第一个引用，并构造由原包装器 const 引用组成的已检查 pair；相等时保留第一和第二个参数的顺序。保留实时绑定、实参求值、引用身份、结果使用及清理，并检查比较器、转发和包装器转换。仍拒绝存储、返回或带类型的比较器、volatile 或不兼容引用及源代码替换。

`cpp-core-v2`: `std::clamp`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用并直接初始化透明 less 或 greater，支持三个相同类型的已检查包装器，包括 const 包装器和引用；上下界须按比较器排序。完整认证 SDK 断言及嵌套条件表达式，先比较值与下界，再比较上界与值，返回所选原包装器的 const 引用；边界相等时选择值。保留实时绑定、实参求值、引用身份、结果使用及清理，并检查比较器、转发和包装器转换。仍拒绝存储、返回或带类型的比较器、volatile 或不兼容引用及源代码替换。

`cpp-core-v2`: `std::max`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用并直接初始化透明 less 或 greater，支持两个相同类型的已检查包装器，包括 const 包装器和引用。完整认证的 SDK 条件表达式比较第一个引用与第二个引用，返回所选原包装器的 const 引用；相等时选择第一个参数。保留实时绑定、实参求值、引用身份、结果使用及清理，并检查比较器、转发和包装器转换。仍拒绝存储、返回或带类型的比较器、volatile 或不兼容引用及源代码替换。

`cpp-core-v2`: `std::min`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用并直接初始化透明 less 或 greater，支持两个相同类型的已检查包装器，包括 const 包装器和引用。完整认证的 SDK 条件表达式比较第二个引用与第一个引用，返回所选原包装器的 const 引用；相等时选择第一个参数。保留实时绑定、实参求值、引用身份、结果使用及清理，并检查比较器、转发和包装器转换。仍拒绝存储、返回或带类型的比较器、volatile 或不兼容引用及源代码替换。

`cpp-core-v2`: `std::partition_point`, `std::reference_wrapper<T>`, `std::logical_not<>`. 直接调用并直接初始化透明谓词，支持已检查的算术包装器指针范围，包括 const 包装器和引用。范围须按零值引用在非零值之前分区；返回首个非零位置或结束指针。须认证完整 SDK 二分搜索、距离、正数折半、前进、逻辑取反、转发、引用转换及返回指针。保留实时绑定、实参求值、结果使用及清理；仍拒绝成员、返回或带类型的谓词、volatile 引用及源代码替换。

直接 `std::copy_if`, `std::remove_copy_if`, `std::replace_copy_if`, `std::partition_copy`, `std::replace_if`, `std::remove_if`, `std::is_partitioned`, `std::partition_point` 还支持透明 `std::logical_not<>` 的可变或 const 变量及引用参数。实参必须直接引用变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。受支持引用值、真值转换、实时绑定、提前退出、计数、空输入结果、实参求值与清理保持 SDK 行为。

直接调用 `std::is_partitioned` 并直接初始化透明 `std::logical_not<>`，检查有限 `std::reference_wrapper<T>` 指针范围内的所有零值算术引用是否位于非零值之前。空输入、全零及全非零范围返回 true；非零尾段中出现零值立即返回 false。须认证 SDK 入口、完整前缀和尾段扫描、范围结束检查、两次扫描间的递增、布尔返回、逻辑取反、转发及引用转换。保留实时绑定、const 包装器和引用、实参求值、结果使用及清理。成员、返回或带类型的操作对象、volatile 引用和源代码替换仍被拒绝。 用户源码中的 `using std::is_partitioned;` 声明可导入固定 SDK 的重载集；每次调用仍须通过相同的源码与操作检查。

`cpp-core-v2`: `std::equal_range`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用支持直接初始化的透明 less 或 greater，输入为已验证 SDK 包装类型的原始指针范围。包装对象及引用对象均可带 const；引用对象支持 bool、不超过 64 位的整数、float、double 及对象指针。查询值须使用同一不带限定的包装类型。对于按所选比较器分区的范围，完整认证的 SDK 搜索通过已检查的 pair 返回下界和上界指针。查询 const 引用、可调用和指针复制特征、比较器引用别名、identity 投影、迭代器策略、正数折半、两个比较方向、完整外层循环及借用比较器的下界和上界搜索均需验证。比较读取当前引用值，保留绑定，各实参求值一次，析构保持原语义。存储、返回或带类型的比较器、源码替换、不兼容或 volatile 引用对象仍被拒绝。

`cpp-core-v2`: `std::binary_search`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用支持直接初始化的透明 less 或 greater，输入为已验证 SDK 包装类型的原始指针范围。包装对象及引用对象均可带 const；引用对象支持 bool、不超过 64 位的整数、float、double 及对象指针。查询值须使用同一不带限定的包装类型。对于按所选比较器分区的范围，完整认证的 SDK 搜索返回首个不小于查询值的元素或结束指针。查询 const 引用、可调用特征、identity 投影、迭代器策略的距离计算与前进、invoke 路径、正数折半及完整二分搜索均需验证。比较读取当前引用值，保留绑定，各实参求值一次，析构保持原语义。存储、返回或带类型的比较器、源码替换、不兼容或 volatile 引用对象仍被拒绝。 在认证的 lower_bound 搜索之后，SDK 仅在结果不是结束指针且 const 查询值对该元素的反向比较为假时返回 true。两个比较方向和短路结果均需验证。

`cpp-core-v2`: `std::upper_bound`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用支持直接初始化的透明 less 或 greater，输入为已验证 SDK 包装类型的原始指针范围。包装对象及引用对象均可带 const；引用对象支持 bool、不超过 64 位的整数、float、double 及对象指针。查询值须使用同一不带限定的包装类型。对于按所选比较器分区的范围，完整认证的 SDK 搜索返回首个大于查询值的元素或结束指针。查询 const 引用、可调用特征、identity 投影、迭代器策略的距离计算与前进、invoke 路径、正数折半及完整二分搜索均需验证。比较读取当前引用值，保留绑定，各实参求值一次，析构保持原语义。存储、返回或带类型的比较器、源码替换、不兼容或 volatile 引用对象仍被拒绝。

`cpp-core-v2`: `std::lower_bound`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用支持直接初始化的透明 less 或 greater，输入为已验证 SDK 包装类型的原始指针范围。包装对象及引用对象均可带 const；引用对象支持 bool、不超过 64 位的整数、float、double 及对象指针。查询值须使用同一不带限定的包装类型。对于按所选比较器分区的范围，完整认证的 SDK 搜索返回首个不小于查询值的元素或结束指针。查询 const 引用、可调用特征、identity 投影、迭代器策略的距离计算与前进、invoke 路径、正数折半及完整二分搜索均需验证。比较读取当前引用值，保留绑定，各实参求值一次，析构保持原语义。存储、返回或带类型的比较器、源码替换、不兼容或 volatile 引用对象仍被拒绝。

`cpp-core-v2`: `std::minmax_element`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用支持直接初始化的透明 less 或 greater，输入为已验证 SDK 包装类型的原始指针范围。包装对象及引用对象均可带 const；引用对象支持 bool、不超过 64 位的整数、float、double 及对象指针。完整认证的 SDK 成对扫描按所选比较器返回首个最小元素及末个最大元素；空范围返回两个结束指针。可调用特征、前向迭代器特征、identity 投影、带两个引用字段的私有比较适配器、invoke 路径及 pair 构造均需验证。比较读取当前引用值，保留绑定，各实参求值一次，析构保持原语义。存储、返回或带类型的比较器、源码替换、不兼容或 volatile 引用对象仍被拒绝。

`cpp-core-v2`: `std::set_intersection`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用支持直接初始化的透明 less 或 greater，两个原始指针输入范围及可修改输出必须使用同一已验证 SDK 包装类型。输入 const 限定可不同；引用对象支持 bool、不超过 64 位的整数、float、double 及对象指针，也可带 const。对于按比较器排序的输入，完整认证的 SDK 扫描按两个方向比较当前引用值，按顺序复制匹配元素的第一输入绑定。输出重复数量为两个输入数量的最小值。前向迭代器类别分派、两侧的单侧搜索和二分搜索、identity 投影、相等时输出的辅助函数及迭代器策略均需验证。引用对象保持不变。比较器引用转发、私有三字段结果、移动构造及输出投影均需验证。各实参求值一次，析构保持原语义。存储、返回或带类型的比较器、源码替换、const 输出及不兼容引用对象仍被拒绝。

`cpp-core-v2`: `std::set_symmetric_difference`, `std::reference_wrapper<T>`, `std::less<>`, `std::greater<>`. 直接调用支持直接初始化的透明 less 或 greater，两个原始指针输入范围及可修改输出必须使用同一已验证 SDK 包装类型。输入 const 限定可不同；引用对象支持 bool、不超过 64 位的整数、float、double 及对象指针，也可带 const。对于按比较器排序的输入，完整认证的 SDK 扫描按两个方向比较当前引用值，消耗匹配元素，并按顺序复制未匹配绑定及两侧剩余区间。输出重复数量为两个输入数量之差的绝对值。引用对象保持不变。比较器引用转发、私有三字段结果、移动构造及输出投影均需验证。各实参求值一次，析构保持原语义。存储、返回或带类型的比较器、源码替换、const 输出及不兼容引用对象仍被拒绝。

`cpp-core-v2` 支持直接调用带直接初始化的透明 `std::less<>` 或 `std::greater<>` 的 `std::set_difference`，用于同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针输入范围及可修改输出。输入包装对象的 const 限定可不同；引用对象支持 bool、不超过 64 位的整数、float、double 或对象指针，也可带 const。对于按所选比较器排序的输入，完整认证的 SDK 扫描按两个方向比较当前引用值，消耗匹配元素，并按顺序复制第一范围剩余元素的绑定。输出重复数量为第一输入数量减去第二输入数量，最小为零。引用对象保持不变，复制尾段、pair 结果及输出投影均需验证。各实参求值一次，析构保持原语义。存储、返回或带类型的比较器、源码替换、const 输出及不兼容引用对象仍被拒绝。

`cpp-core-v2` 支持直接调用带直接初始化的透明 `std::less<>` 或 `std::greater<>` 的 `std::set_union`，用于同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针输入范围及可修改输出。输入包装对象的 const 限定可不同；引用对象支持 bool、不超过 64 位的整数、float、double 或对象指针，也可带 const。对于按所选比较器排序的输入，lowering 按 SDK 比较顺序读取当前引用值，相等时优先选择第一范围，并向输出复制包装绑定。引用对象保持不变，两侧复制尾段和输出末端结果均被保留。所有实参各求值一次，析构保持原语义。完整 SDK 重载、比较器、转发、包装转换、引用别名、循环、赋值和尾段转移均需认证。存储、返回或带类型的比较器、源码替换、const 输出及不兼容引用对象仍被拒绝。 对于每个比较器等价类，输出重复数量取两个输入中的最大值。证明还检查私有三字段结果对象、其构造、move 操作及输出投影。

`cpp-core-v2` 支持直接调用带直接初始化的透明 `std::less<>` 或 `std::greater<>` 的 `std::merge`，用于同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针输入范围及可修改输出。输入包装对象的 const 限定可不同；引用对象支持 bool、不超过 64 位的整数、float、double 或对象指针，也可带 const。对于按所选比较器排序的输入，lowering 按 SDK 比较顺序读取当前引用值，相等时优先选择第一范围，并向输出复制包装绑定。引用对象保持不变，两侧复制尾段和输出末端结果均被保留。所有实参各求值一次，析构保持原语义。完整 SDK 重载、比较器、转发、包装转换、引用别名、循环、赋值和尾段转移均需认证。存储、返回或带类型的比较器、源码替换、const 输出及不兼容引用对象仍被拒绝。

`cpp-core-v2` 支持在同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针范围上，直接调用带直接初始化的透明 `std::less<>` 或 `std::greater<>` 的 `std::includes`。两个输入包装对象的 const 限定可不同；引用对象支持 bool、不超过 64 位的整数、float、double 或对象指针，也可带 const。对于按所选比较器排序的范围，完整认证的 SDK 扫描按两个方向读取引用对象的当前值，保留重复元素数量，并返回第一个范围是否包含第二个范围。第二个范围为空时返回 true。所有实参各求值一次，短路和析构保持原语义。证明检查两个比较特化、转发、包装转换、identity 投影、可调用性检查、比较器引用别名、invoke 分派和每个循环递增。存储、返回或带类型的比较器、源码替换、不兼容引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 支持在同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针范围上，直接调用带直接初始化的透明 `std::less<>` 或 `std::greater<>` 的 `std::lexicographical_compare`。两个输入包装对象的 const 限定可不同；引用对象支持 bool、不超过 64 位的整数、float、double 或对象指针，也可带 const。两个比较方向均读取引用对象的当前值，在首次差异处短路，并处理空范围、相等范围和真前缀。所有实参各求值一次，析构保持原语义。完整的所选 SDK 扫描、两个方向的比较特化、转发、包装转换、identity 投影、迭代器解包及 invoke 分派均需验证。存储、返回或带类型的比较器、源码替换、不兼容引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用直接初始化的透明 `std::less<>` 或 `std::greater<>` 比较器的 `std::is_sorted`，用于已检查 SDK `std::reference_wrapper<T>` 的原始指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。比较读取引用对象的当前值，返回范围是否已排序的布尔结果，绑定和引用对象保持不变。空输入返回 true。实参各求值一次，接收对象的析构保持原语义。所选 SDK 的小于或大于比较及转发、比较器引用别名、相邻元素扫描及末尾比较均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用直接初始化的透明 `std::less<>` 或 `std::greater<>` 比较器的 `std::is_sorted_until`，用于已检查 SDK `std::reference_wrapper<T>` 的原始指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。比较读取引用对象的当前值，返回指向第一个逆序元素的原始迭代器，已排序范围返回末尾迭代器，绑定和引用对象保持不变。空输入返回起始迭代器。实参各求值一次，接收对象的析构保持原语义。所选 SDK 的小于或大于比较及转发、比较器引用别名及相邻元素扫描均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

直接调用 `std::max_element` 并直接初始化透明 `std::less<>` 或 `std::greater<>` 时，支持已检查 SDK `std::reference_wrapper<T>` 的原始指针范围，包括 const 包装对象和引用对象、bool、至多 64 位整数、float、double 及支持的对象指针。比较读取实时引用值，返回所选比较下的第一个最大值迭代器；空输入返回起始迭代器。绑定、引用对象、求值和临时对象清理保持原语义。完整验证 SDK 公共重载、前向迭代器及可调用断言、比较器引用别名、所选比较、转发、引用转换与扫描。存储、返回、带类型或表达式比较器、源码替换、枚举或嵌套引用对象及间接调用仍被拒绝。

直接调用 `std::min_element` 并直接初始化透明 `std::less<>` 或 `std::greater<>` 时，支持已检查 SDK `std::reference_wrapper<T>` 的原始指针范围，包括 const 包装对象和引用对象、bool、至多 64 位整数、float、double 及支持的对象指针。比较读取实时引用值，返回所选比较下的第一个最小值迭代器；空输入返回起始迭代器。绑定、引用对象、求值和临时对象清理保持原语义。完整验证 SDK 公共重载、前向迭代器及可调用断言、比较器引用别名、identity、invoke、所选比较、转发、引用转换与扫描。存储、返回、带类型或表达式比较器、源码替换、枚举或嵌套引用对象及间接调用仍被拒绝。

四个迭代器参数的直接 `std::find_end` 调用使用透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且各输入的 const 限定符可不同的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、前向迭代器类别转换、搜索及重试循环、指针对构造、迭代器推进、恒等投影及谓词调用、所选透明比较运算、转发、引用转换及已使用声明的重声明。按范围顺序比较引用对象的当前值，返回最后一个对模式全部元素满足谓词的连续子序列起始指针；空模式返回输入结束指针，没有匹配时返回输入结束指针。两个范围均保持不变。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

获准的包装器 `std::find_first_of`, `std::search`, `std::find_end`, `std::search_n` 还支持透明比较对象的可变或 const 变量及引用参数：`std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>`。实参必须直接引用变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。保留现有相同包装器类型及输入各自 const 限定规则。比较读取实时引用值，保持顺序、提前退出、不同长度行为、原始迭代器、空输入实参求值与清理。

四个迭代器参数的直接 `std::search` 调用使用透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且各输入的 const 限定符可不同的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、随机访问分派、范围长度、搜索循环、指针对构造、迭代器推进、恒等投影及谓词调用、所选透明比较运算、转发、引用转换及已使用声明的重声明。按范围顺序比较引用对象的当前值，返回首个对模式全部元素满足谓词的连续子序列起始指针；空模式返回输入开始指针，没有匹配时返回输入结束指针。两个范围均保持不变。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

四个迭代器参数的直接 `std::find_first_of` 调用使用透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且各输入的 const 限定符可不同的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、嵌套搜索循环及谓词调用、所选透明比较运算、转发、引用转换及已使用声明的重声明。按嵌套范围顺序比较引用对象的当前值，返回首个对任意候选满足谓词的输入指针；没有匹配或任一范围为空时返回首个输入范围的结束指针。两个范围均保持不变。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

四个迭代器参数的直接 `std::mismatch` 调用使用透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且各输入的 const 限定符可不同的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、所选透明比较运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值，返回指向首次谓词失败位置的两个指针；比较由两个尾指针共同限制；所有比较均为 true 时返回较短范围尾指针及对应迭代器，任一范围为空时返回两个原始指针。还校验 SDK 指针距离最小值的重载与比较器、有界辅助函数及 pair 构造。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。 额外校验身份投影、invoke、辅助委托与循环、迭代器重新包装、make_pair、pair 构造和 move。

获准的包装器三迭代器和四迭代器形式 `std::equal`、`std::mismatch` 还支持透明比较对象的可变或 const 变量及引用参数：`std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>`。实参必须直接引用变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。保留现有相同包装器类型及输入各自 const 限定规则。比较读取实时引用值，保持顺序、提前退出、不同长度行为、原始迭代器对、空输入实参求值与清理。

三个迭代器参数的直接 `std::mismatch` 调用使用透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且各输入的 const 限定符可不同的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、所选透明比较运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值，返回指向首次谓词失败位置的两个指针；所有比较均为 true 时返回首范围尾指针及对应的第二迭代器，空输入返回两个原始指针。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。 额外校验身份投影、invoke、辅助委托与循环、迭代器重新包装、make_pair、pair 构造和 move。

四个迭代器参数的直接 `std::equal` 调用使用透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且各输入的 const 限定符可不同的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、所选透明比较运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值并返回布尔结果：首次谓词失败即返回 false，两个范围均为空时返回 true。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。 额外校验 SDK 迭代器类别、距离计算、长度预检查、身份投影和 invoke；两范围长度不同会在元素比较前返回 false，并遵守两侧尾指针。

三个迭代器参数的直接 `std::equal` 调用使用透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且各输入的 const 限定符可不同的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、所选透明比较运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值并返回布尔结果：首次谓词失败即返回 false，首个范围为空时返回 true。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 的直接 `std::search_n` 支持 包装器元素为 const 或可写的原始指针输入范围及相同包装器类型的左值查询，元素为引用 bool、最多 64 位整数、float 或 double 的 `std::reference_wrapper<T>`。查询包装器及引用对象可为 const，计数可为内置整数或非作用域枚举，其经 SDK 提升后的底层整数类型须不超过 64 位。完整校验 SDK 公共函数、计数转换、随机访问分派及特征、边界、搜索及重试循环、指针对构造、迭代器推进、恒等投影、比较、转发、引用转换和已使用声明的重声明。对实时查询连续满足指定次数谓词时返回首个起始指针；计数为零时返回输入开始指针，无匹配时返回输入结束指针。保留绑定和值、别名、NaN 与正负零比较语义、参数只求值一次及临时对象清理。 不兼容的查询包装器类型、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 的直接 `std::search_n` 支持 const 原始指针输入范围及相同包装器类型的左值查询，元素为引用 bool、最多 64 位整数、float 或 double 的 `std::reference_wrapper<T>`。查询包装器及引用对象可为 const，计数可为内置整数或非作用域枚举，其经 SDK 提升后的底层整数类型须不超过 64 位。完整校验 SDK 公共函数、计数转换、随机访问分派及特征、边界、搜索及重试循环、指针对构造、迭代器推进、恒等投影、比较、转发、引用转换和已使用声明的重声明。对实时查询连续满足指定次数谓词时返回首个起始指针；计数为零时返回输入开始指针，无匹配时返回输入结束指针。保留绑定和值、别名、NaN 与正负零比较语义、参数只求值一次及临时对象清理。 非 const 输入包装器或不兼容的查询包装器类型、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

四个迭代器参数的直接 `std::find_end` 调用使用透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、前向迭代器类别转换、搜索及重试循环、指针对构造、迭代器推进、恒等投影及谓词调用、所选透明比较运算、转发、引用转换及已使用声明的重声明。按范围顺序比较引用对象的当前值，返回最后一个对模式全部元素满足谓词的连续子序列起始指针；空模式返回输入结束指针，没有匹配时返回输入结束指针。两个范围均保持不变。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

四个迭代器参数的直接 `std::search` 调用使用透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、随机访问分派、范围长度、搜索循环、指针对构造、迭代器推进、恒等投影及谓词调用、所选透明比较运算、转发、引用转换及已使用声明的重声明。按范围顺序比较引用对象的当前值，返回首个对模式全部元素满足谓词的连续子序列起始指针；空模式返回输入开始指针，没有匹配时返回输入结束指针。两个范围均保持不变。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

四个迭代器参数的直接 `std::find_first_of` 调用使用透明 `std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、嵌套搜索循环及谓词调用、所选透明比较运算、转发、引用转换及已使用声明的重声明。按嵌套范围顺序比较引用对象的当前值，返回首个对任意候选满足谓词的输入指针；没有匹配或任一范围为空时返回首个输入范围的结束指针。两个范围均保持不变。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

四个迭代器参数的直接 `std::mismatch` 调用使用透明 `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、所选透明比较运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值，返回指向首次谓词失败位置的两个指针；比较由两个尾指针共同限制；所有比较均为 true 时返回较短范围尾指针及对应迭代器，任一范围为空时返回两个原始指针。还校验 SDK 指针距离最小值的重载与比较器、有界辅助函数及 pair 构造。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。 额外校验身份投影、invoke、辅助委托与循环、迭代器重新包装、make_pair、pair 构造和 move。

三个迭代器参数的直接 `std::mismatch` 调用使用透明 `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、所选透明比较运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值，返回指向首次谓词失败位置的两个指针；所有比较均为 true 时返回首范围尾指针及对应的第二迭代器，空输入返回两个原始指针。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。 额外校验身份投影、invoke、辅助委托与循环、迭代器重新包装、make_pair、pair 构造和 move。

四个迭代器参数的直接 `std::equal` 调用使用透明 `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、所选透明比较运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值并返回布尔结果：首次谓词失败即返回 false，两个范围均为空时返回 true。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。 额外校验 SDK 迭代器类别、距离计算、长度预检查、身份投影和 invoke；两范围长度不同会在元素比较前返回 false，并遵守两侧尾指针。

三个迭代器参数的直接 `std::equal` 调用使用透明 `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、所选透明比较运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值并返回布尔结果：首次谓词失败即返回 false，首个范围为空时返回 true。输入绑定和值、别名、NaN 与正负零的比较语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

四个迭代器参数的直接 `std::mismatch` 调用使用透明 `std::equal_to<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、透明相等运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值，返回指向首次不匹配位置的两个指针；比较由两个尾指针共同限制；全部相等时在较短范围的尾端停止并返回对应迭代器，任一范围为空时返回两个原始指针。还校验 SDK 指针距离最小值的重载与比较器、有界辅助函数及 pair 构造。输入绑定和值、别名、NaN 与正负零的相等语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。 额外校验身份投影、invoke、辅助委托与循环、迭代器重新包装、make_pair、pair 构造和 move。

三个迭代器参数的直接 `std::mismatch` 调用使用透明 `std::equal_to<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、透明相等运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值，返回指向首次不匹配位置的两个指针；全部相等时返回首范围尾指针及对应的第二迭代器，空输入返回两个原始指针。输入绑定和值、别名、NaN 与正负零的相等语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。 额外校验身份投影、invoke、辅助委托与循环、迭代器重新包装、make_pair、pair 构造和 move。

四个迭代器参数的直接 `std::equal` 调用使用透明 `std::equal_to<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、透明相等运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值并返回布尔结果：首次不匹配即返回 false，两个范围均为空时返回 true。输入绑定和值、别名、NaN 与正负零的相等语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。 额外校验 SDK 迭代器类别、距离计算、长度预检查、身份投影和 invoke；两范围长度不同会在元素比较前返回 false，并遵守两侧尾指针。

三个迭代器参数的直接 `std::equal` 调用使用透明 `std::equal_to<>` 时，支持相同 `std::reference_wrapper<T>` 类型且元素限定符一致的原始指针范围，引用类型为 bool、最多 64 位整数、float 或 double。两个输入及引用对象均可为 const。校验覆盖完整 SDK 公共函数、指针展开、比较循环、透明相等运算、转发、引用转换及已使用声明的重声明。比较引用对象的当前值并返回布尔结果：首次不匹配即返回 false，首个范围为空时返回 true。输入绑定和值、别名、NaN 与正负零的相等语义、参数求值和临时对象清理保持一致。限定符或包装器类型不同的输入、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

直接调用 `std::unique_copy` 并使用透明 `std::equal_to<>`，支持相同 `std::reference_wrapper<T>` 类型的原始指针输入范围和可写输出范围，引用类型为 bool、最多 64 位整数、float 或 double。输入包装器元素和引用对象可为 const。校验覆盖完整 SDK 公共函数、从输入重新读取的标签选择、辅助循环、透明相等运算、包装器隐式默认平凡复制赋值、pair 构造、move、转发、引用转换及已使用声明的重声明。每组相邻相等值的首个绑定按稳定顺序复制，并返回输出尾指针；输入绑定、引用值、未使用的输出绑定、别名、当前值读取、正负零、空范围、单元素范围、参数求值和临时对象清理保持一致。带限定符的输出包装器、不同包装器类型、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

直接 `std::adjacent_find` 还支持透明比较对象的可变或 const 变量及引用参数：`std::equal_to<>`, `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>`。直接 `std::unique` 和 `std::unique_copy` 支持透明 `std::equal_to<>` 的相同变量形式。实参必须直接引用变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。现有引用值与输出类型范围、实时绑定、比较顺序、稳定压缩、返回迭代器、空输入实参求值及清理保持 SDK 行为。

直接调用 `std::unique` 并使用透明 `std::equal_to<>`，支持可写原始指针范围中的 `std::reference_wrapper<T>`，引用类型为 bool、最多 64 位整数、float 或 double，引用对象可为 const。校验覆盖 SDK 公共函数、完整的初始相邻查找与后续压缩循环、身份投影、invoke、透明相等运算、迭代器策略与移动、包装器隐式默认平凡移动赋值、pair 构造、move、转发、引用转换及已使用声明的重声明。相邻相等值按稳定顺序压缩，每组保留首次出现的绑定并返回新尾指针；引用值、别名、当前值读取、正负零、空范围、单元素范围、参数求值和临时对象清理保持一致。带限定符的包装器元素、volatile 或枚举引用、long double、扩展整数、嵌套包装器和用户记录引用仍不支持；成员表达式或返回的操作对象、类型化或表达式操作、独立函数地址、间接或转换调用，以及改写已使用的 SDK 声明仍会拒绝。

直接调用 `std::adjacent_find` 并使用透明 `std::not_equal_to<>`, `std::less<>`, `std::greater<>`, `std::less_equal<>`, `std::greater_equal<>`，支持由原始指针界定的 `std::reference_wrapper<T>` 范围，引用类型为 bool、最多 64 位整数、float 或 double；包装器元素和引用对象均可为 const。校验覆盖 SDK 公共函数、move 适配、身份投影、invoke、完整查找循环、所选比较运算、转发和引用转换。比较读取当前引用值，返回首个相邻匹配的位置或尾指针，保留绑定、引用值、空范围、单元素范围、别名、NaN、正负零、参数求值及临时对象清理。成员表达式或返回的操作对象、类型化操作、表达式操作、间接或转换调用、改写已使用 SDK 声明，以及 volatile、枚举、long double、扩展整数、嵌套包装器和用户记录引用仍不支持。

直接调用 `std::adjacent_find` 并使用透明 `std::equal_to<>`，支持由原始指针界定的 `std::reference_wrapper<T>` 范围，引用类型为 bool、最多 64 位整数、float 或 double；包装器元素和引用对象均可为 const。校验覆盖 SDK 公共函数、move 适配、身份投影、invoke、完整查找循环、相等运算、转发和引用转换。比较读取当前引用值，返回首个相邻匹配的位置或尾指针，保留绑定、引用值、空范围、单元素范围、别名、NaN、正负零、参数求值及临时对象清理。成员表达式或返回的操作对象、类型化操作、表达式操作、间接或转换调用、改写已使用 SDK 声明，以及 volatile、枚举、long double、扩展整数、嵌套包装器和用户记录引用仍不支持。

直接调用 `std::remove_if`，配合直接初始化的透明 `std::logical_not<>`，支持算术引用的可写 `std::reference_wrapper<T>` 原始指针区间。移除引用值转换为假的包装器，按原有顺序压缩保留的绑定，并返回新的结束指针。引用对象的值保持不变，包括常量引用、NaN 和带符号零。验证完整 SDK 移除循环与委托查找循环、准确的隐式平凡包装器移动赋值、移动、谓词、转发和转换，保留空区间、别名、实参求值及临时对象清理。常量包装器元素、volatile 或枚举引用、long double、扩展整数、嵌套包装器及成员或返回的输入操作对象仍不支持。

直接调用 `std::for_each_n`，配合直接初始化的透明 `std::bit_not<>`，支持 `std::reference_wrapper<T>` 原始指针区间和最多 64 位整数计数，包括 bool 及进行整数提升的小整数。支持常量包装器元素与常量算术引用。正计数调用谓词并前进指针，零和负计数返回原指针。绑定及引用值保持不变。验证完整 SDK 计数转换、循环、谓词、转发及引用转换，保留指针结果、实参求值与临时对象清理。枚举、浮点或扩展整数计数，volatile 或枚举引用，long double、嵌套包装器及成员或返回的输入运算表达式仍不支持。 引用类型限定为 bool 与最多 64 位整数，不支持浮点引用。按位取反保留整数提升，运算结果被丢弃。

直接 `std::for_each` 和 `std::for_each_n` 还支持透明 `std::negate<>`、整数 `std::bit_not<>` 和 `std::logical_not<>` 的可变或 const 变量及引用参数。实参必须直接引用变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。引用值和计数类型继续遵循各自规则。for_each 返回的对象可继续用于受支持的标量调用；for_each_n 在正计数时前进，零或负计数返回原指针。绑定、引用值、实参求值与清理保持 SDK 行为。

对这些已检查的包装器 `std::for_each`、`std::for_each_n` 以及六种 `std::logical_not<>` 搜索、计数和布尔算法的已求值结果进行类型查询时，会保留调用方的精确依赖。经验证的调用提供其私有 SDK 签名和函数体。可查询返回的函数对象、原始迭代器、指针差计数和布尔结果类型。显式模板参数、输入和计数表达式以及选中的源代码回调仍须通过原有检查；一个调用获准并不允许对同一声明的其他引用。查询不会增加参数求值或清理。

已检查的包装器一元和二元 `std::transform`、`std::accumulate`、`std::reduce`、`std::inner_product`、一元和二元 `std::transform_reduce`、`std::adjacent_difference`、带初值的 `std::inclusive_scan`、`std::exclusive_scan`、带初值的 `std::transform_inclusive_scan` 和 `std::transform_exclusive_scan` 的已求值结果也支持类型查询。每个显式操作参数都必须是已检查的 SDK 函数对象，且该精确调用须先通过现有的完整算法验证。调用方的原始类型、模板参数、表达式、构造和选中的源代码操作符仍须接受检查。查询保留结果类型，不增加运行时调用、操作数求值或清理。

使用获准 SDK 谓词的已检查包装器 `std::copy_if`、`std::remove_copy_if`、`std::replace_copy_if`、`std::remove_if`、`std::is_partitioned`、`std::partition_point` 和 `std::partition_copy` 也支持对已求值结果进行类型查询，包括 partition_copy 返回的原始迭代器对。执行获准的 `std::replace_if` 调用后，其已检查的精确调用形式可提供 `void` 类型查询。仍须通过完整算法验证及原始参数、模板、构造和源代码操作符检查。查询不增加复制、绑定替换、参数求值或清理。

获准的包装器 `std::adjacent_find`、`std::unique` 和 `std::unique_copy` 还支持对默认重载及已检查透明 SDK 谓词调用的已求值结果进行类型查询，包括受支持的谓词变量和引用参数。查询保留精确的可变或 const 包装器指针结果类型。仍须通过完整算法验证及原始参数、模板、构造和源代码操作符检查。查询不增加参数求值、复制、绑定替换或清理。

获准的包装器 `std::equal` 和 `std::mismatch` 还支持对默认重载及已检查透明 SDK 谓词调用的已求值结果进行类型查询，包括受支持的谓词变量和引用参数。查询保留 bool 结果与原始迭代器对中精确的指针类型，包括输入各自的 const 限定。仍须通过完整算法验证及原始参数、模板、构造和源代码操作符检查。查询不增加参数求值、复制、绑定替换或清理。

获准的包装器 `std::find_first_of`、`std::search`、`std::find_end` 和 `std::search_n` 还支持对默认重载及已检查透明 SDK 谓词调用的已求值结果进行类型查询，包括受支持的谓词变量和引用参数。查询保留精确的可变或 const 包装器指针结果类型。仍须通过完整算法验证及原始参数、模板、构造和源代码操作符检查。查询不增加参数求值、复制、绑定替换或清理。

内嵌 r15 SDK 和生成的 `std::search_n` 代码会先检查计数，再计算搜索偏移。零和负计数返回输入起点；超过区间长度的计数直接返回终点，不调用谓词，包括超出 `ptrdiff_t` 的无符号值。计数比较保留整数提升和位宽，实参求值、清理、包装器绑定及原始迭代器类型保持原有语义。

Windows MSVC x64 和 ARM64 的包装器记录使用与目标位宽一致的整数类型表示基类填充。构造、复制、重新绑定和 `get()` 保留 SDK 布局及实时引用对象，生成对象验证检查同一布局。

包装器 `std::search_n` 还接受非作用域枚举计数，前提是经 SDK 提升后的底层整数类型不超过 64 位。所选 `__convert_to_integral`、枚举与底层类型特征、别名及已使用的重声明必须来自固定 SDK；用户改写仍会拒绝。非正数和超长计数、可变或 const 输入、结果类型查询、单次求值及临时对象清理沿用已有计数规则。

直接调用 `std::for_each_n`，配合直接初始化的透明 `std::negate<>`，支持 `std::reference_wrapper<T>` 原始指针区间和最多 64 位整数计数，包括 bool 及进行整数提升的小整数。支持常量包装器元素与常量算术引用。正计数调用谓词并前进指针，零和负计数返回原指针。绑定及引用值保持不变。验证完整 SDK 计数转换、循环、谓词、转发及引用转换，保留指针结果、实参求值与临时对象清理。枚举、浮点或扩展整数计数，volatile 或枚举引用，long double、嵌套包装器及成员或返回的输入运算表达式仍不支持。 一元负号保留整数提升与浮点行为，运算结果被丢弃。

直接调用 `std::for_each`，配合直接初始化的透明 `std::bit_not<>`，支持 bool 与最多 64 位整数的 `std::reference_wrapper<T>` 原始指针区间，包括常量包装器元素和常量引用。按位取反保留整数提升，绑定及引用值保持不变。返回的空操作对象可丢弃、存储或返回，并保留按位调用行为。验证完整 SDK 循环、结果对象的准确平凡移动、操作、转发与转换，保留空区间、实参求值及临时对象清理。浮点、volatile、枚举或扩展整数引用，嵌套包装器及成员或返回的输入运算表达式仍不支持。

直接调用 `std::for_each`，配合直接初始化的透明 `std::negate<>`，支持 `std::reference_wrapper<T>` 原始指针区间，包括常量包装器元素和常量算术引用。一元负号保留整数提升与浮点行为，绑定及引用值保持不变。返回的空操作对象可丢弃、存储或返回，并保留算术调用行为。验证完整 SDK 循环、结果对象的准确平凡移动、谓词、转发与转换，保留空区间、实参求值及临时对象清理。volatile 或枚举引用、long double、扩展整数、嵌套包装器及成员或返回的输入运算表达式仍不支持。

直接调用 `std::for_each_n`，配合直接初始化的透明 `std::logical_not<>`，支持 `std::reference_wrapper<T>` 原始指针区间和最多 64 位整数计数，包括 bool 及进行整数提升的小整数。支持常量包装器元素与常量算术引用。正计数调用谓词并前进指针，零和负计数返回原指针。绑定及引用值保持不变。验证完整 SDK 计数转换、循环、谓词、转发及引用转换，保留指针结果、实参求值与临时对象清理。枚举、浮点或扩展整数计数，volatile 或枚举引用，long double、嵌套包装器及成员或返回的输入运算表达式仍不支持。

直接调用 `std::for_each`，配合直接初始化的透明 `std::logical_not<>`，支持 `std::reference_wrapper<T>` 原始指针区间，包括常量包装器元素和常量算术引用。每次谓词调用读取当前引用值，绑定及引用值保持不变。返回的空函数对象可丢弃、存储或返回。验证完整 SDK 循环、结果操作对象的准确平凡移动、谓词、转发与转换，保留空区间、实参求值及临时对象清理。volatile 或枚举引用、long double、扩展整数、嵌套包装器以及成员或返回的输入运算表达式仍不支持。

直接调用 `std::replace_if`，配合直接初始化的透明 `std::logical_not<>`，支持可写的 `std::reference_wrapper<T>` 原始指针区间及同一特化的替换包装器。布尔转换为假的算术引用选择替换绑定，其余绑定和所有引用值保持不变。支持常量引用与常量替换包装器。验证完整 SDK 循环、准确的隐式平凡包装器赋值、谓词、转换及递增，保留空区间、void 结果、实参求值及临时对象清理。不同包装器特化、volatile 或枚举引用、long double、扩展整数及嵌套包装器仍不支持。

直接调用 `std::replace_copy_if`，配合直接初始化的透明 `std::logical_not<>`，还支持以 `std::reference_wrapper<U>` 作为标量输出的替换值。输入和替换引用可使用不同的受支持算术类型，各分支转换为输出类型。验证完整 SDK 循环及两处包装器转换。每次选择替换时，都通过已捕获的包装器地址读取当前引用值，保留实时值、输出别名、常量引用、空区间、输出末尾、实参求值与清理。volatile 或枚举引用、long double、扩展整数及嵌套包装器仍不支持。

直接调用 `std::replace_copy_if`，配合直接初始化的透明 `std::logical_not<>`，还支持输出与替换值使用和输入相同的 `std::reference_wrapper<T>` 特化。布尔转换为假的算术引用选择当前替换绑定，其余元素保留输入绑定。验证两条 SDK 隐式平凡复制赋值及完整循环，保留常量输入与引用、实时替换绑定、空区间、输出末尾、实参求值及清理。不同包装器特化之间的转换仍不支持。

直接调用 `std::partition_copy`，配合直接初始化的透明 `std::logical_not<>`，还支持一个标量输出和一个与输入相同 `std::reference_wrapper<T>` 特化的输出，顺序可互换。标量分支复制转换后的引用值，包装器分支复制引用绑定；验证两条 SDK 赋值与转换、所有迭代器递增及两个 pair 字段，保留实时引用、标量输出别名、常量输入、空区间、结果使用、实参求值及临时对象清理。不同包装器特化之间的转换仍不支持。

直接调用 `std::partition_copy`，配合直接初始化的透明 `std::logical_not<>`，还支持两个与输入使用相同 `std::reference_wrapper<T>` 特化的输出区间。复制引用绑定，保留各输出的顺序及两个返回末尾指针，支持常量输入包装器和常量算术引用。验证 SDK 循环、两条隐式平凡包装器复制赋值、逻辑非、转发、引用转换及 pair 布局与构造；保留实时引用、空区间、结果使用、实参求值和临时对象清理。混合标量与包装器输出及不同包装器特化之间的转换仍不在此路径内。

直接调用 `std::partition_copy`，配合直接初始化的透明 `std::logical_not<>`，可将有限 `std::reference_wrapper<T>` 指针区间中布尔转换为假的算术引用复制到第一个标量输出，其余值复制到第二个输出，各自保留原顺序。两个输出可使用不同的受支持标量类型，返回的 `std::pair` 包含两个输出末尾指针。保留空区间、常量输入、布尔与输出转换、实时引用、输出别名、实参求值、结果使用及临时对象清理；验证 SDK 入口、完整双分支循环、两条赋值、所有迭代器递增、pair 布局与构造、逻辑非、转发与引用转换。包装器输出、成员或返回的操作对象及源代码替换仍不在此路径内。

直接调用 `std::replace_copy_if`，配合直接初始化的透明 `std::logical_not<>`，可将有限 `std::reference_wrapper<T>` 指针区间中布尔转换为假的算术引用替换为标量替换值，其余值按原顺序复制到标量输出区间。返回输出末尾指针，保留空区间、常量输入、布尔与输出转换、实时引用及替换值引用、输出别名、实参求值、结果使用及临时对象清理；验证 SDK 入口、完整双分支循环、两条赋值与递增、输出返回、逻辑非、转发与引用转换。包装器输出或替换值、成员或返回的操作对象及源代码替换仍不在此路径内。

直接调用 `std::remove_copy_if`，配合直接初始化的透明 `std::logical_not<>`，也支持指向同一算术 `std::reference_wrapper<T>` 特化的输出指针。选中元素按原顺序将输出包装器重新绑定到原引用对象，保留引用对象身份和值；空输入保留输出绑定不变。保留常量输入包装器与引用对象、实时绑定、实参求值、结果使用及临时对象清理；验证完整 SDK 筛选循环、谓词取反、输出返回及精确的隐式平凡包装器复制赋值。需要转换的包装器输出、成员或返回的操作对象及源代码替换仍被拒绝。

直接调用 `std::copy_if`，配合直接初始化的透明 `std::logical_not<>`，也支持指向同一算术 `std::reference_wrapper<T>` 特化的输出指针。选中元素按原顺序将输出包装器重新绑定到原引用对象，保留引用对象身份和值；空输入保留输出绑定不变。保留常量输入包装器与引用对象、实时绑定、实参求值、结果使用及临时对象清理；验证完整 SDK 辅助函数、恒等投影与 invoke 分派、pair 返回及精确的隐式平凡包装器复制赋值。需要转换的包装器输出、成员或返回的操作对象及源代码替换仍被拒绝。

直接调用 `std::remove_copy_if`，配合直接初始化的透明 `std::logical_not<>`，可将有限 `std::reference_wrapper<T>` 指针区间中布尔转换为真的算术引用值按原顺序复制到标量输出区间。返回输出末尾指针，空输入保留输出不变。保留实时绑定、常量输入、布尔与输出转换、输出别名、实参求值、结果使用及临时对象清理；验证 SDK 入口、完整筛选循环、谓词取反、赋值、递增、输出返回、转发与引用转换。包装器输出、成员或返回的操作对象及源代码替换仍不在此路径内。

直接调用 `std::copy_if`，配合直接初始化的透明 `std::logical_not<>`，可将有限 `std::reference_wrapper<T>` 指针区间中布尔转换为假的算术引用值按原顺序复制到标量输出区间。返回输出末尾指针，空输入保留输出不变。保留实时绑定、常量输入、布尔与输出转换、输出别名、实参求值、结果使用及临时对象清理；验证 SDK 入口、完整辅助循环、两次 invoke 调用、恒等投影、引用转换、转发和 pair 返回。包装器输出、成员或返回的操作对象及源代码替换仍不在此路径内。

直接调用 `std::none_of`，配合直接初始化的透明 `std::logical_not<>`，可在有限 `std::reference_wrapper<T>` 指针区间中遇到首个布尔转换为假的算术引用后返回假；空区间和没有零值引用的区间返回真。保留实时绑定、常量包装器与引用对象、布尔转换、实参求值、结果使用及临时对象清理；验证 SDK 入口、完整循环、布尔返回、逻辑非、转发与引用转换。成员、有类型、返回或带副作用的运算表达式及源代码替换仍不在此路径内。

直接 `std::find_if`、`std::find_if_not`、`std::count_if`、`std::all_of`、`std::any_of` 和 `std::none_of` 还支持透明 `std::logical_not<>` 的可变或 const 变量及引用参数。实参必须直接引用变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。受支持引用值、真值转换、实时绑定、提前退出、计数、空输入结果、实参求值与清理保持 SDK 行为。

直接调用 `std::any_of`，配合直接初始化的透明 `std::logical_not<>`，可在有限 `std::reference_wrapper<T>` 指针区间中找到首个布尔转换为假的算术引用后返回真；空区间和没有零值引用的区间返回假。保留实时绑定、常量包装器与引用对象、布尔转换、实参求值、结果使用及临时对象清理；验证 SDK 入口、恒等投影、调用链、逻辑非、转发与引用转换。成员、有类型、返回或带副作用的运算表达式及源代码替换仍不在此路径内。

直接调用 `std::all_of`，配合直接初始化的透明 `std::logical_not<>`，可检查有限 `std::reference_wrapper<T>` 指针区间中的算术引用是否全部转换为假；首个非零引用即结束检查，空区间返回真。支持实时绑定、常量包装器与引用对象、空区间、布尔转换、实参求值和结果使用；验证 SDK 入口、恒等投影、调用链、逻辑非、转发与引用转换。成员、有类型、返回或带副作用的运算表达式及源代码替换仍不在此路径内。

直接调用 `std::find_if_not`，配合直接初始化的透明 `std::logical_not<>`，可在有限 `std::reference_wrapper<T>` 指针区间中返回首个转换为真的算术引用的迭代器，无匹配则返回区间末尾；匹配后立即停止。支持实时绑定、常量包装器与引用对象、空区间、布尔转换、实参求值和结果使用；验证 SDK 入口、完整搜索循环、逻辑非、转发与引用转换。成员、有类型、返回或带副作用的运算表达式及源代码替换仍不在此路径内。

直接调用 `std::find_if`，配合直接初始化的透明 `std::logical_not<>`，可在有限 `std::reference_wrapper<T>` 指针区间中返回首个转换为假的算术引用的迭代器，无匹配则返回区间末尾；匹配后立即停止。支持实时绑定、常量包装器与引用对象、空区间、布尔转换、实参求值和结果使用；验证 SDK 入口、完整搜索循环、逻辑非、转发与引用转换。成员、有类型、返回或带副作用的运算表达式及源代码替换仍不在此路径内。

直接调用 `std::count_if`，配合直接初始化的透明 `std::logical_not<>`，可统计有限 `std::reference_wrapper<T>` 指针区间中转换为假的算术引用。支持实时绑定、常量包装器与引用对象、空区间、布尔转换、实参求值和结果使用；验证 SDK 入口、恒等投影、调用链、逻辑非、转发与引用转换。成员、有类型、返回或带副作用的运算表达式及源代码替换仍不在此路径内。

直接二元 `std::transform` 现在接受两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::greater_equal<>`。有限路径验证 SDK 入口、大于等于比较、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换进行大于等于比较，再把布尔结果转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 还支持这里列出的全部十六种透明算术、比较和逻辑运算对象的可变或 const 变量及引用参数。实参必须直接引用变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。两侧引用值类型、含 bool 的独立输出转换、有效算术、比较精度和逻辑短路继续遵循各自规则。实时重叠写入、绑定、末尾指针以及空输入的参数求值和清理均保持 SDK 顺序。

直接二元 `std::transform` 现在接受两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::less_equal<>`。有限路径验证 SDK 入口、小于等于比较、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换进行小于等于比较，再把布尔结果转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::greater<>`。有限路径验证 SDK 入口、大于比较、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换进行大于比较，再把布尔结果转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::less<>`。有限路径验证 SDK 入口、小于比较、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换进行小于比较，再把布尔结果转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::not_equal_to<>`。有限路径验证 SDK 入口、不等比较、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换比较不等性，再把布尔结果转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::equal_to<>`。有限路径验证 SDK 入口、相等比较、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换比较相等性，再把布尔结果转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在支持直接初始化的透明 `std::logical_or<>`、两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间，以及含布尔类型的可写标量输出。有限路径验证 SDK 入口、逻辑运算、转发、两侧引用转换和完整循环。每轮先把左侧当前值转换为布尔值，仅在左侧为假时读取右侧，再把布尔结果转换到输出类型并推进三个指针。后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在支持直接初始化的透明 `std::logical_and<>`、两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间，以及含布尔类型的可写标量输出。有限路径验证 SDK 入口、逻辑运算、转发、两侧引用转换和完整循环。每轮先把左侧当前值转换为布尔值，仅在左侧为真时读取右侧，再把布尔结果转换到输出类型并推进三个指针。后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持整数和布尔引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::bit_xor<>`。有限路径验证 SDK 入口、按位异或、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换计算结果，再转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持整数和布尔引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::bit_or<>`。有限路径验证 SDK 入口、按位或、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换计算结果，再转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持整数和布尔引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::bit_and<>`。有限路径验证 SDK 入口、按位与、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换计算结果，再转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持整数和布尔引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::modulus<>`。有限路径验证 SDK 入口、取余、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换计算余数，再转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::divides<>`。有限路径验证 SDK 入口、除法、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换计算商，再转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::minus<>`。有限路径验证 SDK 入口、减法、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换计算差，再转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::plus<>`。有限路径验证 SDK 入口、加法、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换计算和，再转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接二元 `std::transform` 现在接受两个受支持算术引用类型（可混合类型）的 `std::reference_wrapper<T>` 原始指针区间、含布尔类型的可写标量输出，以及直接初始化的透明 `std::multiplies<>`。有限路径验证 SDK 入口、乘法、转发、两侧引用转换和完整循环。每轮读取两侧引用的当前值，按通常算术转换计算乘积，再转换到输出元素类型并推进三个指针；后续读取会观察到重叠输出的写入。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接一元 `std::transform` 现在接受受支持算术引用类型的 `std::reference_wrapper<T>` 原始指针区间、可写标量（含布尔）输出区间，以及直接初始化的透明 `std::logical_not<>`。有限路径验证 SDK 入口、logical_not、转发、引用转换和完整循环。每轮读取引用的当前值，执行布尔真值转换与逻辑非，再转换为输出元素类型并推进；输出重叠时保留这一顺序。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接一元 `std::transform` 现在接受受支持整数引用类型的 `std::reference_wrapper<T>` 原始指针区间、可写标量输出区间，以及直接初始化的透明 `std::bit_not<>`。有限路径验证 SDK 入口、bit_not、转发、引用转换和完整循环。每轮读取引用的当前值，执行整数提升与按位取反，再转换为输出元素类型并推进；输出重叠时保留这一顺序。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接初始化透明 `std::negate<>` 或 `std::bit_not<>` 的一元 `std::transform` 也支持已认证包装器指针输入范围和可写、无限定的 `bool*` 输出。先计算完整、按通常规则提升的一元结果，再在 SDK 写入位置转换为 bool。取负仍支持算术引用值，按位取反仍要求受支持整数；全为一的 unsigned 64 位值取反后输出 false。const 访问、原位或移位 bool 引用值别名、绑定、末尾、空输入参数和清理保留 SDK 顺序。源码替换、带类型／成员／返回运算表达式、volatile 或不支持的输出及引用值仍被拒绝。

直接一元 `std::transform` 现在接受受支持算术引用类型的 `std::reference_wrapper<T>` 原始指针区间、可写标量输出区间，以及直接初始化的透明 `std::negate<>`。有限路径验证 SDK 入口、negate、转发、引用转换和完整循环。每轮读取引用的当前值，执行一元提升与取负，再转换为输出元素类型并推进；输出重叠时保留这一顺序。空区间返回原输出指针，实参副作用和清理保持完整。成员、返回、逗号及有类型的运算表达式、间接调用和源代码替换仍不属于此路径。

直接一元 `std::transform` 还支持透明 `std::negate<>`、整数 `std::bit_not<>` 和 `std::logical_not<>` 的可变或 const 变量及引用参数。实参必须直接引用变量，并选中经过认证的同类型空 SDK 对象的隐式、平凡且不抛异常的复制。引用值与输出类型继续遵循各自规则，包括 bool 输出。提升后的一元结果、实时重叠写入、绑定、末尾指针以及空输入的参数求值和清理均保持 SDK 顺序。

一元 `std::transform_reduce` 支持经校验 SDK `std::reference_wrapper<T>` 的原始指针区间，referent 可为受支持的内置标量，输入和 referent 均可为 const。五参数形式支持直接初始化的透明 `std::plus<>` 和 `std::negate<>`，或满足上述复制认证规则的直接变量引用；累加器可为不超过 64 位的受支持整数、float 或 double。经校验的 C++17 SDK 循环按通常的一元提升规则对当前 referent 取负，与移动后的累加器相加，并在每一步将和转换回累加器类型；空区间返回初值。保留绑定、referent、操作数单次求值和清理语义，并校验两个运算对象的初始化、转发、转换及完整 SDK 循环。仍拒绝自定义、带类型、成员、返回或带副作用的运算表达式及源码替换。

显式传入透明 plus 和 multiplies 的 `std::transform_reduce` 支持两个经校验 SDK `std::reference_wrapper<T>` 的原始指针范围；引用对象为内建标量，可含 const 输入、const 引用对象和不同引用类型，累加器须为支持的算术类型。它依次读取当前引用值，对每次乘积和求和执行通常算术转换，并将每次求和结果转换回累加器类型；第一范围为空时返回初始值。保留包装器绑定、引用对象、操作数的一次求值和清理。所选 SDK 模板、完整循环及两侧包装器转换均经过校验。仍拒绝源码替换、自定义运算、枚举或嵌套包装器引用对象、不支持的累加器、标量与包装器混合范围，以及间接或强转后的被调用函数。 默认四参数 SDK 委托、透明 plus 与 multiplies 对象、累加器移动及转发也均经过校验。 所选 C++17 SDK 循环移动累加器，两个初始化、运算及转发均经过校验；仍拒绝带类型、成员、返回或带副作用的运算表达式。

显式传入透明 plus 和 multiplies 的 `std::inner_product` 支持两个经校验 SDK `std::reference_wrapper<T>` 的原始指针范围；引用对象为内建标量，可含 const 输入、const 引用对象和不同引用类型，累加器须为支持的算术类型。它依次读取当前引用值，对每次乘积和求和执行通常算术转换，并将每次求和结果转换回累加器类型；第一范围为空时返回初始值。保留包装器绑定、引用对象、操作数的一次求值和清理。所选 SDK 模板、完整循环及两侧包装器转换均经过校验。仍拒绝源码替换、自定义运算、枚举或嵌套包装器引用对象、不支持的累加器、标量与包装器混合范围，以及间接或强转后的被调用函数。 所选 C++17 SDK 循环以左值使用累加器，两个初始化、运算及转发均经过校验；仍拒绝带类型、成员、返回或带副作用的运算表达式。

显式传入透明 minus 的 `std::adjacent_difference` 还支持已认证 `std::reference_wrapper<T>` 的原始指针范围及可写的算术输出（包括 bool），输入和算术引用对象均可为 const。它复制首个引用绑定并写出当前引用值，随后读取新绑定和已保存绑定的当前引用值作差，检查算术及输出转换。输出覆盖引用对象时，已保存的绑定会观察到写入结果。空输入返回原输出指针，其余返回推进后的指针，并保留参数求值和清理。SDK 迭代器值类型、包装复制、转换、移动和完整循环均经过验证；源代码替换、自定义运算以及不支持的类型或调用形式仍会被拒绝。 四参数形式支持直接初始化的透明 `std::minus<>`，也支持满足上述复制认证规则的同类型可变／const 变量，包括引用参数。所选 SDK 初始化、减法及两个包装器操作数的转发均经过校验；仍拒绝带类型、成员、返回或带副作用的运算表达式。

显式传入透明 plus 的 `std::accumulate` 支持经校验 SDK `std::reference_wrapper<T>` 的原始指针范围；引用对象为内建标量，可含 const 输入和 const 引用对象，累加器须为支持的算术类型。它按顺序读取当前引用值，执行通常算术转换，并将每次求和结果转换回累加器类型；空范围返回初始值。包装器绑定和引用对象保持不变，操作数只求值一次，清理行为得到保留。所选 SDK 模板、完整循环和包装器转换均经过校验。仍拒绝源码替换、自定义运算、枚举或嵌套包装器引用对象、不支持的累加器类型，以及间接或强转后的被调用函数。 四参数形式接受直接初始化的透明 `std::plus<>`，或对该 SDK 类型的可变或 const 变量的直接引用。所选 C++17 SDK 运算以左值使用累加器，构造、运算及转发均经过校验；仍拒绝带类型或动态生成的运算对象。

显式传入透明 plus 的 `std::reduce` 支持经校验 SDK `std::reference_wrapper<T>` 的原始指针范围；引用对象为内建标量，可含 const 输入和 const 引用对象，累加器须为支持的算术类型。它按顺序读取当前引用值，执行通常算术转换，并将每次求和结果转换回累加器类型；空范围返回初始值。包装器绑定和引用对象保持不变，操作数只求值一次，清理行为得到保留。所选 SDK 模板、完整循环和包装器转换均经过校验。仍拒绝源码替换、自定义运算、枚举或嵌套包装器引用对象、不支持的累加器类型，以及间接或强转后的被调用函数。 四参数形式接受直接初始化的透明 `std::plus<>`，或对该 SDK 类型的可变或 const 变量的直接引用。所选构造及运算、累加器移动和转发均经过校验；仍拒绝带类型或动态生成的运算对象。

显式传入透明 plus 的 `std::exclusive_scan` 支持经校验 SDK `std::reference_wrapper<T>` 的原始指针输入范围，引用对象为内建标量，可含 const 输入和引用对象；输出范围可写，累加器须为支持的算术类型，输出也支持 bool。五参数形式接受直接初始化的透明 `std::plus<>`，或对该 SDK 类型的可变或 const 变量的直接引用。它在每次输出写入前读取当前引用值，执行通常算术转换，将每次新的求和结果转回累加器类型，再单独转换并写入更新前的累加器值。空输入不写入并返回原输出指针，否则返回输出末端。输出可与引用对象重叠，保留包装器绑定、操作数的一次求值和清理。所选 SDK 循环、plus 构造及运算、转发和包装器转换均经过校验。仍拒绝源码替换、自定义或带类型的运算、动态生成的运算对象、枚举或嵌套包装器引用对象、不支持的标量类型，以及间接或强转后的被调用函数。 所选 SDK 标量移动也经过校验。

默认 `std::transform_reduce` 支持两个经校验 SDK `std::reference_wrapper<T>` 的原始指针范围；引用对象为内建标量，可含 const 输入、const 引用对象和不同引用类型，累加器须为支持的算术类型。它依次读取当前引用值，对每次乘积和求和执行通常算术转换，并将每次求和结果转换回累加器类型；第一范围为空时返回初始值。保留包装器绑定、引用对象、操作数的一次求值和清理。所选 SDK 模板、完整循环及两侧包装器转换均经过校验。仍拒绝源码替换、自定义运算、枚举或嵌套包装器引用对象、不支持的累加器、标量与包装器混合范围，以及间接或强转后的被调用函数。 默认四参数 SDK 委托、透明 plus 与 multiplies 对象、累加器移动及转发也均经过校验。

带初值的默认 `std::reduce` 支持经校验 SDK `std::reference_wrapper<T>` 的原始指针范围；引用对象为内建标量，可含 const 输入和 const 引用对象，累加器须为支持的算术类型。它按顺序读取当前引用值，执行通常算术转换，并将每次求和结果转换回累加器类型；空范围返回初始值。包装器绑定和引用对象保持不变，操作数只求值一次，清理行为得到保留。所选 SDK 模板、完整循环和包装器转换均经过校验。仍拒绝源码替换、自定义运算、枚举或嵌套包装器引用对象、不支持的累加器类型，以及间接或强转后的被调用函数。

默认 `std::adjacent_difference` 还支持已认证 `std::reference_wrapper<T>` 的原始指针范围及可写的算术输出（包括 bool），输入和算术引用对象均可为 const。它复制首个引用绑定并写出当前引用值，随后读取新绑定和已保存绑定的当前引用值作差，检查算术及输出转换。输出覆盖引用对象时，已保存的绑定会观察到写入结果。空输入返回原输出指针，其余返回推进后的指针，并保留参数求值和清理。SDK 迭代器值类型、包装复制、转换、移动和完整循环均经过验证；源代码替换、自定义运算以及不支持的类型或调用形式仍会被拒绝。

默认 `std::exclusive_scan` 支持经校验 SDK `std::reference_wrapper<T>` 的原始指针输入范围，引用对象为内建标量，可含 const 输入和 const 引用对象；输出须为可写算术指针范围，累加器须为支持的算术类型。它先读取本轮当前引用值，再写出旧累加值，并执行通常算术转换及每次求和到累加器类型的转换。空输入返回原输出指针，其余返回推进后的指针。保留包装器绑定、操作数求值和清理，包括输出与引用对象重叠的情况。SDK 委托、透明加法、转发、标量移动和完整循环均经过校验。仍拒绝源码替换、自定义运算、枚举或嵌套包装器引用对象、不支持的累加器或输出类型，以及间接或强转后的被调用函数。

默认 `std::inner_product` 支持两个经校验 SDK `std::reference_wrapper<T>` 的原始指针范围；引用对象为内建标量，可含 const 输入、const 引用对象和不同引用类型，累加器须为支持的算术类型。它依次读取当前引用值，对每次乘积和求和执行通常算术转换，并将每次求和结果转换回累加器类型；第一范围为空时返回初始值。保留包装器绑定、引用对象、操作数的一次求值和清理。所选 SDK 模板、完整循环及两侧包装器转换均经过校验。仍拒绝源码替换、自定义运算、枚举或嵌套包装器引用对象、不支持的累加器、标量与包装器混合范围，以及间接或强转后的被调用函数。

默认 `std::accumulate` 支持经校验 SDK `std::reference_wrapper<T>` 的原始指针范围；引用对象为内建标量，可含 const 输入和 const 引用对象，累加器须为支持的算术类型。它按顺序读取当前引用值，执行通常算术转换，并将每次求和结果转换回累加器类型；空范围返回初始值。包装器绑定和引用对象保持不变，操作数只求值一次，清理行为得到保留。所选 SDK 模板、完整循环和包装器转换均经过校验。仍拒绝源码替换、自定义运算、枚举或嵌套包装器引用对象、不支持的累加器类型，以及间接或强转后的被调用函数。

直接使用默认比较的 `std::is_heap` 还支持指向同一受检 SDK `std::reference_wrapper<T>` 的原生指针区间，被引用对象须为可比较的内建标量，可包含 const 输入和 const 被引用对象。它比较当前引用值，返回该区间是否满足堆顺序，保持包装器绑定及被引用对象不变。空区间和单元素区间返回 true。操作数只求值一次，清理行为保持不变。所选 SDK 的委托、默认比较、迭代器差值类型及完整父子扫描均经过验证。源代码替换、自定义谓词、枚举或嵌套包装器的被引用对象，以及间接或经强制转换的调用仍被拒绝。

直接使用默认比较的 `std::is_heap_until` 还支持指向同一受检 SDK `std::reference_wrapper<T>` 的原生指针区间，被引用对象须为可比较的内建标量，可包含 const 输入和 const 被引用对象。它比较当前引用值，返回首个违反堆顺序的子元素原始指针或结束指针，保持包装器绑定及被引用对象不变。空区间和单元素区间返回结束指针。操作数只求值一次，清理行为保持不变。所选 SDK 的委托、默认比较、迭代器差值类型及完整父子扫描均经过验证。源代码替换、自定义谓词、枚举或嵌套包装器的被引用对象，以及间接或经强制转换的调用仍被拒绝。

直接使用默认比较的 `std::minmax` 还支持由同一受检 SDK `std::reference_wrapper<T>` 构成的非空初始化列表，被引用对象须为可比较的内建标量，可包含 const 输入和 const 被引用对象。它比较当前值并返回保持所选引用绑定的包装器副本对；最小值等价时选择首个元素，最大值等价时选择最后一个元素。操作数只求值一次，清理行为保持不变。所选 SDK 的列表访问、默认比较、投影、成对比较循环和包装器副本的 pair 构造均经过验证。源代码替换、自定义谓词、枚举或嵌套包装器的被引用对象，以及间接或经强制转换的调用仍被拒绝。

直接使用默认比较的 `std::max` 还支持由同一受检 SDK `std::reference_wrapper<T>` 构成的非空初始化列表，被引用对象须为可比较的内建标量，可包含 const 输入和 const 被引用对象。它比较当前值并返回保持所选引用绑定的包装器副本；最大值等价时选择首个元素。操作数只求值一次，清理行为保持不变。所选 SDK 的列表访问、`max_element` 委托、默认比较和包装器复制均经过验证。源代码替换、自定义谓词、枚举或嵌套包装器的被引用对象，以及间接或经强制转换的调用仍被拒绝。

直接使用默认比较的 `std::min` 还支持由同一受检 SDK `std::reference_wrapper<T>` 构成的非空初始化列表，被引用对象须为可比较的内建标量，可包含 const 输入和 const 被引用对象。它比较当前值并返回保持所选引用绑定的包装器副本；最小值等价时选择首个元素。操作数只求值一次，清理行为保持不变。所选 SDK 的列表访问、`min_element` 委托、默认比较和包装器复制均经过验证。源代码替换、自定义谓词、枚举或嵌套包装器的被引用对象，以及间接或经强制转换的调用仍被拒绝。

双值默认 `std::minmax` 支持同一种 `std::reference_wrapper<T>` 的两个对象，被引用内置类型须可比较，并包括常量输入和被引用对象。它比较当前值，返回指向原始最小值与最大值包装对象的一对常量引用；等价时保留参数顺序。绑定、被引用对象及对象身份不变。每个参数求值一次并保留清理。SDK 委托、默认比较器和引用对构造均经过验证。自定义谓词、枚举或嵌套引用，以及替换 SDK 声明仍会被拒绝。

默认三参数 `std::clamp` 支持同一种 `std::reference_wrapper<T>` 的三个对象，包括常量输入和被引用对象，并要求边界有效。它比较被引用内置类型的当前值，返回原始值、下界或上界包装对象的常量引用；与边界相等时保留值包装对象。绑定、被引用对象和对象身份不变；每个参数求值一次并保留清理。所选 SDK 委托、默认比较器、比较顺序与返回引用均经过验证。自定义谓词、枚举或嵌套引用，以及替换 SDK 声明仍会被拒绝。

双值默认 `std::max` 支持同一种 `std::reference_wrapper<T>` 的两个对象，包括常量输入和被引用对象。它比较被引用内置类型的当前值，返回所选原始包装对象的常量引用；相等时选择第一个参数，保留绑定、被引用对象和对象身份。两个参数各求值一次并保留清理。所选 SDK 委托、默认比较器、被引用对象转换与返回引用均经过验证。自定义谓词、枚举或嵌套引用，以及替换 SDK 声明仍会被拒绝。

双值默认 `std::min` 支持同一种 `std::reference_wrapper<T>` 的两个对象，包括常量输入和被引用对象。它比较被引用内置类型的当前值，返回所选原始包装对象的常量引用；相等时选择第一个参数，保留绑定、被引用对象和对象身份。两个参数各求值一次并保留清理。所选 SDK 委托、默认比较器、被引用对象转换与返回引用均经过验证。自定义谓词、枚举或嵌套引用，以及替换 SDK 声明仍会被拒绝。

`std::search_n` 支持同一种 `std::reference_wrapper<T>` 的原始指针区间和查询值，允许常量输入、查询和被引用对象，次数采用内置整数类型。它比较被引用对象的当前内置类型值，寻找首个达到指定长度的连续匹配；次数为零时返回起点，无匹配时返回终点。别名、绑定、被引用对象、参数求值与清理均保留。所选 SDK 次数转换、随机访问资格、可调用与迭代器特征、相等与恒等投影调用、匹配循环和 `pair` 构造均经过验证。自定义谓词、枚举或嵌套引用，以及替换 SDK 声明仍会被拒绝。

`std::find_end` 支持同一种 `std::reference_wrapper<T>` 的两个原始指针区间，包括常量输入和被引用对象。它比较被引用内置类型的当前值，返回最后一个匹配子序列的位置；模式为空或无匹配时返回终点。别名、重叠区间、绑定、被引用对象、参数求值与清理均保留。所选 SDK 迭代器特征、相等与恒等投影调用、迭代器类别转换、匹配循环和 `pair` 构造均经过验证。自定义谓词、枚举或嵌套引用，以及替换 SDK 声明仍会被拒绝。

`std::search` 支持同一种 `std::reference_wrapper<T>` 的两个原始指针区间，包括常量输入和被引用对象。它比较被引用内置类型的当前值，返回首个匹配子序列的位置；模式为空时返回起点，无匹配时返回终点。别名、重叠区间、绑定、被引用对象、参数求值与清理均保留。所选 SDK 迭代器特征、相等与恒等投影调用、长度检查、匹配循环和 `pair` 构造均经过验证。自定义谓词、枚举或嵌套引用，以及替换 SDK 声明仍会被拒绝。

`std::equal_range` 支持原始指针区间与同一种 `std::reference_wrapper<T>` 查询值，包括常量输入、查询对象和被引用对象。它比较被引用内置类型的当前值，返回等价元素区间的迭代器对；空区间或无匹配时两个迭代器相等。区间必须依次排列小于查询值、等价和大于查询值的元素。绑定、被引用对象、参数求值与清理均保留。所选 SDK 搜索辅助函数、类型特征、投影和 `pair` 构造均经过验证。自定义比较、枚举或嵌套引用，以及替换 SDK 声明仍会被拒绝。

直接调用默认 `std::binary_search` 也支持同一已检查 SDK `std::reference_wrapper<T>` 的原始指针区间与查询值，包括 const 输入、查询值、引用对象及内建标量引用对象。区间须按引用对象当前值排序。它返回是否存在等价值；空区间返回 false。比较读取当前值并保持引用绑定与对象。三个参数均只求值一次，并保留接收对象清理。所选 SDK 委托、比较器引用别名、可调用性特征、恒等投影、指针距离、半长度和二分搜索均被验证。源码替换、自定义比较、枚举或嵌套包装器引用对象，以及间接或转换后的被调用函数仍被拒绝。

直接调用默认 `std::upper_bound` 也支持同一已检查 SDK `std::reference_wrapper<T>` 的原始指针区间与查询值，包括 const 输入、查询值、引用对象及内建标量引用对象。区间须按查询值是否小于元素值分区，不满足该比较的元素在前。它返回第一个大于查询值的元素或末尾；空区间返回起始指针。比较读取引用对象当前值，保持引用绑定与对象。三个参数均只求值一次并保留接收对象清理。所选 SDK 委托、可调用性和迭代器复制特征、恒等投影、指针距离、半长度及中点推进均被验证。源码替换、自定义比较、枚举或嵌套包装器引用对象，以及间接或转换后的被调用函数仍被拒绝。

直接调用默认 `std::lower_bound` 也支持同一已检查 SDK `std::reference_wrapper<T>` 的原始指针区间与查询值，包括 const 输入、const 查询值、const 引用对象和内建标量引用对象。区间必须按引用对象当前值是否小于查询对象当前值分区。它返回第一个不小于查询值的元素，或区间末尾；空区间返回起始指针。比较读取引用对象当前值，并保持所有引用绑定和引用对象。三个参数均只求值一次，并保留接收对象清理。所选 SDK 委托、可调用性特征、恒等投影、指针距离、前进、半长度和二分搜索均被验证；源码替换、自定义比较、枚举或嵌套包装器引用对象，以及间接或转换后的被调用函数仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::set_intersection`，将同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针范围写入可变包装对象指针范围，包括 const 输入、const 引用对象及内置标量引用对象。输入须按当前引用值排序，输出须有足够空间且不与输入重叠。算法比较当前值，复制共有元素在第一范围中的绑定，保持引用对象及第一范围顺序不变。每个值的次数取两个范围中较小的次数。任一输入为空时，输出保持不变。返回输出末尾，五个实参各求值一次，并保留接收对象析构。所选 SDK 的迭代器类别别名与分派、指针 move 与 swap、单侧及二分搜索、比较与 identity 调用、输出辅助函数及结果构造函数均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::set_symmetric_difference`，将同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针范围写入可变包装对象指针范围，包括 const 输入、const 引用对象及内置标量引用对象。输入须按当前引用值排序，输出须有足够空间且不与输入重叠。算法比较当前值并复制任一范围剩余元素的绑定，引用对象保持不变。相等元素逐对抵消；剩余元素保留来源顺序和绑定。任一范围为空时复制另一范围。返回输出末尾，五个实参各求值一次，并保留接收对象析构。所选 SDK 的委托调用、比较器引用、结果载体字段与构造、move 适配、扫描及两个内部 copy 尾部路径均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::set_difference`，将同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针范围写入可变包装对象指针范围，包括 const 输入、const 引用对象及内置标量引用对象。输入须按当前引用值排序，输出须有足够空间且不与输入重叠。算法比较当前值并复制第一范围剩余元素的绑定，引用对象保持不变。每个值的次数为第一范围次数减去第二范围次数，最少为零。第一范围为空时返回未改变的输出指针；第二范围为空时复制第一范围。返回输出末尾，五个实参各求值一次，并保留接收对象析构。所选 SDK 的比较、引用转发、move 适配、pair 输出字段、扫描和内部 copy 尾部路径均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::set_union`，将同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针范围写入可变包装对象指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。输入必须已按引用对象当前值排序，输出须有足够空间且不与任一输入重叠。算法比较当前值并复制绑定，引用对象保持不变。每个值的重复次数取两个输入中的较大次数，相等时优先保留第一个范围的绑定。返回输出末尾；空输入复制另一范围剩余部分。五个实参各求值一次，接收对象析构保持原语义。所选 SDK 的比较、move 适配、比较器引用别名、返回结构、赋值、扫描及两条内部 copy 尾部路径均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::merge`，将同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针范围合并到可写包装对象指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。两个输入范围必须已按引用对象的当前值排序。输出必须能容纳两个范围且不与任一输入重叠。合并比较当前引用值，复制包装对象绑定并返回输出末尾，引用对象保持不变。相等值先取第一个范围，再取第二个范围；空范围直接复制另一范围的剩余部分。五个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的比较、比较器引用别名、赋值、合并扫描和两条 copy 尾部路径均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::includes`，用于同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。两个范围必须已按引用值排序。扫描读取引用对象的当前值，并计入重复元素数量，绑定和引用对象保持不变。第二个范围为空时返回 true；第一个范围为空且第二个范围非空时返回 false。四个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的小于比较、可调用性特征、move 适配、比较器引用别名、identity 投影、invoke 分派及扫描均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::lexicographical_compare`，用于同一已检查 SDK `std::reference_wrapper<T>` 的两个原始指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。比较按两个方向读取引用对象的当前值，处理空范围和真前缀，绑定和引用对象保持不变。相等范围返回 false。四个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的小于比较、identity 投影、迭代器解包、invoke 分派及扫描均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::is_sorted`，用于已检查 SDK `std::reference_wrapper<T>` 的原始指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。比较读取引用对象的当前值，返回范围是否已排序的布尔结果，绑定和引用对象保持不变。空输入返回 true。两个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的小于比较、比较器引用别名、相邻元素扫描及末尾比较均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::is_sorted_until`，用于已检查 SDK `std::reference_wrapper<T>` 的原始指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。比较读取引用对象的当前值，返回指向第一个逆序元素的原始迭代器，已排序范围返回末尾迭代器，绑定和引用对象保持不变。空输入返回起始迭代器。两个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的小于比较、比较器引用别名及相邻元素扫描均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::minmax_element`，用于已检查 SDK `std::reference_wrapper<T>` 的原始指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。比较读取引用对象的当前值，返回由指向第一个最小值和最后一个最大值的原始迭代器组成的 pair，绑定和引用对象保持不变。空输入的两个返回字段均为起始迭代器。两个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的排序、分类与可调用断言、恒等投影、比较适配器、pair 构造及成对扫描均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::max_element`，用于已检查 SDK `std::reference_wrapper<T>` 的原始指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。比较读取引用对象的当前值，返回指向第一个最大值的原始迭代器，绑定和引用对象保持不变。空输入返回起始迭代器。两个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的小于比较、迭代器分类和可调用断言、比较器引用别名及最大值扫描均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::min_element`，用于已检查 SDK `std::reference_wrapper<T>` 的原始指针范围，包括 const 输入包装对象、const 引用对象及内置标量引用对象。比较读取引用对象的当前值，返回指向第一个最小值的原始迭代器，绑定和引用对象保持不变。空输入返回起始迭代器。两个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的小于比较、迭代器分类和可调用断言、比较器引用别名、identity 分派及最小值扫描均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::unique_copy`，从已检查 SDK `std::reference_wrapper<T>` 的原始指针输入范围复制到同一包装类型的可变原始指针输出范围。支持 const 输入包装对象、const 引用对象及内置标量引用对象。比较读取引用对象的当前值，复制每组相邻相等元素的第一个绑定并返回原始输出末尾迭代器；输入绑定和引用对象保持不变。空输入不写入输出。三个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的默认相等比较、迭代器 traits 和标签分派、输入重读循环、包装对象的平凡复制赋值及指针 pair 返回路径均需验证。源码替换、自定义比较、异构包装类型、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::unique`，用于已检查 SDK `std::reference_wrapper<T>` 的可变原始指针范围，包括 const 引用对象和内置标量引用对象。比较读取引用对象的当前值，保留每组相邻相等元素的第一个绑定，压缩后续绑定并返回原始的新范围末尾迭代器；引用对象本身不被赋值。空范围和单元素范围保持标准行为。两个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的相等比较、相邻扫描、迭代器移动和引用验证、包装对象的平凡赋值及指针 pair 返回路径均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::find_first_of`，用于相同已检查 SDK `std::reference_wrapper<T>` 的两个原始指针范围，包括各范围独立的 const 输入和内置标量引用对象。比较读取引用对象的当前值，返回第一范围内首个与第二范围任意元素相等的原始迭代器；没有匹配或范围为空时返回第一范围末尾。四个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的公开函数体、辅助函数体、内置相等谓词和两个扫描循环均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象、异构载体及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用使用默认比较的 `std::adjacent_find`，用于已检查 SDK `std::reference_wrapper<T>` 的原始指针范围，包括 const 输入和内置标量引用对象。比较读取引用对象的当前值，返回首对相邻值相等元素中的第一个原始迭代器；没有匹配时返回范围末尾，空范围和单元素范围也遵循这一规则。两个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的指针移动、恒等投影、调用转发、相等比较和循环体均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用四迭代器形式的 `std::mismatch`，用于同一已检查 SDK `std::reference_wrapper<T>` 类型的原始指针范围，各输入可独立带 const，引用对象限于内置标量。结果包含首对引用对象值不等处的两个原始迭代器；全部匹配时，在较短范围的末尾停止，任一范围为空时也遵循这一规则。四个实参各求值一次，接收对象的析构保持原语义。所选 SDK 的指针长度限制或混合 const 循环、最小长度比较、解包、恒等投影、调用、相等比较、迭代器对构造和重新包装路径均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象、异构包装类型及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用三迭代器形式的 `std::mismatch`，用于同一已检查 SDK `std::reference_wrapper<T>` 类型的原始指针范围，各输入可独立带 const，引用对象限于内置标量。结果是已检查的 `std::pair`，包含首对引用对象值不等处的两个原始迭代器；全部匹配时，两者均前进第一范围的长度。第二范围必须至少包含相应数量的元素。源码实参各求值一次，接收对象的析构保持原语义。所选 SDK 的解包、恒等投影、调用、相等比较、迭代器对构造和重新包装路径均需验证；源码替换、自定义比较、枚举或嵌套包装引用对象、异构包装类型及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用三迭代器形式的 `std::equal`，用于同一已检查 SDK `std::reference_wrapper<T>` 类型的原始指针范围，可包含 const 输入和内置标量引用对象。比较读取引用对象的值，空范围相等，并在首次不匹配时停止。各源码实参只求值一次，接收对象的析构保持原语义。所选 SDK 的解包、谓词和转换声明均需验证；源码比较、枚举或嵌套包装引用对象、异构包装类型、SDK 替换及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用四迭代器形式的 `std::equal`，用于同一已检查 SDK `std::reference_wrapper<T>` 类型的原始指针范围，各输入可独立带 const，引用对象限于内置标量。长度不等时在读取元素前返回 false；两个空范围相等。等长范围比较引用对象的当前值，并在首次不匹配时停止。四个源码实参各求值一次，接收对象的析构保持原语义。所选 SDK 的迭代器特征、类别标签、距离调用、解包辅助函数、恒等投影和相等谓词均需验证。源码替换、自定义比较、枚举或嵌套包装引用对象、异构包装类型及间接或强制转换调用仍被拒绝。

`cpp-core-v2` 还支持直接调用 `std::remove`，用于同一已检查 SDK `std::reference_wrapper<T>` 类型的可写原始指针范围，引用对象为内置标量，比较值为同类型左值或受支持的临时包装。它比较引用对象的值，按原顺序移动保留元素的包装绑定，并返回逻辑末尾。与输入范围别名的比较值保持实时读取，临时值保留初始绑定快照。实参只求值一次，接收对象的析构保持原语义。所选 SDK 的查找、扫描、引用转换及平凡移动赋值均需验证；源码比较、枚举或嵌套引用对象、异构值、SDK 替换及间接或强制转换调用仍被拒绝。

直接 `std::replace_copy` 支持相同精确且经校验的 `std::reference_wrapper<T>` 原始指针范围，输入可为 const，输出必须可修改，旧值和新值必须为同类型包装器左值。受支持的内置标量等值比较读取当前引用值。SDK 校验覆盖指针循环、两次引用转换、两个平凡绑定赋值分支及有序的迭代器前进。五个原始实参各求值一次。匹配时复制当前替换绑定，否则复制当前输入绑定，并返回输出末尾。旧值或新值与输入或输出元素互为别名时，循环全程保留即时变化。空输入不写入。被引用对象和临时对象清理保持源码行为。源码自定义比较、枚举或嵌套包装器引用对象、异构值、SDK 替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::remove_copy` 支持相同精确且经校验的 `std::reference_wrapper<T>` 原始指针范围，输入可为 const，输出必须可修改，比较值必须为同类型包装器左值。受支持的内置标量等值比较读取当前引用值。SDK 校验覆盖指针循环、两次引用转换、条件平凡绑定赋值及输出前进。每个原始实参各求值一次。不匹配的绑定按输入顺序向前复制，并返回输出末尾。比较包装器与输入或输出元素互为别名时保留即时变化。空输入或全部匹配时不写入。被引用对象和临时对象清理保持源码行为。源码自定义比较、枚举或嵌套包装器引用对象、异构值、SDK 替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::replace` 支持相同精确且经校验的 `std::reference_wrapper<T>` 可修改原始指针范围，以及同类型的旧值和新值包装器左值。被引用对象使用受支持的内置标量等值比较。SDK 校验覆盖循环、两次引用转换及选中的平凡绑定赋值。每个原始实参各求值一次；比较读取当前引用值，匹配时复制当前替换绑定。旧值或新值包装器与范围元素互为别名时，循环全程保留其即时变化。空范围或无匹配时不写入。被引用对象和临时对象清理保持源码行为。源码自定义比较、嵌套包装器、异构值、SDK 替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::rotate_copy` 支持相同精确且经校验的 `std::reference_wrapper<T>` 原始指针范围，输入可为 const，输出必须可修改。SDK 校验覆盖两次嵌套的 `std::copy` 调用，并复用其完整的转移证明。四个原始实参各求值一次。先复制中点至末尾的绑定，再复制起点至中点的绑定，最后返回输出末尾。空输入不写入；中点位于任一端点时保持输入顺序。被引用对象和临时对象清理保持源码行为。SDK 或元数据的源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::copy_n` 支持相同精确且经校验的 `std::reference_wrapper<T>` 原始指针范围，输入可为 const，输出必须可修改。计数支持内置无符号整数或非负的有符号常量整数。校验覆盖选中的指针 iterator_traits 特化、差值类型别名、完整的迭代器类别继承链、SDK 恒等计数转换、局部计数及精确的嵌套 `std::copy` 调用链。每个原始实参各求值一次，向前复制绑定并返回输出末尾；零计数不写入。被引用对象和临时对象清理保持源码行为。动态有符号、负数及枚举计数、SDK 或元数据的源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::move_backward` 支持相同精确且经校验的 `std::reference_wrapper<T>` 载体的原始指针范围，输入可以只读，输出必须可修改。选中的 SDK 校验覆盖经典策略、指针可复制构造元数据、两层 move 和输出指针回退，再复用完整的解包、重包、pair 与 memmove 校验。每个原始实参各求值一次，从后向前复制绑定到以输出指针为终点的范围，并返回该范围起点；输入为空时不写入并返回原输出指针。合法的向右重叠范围保持复制顺序。被引用对象生命周期和临时对象清理保持原有源码行为。SDK 或 traits 的源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接三实参 `std::move` 支持相同精确且经校验的 `std::reference_wrapper<T>` 原始指针范围，输入可为 const，输出必须可修改。校验覆盖经典策略、输入与输出指针的可复制构造断言及两层 SDK move，并复用完整的解包、重包、pair 和 memmove 校验。每个原始实参各求值一次，向前转移绑定并返回输出末尾；空输入返回原输出指针。合法的向左重叠保持转移顺序。被引用对象和临时对象清理保持源码行为。SDK 或 trait 的源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::copy_backward` 支持相同精确且经校验的 `std::reference_wrapper<T>` 载体的原始指针范围，输入可以只读，输出必须可修改。选中的 SDK 校验覆盖经典策略、指针可复制构造元数据、两层 move 和输出指针回退，再复用完整的解包、重包、pair 与 memmove 校验。每个原始实参各求值一次，从后向前复制绑定到以输出指针为终点的范围，并返回该范围起点；输入为空时不写入并返回原输出指针。合法的向右重叠范围保持复制顺序。被引用对象生命周期和临时对象清理保持原有源码行为。SDK 或 traits 的源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::copy` 支持相同精确且经校验的 `std::reference_wrapper<T>` 载体的原始指针范围，输入可以只读，输出必须可修改。选中的 SDK 校验覆盖指针解包和重包、pair 构造、平凡复制派发，以及与载体尺寸一致的运行时 memmove 字节数。每个原始实参各求值一次，按顺序复制绑定并返回前进后的输出指针；输入为空时不写入。被引用对象生命周期和临时对象清理保持原有源码行为。SDK 源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::fill` 支持相同精确、可修改且经校验的 `std::reference_wrapper<T>` 载体的原始指针范围及相同载体的填充值。完整 SDK 校验覆盖指针 iterator traits 来源、平凡的 SDK 类别标签构造和选中的范围派发，再复用完整计数填充校验。每个原始实参各求值一次，每次写入复制当前填充值绑定；空范围不写入。被引用对象生命周期和临时对象清理保持原有源码行为。SDK 或迭代器 traits 的源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::fill_n` 支持精确、可修改且经校验的 `std::reference_wrapper<T>` 载体的原始指针、受支持的内建整数数量及相同载体的填充值。完整的选中 SDK 校验覆盖恒等整数数量转换、赋值循环和平凡的包装器复制赋值。每个原始实参各求值一次，每次写入复制当前填充值绑定，并返回前进后的输出指针；数量为零或负数时不写入并返回原指针。只读、嵌套和受支持的函数引用保留已校验类型。被引用对象生命周期和临时对象清理保持原有源码行为。SDK 源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::reverse_copy` 支持相同精确且经校验的 `std::reference_wrapper<T>` 载体的原始指针范围，输入可以只读，输出必须可修改。完整的选中 SDK 函数体校验覆盖逆向复制循环和平凡的包装器复制赋值。三个迭代器实参各求值一次，按逆序复制载体绑定，保持输入不变，并返回前进后的输出指针；输入为空时不写入并返回原输出指针。被引用对象生命周期和临时对象清理保持原有源码行为。SDK 源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::reverse` 支持相同精确、可修改的 `std::reference_wrapper<T>` 载体的原始指针范围。完整 SDK 校验覆盖两层 move、经典策略、迭代器类别别名及平凡标签构造、随机访问循环和选中的元素交换。两个迭代器实参各求值一次，只反转包装器绑定；空范围和单元素范围不写入载体。被引用对象生命周期和临时对象清理保持原有源码行为。SDK 或迭代器 traits 的源码替换、用户 ADL 交换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接三迭代器 `std::swap_ranges` 支持相同精确 `std::reference_wrapper<T>` 载体的原始指针范围。现有完整 SDK 校验覆盖范围循环、迭代器适配器、选中的元素交换和指针结果构造。每个迭代器实参各求值一次，只交换包装器绑定，并返回前进后的第二个指针；首个范围为空时返回原第二个指针。被引用对象生命周期和临时对象清理仍遵循原有源码路径。用户 ADL 交换、SDK 源码替换、间接或转换调用及不受支持的原始依赖仍被拒绝。

直接 `std::iter_swap` 支持指向相同精确 `std::reference_wrapper<T>` 载体的原始指针。完整 SDK 函数体必须解引用两个原始迭代器参数，并选中经过校验的 SDK 元素交换。两个指针表达式各求值一次，交换只改变包装器绑定。自交换、被引用对象生命周期和临时对象清理保持原有行为。用户 ADL 交换、选中 SDK 的重声明或特化、转换或间接调用，以及不受支持的原始依赖仍被拒绝。

实际执行的直接 `std::exchange` 调用支持精确的 `std::reference_wrapper<T>` 目标和相同包装器类型的替换值，包括 const 以及左值、右值视图。独立的完整 SDK 函数体校验检查选中的包装器构造、赋值、move 和 forward 调用。两个实参在读取原绑定前各求值一次；返回原包装器，并将目标改绑到替换值，自交换也保留该语义。移动或复制的只有包装器指针，被引用对象的生命周期和临时对象清理仍遵循原有源码路径。不同包装器或源码类型的替换转换，以及选中 SDK 操作的源码替换仍被拒绝。

实际执行的直接 `std::swap` 调用也支持精确的 `std::reference_wrapper<T>` 包装器，并复用现有的 SDK 元素交换函数体校验。交换只改变存储的引用指针；const、嵌套、源码结构体和受支持的函数引用仍保留经检查的布局与签名。两个操作数各求值一次，自交换保留原绑定，原有临时对象清理和被引用对象的生命周期保持完整。选中的 swap/move 函数的源码替换或重声明，以及其他不受支持的原始依赖仍被拒绝。

固定 SDK 的 `std::exchange` 的非求值 `decltype`、`sizeof` 和 `noexcept` 调用也支持精确的 `std::reference_wrapper<T>` 包装器，以及相同包装器类型的替换值，包括 const 视图和左值、右值引用视图。const、嵌套、源码结构体和受支持的函数引用仍保留经检查的存储布局和原始签名。查询认证 SDK 声明和已解析的无异常特征，并保留包装器的平凡生命周期；不会选择 exchange 函数体、包装器赋值或被引用对象的构造、调用和析构。操作数的副作用不会执行。源码重声明、特化、别名、默认实参和操作数函数体仍接受常规检查；实际执行的包装器 exchange 需通过独立的完整 SDK 函数体校验。

纯 `std::swap` 查询现在也支持精确的 `std::reference_wrapper` 载体，包括常量被引用对象、嵌套包装器、源记录及函数引用。仅检查包装器布局与载体的平凡生命周期；查询不创建包装器，也不选择被引用对象的构造、调用或析构。精确的结果、约束、异常来源及所有原始操作数依赖仍会检查。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#scalar-utilities-and-pairs-from-utility).

纯指针 `std::swap` 类型与 `noexcept` 查询现在会验证精确的无返回值引用签名、结果别名、移动／赋值约束及已解析的 SDK 异常特征，无需选择 SDK 函数体。回调指向的函数签名和源记录布局仍会检查，操作数不被求值并保留原始异常状态；函数、别名、约束或特征的用户重声明仍被拒绝。实际交换保留已有函数体证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#scalar-utilities-and-pairs-from-utility).

纯回调指针与固定数组的 `std::exchange` 查询现在可使用精确的指针／引用签名和已解析的固定 SDK 移动／赋值特征，无需实例化 SDK 函数体。指针限定、`noexcept` 移除、函数引用退化、空指针替换及允许的数组转换仍保留检查后的类型身份。操作数不被求值，其原始异常状态和源依赖仍会检查；实际交换仍需证明所选函数体。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#scalar-utilities-and-pairs-from-utility).

pair 和 tuple 的值元素也接纳经过检查的固定实参数量普通函数指针，包括嵌套的 pair、tuple 和数组字段。构造、赋值、工厂、投影、引用绑定和 swap 保留指针值或绑定；兼容转换支持函数名衰变、`nullptr`、去除 `noexcept` 及转为 `bool`。相等与不等比较接纳兼容的回调签名。原始签名、别名、数组边界、异常说明和函数定义的源码检查仍然适用。

这些自有记录的选定构造函数还可以使用额外的默认实参；每次使用都检查原始形参作用域和源码。泛型 swap 保留受支持的复制回退，并在赋值前清理构造默认实参的临时对象。pair 的每个成员初始化器在下一个字段开始前清理这些临时对象。直接构造和 `make_pair` 在构造字段前将所有调用方实参各求值一次，保留左值别名、捕获的纯右值和外层临时对象生命周期；工厂函数中的数组实参衰变为元素指针。

Core v2 接受经过认证的空与非空 `<tuple>` 值，包括构造、赋值、工厂函数、交换、比较和 `get`。`std::tie` 和 `std::forward_as_tuple` 创建经过认证的引用元组，其 `get` 和 `apply` 操作保留别名、值类别以及对所绑定对象的写入。`std::make_tuple` 会解包经过认证的 `ref`/`cref` 实参，包括引用与值混合的结果；这些结果支持精确兼容的直接构造和同类型复制／移动构造、按索引或唯一类型的 `get`，以及来自元组或 pair 的同类型或兼容异构赋值。全部六种比较使用相同的递归异构值规则。成员及自由 swap 逐元素交换值，不改变引用绑定。从元组或 pair 进行的兼容转换构造会绑定引用元素，并独立初始化值元素。`std::apply` 通过现有经过检查的可调用对象边界，将引用元素作为其所引用对象传递，并传递值元素。当选定的 libc++ 叶节点复制或移动构造函数与所提供的源对象及值类别匹配时，逐元素直接构造和 `std::make_tuple` 也接纳源码自有的非平凡标准布局记录。工厂函数会认证转发到该构造函数的过程，包括自有值与 `ref`/`cref` 元素混合的情况。当默认化的 libc++ 元组、实现类及叶节点构造函数选定受支持的源码自有元素构造函数时，同类型整元组复制／移动构造也接受这些自有记录。混合的标量与引用元素保留各自的值和绑定；没有可用的移动操作时，右值可以选择元素的复制构造函数。源元组只求值一次；自有字段按元素顺序构造，并按逆序析构。其他拥有所有权的元组构造函数仍受限制。普通值元组同样可以在构造和赋值时，从全引用或混合的元组／pair 源复制或转换所引用的值。赋值按元素顺序存储独立的值，并以引用返回目标元组。引用元组也可以由精确兼容的引用直接构造，或进行复制构造，同时保留其绑定。同类型赋值逐元素通过这些绑定写入。精确兼容的转换构造保留对源元组元素的引用，异构标量赋值则先转换源所引用的值，再通过目标绑定写入。递归比较读取所引用对象，并使用与值元组相同的异构标量叶节点。pair 转换在构造时绑定兼容的 pair 字段，在赋值时则通过目标引用写入转换后的值。成员及自由 swap 逐元素交换所引用的值，不改变元组绑定。每次元组 swap 都会认证选定的 SDK 实现、按索引的叶节点投影和元素交换；支持包含引用包装器的普通嵌套 pair。包装器值交换其绑定，用户 ADL swap 和源码特化仍被拒绝。精确的 `std::apply` 接受经过认证的元组、值／引用／混合 pair，以及元素满足现有复合值与回调规则的数组。具名函数、已存储的函数指针、源码与标准函数对象、引用包装器、源码成员指针和 `mem_fn` 包装器使用相同的经过检查的参数与结果边界。可变与 const 左值、右值以及实体化的临时对象保留元素限定和引用类别。const 左值引用形参也可通过 `apply`、`invoke`、引用包装器调用和成员适配器绑定同一无限定类型的亡值。这些绑定保留原始存储和临时对象生命周期；返回引用不会延长该生命周期。空数组在求值数组表达式后调用无参回调；嵌套数组元素可以绑定已接纳的数组引用形参。按值传递的 SDK 回调参数和结果仍不在支持范围内。可调用对象和源对象各只求值一次；选定的 `get` 操作须先取得精确的 SDK 声明、索引和转发证明，才会降低为标量操作、成员投影或普通调用。精确的 `tuple_cat` 接受零个实参，或值、引用及混合引用的元组／pair 源，以及标量和递归复合数组；它在读取元素前先将所有源各求值一次，并在保留引用绑定的同时直接构造精确的拼接元组。非平凡的源码自有标准布局数组元素使用固定版本 libc++ 结果构造所选定的复制或移动构造函数；其结果元组按逆序析构元素。此类结果元组本身也可以向另一个 `tuple_cat` 提供自有元素。嵌套拼接过程中仍保留左值复制、右值移动、引用绑定及临时对象清理。它们还可以向 `std::apply` 提供引用实参及选定的按值回调实参，源类别和形参生命周期与自有数组相同。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#value-tuples-from-tuple).

固定 SDK 的精确 `std::make_from_tuple` 接受经认证的 tuple、pair 和 array 输入，构造已接纳的标量或源码记录结果。输入只求值一次，保留所选转换、构造模板、默认实参及返回值／字段／数组最终目标的副作用和 C++17 复制消除。辅助函数的实参与默认临时对象先于调用方 tuple 临时对象销毁。运行时操作须有已检查的定义；纯 `decltype`／`noexcept` 查询检查两层工厂签名与异常来源，不生成运行时辅助函数。引用输入、引用结果和直接引用字段须证明所指对象仍存活；失效的转发载体、逃逸临时对象、未知或嵌套引用来源、带基类的结果记录、源码替换、私有辅助函数直接调用及独立函数地址仍被拒绝。NeverC 源语言仍仅为 C23。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#make-from-tuple-result-construction).

固定 SDK 的直接 `std::hash<std::string_view>` 和 `std::hash<std::string>` 调用按已接纳 char 载体的逻辑字节计算哈希。输入只求值一次，完整的 32 位 Murmur2 和 64 位 CityHash 路径保留 SDK ABI1 算术及窄中间值截断，并认证所选公开、继承和辅助声明链。纯 `decltype`／`noexcept` 查询保留实参的异常来源，不生成算法运行时辅助函数。私有辅助调用、独立方法地址、源码替换仍不在本契约内。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#string-and-string-view-hashing).

经过检查的普通函数指针值也可存入 `std::optional` 和 `std::vector`，包括 `noexcept` 签名及已接纳的复合、嵌套 vector 元素。构造、复制、修改和 swap 保留指针值。optional 转换保留空源并转换所含指针值；回调相等比较接纳兼容签名、函数名和 `nullptr`。vector emplace 支持经检查的回调转换，并在扩容和移位时保留别名输入。回调排序、可能抛异常的 optional 访问及既有的 vector 分配函数定义要求仍受各自边界限制。 `find_if`、`find_if_not`、`none_of`、`all_of`、`any_of`、`count_if`、`copy_if`、`remove_copy_if`、`is_partitioned`、`partition_point`、`partition_copy`、`replace_if`、`replace_copy_if` 和 `remove_if` 的源代码谓词对象也接纳兼容的 `noexcept` 函数指针到普通函数指针参数的经检查转换；元素、替换值及输出存储类型仍须精确匹配。 [optional](../../utils/translate-frontends/docs/cpp-core-v2.md#value-optionals-from-optional) · [vector](../../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector)

`std::equal_to`、`std::not_equal_to`、`std::logical_and`、`std::logical_or` 和 `std::logical_not` 支持普通函数指针值，包括 `noexcept` 签名及有类型的 `const` 实参。透明调用接纳函数名、兼容签名以及与回调配对的 `nullptr`。存储的函数对象和 `std::invoke` 保留经检查的转换、实参副作用、别名及临时对象清理。逻辑调用会求值两个实参；这些操作不会调用指针所指向的函数，回调排序仍受既有边界限制。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

这 14 个一元谓词算法也接纳固定 SDK 的 `std::logical_not<F>` 和透明的 `std::logical_not<>`，用于函数指针元素，包括 `noexcept` 签名、有类型的 `const` 及算法允许的 const 输入。选中的参数保留兼容的 `noexcept` 移除和经检查的指针临时值。元素、替换值与输出存储仍须精确匹配；空范围及调用者临时对象清理保留原有时序，不会调用回调目标。

有类型和透明的 `std::equal_to`、`std::not_equal_to`、`std::logical_and`、`std::logical_or` 和 `std::logical_not` 也支持仅用于 `decltype`、`noexcept` 和常量 `sizeof` 表达式的直接调用及 `std::invoke`。固定 SDK 的实际替换签名提供布尔结果，无需实例化未使用的适配器或调用运算符函数体。已支持的标量与回调转换保留精确的参数类型和异常说明；调用者别名、操作数、选中的默认实参及临时对象清理仍检查原始来源，查询不会执行它们。独立 SDK 函数地址及替换声明继续受既有边界限制。

受支持函数和函数指针的 `std::invoke` 查询，现在也可在没有求值调用实例化 SDK 适配器函数体时使用。标量、`void` 及引用参数或结果保留原始函数签名、实参转换和 `noexcept` 说明；记录类型的值仍需使用既有的已实例化调用路径。查询检查固定 SDK 的结果与分派声明，不执行可调用对象、默认实参或临时对象清理。调用者函数定义、书写的别名和操作数来源仍遵循既有检查。

有类型和透明的 `std::logical_and`、`std::logical_or` 和 `std::logical_not` 也接受已支持的完整对象指针、`void*` 和 `decltype(nullptr)` 值，包括已支持的 `const` 类型。直接调用、`std::invoke` 及其类型查询保留相同的布尔转换和实参求值规则。14 类已支持的单参数谓词算法可在对象指针和 `void*` 范围上使用固定 SDK 的 `std::logical_not`。不完整或易失的所指类型、用户定义转换及替换 SDK 声明仍遵循既有检查。

来源自调用者的普通函数声明，在整个声明族没有运行时使用时，也可无需定义而为直接调用和 `std::invoke` 查询提供类型元数据。已支持的标量、`void`、引用和回调结果保留原始签名；别名、数组边界、异常表达式、选中的默认实参及临时清理仍会检查，查询操作数不会执行。运行时调用和存储函数地址仍需定义，C 导出及成员声明继续遵循既有要求。

`std::declval<T>()` 现在可在不求值查询中提供已支持的标量、记录、数组、回调和 `void` 类型元数据。通过固定 SDK 的公开声明及实际选中的内部声明检查引用折叠、原始别名和函数签名。成员访问、结果 traits、`noexcept` 和常量 `sizeof` 查询不会执行对象构造、析构或回调。独立 SDK 函数地址、替换声明及不受支持的书写类型仍遵循既有边界。

已支持的源代码自定义函数对象现在可仅根据实际选中 `operator()` 的签名完成不求值 `std::invoke` 查询，无须预先生成 SDK 适配器调用体。左值、const 和右值重载保留准确的结果及异常来源，支持标量、回调、数组引用、记录引用和 `void` 结果。选中的源代码方法仍须具有完整检查过的定义。查询操作数不会执行回调、构造或清理。按值传递或返回记录、尚未实例化的方法模板仍保留各自的适配器要求。

已支持的全部 19 类算术、位运算、比较和逻辑函数对象，现在都可为直接调用及 `std::invoke` 不求值查询提供签名，同时支持显式类型和透明对象。查询保留窄化返回类型、整数提升、混合数值类型及已支持的对象指针比较；不会执行操作数、默认值、转换或临时清理，包括除法或取模运算中除数为零的查询。SDK 声明、异常元数据和调用方书写的来源仍须检查。指针算术和源代码重载运算符保留各自的要求。

已支持的 `reference_wrapper` 调用及外层 `std::invoke` 现在可直接根据签名完成不求值查询，无须先调用包装器。函数、函数指针、已支持的 SDK 函数对象和有定义的源代码调用运算符保留准确的标量、引用或 `void` 结果及异常来源。固定 SDK 的内部结果 trait、`get` 声明、包装器存储、调用者操作数和源代码定义仍分别检查。查询不会执行回调、默认实参、转换或临时对象清理。按值传递或返回记录及尚未实例化的方法模板仍保留各自的适配器要求。 准确匹配的 `move`、`forward`、`as_const` 及以引用为模板实参的 `move_if_noexcept` 保留包装器的固定 SDK 存储证明和原始操作数来源。

已接纳的 `std::hash<T>` 也支持直接根据签名进行纯查询，涵盖整数、空指针类型、宽整数、`float`/`double`、受支持的枚举和指针。直接调用、`std::invoke`、`reference_wrapper` 调用及外层 invoke 在首次哈希调用前就保留准确的 `std::size_t` 结果和声明中的 `noexcept` 签名。固定 SDK 的公有载体、选定方法族、继承时的公有类型到基类视图及实际实参转换会共同验证。调用者的别名、操作数、默认实参、源代码定义和临时对象清理继续各自检查；运行时哈希仍使用原有操作证明。 命名空间及块作用域中的 `using std::hash` 和 `using std::invoke` 会验证固定 SDK 的查找声明族；实际类型及调用仍保留各自的来源证明。

Windows 构建保留 Microsoft SDK 原有的内部 `log10`/`pow` 模板，并隔离其编译后的符号，从而保留 SDK 特有的计算及浮点状态行为。真实 SDK 与运行时差分检查须由实现该变更的版本通过原生 CI 验证。 符号隔离还会恢复普通 COFF 与 bigobj 静态库中 MSVC 原有的非关联节记录编号，使最终附加记录逐字节保持一致。
[Windows ABI](../../utils/translate-frontends/docs/design.md#private-windows-math-templates).

按值回调实参在 invoke、apply、函数对象、成员函数、mem_fn 以及直接或经 invoke 调用的引用包装器中，统一按实际选定的形参类型转换。经检查的转换支持函数名衰变、兼容函数指针去除 `noexcept`、`nullptr` 转为函数指针，以及函数或函数指针转为 `bool`；空函数指针转为 false。值形参的顶层 `const` 不改变转换，原始签名、嵌套限定及精确引用绑定仍须检查。

`std::invoke` 的函数、函数指针、源码函数对象、成员函数和 `mem_fn` 路径，以及直接或经 invoke 的 `reference_wrapper` 调用，使用与 `apply` 相同的选定按值构造及额外默认实参规则。默认实参临时对象在回调和形参析构期间存活，并在 SDK 调用返回之前清理。SDK 先保留实参绑定，在所有调用方实参求值后读取转发的标量值、函数指针变量、包装器绑定和接收对象指针；选定的函数指针在回调形参构造前捕获。纯右值可调用对象保留已捕获的值，普通间接调用仍在实参之前求值被调用函数。

直接构造、`make_tuple`、整元组复制／移动和 `tuple_cat` 的选定自有元素构造函数同样保留额外默认实参及逐元素清理；所有调用方实参先求值，再构造元素。`std::apply` 对自有或引用 tuple／pair 字段及数组元素使用选定的按值复制／移动构造函数。其默认实参临时对象在回调和形参析构期间保持存活，随后在 SDK 调用返回调用方之前清理。调用方创建的可调用对象及源临时对象、引用结果和返回记录保留原有的外层生命周期。

经过认证的 `std::array` 对象（包括零长度数组和嵌套数组）也可与引用元组和 pair、`ref`/`cref`、`tie`、`forward_as_tuple`，以及经过检查的 `apply`、`invoke`、引用包装器、成员指针和 `mem_fn` 调用形式组合使用。引用形参和结果保留对象身份、限定和临时对象生命周期；通过元组或 pair 绑定进行的赋值会写入数组值，而不会重新绑定。可调用对象的定义保留其源码自有检查。按值传递的 SDK 回调参数／结果、volatile 数组元素以及用户 `std::array` 特化仍不在支持范围内。源码自有的非平凡标准布局元素可以按引用转发，也可以通过选定的构造函数复制到源码自有的按值回调和 `tuple_cat` 结果中。所得元组可以再次拼接，或用作 `apply` 的源；其他拥有所有权的元组操作仍受限制。当固定版本的迭代器调用链选定受支持的源码自有移动操作时，成员／自由数组 swap 也接纳这些元素。每个元素的 swap 临时对象在处理下一个元素前析构；空数组不执行任何元素操作。当固定版本的填充调用链选定受支持的复制赋值时，数组 `fill` 以及原始指针 `std::fill`/`fill_n` 接纳源码自有记录元素。每次写入都在该时刻读取其绑定的源值，包括存在别名的数组元素。当 Clang 生成该精确的数组循环时，默认化的 `std::array` 复制／移动赋值也会按索引顺序执行选定的源码自有元素赋值。同样，当 Clang 生成其单字段数组初始化器时，默认化的 `std::array` 复制／移动构造会按索引顺序执行选定的源码自有元素构造函数。源数组只求值一次；移动可能选择元素的复制构造函数。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#fixed-value-arrays-from-array).

数组布局还为类型查询提供经过认证的证据，包括对返回数组引用的源码自有调用及精确的已接纳 `std::get` 调用进行的直接分类、引用绑定和 `decltype` 查询。聚合局部初始化器和数组临时对象保留递归的元素析构检查，包括零长度维度和可能抛出异常的平凡析构函数。常量 `get` 结果可以提供维度，而无需执行查询操作数。书写的实参和表达式仍受检查；其他 SDK 被调用函数、构造、不抛异常析构根以及枚举来源证明仍单独处理。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#fixed-value-arrays-from-array).

Core v2 类型元数据现支持自有源码中的不完整非联合类，包括前置声明与未实例化模板类型。分类和数组维度查询保留精确源码身份，不生成类存储；运行时类型与回调仍要求完整的已支持类型。引用／指针操作查询可使用这些身份，并保留精确操作源码证明；实际选中的惰性方法返回签名仍须通过完整类型检查。原生验证须由实现版本的 CI 完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#incomplete-record-type-metadata).

Core v2 操作查询现可检查未知长度数组的类型信息。构造与析构的直接返回、精确引用绑定及数组到指针转换均保留对应源码证据；运行时存储仍受原有限制。原生验证须由实现版本的 CI 完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#unknown-bound-array-operation-types).

Core v2 支持未求值调用中经过检查的命名空间模板声明签名，包括 `declval` 使用的引用、数组、函数和 void 回退模式。实际选择、从签名中消失的模板默认参数及已使用的函数默认参数仍须通过最终来源检查。运行时调用和取址仍要求定义。原生验证以实现版本的 CI 为准；标准头文件及完整 C++/STL 尚未完成。 已有源内定义、但尚未使用的命名空间模板也执行相同的来源检查，函数体仍保持惰性。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#unevaluated-declaration-only-template-signatures).

Core v2 支持默认 sized delete 转发：未被源码重声明的隐式全局 sized delete/delete[] 可调用对应的源码 unsized 定义。显式 sized 定义优先，数组仍使用原表达式的 cookie 布局。此项不提供分配失败处理或异常运行时；原生验证须由实现版本的 CI 完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#single-object-allocation-and-placement-reuse).

Core v2 现支持模板、别名、查询与变换中的普通函数类型及其直接左值／右值引用，包括从函数指针推导类型。原始签名、调整前数组边界与 noexcept 依赖仍会检查；函数引用局部变量、字段、参数与返回值现复用经过验证的回调载体，仍检查原始签名与源码定义；跨平台原生验证须由 CI 完成。 函数右值引用在 invoke、引用包装器、pair/tuple 构造、get 与 apply 中也保持函数左值语义；声明返回 F&& 的调用，其类型查询结果为 F&。 静态函数引用也支持常量地址、按顺序的动态启动初始化、首次使用时的一次性初始化，以及已准入的记录与 pair/tuple 字段；仍拒绝 TLS 和无效绑定。 经检查的普通函数地址和函数左值引用也可作为非类型模板实参；默认值、auto 参数和有界参数包均保留精确声明身份、签名及原始来源检查。 固定 SDK 中 integral_constant 的函数指针值现在通过经检查的函数地址参与回调调用、invoke/apply，以及 pair/tuple 构造和 make_pair/make_tuple 的值元素，仍不开放类型特征常量的存储身份。 经检查的函数指针 integral_constant 对象也支持单字节空存储、复制／移动／赋值、精确转换、零参数取值调用和零参数 invoke，并保留接收者的副作用。 这些对象也可作为 pair 值元素，支持直接构造、make_pair、复制／移动／赋值和混合引用 pair。 这些对象也支持 tuple 值元素的直接构造、`make_tuple`、复制、移动、赋值和混合引用 tuple。经过验证的空基类投影保留原生布局、元素地址和参数副作用，并避免覆盖邻接值。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#bare-function-type-metadata).

tuple 中经过验证的函数指针 trait 对象也支持成员 `swap` 和 `std::swap`。值元素交换数值，引用元素交换其引用目标的值而不重新绑定，空元素保留自身地址且不写入字段。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

经过验证的空函数指针 trait 元素现在也可与受支持的源码 owned 对象组成 tuple。直接构造、`make_tuple` 和整个 tuple 的复制／移动保留所选源码构造函数、默认参数副作用及析构行为。重复空元素保持不同地址。赋值和交换仍须满足各元素现有的生命周期检查。 这些 tuple 也可通过 `tuple_cat` 组合，并通过 `apply` 调用经检查的源码可调用对象。按值传递的 trait 参数具有独立空存储；所选 owned 复制／移动及其清理仍保留现有检查。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

经过验证的函数指针 trait 对象现在在 `invoke` 和 `apply` 的左值、const 与右值引用参数、引用结果、引用 tuple 绑定和包装器访问中保留引用身份。仍执行现有 cv 限定与值类别检查，其他 trait 类型和 SDK `value` 存储别名仍不受支持。 `invoke` 也支持向经检查的源码函数、调用运算符、成员调用及包装后的可调用对象按值传递这些 trait。参数具有独立空存储；所选 owned 构造、默认参数副作用及清理仍保留检查。 `invoke` 和 `apply` 也支持按值返回这些经验证的 trait 对象，并复用现有结果目标存储与源码函数体检查。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

经验证的函数指针 trait 对象也可作为 `array` 元素。初始化、const 访问、复制／移动、嵌套及零长度数组、`fill` 和成员／自由函数 `swap` 保留现有存储及操作检查。不同元素保持不同地址；volatile 元素和其他 trait 类型仍不受支持。 默认对象和空数组保留未使用 `value` 初始化器的懒实例化；实际 `value` 表达式仍须通过现有常量求值检查。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

已支持的空标准函数对象和经验证的回调 trait 对象也可在固定原始数组中默认构造，包括 const 和多维数组。已有逐元素构造为每个对象保留独立存储，并检查数组数量限制。局部、静态和全局数组、常量大小的 `new[]` 以及嵌套 `vector` 的范围源使用同一构造器认证。源码回调定义、分配／释放，以及不支持的 trait 或函数对象类型仍须通过现有检查。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

经验证的 `reference_wrapper` 实参现在可通过 `invoke` 和 `apply` 绑定匹配且受支持的左值引用形参，包括显式存储包装器值的 tuple。固定 SDK 的精确转换及转发形参均须通过认证。调用保留原引用目标的地址、写入效果和 const 限定。已校验的函数和函数指针包装器也可提供普通回调的按值形参，包括兼容的 `noexcept` 去除。回调值在调用方实参求值后读取，保留后续修改；空指针仍按值传递。任意源码转换、函数包装器到布尔值的转换及 volatile 引用目标仍不受支持。 经验证的算术值和对象指针包装器也可通过相同调用路径提供受支持的标量按值形参，包括算术值到 bool 和指针限定转换，并在调用方实参求值后读取。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

带类型的标准函数对象也可通过 `invoke`、`apply` 及直接或经 invoke 调用的可调用对象包装器接收已校验的标量包装器实参。算术、比较和逻辑运算使用选中的形参转换，包括浮点、窄化及算术值到 bool 的转换。SDK 包装器的精确转换及转发流程仍须通过认证。在调用方实参求值完成后读取引用目标，保留后续修改及单次求值语义。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

透明标准函数对象也可在直接调用、`invoke`、`apply` 及可调用对象包装器调用中接收经检查的算术值、对象指针和已接纳函数指针的包装器。固定 SDK 转换须在选中的内建运算前解包精确转发的形参。类型提升、指针比较及回调逻辑运算沿用现有类型检查，不调用回调目标。调用方实参求值完成后才读取引用目标，保留后续修改及单次求值语义。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

经检查的透明标准函数对象包装器实参调用也支持 `decltype` 和 `noexcept` 查询，包括外层 `invoke`。证明沿选中内建运算的 noexcept 表达式认证精确 SDK 转换及转发链，不实例化未使用的运算符函数体。查询不执行调用方副作用；源码转换、替换和未支持的引用目标继续保留原有检查。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

经检查的 `reference_wrapper` 值也可作为 `array` 元素。显式初始化、const 访问、复制／移动、赋值、`fill` 和经校验的交换保留或交换包装器绑定，不对引用目标赋值。嵌套及零长度数组沿用现有布局检查；从数组元素填充时先捕获其绑定，空数组填充仍将实参求值一次。原引用目标类型和源码函数目标仍须检查；含包装器叶节点的比较和用户 ADL swap 保留原有限制。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#fixed-value-arrays-from-array).

`vector` 也接纳经检查的 `reference_wrapper` 值元素，并认证固定 SDK 的平凡复制、移动、赋值及析构属性。构造、复制／移动、赋值、值 emplace、插入、填充、擦除、扩容和交换保留包装器绑定及现有分配检查。扩容时保留别名元素的值；包装器析构不会析构引用目标。const 引用目标、经检查的回调及源码自有记录引用目标仍保留各自检查。用户转换及包装器元素比较继续保留独立边界。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector).

经检查的包装器 vector 也支持从精确类型的左值引用目标执行 `emplace_back(referent)` 和位置插入 `emplace(pos, referent)`。选中的每条 SDK 转发分支、allocator 适配层及包装器构造均须认证。绑定保存引用目标的地址，实参只求值一次，并沿用既有扩容和插入行为。const 绑定及受支持的函数、回调、数组和源码记录引用目标保留各自检查；用户转换和源码替换 SDK 适配层仍被拒绝。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector).

经检查的包装器 optional 也支持从精确类型的左值引用目标执行 `emplace(referent)`。选中的 optional reset、存储地址、转发、placement 构造、直接包装器构造和 engaged 标志均须共同认证。空 optional 和已占用 optional 都绑定原地址，实参只求值一次，并返回所存包装器的引用。const、函数、回调、数组和源码记录引用目标保留各自检查与生命周期；用户转换和源码替换 SDK 操作仍被拒绝。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#value-optionals-from-optional).

经检查的 `make_unique<reference_wrapper<T>>` 也可从精确类型的左值引用目标构造，或复制／移动精确类型的包装器值。选中的 SDK 工厂、分配、转发、包装器构造和所有者构造仍须认证。实参在分配前只求值一次；包装器复制在分配后读取源对象，保留分配回调的影响。所有者销毁包装器存储，不销毁引用目标。const 绑定及受支持的引用目标保留各自检查；任意用户转换、源码替换 SDK 和默认包装器构造仍受限制。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯单对象 `make_unique<T>` 结果查询现在可认证精确的固定 SDK 工厂签名和指针大小的所有者布局，无须实例化分配、构造或删除函数体。`decltype`、`sizeof`、`alignof` 和 `noexcept` 不求值实参，也不执行所有权操作；工厂仍可能抛出异常。元素与实参类型、源码函数体、显式模板实参及 SDK 来源仍须检查。查询不授予运行时构造权限。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `make_unique<T[]>` 查询现在可认证精确的无界数组重载、`size_t` 形参和经检查的数组所有者布局，无须实例化分配或元素构造。动态或较大的外层长度仍是未求值元数据，不选择数组分配、cookie 或清理。内层固定维度、元素类型、源码长度函数体和 SDK 来源仍须检查。实际执行的工厂保留常量长度上限以及构造和删除器要求。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `unique_ptr` 观察查询现可认证尚未实例化函数体或默认删除器函数体的固定 SDK 成员签名，覆盖 `get`、可变/const `get_deleter`、布尔转换、单对象箭头/解引用及数组下标，并保留结果限定和已解析异常元数据。接收者与下标表达式不执行，其源码、选中默认参数、所有者布局和 SDK 来源仍受检查。实际操作和查询临时对象继续要求独立生命周期证明；独立成员地址与 SDK 源码替换仍被拒绝。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `unique_ptr` 观察查询也可完成仍处于惰性状态的固定 SDK `reference_wrapper` 元素布局。`get` 与单对象箭头的指针结果保留包装器类型、const 限定及指针大小 ABI，无需实例化包装器构造、getter 或删除器函数体。标量、数组、函数、回调及源码记录引用继续接受各自的源码和类型检查；SDK 偏特化、源码替换与不支持的引用对象仍被拒绝。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `unique_ptr::release()` 结果查询现可认证固定 SDK 的精确签名，无需其函数体或默认删除器函数体。借用的可变单对象与数组所有者保留原始指针类型、元素限定及有界内层维度；受支持的自定义删除器继续要求源码证明。查询不执行接收者、不清空所有者，也不选择引用对象构造或清理。SDK 包装器元素沿用经过认证的布局完成证明；实际 release 与其他所有权操作仍需独立运行时证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `unique_ptr::reset()` 查询现可认证固定 SDK 签名，无需实例化 reset 或默认删除器函数体。受支持的单对象、数组及自定义删除器所有者保留 `void` 结果和已解析异常元数据。精确的 SDK 零指针或 `nullptr` 默认参数按具体查询使用点证明。接收者、替换指针和默认参数均不执行；实际 reset 与清理继续要求独立运行时证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯成员 `unique_ptr::swap()` 查询现可认证固定 SDK 的精确签名，无需实例化成员或默认删除器函数体。受支持的可变所有者左值保留 `void` 结果和非抛出元数据，覆盖单对象、数组、包装器及自定义删除器。两侧操作数均不执行，也不交换指针。源码与生命周期检查独立保留；实际所有权操作继续要求原有完整证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯自由函数 `std::swap(unique_ptr<T, D>&, unique_ptr<T, D>&)` 查询现可认证固定 SDK 的精确重载，无需实例化包装函数、成员或默认删除器函数体。两个同类型、已检查的可变所有者左值保留 `void` 结果和非抛出元数据，覆盖单对象、数组、包装器及自定义删除器。推导及显式模板参数仍保留原始源码检查。操作数不执行，也不交换指针；实际操作和独立函数取址继续要求原有完整证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `unique_ptr::operator=(nullptr_t)` 查询现可验证固定 SDK 的精确签名，无需实例化赋值、reset 或默认删除器函数体。运算符语法和显式成员调用保留所有者左值引用结果与非抛出元数据，覆盖单对象、数组、包装器及自定义删除器。接收对象和空指针操作数均不执行，也不删除对象或写入指针。源码与生命周期检查独立保留，实际赋值仍要求完整删除证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯同类型及增加 const 的 `unique_ptr` 移动赋值查询现可认证固定 SDK 的精确签名，无需赋值、release、reset 或默认删除器函数体。单对象、数组、多维数组及包装器所有者保留目标 `Owner&` 结果和非抛出元数据。源、目标的已检查布局及操作数原始源码仍须验证。查询不执行操作数、不转移指针，也不选择删除操作；实际移动赋值仍要求完整证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯所有者之间的 `unique_ptr` 相等与不等查询现可认证固定 SDK 的精确签名，无需比较、getter 或默认删除器函数体。单对象、数组、带限定的元素、包装器及自定义删除器保留 `bool` 结果和 C++17 可能抛出的签名。双方布局、显式模板参数及操作数源码仍须检查。查询不执行操作数，也不比较指针值；实际比较仍要求完整的包装函数/getter 证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯所有者之间的 `unique_ptr` 排序查询现可认证固定 SDK 的精确 `<`、`>`、`<=`、`>=` 签名，无需包装函数、getter、比较器或默认删除器函数体。已检查的单对象、数组、带限定的元素、包装器及自定义删除器保留 `bool` 和 C++17 可能抛出的元数据。原始操作数及显式模板类型仍须检查。查询不读取指针，也不执行操作数；实际排序仍要求完整比较器及全序证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `unique_ptr` 与 `nullptr` 比较查询现可认证两种操作数顺序下固定 SDK 的精确签名，无需包装函数、getter、布尔转换、比较器或默认删除器函数体。相等与不等保留 `bool` 和 `noexcept`；`<`、`>`、`<=`、`>=` 保留 `bool` 和 C++17 可能抛出的元数据。已检查的单对象、数组、带限定的元素、包装器及自定义删除器仍保留原始所有者与空值来源。查询不执行操作数，也不选择删除；实际比较仍要求完整证明。 这六种比较的签名认证也覆盖直接运算符函数调用、已解析的 using 声明和命名空间重导出，以及通过已支持的 `std::move`／`std::forward` 所有者适配器进行的查询。这些查询不会实例化未使用的比较函数体。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `default_delete` 调用结果查询现可认证固定 SDK 的 `void`、`noexcept` 签名，无需删除函数体。单对象、带限定的元素、数组、多维数组和已检查的包装器元素仍保留空删除器的一字节布局，以及原始接收者、指针和模板来源。查询不读取指针，不选择生命周期或释放操作，也不执行操作数副作用。实际删除仍要求完整函数体和生命周期证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯分配器 `allocate`、`deallocate`、`address`、`max_size` 查询现可认证固定 SDK 的精确签名，无需成员函数体。仍须检查一字节分配器，以及标量、源记录或 SDK 包装器元素的布局。分配保留指针结果和可能抛出的签名；释放、两种地址重载和大小上限保留精确结果与不抛出的元数据。原始接收者、指针、计数、提示和数值来源仍须检查，但不执行。运行时分配限制及实际操作的完整证明保持独立。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯分配器 `construct` 和 `destroy` 调用查询现可认证固定 SDK 的精确 `void`、可能抛出签名，无需成员函数体。构造仍检查具体目标指针、模板参数包、引用折叠和实参类别，目标类型可与分配器元素不同。标量、源记录和 SDK 包装器布局仍须检查；未选择生命周期操作时，可查询已删除构造或析构的类型。原始操作数和模板来源保持独立，操作数不执行，实际构造和销毁仍要求完整生命周期证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `allocator_traits` 查询现可认证固定 SDK 的分配、释放、构造、销毁、`max_size` 和复制选择签名，无需转发或回退函数体。仍检查精确分配器和元素布局、模板选择参数、转发引用、结果类型和异常元数据，包括包装器元素及不同源类型的销毁。原始操作数和模板来源保持独立，操作数不执行；实际操作仍要求完整存储、拷贝和生命周期证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `pointer_traits::pointer_to` 查询现可认证固定 SDK 的精确指针、引用类型和不抛出元数据，无需成员函数体或读取地址。类和成员声明链通过检查后，才补全带限定、嵌套及源记录包装器的布局。原始操作数、别名和模板来源保持独立，操作数副作用不执行；不支持的被引用类型布局、源代码替换和独立函数地址仍被拒绝。实际地址操作仍要求完整证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

纯 `std::addressof` 查询在认证内建函数签名及声明链后，可补全精确的 SDK 包装器布局，包括带限定符、嵌套及引用源码记录的包装器。源码函数的参数和结果可在查询遍历前使用这些借用布局。原始操作数、别名、默认参数和源码函数仍需检查，操作数副作用不会执行；不支持的被引用类型布局、源码替换、转换后的被调用函数和独立函数地址仍会被拒绝。实际取地址操作保留现有证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#memory-header-from-memory).

通过 `std::move`、`std::forward`、`std::as_const` 以及使用引用模板参数的 `std::move_if_noexcept` 进行纯包装器引用查询时，可在认证内建函数身份、签名和声明链后准备精确的 SDK 布局。带限定符、嵌套和引用源码记录的包装器、导入的被调用函数，以及源码函数的参数和结果，保留精确的引用类别及 const 视图。布局准备不会授予调用许可或选择被引用对象的生命周期；操作数副作用不会执行，原始源码、别名、默认参数和模板检查仍然保留。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#scalar-utilities-and-pairs-from-utility).

运行时函数引用现在可降级已检查的透明单元素花括号绑定，包括引用成员和对已存储回调的绑定。通过对象或箭头访问静态函数引用数据成员时，使用已检查的静态存储路径。接收者副作用只发生一次，临时接收者在完整表达式结束后清理，`std::ref` 调用也保留此时机。原始目标定义、别名、异常声明、默认参数和接收者布局仍需检查；SDK 目标地址及不支持的源码依赖仍会被拒绝。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#runtime-function-references).

经验证的函数指针 trait 包装器也可通过 `invoke`、`apply` 和直接包装调用提供精确类型的 trait 按值形参，包括 const 引用目标和右值包装器。固定 SDK 的 trait 拷贝和包装器转换仍须通过认证。每个形参使用独立的一字节存储；可调用对象及实参的副作用只发生一次，原源码函数目标仍须检查。其他 trait 域和 SDK `value` 存储别名保留原有限制。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

经验证的源码自有标准布局记录包装器也可通过 `invoke`、`apply` 和直接包装调用提供精确类型的按值形参。选中的源码拷贝构造、默认参数及依赖仍须通过检查。形参使用独立存储，在拷贝前观察后续实参的修改；默认参数副作用和形参清理保留调用中的生命周期。右值包装器仍提供左值引用目标。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#functional-header-from-functional).

经检查的零参数 `optional::emplace()` 现在可对已接纳的非 const 标量、平凡源码自有记录、经验证的函数指针 trait 对象及由这些元素递归组成的数组进行值初始化。固定 SDK 的 reset、原位构造和引用返回函数体均须通过认证。重复 emplace 保留内部存储身份，标量和指针值重置为零或空指针。非平凡默认构造函数、默认成员初始化器及 volatile 元素仍不在此路径的支持范围内。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#value-optionals-from-optional).

尚未完成的函数指针 `integral_constant` 特化现在可在未使用别名、类型变换、兼容类型查询，以及嵌套 pair、tuple 和引用包装器的元数据中保留类型身份。固定 SDK 主模板的精确初始化器、声明实参和源码函数签名均须检查，无须完成 trait 类型或生成其存储。源码函数体仍须独立通过配置检查。实际对象、回调地址及 SDK `value` 存储别名仍须通过原有定义、布局和操作检查。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

Core v2 一元类型变换现检查原始输入、实际替换后的源码与结果，包括最终类型中消失的别名模板参数。固定版本的十六种变换复用现有类型元数据与运行时类型，原生验证仍仅在 CI 执行。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#unary-type-transforms).

Core v2 还支持经过检查的 `__array_rank` 和 `__array_extent`，维度索引为非负常量整数，也支持固定数组类型配合模板索引。折叠前仍检查类型和索引源码；不支持的数组类型与隐式类转换索引仍被拒绝。原生结果须由实现版本的 CI 验证。 未知长度数组（如 `int[][3]`）的类型元数据、别名和模板实参现可保留维数、为零的外层长度及已知内层边界。运行时类型和操作查询仍遵循各自限制；标准头文件及完整 C++／STL 尚未完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#array-type-queries).

Core v2 新增经过检查的内置类型分类查询，包括 `__is_integral`、`__is_pointer` 和 `__is_same`。查询保留 C++ 类型身份，在生成布尔值前检查写出的操作数。不支持的操作数类型及其他类型特征仍受限制。O0／O2 与重定位验证须由实现版本的 CI 执行；标准头文件和完整 C++／STL 尚未完成。 记录类型查询还覆盖聚合、空类、标准布局、平凡性、平凡可复制性、POD、多态／抽象属性，以及已支持类型的 `__is_base_of` 基类关系。 同时支持标准 `final` 类，以及 `__is_final`、`__is_literal`、`__has_unique_object_representations` 查询。结果保留源码的填充、空类表示和 C++17 字面类型语义；其他属性与虚方法仍受现有限制。 可析构性和平凡析构查询也已覆盖受支持的类，保留私有／已删除析构函数、引用及模板函数体和异常规格的延迟处理语义。 具体类型分类和数组查询还会在输出前验证完整的类型／值来源依赖，包括别名边界、缓存及生成的常量；假值或越界结果也不能跳过检查。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#builtin-type-classification).

Core v2 的构造、赋值、转换和析构查询覆盖受支持的非类类型、指针、引用及定长数组。类操作查询保留实际选择的表达式，可接纳隐式平凡操作，以及定义、参数转换和析构依赖均已完成检查的普通操作。构造查询还可使用未被改写的普通默认实参，逐一验证参数身份、原始表达式和临时对象清理依赖，查询本身不执行副作用；nothrow 默认实参还须验证实际调用类型的异常规格已解析。类值及定长数组的不抛异常析构查询现保留选中的析构函数和已解析原型，并验证隐式析构、普通显式默认析构或已完成的普通用户析构源码；删除、访问限制及引用的短路路径保持原结果。隐式平凡构造可与这些已验证的析构组合。已实例化且原始内联定义完整验证的模板操作也可用于查询；未实例化函数体、类外分离定义、复制的成员模板来源和未解析的异常依赖仍需补充证据。O0/O2、重定位和八种 ABI 验证须由实现版本 CI 完成；标准头文件和完整 C++／STL 尚未完成。 `decltype` 最外层调用结果（含括号和内置逗号右侧）保留析构惰性；实参及被调用函数源码仍接受检查。 重建的异常表达式保留原始及解析后的源码检查；已引用内联友元在精确来源与具体签名验证后，仍保持未使用函数体惰性。 查询源码可使用已检查的普通平凡默认化操作或已完成检查的生成操作，并保留初始化器、函数体和布局依赖。 模板类内默认化操作还须具有实际默认化标志、已完成检查的具体签名和精确的原始类内声明；实际访问且保留选择证据的析构查询可触发首次具体签名检查，未使用模板函数体保持惰性。 已验证的默认化构造和赋值也可作为查询操作；非平凡操作必须已有完成检查的生成正文。 选中的普通默认实参内，生成构造和赋值可复用同一证据；选中声明早于其默认化定义时，也会保留后续定义的来源依赖。 已验证的具体模板构造函数，其已实例化默认实参在精确参数及初始化器来源检查完成后也可参与查询；未选中的默认实参保持惰性。 实际消费的基类、值成员及数组元素析构图，也可补齐已有解析结果的默认化析构签名首次检查，并保留根查询的原始结果。 构造、转换或赋值查询保留的完整类类型纯右值结果，也可触发同样的所属子对象析构签名检查；引用结果不会检查所引用对象的析构。 构造、赋值和转换查询可组合使用已检查的普通类模板构造函数及运算符签名，同时保持未使用的类内函数体未实例化。完整保留操作中的每个选定表达式均须具有自己的精确证明，包括嵌套转换与构造实参；默认实参保留独立源码检查，实际消费的类临时对象保留完整所属子对象析构检查。获准完成的普通析构签名可提供查询源码依赖；真实调用和析构仍须检查函数体。 成员模板构造函数、赋值运算符和转换函数，还须验证精确保留的选择记录与模板实参来源；即使函数体已经存在，也须独立证明本次消费的模板默认值。 复制的成员模板还须检查有界的原始声明链及精确的外层、内层实参身份；后续定义或其他特化不能补充缺失的证明。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#non-record-operation-traits).

Core v2 新增通过源码中精确的全局 C 声明直接调用 `malloc/calloc/realloc/free`。源码定义的 C++ 分配器可与独立 C 调用方双向分配、调整大小和释放内存。易失间接桥接保留进程分配器在大小为零及 `realloc` 失败时的实际行为；本机宽度、调用约定、源码和 IR 检查继续生效。O0/O2 验证须由实现版本的 CI 执行。默认可抛异常的 C++ 分配、标准头文件和完整 C++／STL 尚未完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#native-c-heap-calls).

Core v2 现已实现已接纳静态记录、数组和延长寿命临时对象的析构登记，每个完整对象构造完成后分别登记。常量根对象保留原值，局部对象在首次经过声明时登记，非局部对象通过原生启动登记。宿主 CRT 保留退出和模块卸载时的析构顺序。O0／O2、模块卸载和并发首次使用测试需由实现版本的 CI 验证。TLS、异常展开、分配失败处理与标准头文件以及完整 C++／STL 仍未完成；不支持手动／DynCode 加载。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#static-destruction).

Core v2 现已实现已接纳对象与引用的非局部动态初始化，包括已实例化的模板对象。 经过检查的内部原生启动函数在 main 前按定义顺序执行，先完成零初始化与常量初始化，并在每个初始化器之后清理普通临时对象。 静态局部对象保留首次使用初始化。 程序及独立 C 入口的 O0／O2 测试需由实现版本的 CI 验证。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#nonlocal-dynamic-initialization).

运行时长度 new[] 现可使用源代码定义、无 placement 参数的 noexcept 类分配函数。非法长度在调用分配函数前返回空指针；合法元素通过循环构造，默认参数临时对象逐元素清理。显式前缀和长度转换临时对象保留到整个完整表达式结束。重复聚合填充现可直接初始化实际元素，包括嵌套列表和对既有对象的引用；需要额外临时对象的填充、抛异常或 placement 的运行时分配、分配失败处理及完整 C++/STL 仍未完成。原生验证等待实现版本 CI。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#runtime-array-allocation).

普通默认 C++ 堆现支持已接纳对象、数组、unique_ptr、分配器、string、vector 和 unordered_map 的成功且不溢出的分配。未经源码重声明的隐式运算符或固定 SDK 的精确声明，通过已有原生 malloc/free 协议降低；零字节 new 至少请求一字节。仍检查宿主 CRT、size_t ABI 和默认 new 对齐。源码定义优先；六种普通全局 new/delete 签名中任意一种出现源码声明或定义，都会禁止整组默认回退。源码 delete/default_delete 还须排除类及继承分配器、源码全局自定义分配重载（含 placement/nothrow/aligned）造成的未证明配对；SDK 显式全局分配器独立检查。直接运算符调用、函数地址、扩展对齐回退及分配失败和异常处理仍未支持。纯类型／生命周期查询不会生成堆包装函数或数组 cookie 标记。NeverC 源语言仍仅为 C23。 原始默认 new[] 仍须证明常量长度；运行时不抛出类分配沿用独立的现有契约。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#single-object-allocation-and-placement-reuse).

实验性命令 `neverc translate` 通过 `cpp-core-v1`、`cpp-core-v2`、`cpp-project-v1` 和 `cpp-math-v1` 生成可审查的 `.nc` 源码。

**目前仅实现了 C++ 输入转译。** 易语言（E Language，`.e`）、Python、Go、Rust、TypeScript 和 JavaScript 均为未来计划，尚无可用的转译器。

偏特化声明保留主模板参数检查的原始结果，包含内部参数仍依赖模板的成员变量偏特化副本。即使偏特化从未被选中，也会检查已经解析的参数类型源码。长度未定的参数包展开保留已检查的前缀和实际源码，不推测参数槽位；仍待解析的类内成员变量全特化声明同样保留源码检查。完整 C++／STL 尚未完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#partial-declaration-source-checks).

保留参数检查结果不等于偏特化声明有效：它仍必须比主模板更特化，且参数能够推导。非法参数包模式，以及显式实例化定义之后的 extern 实例化声明，仍报告 C++ 诊断（`TR0202`）。

Core v2 支持已接纳泛型类中的具名普通嵌套类、具体副本及自行编写的成员类显式特化。普通类体不增加模板参数层级；未使用的类体保持延迟实例化，已实例化字段、选中方法、成员模板、对象生命周期和标量静态存储保留准确来源及所属实例。显式实例化指令逐条检查书写的限定符，包括 extern 和没有额外作用的重复指令。继承、动态分配和完整 C++／STL 仍未完成。目前仅实现 C++ 输入，易语言和 Python 等仍在计划中。翻译使用内置 Clang 库，不调用外部 Clang 程序；原生结果以实现提交的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#ordinary-nested-classes-in-generic-owners).

Core v2 支持已接纳记录及具体类模板实例中的标量静态成员变量模板，包括默认参数、有界参数包、偏特化／全特化和类外定义。等价实例共享存储，外层或内层实参不同的实例保留独立对象。已检查的非 inline const 值可以不分配存储直接读取，实际求值的地址和引用访问则需要定义。声明和定义都保留源码检查，对象访问保留接收对象的副作用和临时对象清理。依赖外层类中的类内全特化保留原始声明与具体实例的源码信息：已确定的实参、默认值和参数类型立即检查，依赖外层的部分在实例化后检查。其他结果类型、完整 C++／STL 尚未完成。翻译使用内置 Clang 库，无需外部 Clang 程序。目前仅实现 C++ 输入，易语言、Python 等前端仍在计划中；原生验证以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#concrete-member-variable-templates).

Core v2 支持已接纳的普通记录（包括普通嵌套记录）和具体类模板实例的成员别名模板。默认参数与有界参数包可以引用外层类参数和内层别名参数，各自的实参及替换后的类型源码分别检查。别名保留已有值、引用、数组和记录语义，不生成运行时模板对象。非依赖声明即使未使用也会检查，依赖类型则延迟到替换时处理。翻译使用内置 Clang 库，无需外部 Clang 程序；原生验证以实现版本的 CI 为准。完整 C++／STL 尚未完成。目前仅实现 C++ 输入，易语言、Python 等前端仍在计划中。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#concrete-member-alias-templates).

Core v2 支持已接纳记录和类模板实例中的具体成员函数模板：普通／静态方法、运算符、构造与转换函数，以及有界参数包、默认参数、类外定义和特化。内外层实参分别保留身份，等价调用共享代码和静态存储；选中的调用、默认值及已实例化函数体继续接受源码和生命周期检查。翻译使用内置 Clang 库，无需安装或调用外部 Clang 程序。原生结果以实现版本的 CI 为准。继承、动态分配及完整 C++／STL 尚未完成。目前仅实现 C++ 输入，易语言、Python 等前端仍在计划中。 成员模板声明中具体外层类的限定符，即使尚未实例化函数体，也会接受源码检查。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#concrete-member-function-templates).

Core v2 支持命名空间中的整数、布尔和枚举变量模板，包括推导的 auto、选中的偏特化、完整特化、默认参数和有界类型／标量参数包。C++17 类型特征可用于 static_assert、if constexpr 和其他模板实参。等价实例共享带类型的全局存储，不同实例保留独立值和地址。查询固定类型不会强制实例化未使用的初始化器，也不会凭空分配存储。实际实参、选中的默认值、声明／定义类型和已实例化的初始化器仍会检查。其他结果类型、完整 C++／STL 尚未完成；原生验证以实现版本的 CI 为准。

非类型参数的书写类型中，直接的 sizeof(T)／alignof(T) 可作为声明元数据保留，但 T 必须是同一参数表中的那个类型参数。选中的替换仍需接受具体类型检查；其他依赖查询形式不属于此例外。单独的 extern template 声明不提供当前闭合源单元翻译契约所要求的具体定义。

Core v2 在所有支持的目标上使用标准 C++17 模板解析规则，包括 Windows x64 和 ARM64。模板定义不再使用 MSVC 延迟解析；重复的显式实例化定义和不兼容的异常说明仍报告语言错误。未使用的依赖函数体仍按需实例化。完整 C++／STL 支持尚未完成。 函数的显式特化仍须在本源码单元内提供自身定义；重复声明不能代替定义。

Core v2 支持命名空间作用域中可变的整数、布尔和枚举全局变量，可采用零初始化或经过完整检查的常量初始化。变量及其地址在多次调用间保持一致；引用、指针和参数默认值访问同一存储。`extern` 声明必须在当前源文件中找到定义。常量全局量仍只读。全局记录/数组/指针/引用、线程局部存储和静态局部变量仍受各自的限制。

Core v2 支持对源码自有、非 volatile 的平凡标准布局聚合类型进行自动局部结构化绑定，要求其直接公有字段均为受支持的非 volatile 标量或对象指针。`auto` 会创建一个隐藏的值对象；`auto&`、`const auto&` 和 `auto&&` 保留别名和字段限定。初始化器只执行一次；只有当精确的隐藏所有者符合 C++17 生命周期延长条件时，引用分解才会延长临时对象的生命周期。用户自定义的类 tuple 分解、引用／mutable／位域成员、基类、非平凡记录以及静态／全局存储仍不在支持范围内。允许使用 C++17 `if`/`switch` 初始化语句和普通 `for` 初始化；用作条件变量的结构化绑定仍被拒绝。 [记录契约](../../utils/translate-frontends/docs/cpp-core-v2.md#local-record-structured-bindings).

原生定长数组也支持自动局部结构化绑定，包括多维数组。值形式初始化一个独立的隐藏数组；引用形式为原始元素或行建立别名。选定的元素复制／移动操作、逐元素的实参清理和逆序析构保持现有语义。纯右值数组直接初始化其目标，精确的临时所有者保留其作用域生命周期。源表达式只执行一次。变长／未知长度／零长度数组、不受支持的元素操作以及被排除的声明位置仍不属于此契约。 [数组契约](../../utils/translate-frontends/docs/cpp-core-v2.md#local-array-structured-bindings).

经过认证的 `std::pair`、`std::tuple` 和非空 `std::array` 对象也支持自动局部结构化绑定。值所有者保持独立，引用元素保留其所引用对象，const 所有者保留浅层 const 限定。每个隐藏引用都使用精确选定的 SDK `get`；`tuple_size` 和 `tuple_element` 通过其实际的 SDK 特化及 const 转发接受检查。所有者初始化和临时对象清理先于绑定引用的初始化完成。用户自定义的类 tuple 协议、源码 trait／get 特化以及不受支持的容器元素仍不在支持范围内。 [SDK 绑定契约](../../utils/translate-frontends/docs/cpp-core-v2.md#local-sdk-structured-bindings).

Core v2 支持对已支持的固定数组和源文件中定义的范围执行 C++17 范围 `for`，通过已解析的成员函数或 ADL 查找调用 `begin/end`。值或引用循环变量、已接纳的记录／原生数组／SDK 结构化绑定、记录类型迭代器及不同类型的结束迭代器遵循普通调用和生命周期规则。范围初始化及 `begin/end` 只执行一次；每次迭代的对象在递增或退出前析构，覆盖 `continue`、`break` 和 `return`。其他类模板形式、标准头文件、STL 容器和 C++20 范围初始化语句尚不在本次支持范围内。原生验证以实现版本的 CI 为准。

Core v2 支持在其他条件均受支持的标准布局类中使用 `private/protected` 数据成员。内置 Clang 在翻译前检查访问权限；合法的成员函数、构造函数、工厂函数、默认参数和生成的复制/移动操作使用同一份带类型的成员存储。类外非法访问仍报告 C++ 诊断。访问标签属于源语言规则，不提供运行时保密能力。混合访问级别的非标准布局类、模板友元、继承和不支持的字段类型仍受限制。原生结果以实现版本的 CI 为准。

Core v2 支持从已接纳类模板、偏特化、成员类模板、复制的全特化及普通嵌套类中实例化非模板友元函数。隐藏友元 ADL、私有访问、自由运算符、命名空间重复声明、局部类和标量静态局部变量保留实际函数身份，生成普通带类型调用，不增加隐式接收者。未使用的模板函数体保持延迟处理，选中的定义和保留的原始签名会检查。若实际使用了另一份声明的签名，原始签名必须提供完整的非依赖来源证据，包括自身默认参数和已解析的 noexcept。未支持的依赖友元类模板形式、友元类型包展开、完整 C++／STL 尚未完成；现有非依赖友元类型及成员友元规则不变。目前仅实现 C++ 输入，易语言和 Python 待实现。翻译使用内置 Clang 库，不调用外部 Clang 程序；原生验证以实现提交的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#instantiated-non-template-friend-functions).

Core v2 支持已接纳泛型类体中替换完成的非模板友元类型，包括 `friend T;`、别名、依赖嵌套类型和类特化。内置 Sema 检查私有访问及合法但不授予权限的非类类型友元。原始与替换后的类型来源都会检查；来源不明的副本、未支持的依赖友元类模板形式和类型包展开仍不支持。友元授权不生成额外运行时函数、接收者或存储。原生行为需由实现提交的 CI 验证，完整 C++／STL 尚未完成。目前仅实现 C++ 输入，易语言和 Python 待实现；Clang 库内置，不调用外部 Clang 程序。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#instantiated-non-template-friend-types).

Core v2 支持已接纳普通类和泛型类中的自由友元函数模板，包括已支持的运算符、ADL、私有访问、内外层参数、默认值及参数包。具体调用保留引用、对象生命周期和独立的实例／静态存储身份；原始声明与实际函数体上下文都会检查。未支持的依赖友元类模板形式、无法证明来源的复制链和完整 C++／STL 尚未完成，原生行为需由实现提交的 CI 验证。目前仅实现 C++ 输入，易语言和 Python 待实现。Clang 库内置，不调用外部 Clang 程序。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#friend-function-templates).

Core v2 支持已接纳普通类和泛型类中的普通友元类模板声明，包括命名空间内新声明、已有目标及已支持的限定目标，并保留各特化的访问权限。重复授权共享目标类及静态存储；原始声明自行写入默认参数会被拒绝，继承的默认参数保留来源检查。未支持的依赖友元形式及完整 C++／STL 尚未完成，原生行为需由实现提交的 CI 验证。目前仅实现 C++ 输入，易语言和 Python 待实现。Clang 库内置，不调用外部 Clang 程序。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#friend-class-templates).

Core v2 支持指向已有定义的普通自由函数和静态成员函数的带类型指针，包括间接调用、回调参数和返回值、引用传递、数组及字段，以及常量初始化的全局、静态局部和普通静态成员回调存储。调用目标在参数求值前保存。仅接纳默认函数 ABI 及已支持的标量／引用签名；按值传递对象、非静态成员指针、可变参数、lambda 和跨编译单元回调仍待实现。完整 C++／STL 尚未完成，原生验证需由 CI 执行。目前仅实现 C++ 输入，易语言和 Python 待实现。Clang 库内置，不调用外部 Clang 程序。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#ordinary-function-pointers).

Core v2 也支持对已接纳的具体自由函数模板和静态成员函数模板实例取址，可显式指定实参，或由预期指针类型推导。仍检查源码、定义与 ABI；直接调用和回调共享同一实例的函数及静态局部存储，不同实例保持独立。完整 C++／STL 尚未完成，原生验证需由 CI 执行。目前仅实现 C++ 输入，易语言和 Python 待实现。使用内置 Clang 库，不调用外部 Clang 程序。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#concrete-function-template-pointers).

Core v2 也支持具体命名空间变量模板和静态成员变量模板中的常量／零初始化回调。同一实例共享存储，不同实例即使指向同一函数也保持独立；直接书写的 auto 仍接受完整源码检查，指针值模板实参尚不支持。完整 C++／STL 尚未完成，原生验证需由 CI 执行。目前仅支持 C++ 输入，易语言和 Python 待实现；Clang 库内置，不调用外部 Clang 程序。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#callback-variable-templates).

Core v2 支持具名非模板嵌套记录，包括通过合法别名或工厂函数暴露的私有/受保护类型。每个嵌套对象保留自己的存储和接收者；显式外层引用、作用域类型身份、成员/数组复制与移动、析构及嵌套迭代器沿用普通语义。按值依赖类型先输出。匿名嵌套记录、其他类模板形式、继承及完整 STL 仍待实现；原生验证以实现版本的 CI 为准。

Core v2 支持具有定义的整数、布尔及枚举静态数据成员，包括 inline/constexpr 与类外定义，可采用常量或零初始化。所有实例共享同一份带类型存储。接收对象的副作用与临时对象清理得到保留；静态成员引用不随临时接收对象销毁而失效。对于类内常量初始化经过检查的非 inline const 整数、布尔及枚举成员，读取值或丢弃表达式结果无需另行定义，也不会创建全局对象；对成员取地址或绑定引用仍要求当前源文件中有定义。其他静态类型及完整 STL 仍待实现；原生验证以实现版本的 CI 为准。

Core v2 保留已接纳静态局部对象与引用的常量初始化和同步首次初始化。动态初始化器可使用参数、自动局部变量与 this。无锁 32 位守卫在构造和普通临时对象清理后发布结果；需要析构的常量对象只用守卫登记析构。引用保持既有别名，也可在永久存储中延长已接纳临时对象的寿命。析构遵循上述登记契约；TLS 和 constexpr 函数内的静态声明仍未支持。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#dynamic-local-static-initialization) [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#dynamic-local-static-temporaries)

Core v2 支持指向对象的引用成员，包括数组、指针和回调指针对象。构造保存绑定，成员访问及复制／移动保留别名；const 容器仍可访问可修改的引用目标。聚合初始化保留延寿临时对象及其析构顺序，也支持同步初始化的局部静态对象组和具有独立身份的运行时数组默认元素。产生临时对象的数组默认元素现具有独立的语义身份；展开受到资源限制，直接初始化省略元素的默认构造函数保留其参数临时量清理边界。完整 C++／STL 尚未完成；原生验证以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#reference-members).

Core v2 支持内联命名空间、命名空间别名，以及命名空间和代码块内的 using 指令与已解析的 using 声明。导入的函数、变量、类型和无作用域枚举值保留原有身份、名称查找规则、访问检查及对象生命周期。内联命名空间保留与父命名空间合并的查找及双向实参依赖查找，并支持嵌套和重开。类成员导入、继承构造函数、其他类模板形式、C++20 枚举导入及显式嵌套 inline 语法尚未在此阶段支持；完整 C++／STL 仍未完成。

Core v2 支持普通函数及已支持的函数模板具体实例中的 C++17 if constexpr，仅为选中分支生成运行时代码，并保留初始化存储和作用域清理。普通函数仍检查两个源分支；Clang 不会实例化被丢弃的依赖分支。普通函数及全局变量的完整定义限制仍然保留，C++23 if consteval 不受支持。

Core v2 支持最多 64 个参数的自由函数模板，可混合受支持的类型参数与整数、布尔及枚举值参数。类型推导、显式实例化与特化、默认类型、已解析的函数默认参数、递归和命名空间导入均转为普通的有类型函数。不同实例保留各自的局部类型与标量静态存储，即使来自不同主模板且函数签名相同也不会合并。泛型函数体及未使用的延迟默认参数在实例化后接受检查，每个已生成实例的函数体都会检查。sizeof/noexcept 内只有签名的模板调用仍要求定义。未支持的依赖友元类模板形式、标准头文件和完整 STL 尚未完成；原生验证以实现版本的 CI 为准。

支持 C++17 标量 auto 实参、依赖类型的标量参数、数组长度推导及有限的 constexpr 模板递归。值实参转换为带类型常量，不增加运行时参数；等价实参共享实例，不同值或推导类型保留各自的静态存储。书写的实参表达式和参数类型会在折叠消除前接受检查。指针／引用／类值实参仍不在本次增量范围内。

Core v2 支持命名空间中标准布局类模板的具体实例，最多包含 64 个类型或标量值参数。默认类型、显式实例化／特化、类型别名、固定数组和已选中的字段初始化均生成普通的带类型记录。等价实参共享类型，不同值、类型及主模板保留独立身份；复制、引用、返回和析构沿用现有成员语义。书写的类型、默认类型及实参表达式在消除前接受检查。完整 C++／STL 尚未完成。原生结果以实现版本的 CI 为准。

这些标准布局类模板还支持普通具名成员函数，包括 const 和引用限定重载、静态工厂函数、成员范围循环，以及每个实例独立的标量静态局部变量。未使用的泛型函数体和默认参数保持按需实例化；选中的调用和所有已实例化函数体都接受现有的类型、定义和生命周期检查。类外定义处的外层模板参数类型写法也会检查。成员函数沿用普通的接收对象和返回值约定，不生成运行时模板参数。

这类模板支持用户定义的默认／参数构造、显式／转换构造及复制、移动构造函数，也支持类外定义和显式实例化／特化。未使用的函数体、初始化器和默认参数保持按需实例化；实际构造必须有经过检查的定义。复制构造的默认参数只在省略实参时求值。初始化直接写入目标存储，遵循成员声明顺序，并沿用现有临时对象、字段和数组的生命周期规则。继承构造仍未支持，原生结果须由实现版本的 CI 确认。

这些类模板也支持用户定义的非虚析构函数，包括类外定义及显式实例化／特化。内置前端判定需要析构定义时必须提供定义，直接返回对象也在检查范围内。仅查询类型或不求值的表达式仍保持未使用析构体的惰性，同时检查已选中的异常规范。析构函数使用明确的接收对象存储，先执行用户函数体，再逆序清理成员和数组元素，并沿用完整表达式、引用延长及按值传参的生命周期规则。显式析构调用及完整 C++／STL 尚未完成；原生验证须以实现版本的 CI 为准。

这类模板支持用 `= default` 定义默认、复制、移动构造函数，复制、移动赋值及非虚析构函数，包括类内和类外定义。已选中的操作保留成员顺序、源与目标存储身份、赋值的引用结果和逆序清理；值初始化区分首次声明默认化与类外默认化。未使用的函数体保持惰性，查询仍检查已解析的书写异常规范，实际使用的非平凡操作必须有属于输入源码的生成定义。显式特化不能继承主模板的默认化标记。完整 C++17／STL 尚未完成，原生验证以实现版本的 CI 为准。

这类模板也支持用户定义的运算符和转换函数，包括复制／移动赋值、迭代器式解引用／自增／下标／箭头、函数对象及显式 bool 转换。已选中的重载保留 C++17 求值顺序、引用别名、最终结果存储和既有生命周期。未使用的函数体与默认参数保持惰性；已选声明和所有实例化函数体仍接受源码与定义检查。类外定义、显式实例化／特化及各实例的标量静态局部变量使用普通有类型函数，不增加运行时模板参数。分配运算符、标准头文件及完整 C++17／STL 尚未完成；原生结果以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#class-template-operators-and-conversions).

在受支持的自由函数模板或类模板成员中实例化的局部类，可使用普通方法、构造／析构、运算符、转换和已选 defaulted 操作。具体成员来源、源码类型、接收对象存储和各实例的标量静态局部变量仍受检查，所有已实例化的局部函数体都要检查。显式函数实例化的每次声明都会保留书写的实参、类型、转换函数名、限定名和属性检查，包括 extern 及不产生新实例的重复声明，避免复用规范声明时漏掉不支持的源码。原生结果以实现版本的 CI 为准，完整 C++17／STL 尚未完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#instantiated-local-classes-and-written-directives).

Core v2 还支持命名空间作用域内的普通运算符函数模板。算术、比较、返回引用的操作、迭代器运算符和记录返回值保留已选重载、C++17 求值顺序、实际存储及临时对象清理。ADL、显式调用、特化／实例化和每个实例的局部类型及静态变量沿用现有带类型函数，不生成运行时模板参数。未使用的函数体保持按需实例化；选中源码、每个已实例化函数体及显式实例化语句的原始写法都会检查。未支持的依赖友元类模板形式、分配、标准头文件及完整 C++17／STL 仍待实现；原生验证以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#namespace-operator-function-templates).

这些类模板还支持零初始化或常量初始化的整数、布尔和枚举静态数据成员，包括 inline／constexpr 和类外定义。等价模板实参共享存储，不同实例保留独立对象。已检查的非 inline const 值无需虚构存储；实际求值的地址或引用访问需要定义。未使用的初始化器保持按需实例化；已实例化的初始化器及每条显式实例化语句保留源码检查。接收对象的副作用和临时对象清理沿用普通带类型操作。其他静态类型、完整 C++17／STL 仍未完成；原生验证以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#class-template-scalar-static-data).

Core v2 支持已准许的函数、运算符和类模板的标量非类型参数默认值。依赖前面参数的表达式、标量 auto、继承的默认值和已检查的 constexpr 转换保留 Clang 的选择与推导规则。省略参数与显式写出等价参数时，共享函数、记录和静态对象。非依赖默认值的原始源码会检查；未使用的依赖默认值保持按需实例化。转换成功后保留原表达式及转换后源码，替换或转换失败仍按普通重载回退处理。完整 C++17／STL 尚未完成；原生验证以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#scalar-template-parameter-defaults).

推导返回类型的引用转换保留别名和 const 属性，包括 decltype(auto) 及 auto&& 的引用折叠。内置前端在筛选候选前推导必要的返回类型，不实例化无关的按值返回模板函数体。完整 C++17／STL 尚未完成，原生验证以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#deduced-reference-conversions). 首次将对象传给引用参数时，会在兼容性筛选前推导必要的转换结果。直接绑定阶段的筛选不免除后续间接初始化所需的返回类型推导；竞争候选 auto& 的函数体若无法推导返回类型，仍会给出普通 C++ 诊断。

Core v2 支持已准许的命名空间函数、运算符和类模板中的具体类型及标量参数包。主模板最多 64 个参数，每个包最多 64 个元素。展开参数保留独立存储，sizeof... 转为带类型的数量，已解析的折叠表达式保留求值顺序、短路及析构。书写的参数类型可保留同一模板参数列表中依赖包数量的元数据。空折叠的模式不实例化；初始值和实际生成的元素仍会检查。完整 C++17／STL 尚未完成，原生结果以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#concrete-parameter-packs).

Core v2 支持命名空间别名模板，可组合已支持的标量、void、指针、引用、固定数组和类类型，以及类型／标量默认参数和具体参数包。别名共享原有类型身份和静态存储。真正使用时检查书写的实参、选中的默认值和替换后的底层源码；未选中的候选、未实例化的依赖函数体仍按需处理。显式实参保留调用者上下文，包括同一模板的递归调用和 this 表达式。参数数量、每个包的元素数量和源码展开深度上限均为 64，并受共享源码预算约束。完整 C++17／STL 尚未完成；原生验证以实现版本的 CI 为准。目前仅实现 C++ 输入，易语言、Python 等前端仍在后续范围。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#namespace-alias-templates).
选中的非类型模板实参还会保留参数类型的源码，包括显式函数模板实参和推导出的空参数包；别名折叠掉的不支持源码仍会检查。

非依赖的类型默认参数即使位于未使用的模板中也会检查。带限定名称的函数模板调用及具体转换函数定义保留实际书写的源码，包括类外定义中的别名写法。返回左值引用的转换可为 const 引用实参生成扩宽类型的临时值；右值引用实参仍遵循 C++ 的重载限制。
Core v2 还支持项目自有命名空间中的类模板偏特化。选中的偏特化及其推导参数与主模板实参分别检查。别名共享同一具体类型及静态存储，已支持的成员保留调用、引用和生命周期语义。参数数量及每个参数包的元素数量上限仍为 64。完整 C++17／STL 尚未完成，原生验证以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#class-template-partial-specializations).

Core v2 支持独立的 `decltype(nullptr)` 值类型，包括别名、引用、字段／数组、回调签名，以及零初始化或常量初始化的标量静态存储和变量模板存储。转换为指针时保留源表达式副作用与临时对象析构；不同空值对象保留各自地址。`nullptr_t` 模板实参现已支持经检查的默认值、参数包和既有模板来源场景，其实例身份与整数零、布尔 false 不同。指针类型的空值模板实参和标准头文件仍未支持；原生验证须以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#null-pointer-values).

Core v2 支持普通类与泛型类中的委托构造，包括构造链和选定的构造函数模板。目标构造与委托方函数体使用同一最终对象，成员只初始化一次，实参临时对象在委托方函数体执行前析构。源代码选择与定义检查仍然有效；原生验证须以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#delegating-constructors).

Core v2 支持普通、泛型及嵌套类中的 const 数据成员，类型可为受支持的标量、记录、指针、回调或数组。初始化写入最终存储，保留源代码选定的复制／移动、独立成员地址及 const 引用／指针类型；非法写入和已删除的赋值由 Clang 拒绝。这可用于自定义的常量键值类，标准头文件与完整 STL 仍未完成。原生验证须以实现版本的 CI 为准。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#const-data-members).

Core v2 接受受支持的删除函数，以及默认生成后被删除的特殊成员，包括相应的模板和友元形式。Clang 保留重载选择与访问诊断；删除声明不生成可执行定义。仅可移动对象、选定的复制回退及 C++17 保证的复制消除沿用现有生命周期处理。须由实现版本的 CI 验证，完整 C++／STL 仍未完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#deleted-function-declarations).

Core v2 接受 IEEE `float` 和 `double`、算术与转换、引用、字段与数组、回调，以及零初始化或常量初始化的标量静态对象。精确的字面量位表示及独立目标布局检查保留数据表示；执行要求默认浮点环境，并禁用快速数学、额外精度和隐式乘加收缩。须由实现版本的 CI 验证；`long double`、标准头文件及完整 C++／STL 仍未完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#binary-floating-point-values).

Core v2 接受窄字符、UTF-8、UTF-16、UTF-32 和宽字符字符串字面量，保留精确字符单元与静态只读存储，并支持字符数组初始化及补零、完全常量初始化的命名空间数组。别名、字段复制与源代码生命周期沿用现有类型化操作。固定版本的 `<string>` 还支持 `std::string` 的一个有限子集：针对使用默认分配器的 `char`，提供构造、访问和析构；其他字符串操作仍未完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#string-header-and-metadata-from-string).

Core v2 支持命名空间数组、函数内静态数组和类静态数组成员的零初始化或常量初始化，包括已支持的模板实例，并保留可变与 const 规则。 同一数组跨调用保留状态和地址，不同模板实例使用独立存储。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#fixed-array-static-storage).

Core v2 支持静态对象指针的零初始化和常量初始化，保留全局对象、字符串、数组元素及字段的地址，以及有效的末尾后一位指针。命名空间、函数、类和模板存储均保留指针身份、可变性与 const 访问规则。前向地址直接输出为 C23 常量，无需运行时初始化。静态引用、动态分配和完整 STL 仍待完成。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#static-object-pointer-storage).

静态引用现在可绑定已有静态对象、数组／记录子对象和字符串，并覆盖局部／类静态引用及已支持的模板实例。 读写保持原对象身份和 const 权限。 参见[静态引用契约](../../utils/translate-frontends/docs/cpp-core-v2.md#static-reference-bindings)。

Core v2 支持常量初始化的静态引用临时对象，包括标量、数组和记录对象。 完整对象、子对象引用、自指针、const 权限和模板实例身份均保留持久存储。 这些临时对象在 C++17 中的常量初始化仍要求平凡析构；非平凡静态临时对象使用动态初始化与析构登记。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#static-reference-temporary-lifetime-extension).

静态记录对象现在支持常量初始化和共享可写状态，涵盖局部／类静态对象及已支持的模板实例。 对象自身地址、constexpr 构造和静态零初始化保留原对象身份。 参见[静态记录契约](../../utils/translate-frontends/docs/cpp-core-v2.md#static-record-objects)。


Core v2 支持经过检查的全空基类链，涵盖普通／模板构造、复制／移动、赋值和析构。每个基类使用真实的 C 首成员存储并独立验证布局。委托构造保留基类角色，与完整对象调用共享静态初始化，并保留参数清理及从派生类到基类的析构顺序。非空派生类存储、继承构造函数、标准头文件和完整 C++／STL 仍未完成。原生验证需要对应实现版本的 CI。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#empty-base-chains).

已校验的函数指针 trait 对象可作为 `vector` 元素，也可嵌套于已支持的 array、pair、optional 和 vector 元素中。现有增长、复制与移动、插入、删除和范围操作保留独立元素存储及已校验的源码回调。布尔 vector、自定义分配器、未获许可的元素比较和 SDK `value` 存储别名仍受原有限制。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

`make_unique` 支持已校验函数指针 trait 对象的默认、复制和移动构造，包括常量长度数组。选中的固定 SDK 构造函数和精确转发路径须校验；调用方实参保留求值次数，分配与释放仍须有源码定义并正确清理。const 源、释放/重置和 vector 所有权使用已有校验路径。运行时数组长度、未支持的 trait 签名和 SDK `value` 存储别名仍受限制。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

`vector::emplace` 和 `emplace_back` 可将精确匹配、已校验的 trait 值用于已支持 pair 元素的一项或两项。const、左值、右值及已校验源码函数结果保留独立 pair 存储；现有标量转换、实参求值、扩容别名和返回的元素引用保持正确。隐式用户转换、无关 SDK 对象和 trait 到回调的转换仍须通过原有独立校验。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

vector 原位构造的 pair 字段也支持已校验的普通函数指针和函数引用、兼容的 `noexcept` 去除及 `nullptr`。直接降低保留回调签名、源码定义和独立 pair 存储。先捕获所有调用方实参的转发绑定，再读取 pair 字段值，因此后续实参修改引用值时不会被过早快照。隐式用户或 trait 转换、volatile 指针和 SDK 函数地址仍受各自限制。 扩容时在已校验的源码分配完成后、已有元素搬迁前读取字段值；有剩余容量时直接构造，无须分配。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#function-pointer-trait-constants).

嵌套 `vector` 元素支持通过 `emplace` 和 `emplace_back` 按已校验的常量数量 `0..65536` 直接构造，并可传入类型精确匹配的填充值或已支持的标量、回调转换。内层缓冲区独立。类型精确匹配的填充值引用在分配期间保持有效；转换后的值在外层分配后、内层分配前保存。零数量仍求值填充参数，但不分配内层缓冲区。动态或有副作用的数量、用户定义的填充值转换和自定义分配器仍受原有限制。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector).

嵌套 `vector` 的范围置入也支持可复制内层元素的精确原始指针、数组退化和已认证包装迭代器。端点绑定只捕获一次；端点值在外层分配后、内层分配前读取。空范围不分配内层缓冲区。源范围可以来自目标容器，复制后的资源保持独立。需转换的范围、反向或任意迭代器及只能移动的元素仍受原有限制。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector).

嵌套 `vector` 元素也可从精确匹配的 `std::initializer_list<T>` 置入，支持直接临时列表、常量列表和空列表。列表指针和大小在外层分配后读取，并在内层分配前保存。资源元素沿用已有复制操作，直接 SDK 回调常量按值复制。临时底层数组保持所需生命周期；用户定义的列表转换和 SDK 存储别名仍受原有限制。 [C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#vector-header-and-metadata-from-vector).

## 安装与标量转译

使用正常安装的 NeverC 及其标准资源即可。C++ 前端和批准的 SDK 头文件均已内置，无需另行安装 Clang。构建细节见[前端说明](../../utils/translate-frontends/cpp/README.md)。

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

`cpp-core-v1` 接受一个不含 include 的独立 C++17 源文件，支持 `int`、`unsigned int`、`bool`、`void`、简单聚合类型、自由函数、命名空间、重载及文档列出的控制流。所有项目声明都会检查，包括未使用的代码。

使用 `--profile cpp-core-v2` 可增加经过检查的 `typedef`／`using` 类型别名、底层类型为受支持整数类型的枚举、`static_assert`，以及限定范围的对象指针与左值引用。引用保留别名关系，包括参数和返回引用；支持多层指针的 `const` 与空指针。仍限单个源文件，且不允许 include。详见 [core v2 支持范围](../../utils/translate-frontends/docs/cpp-core-v2.md)。

core v2 还增加了固定长度的局部数组与数组字段、多维下标访问，以及数组指针和引用。部分初始化将省略的标量元素置零，记录元素按其选定的初始化方式处理；元素初始化保留源码顺序与别名关系。数组长度与初始化展开量有上限。 全局数组与变长数组仍未支持。

core v2 支持 `switch`／`case`／`default`，包括 C++17 初始化语句、分支贯穿及经过验证的 `[[fallthrough]]` 标注。选择器只求值一次；嵌套 switch 和循环保留各自的 `break`／`continue` 目标。GNU case 范围及其他语句属性仍被拒绝。

Core v2 现支持有符号和无符号的 8／16／32／64 位整数、字符类型及字面量，以及常量 `sizeof`／`alignof` 查询。先按源 C++ 规则完成整数提升与重载决议，再归一化位宽；`long`、`wchar_t` 和大小类型遵循目标平台。枚举也支持窄整数和宽整数作为底层类型。标准库仍在开发中。

Core v2 还支持对象指针偏移、差值、递增／递减和复合赋值，可用于遍历数组并保留多维数组的步长。生成的辅助函数保留 C++17 空指针加减零及空指针相减的行为。对象指针大小比较也已接入，在原生 x86／AArch64 目标上使用独立验证的无符号地址表示；原生结果仍需对应提交的 CI 验证。源码指针／整数转换和完整 STL 仍未完成。

Core v2 会用 NeverC 自身的目标模型核对源类型的尺寸与 ABI 对齐，包括结构体大小和字段偏移。生成代码通过编译期断言再次检查布局，清单同时记录这些校验依据。

Core v2 支持已接纳的记录类型中的具名非虚成员函数，包括 const 重载、左值限定方法、静态方法和 `this`。调用保留原对象身份，并在参数之前求值接收对象。

Core v2 还支持具有标准布局及受支持复制操作的记录类型的普通用户构造函数。局部对象、记录字段与数组元素直接在最终存储位置构造，字段按声明顺序初始化。异常展开仍未支持。

Core v2 为每个按值传递的记录参数创建独立对象，并将记录返回值直接写入调用者的目标位置。构造函数和成员函数采用相同规则；源代码要求的复制和引用别名仍会保留。对于满足条件的平凡记录类型，其他 C++17 实现可能增加参数或返回值的复制。

Core v2 支持普通用户析构函数，以及正常退出时的隐式成员析构。局部对象、记录字段和数组元素逆序析构；临时对象在完整表达式结束时清理，先保存需要使用的值。返回、分支、循环、break 和 continue 均执行对应清理。按值参数在被调用函数退出时析构，返回对象由调用者管理。异常展开仍未支持。

Core v2 支持源参数为 `R&` 或 `const R&` 的普通用户复制构造和复制赋值函数。复制直接作用于实际目标，保留函数的副作用与返回引用。赋值运算符语法先求值右操作数，显式调用 `operator=` 时先求值接收对象。

Core v2 还支持编译器生成或显式 default 的默认构造函数，以及 default 析构函数，包括嵌套记录和数组成员。默认构造使用实际目标位置，并按声明顺序初始化成员；值初始化只执行 C++ 规则要求的清零。类外 default 保留其不同的初始化规则。未使用或仅出现在未求值表达式中的 default 构造不要求额外生成函数体。

Core v2 支持隐式及显式 default 的复制构造，包括嵌套记录和多维数组。选定的成员复制构造按声明及元素顺序在实际目标位置执行，源数组地址只计算一次。平凡复制保持已存储的指针字段原值。按值传参与从源对象返回保留复制副作用，直接转发纯右值不会额外复制。

Core v2 还支持隐式及显式 default 的复制赋值。成员和数组元素保留选定的赋值操作及其顺序，生成的平凡数组复制转为有类型的元素赋值，无需外部内存复制调用。平凡赋值保留指针字段原值并返回实际接收对象。运算符语法先捕获源引用，显式成员调用先捕获接收对象；源值在这些副作用完成后读取，赋值不会额外构造或析构对象。

Core v2 支持已准入字段的默认成员初始化器，包括访问先前成员、普通调用、嵌套记录和数组。选中的默认值使用实际所属对象作为 `this`，显式聚合初始化项保留调用者的 `this`；显式初始化会覆盖对应成员的默认值。隐式或 default 复制及赋值不重跑默认值，用户复制构造函数可为省略的成员选用默认值。构造成员初始化与聚合初始化各自保留临时对象的清理边界。模板与完整 STL 仍在开发中。

Core v2 支持指向已有存活对象的右值引用，包括标量、指针、记录和数组别名、引用参数／返回值、条件亡值及普通 `&&` 限定方法。`static_cast<R&&>(live)` 等转换保留同一个对象，具名右值引用变量仍是左值。重载和现有复制行为遵循 Clang 的选择，引用不会增加清理所有者。

Core v2 支持以存活的 `R&&` 或 `const R&&` 为源的普通用户移动构造和移动赋值。构造使用实际目标；赋值保留源值修改、操作数求值顺序和返回的 `R&` 别名，并支持 `&`／`&&` 限定的接收对象。具名右值引用仍选择左值重载，显式构造函数保留其初始化规则。移动不会结束源对象的生命周期，源与目标仍各自正常析构。

Core v2 还支持隐式及显式 default 移动构造和移动赋值，包括嵌套记录与多维数组。成员按声明和元素顺序执行 C++ 选中的复制或移动。生成的移动不重跑默认成员初始化器，平凡操作保留已存储的指针值。源对象仍正常析构，未使用或平凡的生成操作不需要虚构函数体。

Core v2 支持已解析的标准 `noexcept`、`noexcept(true/false)`、C++17 `throw()` 声明，以及常量 `noexcept(expression)` 查询。查询保留选定函数和析构函数的异常规范，不执行操作数。所有操作数与手写规范仍接受源码检查，包括未使用的代码。抛出、捕获、栈展开、模板和完整 STL 仍在开发中。

Core v2 支持普通成员及自由重载运算符，包括算术、比较、下标、解引用、自增、函数对象与一般赋值签名。调用保留选定函数、引用别名和对象结果。运算符语法遵循 C++17 求值顺序，重载逻辑运算符会求值两个操作数。自由赋值运算符统一从右向左初始化参数，再按逆序析构。模板、内存分配和完整 STL 仍在开发中。

Core v2 也支持现存对象上的普通转换函数：隐式与显式整数、枚举和指针转换，条件中的显式 `bool`，以及引用或对象结果。每次转换执行一次选定的成员函数。引用保留别名；对象纯右值直接初始化实际目标，从引用初始化值则保留选定的复制或移动。const／引用限定、constexpr 与 noexcept 遵循 C++17 选择规则。

Core v2 现支持临时对象接收者，以及存活到整条表达式结束的标量／记录临时引用实参，覆盖构造函数、方法、运算符和转换函数。每次求值都有实际存储，对象结果保留最终目标，引用保留别名。调用及被调用函数的参数析构完成后，临时对象按构造逆序清理，并正确处理条件分支和循环中的重复求值。临时记录对象的数组子对象也受支持。

Core v2 现支持普通自动局部引用延长临时对象的生命周期，包括 `const T& r{T{...}}` 和 `T&& r = T{...}`。标量、枚举、指针和记录临时值使用实际存储；C++ 规定延长生命周期时，指向成员或数组元素的引用会保留完整记录对象。花括号及等号加花括号初始化保留同一个对象。对象在引用所属作用域结束时析构，覆盖条件、循环和提前退出；初始化器内其他临时对象仍在各自完整表达式结束时清理。别名不增加所有者，后续复制或移动保留选定操作。[C++17](../../utils/translate-frontends/docs/cpp-core-v2.md#reference-members).

Core v2 也支持独立定长数组临时值，可用于调用、退化／下标访问、丢弃表达式和自动局部引用。每个数组使用一个实际目标；标量写入、记录构造及共享默认填充按顺序直接初始化各元素，不增加元素所有者。C++ 规定延长生命周期时，多维数组的行／元素引用会保留完整数组。元素在完整表达式或引用作用域结束时按逆序析构。数组引用保留有类型的地址，仍遵守现有维度、存储与展开限制。完整 C++／STL 仍在开发中。

Core v2 支持空的标准布局类及无状态的函数对象／转换对象。空对象保留 C++ 的一字节大小和对齐、语言要求的独立存储、选定构造／运算符调用及正常析构。平凡复制仍会求值操作数；数组和包含空成员的记录会计入存储限额。生成的 NC 使用一个内部存储字节，源字段仍为空。继承、模板和完整 STL 仍在开发中。

Core v2 支持转为 void、`void()` 和 `void{}`，可结合受支持的 void 别名、调用、返回、逗号表达式及条件分支。丢弃非 volatile 左值时保留接收者／下标的副作用而不读取其存储值；丢弃临时对象仍会构造对象，并在原有完整表达式边界析构。不会生成 void 变量或值载体。常量和 noexcept 的操作数仍完整检查。volatile 对象、不支持的操作数类型、模板和完整 STL 不在本次增量范围内。

Core v2 支持受允许函数、方法、调用运算符和用户构造函数的默认参数，包括用户拷贝／移动构造函数的尾随默认参数。名称按声明处解析，默认表达式在每次省略实参的调用中求值。引用和值参数保留原有身份与清理规则。数组遗漏元素和生成的数组复制会在下一个元素前清理默认参数临时对象；显式数组初始化项保留外层完整表达式寿命。未使用或被显式实参覆盖的默认值仍会检查。模板和完整 STL 仍未完成。

## 多文件项目

从编译数据库中明确选择翻译单元，并指定项目根目录。内置前端逐个分析翻译单元，合并器检查定义的完整性、链接属性及共享类型，并保守验证单一定义规则（ODR）。

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

项目输出为 `translated.nc` 和 `translated.h`，仅允许该根目录内的项目头文件。一个文件有多种配置时，用 `--compdb-entry src/a.cpp=4` 选择编译数据库中的零起始索引。保存的编译命令只作为数据解析，绝不会执行。

## 有限的双精度数学支持

`cpp-math-v1` 在项目支持上增加 `double`、文档列出的转换／比较，以及精确签名 `std::fabs(double)` 和 `std::floor(double)`。它使用内置的 Clang 20.1.8／libc++ 200100／macOS 15.5 头文件集合，要求明确的 macOS 15.0 目标（arm64 或 x86_64）。暂不支持通用浮点算术。浮点环境要求屏蔽异常陷阱且禁用非正规数归零模式；四种标准舍入模式均已测试。

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

数学转译还会验证已安装的 NeverC 数学头文件、嵌入实现的身份，并实际执行链接探测。生成的数学模块使用 NeverC 运行库。使用 `-fno-builtin-std` 时，仅当最终输出需要 `fabs`／`floor` 映射才会在写入前失败；无需这些映射的数学代码仍可转译。

## 验证与输出

使用 `--check` 替代输出选项，可运行相同的分析、生成、语法及目标代码验证，但不保留生成文件。`--report PATH` 写入结构化诊断。转译过程不会运行源程序。

输出及附属文件不会覆盖已有文件。`--out-dir` 要求父目录已存在且目标目录不存在；`-o` 要求新的 `.nc` 路径。清单记录目标要求、输入／输出哈希和编译方式，源码映射将生成行关联到原始位置。

CI 结果、运行与安装验证以及测试跳过项按平台分别记录。原生 macOS arm64 与通过 Rosetta 运行的 macOS x86_64 仍是不同的验证环境。完整 C++／STL、异常、不受限制的模板，以及超出文档所述子集的字符串／vector 行为，均不在已公布的支持范围内。详见[支持矩阵](../../utils/translate-frontends/docs/support-matrix.md)、[协议与恢复规则](../../utils/translate-frontends/docs/protocol.md)及[项目示例](../../tests/neverc/Inputs/translate/cpp/project)。

## 无序映射元数据

固定的 C++17 `<unordered_map>` 头文件支持 `std::unordered_map<int, int>` 的元数据，要求使用精确的 SDK `std::hash<int>`、`std::equal_to<int>` 和 `std::allocator<std::pair<const int, int>>`。类型别名、类型同一性、公开成员类型、`sizeof` 和 `alignof` 保留所选目标的布局。

使用 SDK 默认策略的固定 `std::unordered_map<int, int>` 支持默认构造、析构、运行时指针与引用、大小及负载观察器、负载上限设置，以及 `operator[]`, `count`, `erase(key)`, `clear`, `rehash`, `reserve`, `bucket`, `bucket_size`, `max_size`, `max_bucket_count` 和成员 `swap`。重新散列与交换保留已插入节点的地址。分配与释放使用经过检查的全局钩子或已认证的默认实现；输入须满足 SDK 前置条件及分配成功且不溢出的范围。`at`、其他元素类型、自定义策略及源码对 SDK 的重声明仍被拒绝。

可变和只读迭代器支持默认、复制及移动构造与赋值、可变到只读转换、解引用、箭头、递增和相等比较。`begin`/`end`、`cbegin`/`cend`、`find`、范围循环和结构化绑定保留 SDK 节点顺序与键的只读引用。 迭代器及迭代器范围的 `erase` 返回下一个可变迭代器。

同一 int/int 映射还支持 `insert`, `emplace`, `emplace_hint`, `try_emplace`, `insert_or_assign`，参数为 int 键值引用或 int/int 二元组，并支持相应的只读迭代器提示重载。返回值为可变迭代器或迭代器与插入标志的二元组。重复插入保留原映射值，插入或赋值操作则替换它。参数仅捕获一次，节点构造在分配后、桶增长前读取引用值。其他参数类型的 emplace 仍需继续补齐。

可变及只读 `equal_range` 返回匹配迭代器及其后继，未找到时返回两个结束迭代器。范围 `insert` 支持 int/int 二元组的原始指针，以及同一默认策略映射类型的可变或只读迭代器；初始化列表插入支持值类型二元组。端点只求值一次，重复键保留首个值，空范围不访问元素。输入须满足 SDK 的范围有效性前置条件。

默认 int/int 无序映射现支持复制、移动构造及赋值。副本拥有独立节点；复制赋值按固定 SDK 的顺序复用已有节点并保留可复用的桶存储。移动保留节点地址并修正桶前驱，使源对象为空、桶数为零，且保留原负载上限。自我复制赋值保持对象不变。源码堆函数遵循 SDK 的分配、值复制和释放顺序。

零参数 `emplace()` 和仅带提示的 `emplace_hint(hint)` 对默认 int/int 二元组进行值初始化。固定 SDK 会在查找前分配并构造候选节点，包括键重复的情况；返回已有迭代器前会释放未插入的候选节点。提示和接收对象表达式均只求值一次，堆函数保持相同的分配、负载上限及释放顺序。

分段 `emplace` 和 `emplace_hint` 支持经校验的 SDK 标签，以及为每个字段提供零个或一个 int 值或引用的元组。元组对象只捕获一次，其值和引用目标在节点分配后、桶扩容前构造候选节点时读取。重复键仍会分配、构造并释放候选节点，保留已有映射值。标签由操作直接消费，此配置仍不支持独立标签存储。

默认 int/int 无序映射支持仅指定分配器的构造、带同一默认分配器的复制及移动构造，以及 `get_allocator()`。分配器表达式在读取或转移源对象状态前求值，并保留其生命周期。该分配器没有状态：副本仍拥有独立节点，相等分配器之间的移动保留节点地址，查询返回已有支持的分配器值。

桶数量构造函数支持确切的默认哈希、相等比较及分配器，包括省略的 SDK 默认参数和分配器简写重载。所有参数均在分配前求值一次；捕获的数量按固定 SDK 规则选择质数或二次幂桶数。请求零个桶时保持空容器且不分配桶，源码替换仍被拒绝。

无序映射构造函数支持原始 int 二元组区间、默认映射迭代器及确切 value_type 的初始化列表，包括桶数量、默认策略和分配器简写形式。端点和参数均在首次分配桶前捕获一次。插入保持源顺序、重复键的首个值及分配时的载荷读取；空列表不会执行空指针算术。

`hash_function()` 和 `key_eq()` 返回经过验证的默认 `hash<int>` 与 `equal_to<int>` 对象。可变及 const 接收对象、返回函数对象的复制、数组和调用沿用现有函数对象语义。每个接收对象表达式只求值一次；观察接口保持映射状态且不分配存储。仍严格验证 SDK 类型、声明及函数体。

默认映射的 local_iterator 与 const_local_iterator 支持按桶 begin/end/cbegin/cend、复制、赋值、可变到只读转换、解引用及递增。三个字宽的存储保留节点、桶索引和捕获的桶数，遍历在桶边界停止。接收对象和桶表达式均只求值一次。须满足桶及迭代器有效性前置条件；区间插入和构造保留捕获的局部桶信息，并在该桶末端停止。

默认 int/int 无序映射支持节点句柄：空句柄、默认及移动构造、移动赋值、析构、bool/empty、通过 const 句柄取得可修改的 key/mapped 引用、get_allocator、成员及 ADL swap、按键或只读迭代器 extract，以及普通和带提示的节点插入。提取及成功转移保留元素地址。普通重复键插入将所有权交给 result.node 并清空源句柄；带提示的重复键插入保留源句柄。删除回调在句柄清空前观察旧状态。须满足分配器和迭代器有效性前置条件；自定义策略与其他元素类型仍需另行支持。 插入结果也支持按值或引用的直接结构化绑定，并保留节点所有权及其生命周期。

merge 接受同一默认 int/int unordered_map 的左值或右值源容器。成功转移保留节点地址，重复键留在源容器。目标查找和桶扩容先完成，再从源容器解链，因此分配回调仍能观察源节点。空容器及自合并保留内容。所选 SDK 模板与两侧存储图均须通过认证；自定义策略及 multimap 源容器仍需另行支持。

默认 int/int unordered_map 支持初始化列表赋值，返回原映射的别名引用，保留桶容量与负载策略，复用旧节点并清理未用缓存，重复键保留首个值。赋值表达式先求值列表，显式 operator= 先求值接收对象。翻译侧修正固定 libc++20 的一个已知缺陷：复用节点因重复键被拒绝时，将其删除而非丢失所有权。普通路径保留原生分配轨迹；重复缓存路径明确增加必要的释放。原生缺陷复现与通过的对照测试分别保留，生成程序须释放全部分配。

默认映射的精确 insert_return_type 支持 SDK 移动构造、移动赋值和直接 std::swap。先复制迭代器及 inserted 成员，再转移节点；赋值在删除旧节点前更新这两个成员。删除回调可观察该顺序，源节点状态在回调后读取。移动保留源迭代器及布尔值并清空源节点。自移动消耗所持节点，自交换保留节点。结果、节点操作及 swap 的源码特化仍被拒绝。

精确的默认 unordered_map<int,int> 支持 operator==、operator!= 以及限定调用或 ADL 非成员 swap。相等比较按键值内容判断，与桶数、负载上限及插入顺序无关，且不分配内存。交换保留节点地址，交换负载上限与桶存储并修复桶前驱，包括自交换。读取或修改存储前，两侧参数各求值一次。仅引用使用不需要堆函数定义。SDK 原始模板、选中函数体及嵌套 pair 比较仍须认证；源码替换、函数地址及其他策略或类型仍被拒绝。

默认映射的精确可变/常量全局及局部迭代器支持 std::distance、std::advance 和 std::next，包括经过认证的 next 默认步长一。直接循环保留 SDK 前向遍历、迭代器参数副本及局部桶元数据。advance 先绑定迭代器再求值距离，后续参数可在遍历前修改该引用的游标；distance/next 在后续参数前复制迭代器。要求范围可达、距离可表示且前向步长非负。SDK 模板、迭代器特征及选中辅助函数体仍须认证；仅引用使用不需要堆函数定义。

通用 `std::swap<T>` 还支持精确默认映射、节点句柄及可变/常量全局或局部迭代器，两侧参数各捕获一次。经认证的 SDK 移动最终清空临时对象；等价的直接存储交换保留节点所有权及地址、映射哨兵链接和局部迭代器全部元数据，不分配或删除内存。自交换保留所有权。仍检查原始通用 swap 声明和选中的移动/析构函数体，拒绝源码替换及间接函数地址。 未使用的 node_type、insert_return_type 别名及其指针、引用、数组和类型身份查询也会认证并补全 SDK 存储，不构造所有者，也不需要堆函数定义。

精确 SDK insert_return_type 支持隐式默认构造和值初始化，均创建奇异迭代器和空节点；值初始化还将 inserted 清零，默认初始化不写入该布尔成员。数组、源码字段及工厂返回保留这些规则和所有权清理。已有空、部分及完整聚合初始化仍按字段顺序执行。原始构造函数体及嵌套迭代器/节点操作仍须认证，所有者生命周期仍要求源码删除函数。


`cpp-core-v2`: 精确的直接 `std::exchange` 调用也支持使用固定 SDK 默认删除器的标量或数组 `std::unique_ptr`，替换值为同类型右值所有者或 `nullptr`。实参在所有权转移前各求值一次；自交换、返回或丢弃的所有者、外层赋值别名及延长的结果生命周期保持正确清理。纯签名查询不生成所有权或堆辅助函数。选定的 SDK 声明、构造、赋值、辅助函数及异常特征来源仍须验证；自定义或转换所有者及独立函数地址仍不支持。

`cpp-core-v2`: 精确的直接 `std::hash<std::unique_ptr<T>>` 调用支持使用固定 SDK 默认删除器的标量和数组所有者。哈希以借用方式访问所有者，接收者和实参各求值一次，再用已验证的 SDK 指针算法计算所观察指针的哈希；空、const 及已移动的所有者保持原生行为。临时实参仍在完整表达式末尾清理。纯签名查询保留 SDK 调用运算符的潜在抛异常签名，不选择指向对象的析构或增加堆辅助函数。启用特征、选定的 get 与哈希声明、字节辅助函数及 ABI 运算均须验证来源；有状态、引用、可能抛异常或使用特殊指针的删除器，以及独立地址仍不支持。

`cpp-core-v2`: 直接 `std::hash<std::unique_ptr<T,D>>` 也支持现有已接纳的空、平凡、源码自定义删除器，其 noexcept 函数体及普通元素指针仍须验证。哈希不调用删除器；原有所有者清理及临时实参生命周期保持其效果。查询和借用不会在原有删除器源码定义之外增加哈希或所有权辅助函数，现有删除器布局及来源约束继续适用。

`cpp-core-v2`: 运行时 `std::hash<T*>` 验证实际选中的固定字节算法，包括主模板及偏特化声明、字读取、memcpy 和 CityHash ABI 分支。该验证同样覆盖 invoke、apply、引用包装器及结果被丢弃的调用，唯一所有者哈希复用同一验证。签名查询保持独立的无需函数体检查，不增加运行时辅助函数。

`cpp-core-v2`: 已支持的标量和数组 `std::hash<std::unique_ptr<T,D>>` 也支持 invoke、apply、直接 reference_wrapper 调用，以及对包装器的 invoke/apply；适用于默认删除器和现有空自定义删除器。类型完全匹配的所有者实参及元组元素借用原地址，接收者与实参的副作用、临时所有者及元组清理保留原生命周期。运行时验证实际选中的适配器及完整哈希来源，覆盖结果被丢弃的调用。纯查询保留潜在抛异常签名，不要求 hash/get/字节函数体或新增运行时辅助函数。本次适配器尚不支持源对象到所有者的转换。

引用元组借用已支持的所有权对象；值元组通过已选中的同类型移动构造转移所有权并清空源，按元素逆序析构，不按位复制所有权对象。

`cpp-core-v2`: 已支持的 float/double/long long 哈希验证选定的标量声明族、精确联合初始化、参数写入和返回；双字委托还验证完整的选中字节算法。同一运行时验证覆盖 invoke、apply、可调用引用包装器及丢弃结果的调用。标量 apply 和包装器 apply 的签名查询绑定实际保留的 SDK 调用，不要求 hash/标量/字节函数体或新增运行时辅助函数；原有源函数定义仍独立保留。正负零归一化、标量转换和实参副作用保持原行为。本项不扩展整数位宽或接纳 long double。

`cpp-core-v2`: 类型完全匹配的 char 字符串和视图哈希支持 invoke、apply、直接 reference_wrapper 调用，以及对包装器的 invoke/apply。字符串实参借用原有 const 引用地址，视图形参接收独立的值快照；接收者和实参的副作用各发生一次。选定的单层字符串和视图元组复制、移动及临时清理保持所有权和生命周期。运行时调用（含丢弃结果）须验证完整的固定 SDK 字节算法。纯签名查询（含默认视图临时对象及替换运行时字节复制辅助函数的情况）验证实际选中的调用，不增加算法或堆辅助函数。源对象到载体的转换仍待支持。

`cpp-core-v2`: 已支持的独占所有者、char 字符串和字符串视图的精确 SDK reference_wrapper 实参可用于对应哈希及现有 invoke/apply/可调用包装器路径。ref/cref、复制或 const 包装器及选定元组实参槽保留实际引用绑定。所有者和字符串借用被引用对象地址，视图接收选定的独立值副本。包装器与被引用对象的副作用及完整表达式清理保持原有顺序和生命周期。运行时调用（含丢弃结果）验证选中的 SDK 转换、访问及完整哈希来源。纯签名查询保留原始转换和异常来源，不要求运行时函数体或新增哈希、堆辅助函数。源定义转换及替换的 SDK 访问方法仍被拒绝。

`cpp-core-v2`: 含已接纳的 char 字符串、字符串视图及独占所有者的嵌套值元组，在构造、整元组复制／移动、get、tuple_cat 和经过检查的 apply 中保留递归操作证明。字符串副本使用独立缓冲区，视图副本保留独立绑定；所有者仅移动并清空源对象，不能复制。引用元素保留原有引用绑定。每层值元素都保留选定的 SDK 构造、转发／getter 来源、实参副作用及逆序清理，包括临时嵌套元组和丢弃结果。布局与纯签名查询保留原始异常及来源依赖，不授权运行时构造／析构，也不新增堆或删除辅助函数。拥有所有权的元组仍不能使用通用按位复制，源码替换仍被拒绝。

`cpp-core-v2`: 纯 tuple_cat 签名查询接纳经过认证的 tuple、pair 和 array 输入，其元素可为已接纳的标量、引用、char 字符串／视图、独占所有者或递归嵌套载体。const 与零长度数组仍校验实际元素身份。布局与签名检查不选取所有者构造或被指向对象的删除函数体；实际书写的实参、SDK 模板声明族、默认值和异常来源继续接受检查。查询权限绑定于每个确切调用及其输入／结果类型。运行时构造、复制、移动和清理仍须满足各自选定操作的要求。

`cpp-core-v2`: 在已接纳的值类型范围内，直接 std::pair 构造模板调用校验实际选中的推导、模板默认值、约束辅助函数及异常来源。默认构造同时检查选中的隐式默认路径和显式主模板回退。嵌套 pair／array 操作保留实际选中的子对象；用户源码类型继续完成普通类型、构造默认参数、异常及析构来源检查。替换的 SDK 辅助函数与源码宏仍被拒绝，包括原生编译有效的替换。运行时值操作和清理仍须分别满足各自要求。

`cpp-core-v2`: 选定的 std::pair 值构造、整 pair 复制／移动及转换构造，通过确切的 SDK 操作接纳非 const 的 char 字符串和字符串视图元素。字符串副本使用独立缓冲区，移动后清空源字符串；视图副本保留各自指向实时缓冲区的绑定。用户源码返回值和丢弃的临时对象保留实参副作用与逆序清理。工厂函数、默认构造、赋值及 const 值目标继续遵守各自现有的操作要求。

`cpp-core-v2`: 选定的 std::pair 元素构造、整 pair 移动及转换构造，通过确切选中的移动操作转移既有准入范围内非 const unique_ptr 元素的所有权。单对象和数组所有者及其可接纳的默认或空自定义删除器，保留源清空、实参副作用与递归清理，包括返回值和丢弃的临时对象。其他操作继续遵守各自的签名与源码要求。

`cpp-core-v2`: std::make_pair 通过已认证的 SDK 工厂及其选定的 pair 构造函数，构造准入范围内的 string、string_view 和 unique_ptr 元素。每个调用都保留字符串的独立复制、移动源清空、视图和 ref/cref 的借用绑定、实参副作用及结果清理。默认参数、约束、显式性、noexcept 和选定的包装器转换均需验证确切源码；其他操作继续遵守各自要求。

`cpp-core-v2`: std::pair 的选定默认构造通过准确的子构造函数初始化已准入的非 const char 字符串、字符串视图和唯一所有者字段。原始 SDK 约束、隐式默认构造路径及异常规范源码均需验证；标量零值、空字符串、空视图和所有者以及递归清理保持原有语义。空自定义删除器沿用现有的平凡构造边界。

`cpp-core-v2`：选定的 std::pair 赋值及成员/非成员 swap 使用原始选中的 string、string_view 和 unique_ptr 操作。赋值保留目标引用、独立字符串副本、移后源 owner 清空及旧目标释放；交换保留借用绑定、字段顺序、owner 地址与清理。约束、异常表达式、转发及各子操作均保留其精确 SDK 或源码要求。

`cpp-core-v2`: 选定的 std::pair 比较对已接纳的 char 字符串、string_view 和 unique_ptr 元素保留原有字段顺序与短路行为。字符串和视图按无符号字节、嵌入零及长度比较；所有者比较观察到的指针，不移动或删除对象。引用绑定与外层操作数仅求值一次的副作用保持不变。选定的 pair 运算符、子比较及所使用的 SDK 辅助函数均须验证原始源码。

`cpp-core-v2`: 选定的 std::pair 分段构造从两个 tuple 参数对象初始化已接纳的标量、源代码记录、string、string_view 和 unique_ptr 字段，保留参数副作用、值 tuple 的独立副本、先 first 后 second 的构造顺序、选定的尾部默认参数及清理。两层 SDK 构造函数、get/forward 使用、推导和异常规格来源均须独立检查。引用字段必须指向仍然存活的调用方对象；指向按值 tuple 参数的引用及尚未证明的空基类 tuple 投影仍被拒绝。

`cpp-core-v2`：直接调用 `std::not_fn` 可保存已准入的普通函数指针，并通过 `&`、`const&`、`&&` 和 `const&&` 调用适配器。函数指针只捕获一次；可调用对象和参数的副作用、引用绑定、转发、算术结果的逻辑取反及临时对象清理保持原有顺序。选中的 SDK 声明、推导、默认参数、约束、异常来源和运行时函数体仍须分别通过源码检查。有状态或仅可移动的可调用对象、可调用的 `std::reference_wrapper` 和自定义 `operator!` 结果仍待支持。NeverC 源语言保持 C23-only。

`cpp-core-v2`：已准入的函数指针 `std::not_fn` 适配器现支持结果类型查询，以及 `&`、`const&`、`&&` 和 `const&&` 四种形式的 `std::is_nothrow_invocable` 查询，包括 `noexcept` 回调。工厂保留固定 SDK 中可能抛出异常的签名。查询不会执行回调；运行时调用保持现有的转发、逻辑取反和清理行为。其他可调用对象形式仍待支持。

`cpp-core-v2`：已准入的函数指针 `std::not_fn` 适配器可调用返回值中经过检查、无引用限定的 `const operator!`；返回值须为由算术字段组成的普通平凡聚合。算术结果类型会保留，包括 `int` 等非布尔结果；每次运行时调用只执行一次回调和所选取反运算符。结果类型查询不会执行两者的函数体。具有非平凡析构的结果和其他可调用对象形式仍待支持。
