#ifndef NEVERC_FOUNDATION_EXCEPTIONSPECIFICATIONTYPE_H
#define NEVERC_FOUNDATION_EXCEPTIONSPECIFICATIONTYPE_H

namespace neverc {

/// Exception specification kinds for function types.
/// Stored in FunctionTypeBitfields::ExceptionSpecType (3 bits).
enum ExceptionSpecificationType {
  EST_None = 0,             ///< no exception specification
  EST_DynamicNone = 1,      ///< throw()
  EST_Dynamic = 2,          ///< throw(T1, T2, ...)
  EST_MSAny = 3,            ///< Microsoft throw(...)
  EST_NoThrow = 4,          ///< __attribute__((nothrow)) / __declspec(nothrow)
  EST_BasicNoexcept = 5,    ///< noexcept / noexcept(true)
  EST_DependentNoexcept = 6,///< noexcept(expression) not yet evaluated
  EST_NoexceptFalse = 7,    ///< noexcept(false)
};

inline bool isNoexceptExceptionSpec(ExceptionSpecificationType EST) {
  return EST == EST_BasicNoexcept || EST == EST_DependentNoexcept ||
         EST == EST_NoexceptFalse;
}

inline bool isComputedNoexcept(ExceptionSpecificationType EST) {
  return EST == EST_DependentNoexcept || EST == EST_NoexceptFalse ||
         EST == EST_BasicNoexcept;
}

} // end namespace neverc

#endif // NEVERC_FOUNDATION_EXCEPTIONSPECIFICATIONTYPE_H
