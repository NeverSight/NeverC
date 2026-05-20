#ifndef NEVERC_INVOKE_PHASES_H
#define NEVERC_INVOKE_PHASES_H

namespace neverc {
namespace driver {
namespace phases {
enum ID {
  Preprocess,
  Compile,
  Backend,
  Assemble,
  Link,
};

enum { MaxNumberOfPhases = Link + 1 };

const char *getPhaseName(ID Id);

} // end namespace phases
} // end namespace driver
} // end namespace neverc

#endif
