---
name: writing-plans
description: Write an implementation plan from an established specification. Use when the user asks for a plan or when complex work needs a durable handoff before implementation.
---

# Writing implementation plans

Create a plan another engineer can execute without reconstructing the design. Do not require a plan for a small, local, reversible change unless the user asks for one.

Save durable plans under `docs/superpowers/plans/YYYY-MM-DD-<topic>.md` unless the user selects another location. A short plan can stay in the conversation when it is only coordinating the current task.

## Plan content

Start with the outcome, design approach, affected boundaries, and relevant technologies. Then divide the work into independently verifiable tasks. For each task, name exact files, the intended change, meaningful checks, and dependencies on earlier tasks.

Include code only when an interface, invariant, or non-obvious algorithm must be pinned down. Use exact commands when the repository defines them; otherwise identify the check and expected evidence without inventing brittle output. Match task granularity to the work rather than forcing every edit into fixed-duration steps or a commit.

Call out:

- product or API decisions that remain unresolved;
- migrations, compatibility risks, and rollback needs;
- test coverage that proves behavior rather than wording or implementation shape;
- documentation or release work required for completion.

## Review and handoff

Review the plan against the specification for missing behavior, incorrect paths, hidden assumptions, and unverifiable completion criteria. Use an independent reviewer when the plan is large, risky, or intended for delegation; ordinary plans can be self-reviewed.

If the user asked only for planning, finish by linking the plan and summarizing the main decisions. If implementation is already authorized, continue with the plan instead of stopping to ask whether to proceed.
