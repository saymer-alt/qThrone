# Rawline upstream sync (safe, manual)

This repository (`saymer-alt/qThrone`) is **already** a genuine fork of
`ttrroyy/Rawline` (previously `ttrroyy/qThrone`). GitHub will not allow
creating another fork in the same user account.

## Branch ownership

| Ref in this repository | Purpose | Sync policy |
| --- | --- | --- |
| `qthrone` (protected, default) | Our existing qThrone changes | **Never auto-sync** |
| `dev` | Existing work | **Never auto-sync** |
| `experimental/keenetic-arm64` | Keenetic experiments | **Never auto-sync** |
| `upstream/rawline-dev` | Exact tracking snapshot of `ttrroyy/Rawline:dev` | Manual fast-forward only |
| `maintenance/rawline-upstream-sync` | Reviewable maintenance PR | Temporary review branch |

## Updating upstream tracking

After the workflow is merged into the protected `qthrone` branch, manually run:

1. Open **Actions → Sync Rawline upstream (manual only)**.
2. Click **Run workflow**, select `qthrone`, then confirm.
3. Inspect the run log and compare `upstream/rawline-dev` with `qthrone`.
4. Import only specifically reviewed code into a separate feature branch and PR.
   **Do not use** GitHub's ordinary **Sync fork** on the protected `qthrone`
   branch; the upstream default is `dev` and our histories differ.

CLI equivalent (only after merge):

```bash
gh workflow run sync-rawline-upstream.yml --repo saymer-alt/qThrone --ref qthrone
gh run list --repo saymer-alt/qThrone --workflow sync-rawline-upstream.yml --limit 3
```

## Safety contract

- The workflow runs **only** via `workflow_dispatch`: no cron, PR, push, release, build or deploy.
- It updates only `refs/heads/upstream/rawline-dev`. It never updates `qthrone`, `dev`, experimental branches, a release tag, or any Keenetic router.
- It **rejects** history rewrites / non-fast-forward updates. Never use `force=true` to bypass this.
- Uses only the workflow-scoped GitHub token; no extra credentials or secrets.
- Can fail due to restricted GitHub Actions minutes, token write permissions, or repository policies. A failed workflow does **not** mean the source branch was safely updated.
- Existing fork-specific changes must be reviewed/cherry-picked in a **separate PR**, never automatic merged with upstream.
- If GitHub Actions has a $0 usage cap, do **not** raise spending limits solely for this. The tracking branch can instead be advanced by a user-authorized API/client fast-forward.

This document/workflow changes no application runtime behavior.
