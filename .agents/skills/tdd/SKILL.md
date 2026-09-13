---
name: tdd
description: Build a feature or fix through short red-green-refactor cycles. Use when the user explicitly requests TDD, test-first development, or a red-green-refactor workflow.
---

# Test-driven development

Work in vertical slices: prove one observable behavior with a failing test, implement the smallest coherent change that passes it, then continue with the next behavior. Preserve the user's chosen public interface and existing test conventions.

## Cycle

1. Identify the next externally observable behavior and the narrowest stable test boundary.
2. Write one meaningful test and run it to confirm it fails for the intended reason.
3. Implement enough production code to satisfy that behavior without speculative features.
4. Run the focused test, then the affected suite when the change warrants it.
5. Refactor while green when doing so improves ownership, naming, or duplication.

Prefer tests that survive internal refactoring. Mock only at real process, network, time, or nondeterministic boundaries. A test that merely mirrors an implementation detail does not provide useful protection.

Read supporting guidance only when the current design needs it:

- [Test design](tests.md)
- [Mocking boundaries](mocking.md)
- [Interface design](interface-design.md)
- [Deep modules](deep-modules.md)
- [Refactoring while green](refactoring.md)

Do not pause for approval between routine cycles when the requested behavior and interface are already clear. Ask only when a missing product or API decision would materially change the result.
