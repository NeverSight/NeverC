# CI scheduling

NeverC and NeverD share the NeverSight organization runner pool. Standard
GitHub-hosted runner minutes are free for these public repositories, but the
Free plan has a shared limit of 20 concurrent jobs, including at most five
macOS jobs. Workflow cancellation does not limit the jobs inside a matrix.

## Builds and auxiliary validation

- The seven platform build workflows keep the newest run of each workflow/ref.
  `CI_CANCEL_IN_PROGRESS=false` is an explicit opt-out from cancellation.
- Markdown-only pushes and pull requests skip the platform builds; `lint-docs`
  continues validating documentation.
- Python plugin bindings, plugin conformance, and the compiler frontend archive
  audit run at most two matrix jobs at once. Every existing matrix entry remains
  enabled. The Python-disabled regression is a separate job and may run alongside
  the two bindings jobs.
- Markdown-only changes do not trigger the Python plugin compiler builds.
- Release workflows keep their existing publication and cancellation policies.

## Optional GitHub scanning

CodeQL default setup and Code Quality analysis are disabled in this repository's
GitHub settings. These automatically generated workflows are controlled outside
`.github/workflows`; editing CI concurrency alone cannot disable them. Repository
administrators can re-enable them from Settings when scanning is needed.
