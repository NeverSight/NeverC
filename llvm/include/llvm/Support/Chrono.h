//===- llvm/Support/Chrono.h - Utilities for Timing Manipulation-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CHRONO_H
#define LLVM_SUPPORT_CHRONO_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatProviders.h"

#include <chrono>
#include <ratio>
#include <time.h>

namespace llvm {

class raw_ostream;

namespace sys {

/// A time point on the system clock. This is provided for two reasons:
/// - to insulate us against subtle differences in behavior to differences in
///   system clock precision (which is implementation-defined and differs
///   between platforms).
/// - to shorten the type name
/// The default precision is nanoseconds. If you need a specific precision
/// specify it explicitly. If unsure, use the default. If you need a time point
/// on a clock other than the system_clock, use std::chrono directly.
template <typename D = std::chrono::nanoseconds>
using TimePoint = std::chrono::time_point<std::chrono::system_clock, D>;

// utc_clock and utc_time are only available since C++20. Add enough code to
// support formatting date/time in UTC.
class UtcClock : public std::chrono::system_clock {};

template <typename D = std::chrono::nanoseconds>
using UtcTime = std::chrono::time_point<UtcClock, D>;

/// Convert a std::time_t to a UtcTime
inline UtcTime<std::chrono::seconds> toUtcTime(std::time_t T) {
  using namespace std::chrono;
  return UtcTime<seconds>(seconds(T));
}

/// Convert a TimePoint to std::time_t
inline std::time_t toTimeT(TimePoint<> TP) {
  using namespace std::chrono;
  return system_clock::to_time_t(
      time_point_cast<system_clock::time_point::duration>(TP));
}

/// Convert a UtcTime to std::time_t
inline std::time_t toTimeT(UtcTime<> TP) {
  using namespace std::chrono;
  return system_clock::to_time_t(time_point<system_clock, seconds>(
      duration_cast<seconds>(TP.time_since_epoch())));
}

/// Convert a std::time_t to a TimePoint
inline TimePoint<std::chrono::seconds> toTimePoint(std::time_t T) {
  using namespace std::chrono;
  return time_point_cast<seconds>(system_clock::from_time_t(T));
}

/// Convert a std::time_t + nanoseconds to a TimePoint
inline TimePoint<> toTimePoint(std::time_t T, uint32_t nsec) {
  using namespace std::chrono;
  return time_point_cast<nanoseconds>(system_clock::from_time_t(T)) +
         nanoseconds(nsec);
}

} // namespace sys

raw_ostream &operator<<(raw_ostream &OS, sys::TimePoint<> TP);
raw_ostream &operator<<(raw_ostream &OS, sys::UtcTime<> TP);

/// Format provider for TimePoint<>
///
/// The options string is a strftime format string, with extensions:
///   - %L is millis: 000-999
///   - %f is micros: 000000-999999
///   - %N is nanos: 000000000 - 999999999
///
/// If no options are given, the default format is "%Y-%m-%d %H:%M:%S.%N".
template <> struct format_provider<sys::TimePoint<>> {
  static void format(const sys::TimePoint<> &TP, llvm::raw_ostream &OS,
                     StringRef Style);
};

template <> struct format_provider<sys::UtcTime<std::chrono::seconds>> {
  static void format(const sys::UtcTime<std::chrono::seconds> &TP,
                     llvm::raw_ostream &OS, StringRef Style);
};

namespace detail {
template <typename Period> struct unit {
  static const char value[];
};
template <typename Period> const char unit<Period>::value[] = "";

template <> struct unit<std::ratio<3600>> {
  static const char value[];
};
template <> struct unit<std::ratio<60>> {
  static const char value[];
};
template <> struct unit<std::ratio<1>> {
  static const char value[];
};
template <> struct unit<std::milli> {
  static const char value[];
};
template <> struct unit<std::micro> {
  static const char value[];
};
template <> struct unit<std::nano> {
  static const char value[];
};
} // namespace detail

/// Implementation of format_provider<T> for duration types.
///
/// The options string of a duration type has the grammar:
///
///   duration_options  ::= [unit][show_unit [number_options]]
///   unit              ::= `h`|`m`|`s`|`ms|`us`|`ns`
///   show_unit         ::= `+` | `-`
///   number_options    ::= options string for a integral or floating point type
///
///   Examples
///   =================================
///   |  options  | Input | Output    |
///   =================================
///   | ""        | 1s    | 1 s       |
///   | "ms"      | 1s    | 1000 ms   |
///   | "ms-"     | 1s    | 1000      |
///   | "ms-n"    | 1s    | 1,000     |
///   | ""        | 1.0s  | 1.00 s    |
///   =================================
///
///  If the unit of the duration type is not one of the units specified above,
///  it is still possible to format it, provided you explicitly request a
///  display unit or you request that the unit is not displayed.

template <typename Rep, typename Period>
struct format_provider<std::chrono::duration<Rep, Period>> {
private:
  typedef std::chrono::duration<Rep, Period> Dur;
  typedef std::conditional_t<std::chrono::treat_as_floating_point<Rep>::value,
                             double, intmax_t>
      InternalRep;

  template <typename AsPeriod> static InternalRep getAs(const Dur &D) {
    using namespace std::chrono;
    return duration_cast<duration<InternalRep, AsPeriod>>(D).count();
  }

