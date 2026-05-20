#ifndef NEVERC_ANALYZE_SEMACONSUMER_H
#define NEVERC_ANALYZE_SEMACONSUMER_H

#include "neverc/Tree/Core/TreeConsumer.h"

namespace neverc {
class Sema;

class SemaConsumer : public TreeConsumer {
  virtual void anchor();

public:
  SemaConsumer() { TreeConsumer::SemaConsumer = true; }

  virtual void InitializeSema(Sema &S) {}

  virtual void ForgetSema() {}

  // isa/cast/dyn_cast support
  static bool classof(const TreeConsumer *Consumer) {
    return Consumer->SemaConsumer;
  }
};
} // namespace neverc

#endif
