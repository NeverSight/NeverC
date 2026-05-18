#ifndef NEVERC_SEMA_ENTEREXPRESSIONEVALUATIONCONTEXT_H
#define NEVERC_SEMA_ENTEREXPRESSIONEVALUATIONCONTEXT_H

#include "neverc/Analyze/Sema.h"

namespace neverc {

class EnterExpressionEvaluationContext {
  Sema &Actions;

public:
  EnterExpressionEvaluationContext(Sema &Actions,
                                   Sema::ExpressionEvaluationContext NewContext)
      : Actions(Actions) {
    Actions.PushExpressionEvaluationContext(NewContext);
  }

  ~EnterExpressionEvaluationContext() {
    Actions.PopExpressionEvaluationContext();
  }
};

} // namespace neverc

#endif
