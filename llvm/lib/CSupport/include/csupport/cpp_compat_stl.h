#ifndef CSUPPORT_CPP_COMPAT_STL_H
#define CSUPPORT_CPP_COMPAT_STL_H

#ifdef __cplusplus

#include <chrono>
#include <memory>
#include <ratio>
#include <set>
#include <string>
#include <system_error>
#include <type_traits>

typedef std::error_code errc_t;
typedef std::string string_t;
typedef std::error_category errc_category_t;

template <typename T> using uptr_t = std::unique_ptr<T>;
template <typename T> using set_t = std::set<T>;
template <typename T> using init_list_t = std::initializer_list<T>;
template <intmax_t N, intmax_t D = 1> using ratio_t = std::ratio<N, D>;

namespace chrono = std::chrono;
typedef chrono::seconds chrono_sec;
typedef chrono::minutes chrono_min;
typedef chrono::hours chrono_hrs;
typedef chrono::nanoseconds chrono_ns;
typedef chrono::microseconds chrono_us;
typedef chrono::milliseconds chrono_ms;
typedef chrono::steady_clock steady_clock_t;
typedef chrono::system_clock system_clock_t;

static inline errc_t ec_errno(int e) { return {e, std::generic_category()}; }
static inline errc_t ec_sys(int e) { return {e, std::system_category()}; }
static inline errc_t c_make_error_code(std::errc e) {
  return std::make_error_code(e);
}
static inline auto c_set_new_handler(std::new_handler h) {
  return std::set_new_handler(h);
}

#define ERRC_ADDR_IN_USE std::errc::address_in_use
#define MO_RELAXED std::memory_order_relaxed
#define MO_RELEASE std::memory_order_release
#define MO_ACQUIRE std::memory_order_acquire
#define CMOVE(x)                                                               \
  static_cast<typename std::remove_reference<decltype(x)>::type &&>(x)
#define CFORWARD(T, x) static_cast<T &&>(x)

#else /* Pure C mode                                                           \
         --------------------------------------------------------*/

#include "cchrono.h"
#include "cerror_code.h"
#include "cmutex.h"
#include "coptional.h"
#include "crand.h"
#include "cstr.h"
#include "cuptr.h"
#include "cvector.h"

#include <stdatomic.h>
#include <time.h>

typedef cstr_t string_t;
typedef csupport_error_code_t errc_t;

#define CMOVE(x) (x)
#define CFORWARD(T, x) (x)

#define MO_RELAXED memory_order_relaxed
#define MO_RELEASE memory_order_release
#define MO_ACQUIRE memory_order_acquire

typedef int64_t chrono_sec;
typedef int64_t chrono_min;
typedef int64_t chrono_hrs;
typedef int64_t chrono_ns;
typedef int64_t chrono_us;
typedef int64_t chrono_ms;

static inline errc_t ec_errno(int e) { return csupport_ec_generic(e); }
static inline errc_t ec_sys(int e) { return csupport_ec_system(e); }
static inline int csupport_ec_ok(errc_t ec) { return ec.value == 0; }

#endif /* __cplusplus */

#endif /* CSUPPORT_CPP_COMPAT_STL_H */
