---
name: systematic-debugging
description: Diagnose reproducible bugs and test failures by gathering evidence, isolating the failing boundary, and testing a root-cause hypothesis. Use when the cause is unclear or an attempted fix did not work.
---

# Systematic debugging

Find the failing boundary before changing behavior. Scale the investigation to the problem: a clear compiler error may need one inspection, while an intermittent cross-process failure needs controlled reproduction and evidence from each layer.

## Workflow

1. Read the complete error and identify the exact failing command, process, or assertion.
2. Reproduce at the smallest faithful boundary. Record whether the failure is deterministic and which environment or input is required.
3. Inspect recent relevant changes and compare with a nearby working path.
4. Trace the bad value or state backward to its owner. In a multi-component flow, capture inputs and outputs at the boundary most likely to distinguish competing causes.
5. State one falsifiable hypothesis and test it with the smallest useful experiment.
6. Fix the authoritative cause, add a regression test when it protects meaningful behavior, and rerun the affected checks.

Do not stack speculative fixes. If a hypothesis fails, restore or separate the experimental change and use the new evidence to form the next hypothesis. After repeated failures, reassess shared state, ownership, and architecture before adding another patch; involve the user only when the next step requires a product decision, new authority, or an expensive external action.

## Supporting techniques

Read only the reference needed by the current failure:

- [Root-cause tracing](root-cause-tracing.md) for a bad value deep in a call chain.
- [Condition-based waiting](condition-based-waiting.md) for timing and polling failures.
- [Defense in depth](defense-in-depth.md) after the root cause is known and multiple trust boundaries need validation.
- [Polluter finder](find-polluter.sh) for order-dependent test contamination.

## Completion

Report the observed cause, the evidence that distinguishes it from plausible alternatives, the fix or recommended fix, and the checks actually run. If diagnosis was requested without implementation, stop after the evidence-backed explanation.
