# CI safety audit — qThrone

Audited 2026-10-13 at source `c237aea2aabdb6e6c85ec89598d90760ae95874c`; default branch `qthrone`.
GitHub API confirmed repository Actions **disabled** before preparation.
Scope: inherited workflow entry points and referenced build, packaging, installer,
artifact verification and release scripts. This is a targeted CI audit, not a full
application or dependency security audit. No application code or license changed.

## Inherited workflows

All originals are preserved byte-for-byte in `.github/workflows-disabled/`.
GitHub only discovers workflow YAML under `.github/workflows/`; the archived files
are reference material, not runnable entry points at this revision.

| Workflow | Triggers / token | Effects | Decision |
| --- | --- | --- | --- |
| `arch-package.yml` | packaging/archlinux path-filtered push / manual; contents:read | Rolling Arch container, package build/install/remove, GUI launch; binary/log uploads including hidden files | Archive; package installation and artifact publication unnecessary here. |
| `build.yml` | Manual; job-level read permissions, release contents:write, Pages pages:write/id-token:write | Large Go/Rust/Qt/Android matrix, toolchain downloads/cache, binary artifacts; GitHub releases; AUR and signed APT/RPM Pages | Archive entire workflow. AUR/Pages jobs already restrict repository to throneproj/Throne, but release/artifact jobs do not. publish empty still uploads binaries. |
| `csqtt-transport.yml` | Manual; contents:read | Fetch pinned external source, patch, Rust tests/build; upload binaries/source bundle | Archive; transport builds are outside this stage. No additional transport/license investigation performed. |
| `data-preservation.yml` | Path-filtered pushes to security/data-preservation and qthrone / manual; contents:read | Fetch release snapshots, apt install Qt, compile/run database test | No publishing, but external downloads and system packages; archive for later isolated test review. |
| `qwdtt-tests.yml` | qthrone push / PR with path filters / manual; contents:read | Three OS Go race/vet/updater tests; dependency/toolchain downloads, no artifact upload | Candidate for future test CI; archive now to avoid enabling a large inherited matrix in the initial static-only stage. |
| `verify_upgrade.yml` | Manual; release job contents:write, actions:read | Reuse previous artifacts, download/run previous Windows release, publish validated release (possibly latest) | Archive; validation is coupled to publication and requires previous releases. |

No inherited `pull_request_target` or `workflow_run` entry point was found.
Third-party actions/toolchains and container tags need a separate pinning review
before restoring inherited pipelines. No such broad modernization is attempted here.
Called scripts are retained unchanged; archiving a workflow does not make those
scripts safe to run manually. In qThrone, build/pack scripts write deployment
directories and upgrade tests execute downloaded programs; in OpenFlux the
provision integration is opt-in via OPENFLUX_TEST_SSH and the distro Dockerfile
sets synthetic SSH passwords. Neither is executed by the new CI.

## Minimal replacement

`source-safety.yml` is JSON-form YAML (valid GitHub workflow syntax), allowing
validation with Python standard library only. It runs on PRs to `qthrone`
and pushes to `qthrone` / `experimental/keenetic-arm64`, without path filters.
One hosted Ubuntu job, five-minute limit, contents:read only; SHA-pinned checkout,
no persisted credentials, no submodules/LFS, no secrets, cache, artifact uploads,
release, package, Pages, deployment, sudo or repository-code execution.

It checks whitespace errors in the changed source diff and parses changed Python
and JSON files without importing/executing them. Other source languages receive
the whitespace check only. This is **not** Go/C++/Rust compilation, unit testing,
runtime acceptance or Keenetic compatibility evidence. Checkout uses full history
to resolve comparison commits; application dependencies are not downloaded.
For a new branch with an all-zero before SHA, compare against the empty tree.

## Safe future activation — owner action required

1. Keep repository Actions disabled while these Draft PRs are reviewed. Changes
   on the experimental branch do **not** neutralize workflows on the default branch
   or old tags/branches. Do not enable Actions merely to test this PR.
2. Separately approve and merge the safety changes through the existing protected
   default-branch PR process. This preparation does not perform that merge.
3. Before enabling, inventory all historical refs and workflow IDs. Disable all
   inherited workflow IDs at repository level (including archived/old-ref versions).
   Old tag workflows can otherwise remain a publication route. Do not dispatch or
   push old tags. If this cannot be verified, leave repository Actions disabled.
4. Set the default GITHUB_TOKEN to read-only and PR write/approval permission off;
   allow only reviewed actions pinned to full SHAs. Keep publication credentials,
   signing keys, deployment secrets and external publishing integrations absent.
   Repository action allowlists govern actions, not arbitrary shell commands; they
   do not replace disabling historical workflows. Review every workflow change.
5. Enable only the new static workflow, then verify its actual PR/push runs, token
   permissions and absence of artifacts/releases/deployments. Audit any queued or
   unexpected runs before proceeding. Add no required status checks until the
   workflow is in the default branch and its real check name/runs are verified.
   Preserve the solo-maintainer zero-review policy; no Rulesets changed here.
6. Restore individual test workflows only after a separate scoped review. Release,
   nightly cleanup, package publication and deployment remain disabled until
   separately authorized and designed with explicit owner admission.

## Verification and limits

Local validation: parse the replacement workflow as JSON, compile its embedded
Python without execution, run the source checker against the prepared diff, verify
archived workflow hashes below and ensure only workflow paths/documentation change.
GitHub CI is intentionally **NOT RUN** because Actions remain disabled. A local
static check is not a successful GitHub Actions run. Re-read Actions permissions,
default SHA and effective branch rules after opening the Draft PR.

Original Git blob SHA256 (archive integrity; before checkout newline conversion):

```text
fd2e42b6b2be8d49709ea2a5d490572001610c67626cbcf7b61cdfe17a5b3893  arch-package.yml
6a837041b5ce9b01c29eb206060d0f2fc74b3d5f0cc85a7fec6a7f171e7ea64f  build.yml
70d7a72c5344c750a066508b33e0033ee666c5134c1c89cb56e099567e0dc469  csqtt-transport.yml
889afbff33ba7b3622dadd74e05e6157df8bfb0c34979423becfed5bd68ae005  data-preservation.yml
c7129b0ecbe82e8acf3488f90377d8acd41749e8a76e369e595b402b068f29b3  qwdtt-tests.yml
c3509dabc52818f2315d908b584c1ce258ac2d497d9fbb86d8961cc98cfa5676  verify_upgrade.yml
```