  static std::pair<InternalRep, StringRef> consumeUnit(StringRef &Style,
                                                       const Dur &D) {
    using namespace std::chrono;
    if (Style.consume_front("ns"))
      return {getAs<std::nano>(D), "ns"};
    if (Style.consume_front("us"))
      return {getAs<std::micro>(D), "us"};
    if (Style.consume_front("ms"))
      return {getAs<std::milli>(D), "ms"};
    if (Style.consume_front("s"))
      return {getAs<std::ratio<1>>(D), "s"};
    if (Style.consume_front("m"))
      return {getAs<std::ratio<60>>(D), "m"};
    if (Style.consume_front("h"))
      return {getAs<std::ratio<3600>>(D), "h"};
    return {D.count(), detail::unit<Period>::value};
  }

  static bool consumeShowUnit(StringRef &Style) {
    if (Style.empty())
      return true;
    if (Style.consume_front("-"))
      return false;
    if (Style.consume_front("+"))
      return true;
    assert(0 && "Unrecognised duration format");
    return true;
  }

public:
  static void format(const Dur &D, llvm::raw_ostream &Stream, StringRef Style) {
    InternalRep count;
    StringRef unit;
    std::tie(count, unit) = consumeUnit(Style, D);
    bool show_unit = consumeShowUnit(Style);

    format_provider<InternalRep>::format(count, Stream, Style);

    if (show_unit) {
      assert(!unit.empty());
      Stream << " " << unit;
    }
  }
};

extern "C" {
void csupport_get_local_tm(int64_t epoch_sec, struct tm *out);
void csupport_get_utc_tm(int64_t epoch_sec, struct tm *out);
int csupport_expand_chrono_format(const char *style, size_t style_len,
                                  int64_t frac_ms, int64_t frac_us,
                                  int64_t frac_ns, char *out_buf,
                                  size_t out_buf_len);
}

namespace detail {
inline const char unit<std::ratio<3600>>::value[] = "h";
inline const char unit<std::ratio<60>>::value[] = "m";
inline const char unit<std::ratio<1>>::value[] = "s";
inline const char unit<std::milli>::value[] = "ms";
inline const char unit<std::micro>::value[] = "us";
inline const char unit<std::nano>::value[] = "ns";
} // namespace detail

namespace detail {
inline struct tm getStructTM(sys::TimePoint<> TP) {
  struct tm Storage;
  csupport_get_local_tm((int64_t)sys::toTimeT(TP), &Storage);
  return Storage;
}
inline struct tm getStructTMUtc(sys::UtcTime<> TP) {
  struct tm Storage;
  csupport_get_utc_tm((int64_t)toTimeT(TP), &Storage);
  return Storage;
}
template <class T>
inline void chrono_format_impl(const T &Fractional, struct tm &LT,
                               raw_ostream &OS, StringRef Style) {
  if (Style.empty())
    Style = "%Y-%m-%d %H:%M:%S.%N";
  using namespace std::chrono;
  int64_t frac_ms = (long)duration_cast<milliseconds>(Fractional).count();
  int64_t frac_us = (long)duration_cast<microseconds>(Fractional).count();
  int64_t frac_ns = (long)duration_cast<nanoseconds>(Fractional).count();
  char ExpandedFmt[256];
  csupport_expand_chrono_format(Style.data(), Style.size(), frac_ms, frac_us,
                                frac_ns, ExpandedFmt, sizeof(ExpandedFmt));
  char Buffer[256];
  size_t Len = strftime(Buffer, sizeof(Buffer), ExpandedFmt, &LT);
  OS << (Len ? Buffer : "BAD-DATE-FORMAT");
}
} // namespace detail

inline raw_ostream &operator<<(raw_ostream &OS, sys::TimePoint<> TP) {
  struct tm LT = detail::getStructTM(TP);
  char Buffer[sizeof("YYYY-MM-DD HH:MM:SS")];
  strftime(Buffer, sizeof(Buffer), "%Y-%m-%d %H:%M:%S", &LT);
  return OS << Buffer << '.'
            << format("%.9lu",
                      long((TP.time_since_epoch() % std::chrono::seconds(1))
                               .count()));
}

inline void format_provider<sys::UtcTime<std::chrono::seconds>>::format(
    const sys::UtcTime<std::chrono::seconds> &T, raw_ostream &OS,
    StringRef Style) {
  using namespace std::chrono;
  sys::UtcTime<seconds> Truncated(duration_cast<seconds>(T.time_since_epoch()));
  auto Fractional = T - Truncated;
  struct tm LT = detail::getStructTMUtc(Truncated);
  detail::chrono_format_impl(Fractional, LT, OS, Style);
}

inline void format_provider<sys::TimePoint<>>::format(const sys::TimePoint<> &T,
                                                      raw_ostream &OS,
                                                      StringRef Style) {
  using namespace std::chrono;
  sys::TimePoint<seconds> Truncated = time_point_cast<seconds>(T);
  auto Fractional = T - Truncated;
  struct tm LT = detail::getStructTM(Truncated);
  detail::chrono_format_impl(Fractional, LT, OS, Style);
}

} // namespace llvm

#endif // LLVM_SUPPORT_CHRONO_H
