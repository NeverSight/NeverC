---
name: neverc-workflow-trust
description: >
  Keep NeverC GitHub workflows executing the trusted repository definition
  so a fork cannot replace the workflow file or the actions it runs. Use
  when editing .github/workflows, action pins, pull_request triggers,
  reusable workflows, or when the user mentions a workflow fork, an action
  tag, a commit SHA pin, or /neverc-workflow-trust.
---

# Workflow trust

The workflow file executed for NeverSight/NeverC is the file on the trusted
branch. A fork's `.github/workflows` copy is not that file. A tag or branch
on a third-party action is not a fixed action: the owner can move it onto
another commit.

`utils/ci/test_workflow_trust.py` owns the check. It reuses the trigger
reader in `utils/ci/sync-workflow-concurrency.py`. Read the current workflow
files before changing them. Run the test before finishing.

## Triggers

These events load the workflow from the pull request merge commit. A fork
controls that commit, so GitHub would execute the fork's workflow file:

- `pull_request`
- `pull_request_review`
- `pull_request_review_comment`

Do not add them.

`pull_request_target` and `workflow_run` load the workflow from the default
branch. Keep that definition:

- `pull_request_target` may pass the pull request number, head ref, head
  repository, and head SHA to `gh` as environment values. It must not check
  out a repository, the pull request head, or the merge ref, and it must not
  run a script taken from the pull request. `cancel-closed-pr-runs.yml` is
  the existing shape.
- `workflow_run` checks out `github.sha` or a commit the trusted trigger job
  has already bound, with `persist-credentials: false`. It must not check out
  `github.event.workflow_run.head_sha` or the triggering head repository.

`workflow_dispatch` and `workflow_call` run the workflow the caller selected
in this repository. `push` runs the pushed commit, so only a writer of this
repository can change the file that runs. A tag push that publishes a release
stays on that rule.

## Actions and reusable workflows

A third-party `uses:` value is `owner/repo@` plus the full 40-character
commit SHA already used for that action in `.github/`. Copy that SHA and its
version comment. The movable tags that remain in older steps resolve to these
commits: `actions/checkout` v4 `11d5960a326750d5838078e36cf38b85af677262`,
`actions/setup-python` v5.6.0 `a26af69be951a213d495a4c3e4e4022e16d87065`,
`actions/upload-artifact` v4 `ea165f8d65b6e75b540449e92b4886f43607fa02`,
`actions/download-artifact` v4.3.0 `d3f86a106a0bac45b974a628896c90dbdf5c8093`,
`actions/cache` v4.3.0 `0057852bfaa89a56745cba8c7296529d2fc39830`,
`actions/setup-go` v6.5.0 `924ae3a1cded613372ab5595356fb5720e22ba16`,
`ilammy/msvc-dev-cmd` v1.13.0 `0b201ec74fa43914dc39ae48a89fd1d8cb592756`,
`maxim-lobanov/setup-xcode` v1 `ed7a3b1fda3918c0306d1b724322adc0b8cc0a90`,
and `mozilla/sccache-action` v0.0.7
`054db53350805f83040bf3e6e9b8cf5a139aa7c9`. A step that already names a
different full SHA stays on that SHA. A fork of the action is a different
`owner/repo`; do not point a step at one.

A reusable workflow in this repository is
`uses: ./.github/workflows/<file>.yml` with no `@ref`. The caller and the
callee are then the same commit. A reusable workflow in another repository
uses a 40-character SHA.

A checkout that does not push sets `persist-credentials: false`, so the job
token is not written into the workspace git config.

## Finish

```bash
python3 -I -B utils/ci/test_workflow_trust.py
```

`.github/workflows/test-workflow-concurrency.yml` runs the same file. A
failure is a workflow that a fork can replace, or a test that no longer
rejects that shape. Repair the workflow. Do not weaken the test to allow the
trigger or the movable ref.
