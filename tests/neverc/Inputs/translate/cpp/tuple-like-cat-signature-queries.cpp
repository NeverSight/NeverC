#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
namespace pair_carriers {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
using Input = std::pair<Leaf, int>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Input&>(), std::declval<std::tuple<int>>())), std::tuple<Leaf, int, int>));
}

namespace array_carriers {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
using Input = std::array<Leaf, 2>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Input&>(), std::declval<std::tuple<int>>())), std::tuple<Leaf, Leaf, int>));
}

namespace tuple_owners {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
using Input = std::tuple<std::tuple<P>, int>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Input&>(), std::declval<std::tuple<int>>())), std::tuple<std::tuple<P>, int, int>));
}

namespace array_owners {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
using Input = std::array<P, 2>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Input&&>(), std::declval<std::tuple<int>>())), std::tuple<P, P, int>));
}

namespace pair_owners {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
using Input = std::pair<P, int>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Input&&>(), std::declval<std::tuple<int>>())), std::tuple<P, int, int>));
}

namespace tuple_borrowed_owners {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
using Input = std::tuple<BorrowedOwner, int>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Input&>(), std::declval<std::tuple<int>>())), std::tuple<BorrowedOwner, int, int>));
}

namespace tuple_owner_references {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
using Input = std::tuple<P&, int>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Input&>(), std::declval<std::tuple<int>>())), std::tuple<P&, int, int>));
}

namespace tuple_const_view_references {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
using Input = std::tuple<const std::string_view&, int>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Input&>(), std::declval<std::tuple<int>>())), std::tuple<const std::string_view&, int, int>));
}

namespace scalar_array_layout {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
static_assert(sizeof(std::array<int,2>) == 2*sizeof(int));
}

namespace scalar_array_cat {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<std::array<int,2>&>(),std::declval<std::tuple<int>>())),std::tuple<int,int,int>));
}

namespace pair_second_argument {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<std::tuple<int>>(),std::declval<std::pair<Leaf,int>&>())),std::tuple<int,Leaf,int>));
}

namespace const_array_second_argument {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<std::tuple<int>>(),std::declval<const std::array<Leaf,2>&>())),std::tuple<int,Leaf,Leaf>));
}

namespace pair_noexcept {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
static_assert(!noexcept(std::tuple_cat(std::declval<std::pair<Leaf,int>&>(),std::declval<std::tuple<int>>())));
}

namespace array_expression_sizeof {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
static_assert(sizeof(std::tuple_cat(std::declval<std::array<Leaf,2>&>(),std::declval<std::tuple<int>>()))>=2*sizeof(Leaf)+sizeof(int));
}

namespace empty_array_carrier {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<std::array<Leaf,0>&>(),std::declval<std::tuple<int>>())),std::tuple<int>));
}

namespace const_owner_tuple {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<const std::tuple<P,int>&>(),std::declval<std::tuple<int>>())),std::tuple<P,int,int>));
}

namespace empty_custom_deleter {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
struct D{void operator()(int*)const noexcept{}};using U=std::unique_ptr<int,D>;static_assert(__is_same(decltype(std::tuple_cat(std::declval<std::tuple<U,int>&>(),std::declval<std::tuple<int>>())),std::tuple<U,int,int>));
}

namespace same_query_carrier_comma {
using Leaf = std::tuple<std::string, std::string_view>;
using P = std::unique_ptr<int>;
struct NoDestroy { NoDestroy() = delete; ~NoDestroy() = delete; };
using BorrowedOwner = std::unique_ptr<NoDestroy>;
using Other=std::array<Leaf,1>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Other&>(),std::declval<std::tuple<int>>())),std::tuple<Leaf,int>));
static_assert(__is_same(decltype(std::tuple_cat((std::declval<Other&>(),std::declval<Other&>()),std::declval<std::tuple<int>>())),std::tuple<Leaf,int>));
}

namespace owner_string_pointee {
struct NoDestroy { NoDestroy()=delete; ~NoDestroy()=delete; };
using P=std::unique_ptr<std::string>; using Input=std::tuple<P,int>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Input&>(),std::declval<std::tuple<int>>())),std::tuple<P,int,int>));
}

namespace owner_view_pointee {
struct NoDestroy { NoDestroy()=delete; ~NoDestroy()=delete; };
using P=std::unique_ptr<std::string_view>; using Input=std::tuple<P,int>;
static_assert(__is_same(decltype(std::tuple_cat(std::declval<Input&>(),std::declval<std::tuple<int>>())),std::tuple<P,int,int>));
}

namespace owner_noexcept_query {
struct NoDestroy { NoDestroy()=delete; ~NoDestroy()=delete; };
using P=std::unique_ptr<NoDestroy>; using Input=std::tuple<P,int>;
static_assert(!noexcept(std::tuple_cat(std::declval<Input&>(),std::declval<std::tuple<int>>())));
}

namespace owner_sizeof_expression_query {
struct NoDestroy { NoDestroy()=delete; ~NoDestroy()=delete; };
using P=std::unique_ptr<NoDestroy>; using Input=std::tuple<P,int>;
static_assert(sizeof(std::tuple_cat(std::declval<Input&>(),std::declval<std::tuple<int>>()))==sizeof(std::tuple<P,int,int>));
}

int main(){return 0;}
