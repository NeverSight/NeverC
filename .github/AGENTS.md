# Workflows

A workflow that runs in this repository is the file on the trusted branch. A
fork's copy must not replace that file, and a movable action tag must not
replace a step.

Read the [workflow trust skill](../.agents/skills/neverc-workflow-trust/SKILL.md)
before editing these files. Keep `utils/ci/test_workflow_trust.py` passing.
Do not loosen that test to accept a fork-controlled trigger or a movable
`uses:` ref.
