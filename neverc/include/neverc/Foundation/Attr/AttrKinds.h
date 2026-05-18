#ifndef NEVERC_BASIC_ATTRKINDS_H
#define NEVERC_BASIC_ATTRKINDS_H

namespace neverc {

namespace attr {

// A list of all the recognized kinds of attributes.
enum Kind {
#define ATTR(X) X,
#define ATTR_RANGE(CLASS, FIRST_NAME, LAST_NAME)                               \
  First##CLASS = FIRST_NAME, Last##CLASS = LAST_NAME,
#include "neverc/Foundation/AttrList.td.h"
};

} // end namespace attr
} // end namespace neverc

#endif
