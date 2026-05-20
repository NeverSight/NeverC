#ifndef NEVERC_FOUNDATION_EXCEPTIONSPECIFICATIONTYPE_H
#define NEVERC_FOUNDATION_EXCEPTIONSPECIFICATIONTYPE_H

namespace neverc {

enum ExceptionSpecificationType {
  EST_None,    ///< no exception specification
  EST_NoThrow, ///< __attribute__((nothrow)) / __declspec(nothrow)
};

} // end namespace neverc

#endif // NEVERC_FOUNDATION_EXCEPTIONSPECIFICATIONTYPE_H
