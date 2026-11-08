#include <functional>
#include <utility>
#include <cstddef>

struct Counter {
  int value;
  int spare;
  int add(int x) { value += x; return value; }
  int subtract(int x) { value -= x; return value; }
  int read(int x) const { return value + x; }
};
using Method = int (Counter::*)(int);
using ConstMethod = int (Counter::*)(int) const;
using DataMember = int Counter::*;
using OwnedBind = decltype(std::bind(&Counter::add, std::declval<Counter&>(),
                                    std::placeholders::_1));
using BorrowedBind = decltype(std::bind(&Counter::add,
    std::ref(std::declval<Counter&>()), std::placeholders::_1));
using PointerBind = decltype(std::bind(&Counter::add,
    std::declval<Counter*>(), std::placeholders::_1));
using DataBind = decltype(std::bind(&Counter::value,
    std::ref(std::declval<Counter&>())));

extern "C" int nct_member_method_proxy(Counter *object, int x) {
  return object->add(x);
}
extern "C" int nct_member_const_proxy(const Counter *object, int x) {
  return object->read(x);
}

// These definitions deliberately have no main: compile to LLVM text only.
// The separate seven original native bodies retain their real entry wrappers.
extern "C" {
Method nct_member_method_add = &Counter::add;
Method nct_member_method_subtract = &Counter::subtract;
ConstMethod nct_member_method_const = &Counter::read;
DataMember nct_member_data_first = &Counter::value;
DataMember nct_member_data_second = &Counter::spare;
decltype(&nct_member_method_proxy) nct_member_proxy_code = &nct_member_method_proxy;
unsigned long long nct_member_method_size = sizeof(Method);
unsigned long long nct_member_method_align = alignof(Method);
unsigned long long nct_member_const_method_size = sizeof(ConstMethod);
unsigned long long nct_member_const_method_align = alignof(ConstMethod);
unsigned long long nct_member_data_size = sizeof(DataMember);
unsigned long long nct_member_data_align = alignof(DataMember);
unsigned long long nct_member_proxy_size = sizeof(decltype(&nct_member_method_proxy));
unsigned long long nct_member_proxy_align = alignof(decltype(&nct_member_method_proxy));
unsigned long long nct_member_second_offset = offsetof(Counter, spare);
unsigned long long nct_member_owned_bind_size = sizeof(OwnedBind);
unsigned long long nct_member_owned_bind_align = alignof(OwnedBind);
unsigned long long nct_member_borrowed_bind_size = sizeof(BorrowedBind);
unsigned long long nct_member_borrowed_bind_align = alignof(BorrowedBind);
unsigned long long nct_member_pointer_bind_size = sizeof(PointerBind);
unsigned long long nct_member_pointer_bind_align = alignof(PointerBind);
unsigned long long nct_member_data_bind_size = sizeof(DataBind);
unsigned long long nct_member_data_bind_align = alignof(DataBind);
}
