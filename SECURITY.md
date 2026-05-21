# Security Policy

NeverC is a security-research-oriented C23 compiler. We take reports of
defects in the **toolchain itself** seriously — especially issues that let
untrusted inputs compromise the machine running `neverc`, or that cause
incorrect or unsafe code generation beyond what the user asked for.

This policy does **not** cover misuse of NeverC to produce offensive tooling;
that is outside the scope of coordinated disclosure here.

---

## Supported Versions

Security fixes are applied to the default branch (`dev`) and, when
practical, cherry-picked to the latest release tag. Pre-release or
unsupported branches may not receive patches.

| Version | Supported |
|---------|-----------|
| Latest release tag (`v*`) | Yes |
| `dev` | Yes |
| Older tags | Best effort only |

---

## Reporting a Vulnerability

**Please do not open a public GitHub issue for security-sensitive reports.**

### Preferred channel

Use [GitHub Private Vulnerability Reporting](https://github.com/NeverSight/NeverC/security/advisories/new)
for this repository. That keeps details confidential until a fix is ready.

If private reporting is unavailable, contact the maintainers through an
existing private channel you already use with the project (do not post
exploit details or proof-of-concept code in public issues, pull requests,
or discussions).

### What to include

A strong report helps us reproduce and fix faster:

1. **Summary** — what breaks, and the impact (e.g. heap overflow in the
   driver, miscompiled bounds check, crash on malformed IR).
2. **Affected component** — compiler frontend, linker (COFF / ELF / Mach-O),
   shellcode pipeline, driver, etc.
3. **NeverC version or commit** — output of `neverc --version` or the git SHA.
4. **Host platform** — OS, architecture, and how `neverc` was built.
5. **Minimal reproduction** — smallest source file, flags, and command line.
6. **Proof of concept** — crash log, ASan/Valgrind output, or disassembly
   diff showing wrong codegen (attach only what is needed).
7. **Suggested severity** (optional) — your view of exploitability.

### What we need from you

- Good-faith research: do not access systems or data you do not own.
- Give us reasonable time to investigate and ship a fix before public
  disclosure (see timeline below).
- Do not exploit issues against third parties.

---

## In Scope

Reports we treat as security issues include, but are not limited to:

| Area | Examples |
|------|----------|
| **Compiler / linker crashes** | Memory corruption, stack overflow, or UAF when parsing or linking **untrusted** inputs (malformed source, object files, archives, linker scripts). |
| **Incorrect codegen** | The compiler or linker emits code that violates the language ABI or documented semantics for **valid** input, in a way that could plausibly weaken memory safety or control flow without the user opting into unsafe behavior. |
| **Shellcode pipeline integrity** | Bypass of documented constraints (e.g. `-fshellcode-bad-bytes`, documented PIC rules) that causes the tool to **accept** output that violates those checks, or silent corruption of the emitted binary. |
| **Path and file handling** | Directory traversal, arbitrary file read/write, or unsafe symlink behavior in the driver when given attacker-controlled paths or response files. |
| **Supply chain / build** | Compromise of official release artifacts, reproducible-build breaks that hide tampering, or critical secrets embedded in distributed binaries. |
| **Dependency issues** | Vulnerabilities in bundled third-party code **as shipped in NeverC releases**, when exploitable through normal `neverc` use. |

---

## Out of Scope

The following are generally **not** treated as security vulnerabilities:

- **Intended research features** — generating shellcode, syscall stubs, PEB
  import tables, kernel-mode shellcode, or cross-platform binaries when the
  user explicitly requests them.
- **User-controlled malicious source** — compiling attacker-written C that
  deliberately exploits itself or others; that is expected capability.
- **Missing mitigations in user output** — e.g. no stack canaries or ASLR
  in a minimal `-fshellcode` blob unless NeverC documented that it would
  provide them and failed to do so.
- **Denial of service** via extremely large inputs without a plausible
  security impact (still welcome as regular bugs).
- **Issues in LLVM upstream** — please report those to the LLVM project;
  we may still track NeverC-specific triggers or workarounds.
- **Social engineering, physical access, or third-party game / anti-cheat
  systems** — outside this toolchain’s threat model.

When in doubt, report privately anyway; we will clarify scope in the reply.

---

## Response Timeline

We aim to:

| Stage | Target |
|-------|--------|
| Initial acknowledgement | Within **72 hours** |
| Triage and severity assessment | Within **7 days** |
| Fix or mitigation plan | Depends on complexity; critical issues prioritized |
| Coordinated disclosure | After a fix is available on `dev` and, when applicable, a release |

We may ask for more information or offer a draft advisory for your review
before publication.

---

## Disclosure Policy

- We prefer **coordinated disclosure**: work with us on a fix before public
  release of details.
- Credit will be given in release notes or the GitHub Security Advisory
  unless you prefer to remain anonymous.
- We do not pursue legal action against researchers who follow this policy
  in good faith.

---

## Safe Harbor

We support responsible security research on NeverC builds you own or have
permission to test. Research conducted in line with this policy — private
report, no harm to third parties, reasonable disclosure timing — will not
be treated as an attack on our infrastructure.

---

## Hardening Recommendations for Users

NeverC is a powerful toolchain. Operators should:

- Treat **compiler inputs and build scripts** like code execution: only
  compile untrusted sources in isolated environments (VM, container, CI
  sandbox).
- Verify release artifacts against tagged sources when reproducibility
  matters.
- Do not run `neverc` with elevated privileges unless required.
- Review generated shellcode and linked binaries before deployment in
  sensitive environments.

---

## Security Updates

Fixed vulnerabilities will be announced via GitHub Security Advisories and
noted in release notes for tagged releases. Watch the repository releases
or enable GitHub security notifications for updates.
