---
name: codex-project-bootstrap
description: "Check and safely repair per-PC Codex setup for this repository. Use after a fresh clone, on a new PC, when pre-commit hooks or uv PATH resolution fail, or when Codex reports project-hook trust drift. Do not use for UE project creation, generic onboarding, or ordinary pre-commit execution."
argument-hint: "[--check] [--json] | --apply --yes"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
audience: end-user
---

# Codex Project Bootstrap

Use this workflow to diagnose machine-local prerequisites without changing the
repository or operator trust. Read [REFERENCE.md](REFERENCE.md) when a row is not
`PASS`.

## Check

1. Resolve a Python launcher in this order: `py -3`, `python3`, `python`.
2. Run `-c "import sys; print('.'.join(map(str, sys.version_info[:3])))"` with each
   candidate. Require Python 3.13 or newer, matching `pyproject.toml`. If none
   qualifies, report Python=`BLOCKED` with platform installation guidance. Do not
   attempt to launch the Python checker or install Python.
3. With the supported launcher, run:

   ```text
   <python-launcher> .claude/skills/codex-project-bootstrap/scripts/bootstrap.py --check
   ```

4. Report every `PASS`, `WARN`, and `BLOCKED` row. Treat missing gitleaks as the
   single optional `WARN`; do not install it. Treat project trust and hook trust as
   manual boundaries. Direct the operator to review `/hooks` when requested.

Use `--json` for machine-readable output. Check mode is read-only; do not replace
it with installer commands.

## Apply

1. Run the check and show the proposed machine-local changes.
2. Obtain explicit operator consent. Never infer consent from the request to
   diagnose setup.
3. Only after consent, run:

   ```text
   <python-launcher> .claude/skills/codex-project-bootstrap/scripts/bootstrap.py --apply --yes
   ```

4. Preserve custom `core.hooksPath`, Git-LFS legacy bytes, Codex project trust,
   and Codex hook approvals. Never write a trust level, trusted hash, or bypass.
5. Treat a nonzero exit or any final `BLOCKED` row as incomplete. Apply succeeds
   only after its independent post-check passes the automatable rows.

Apply may run only `uv tool install pre-commit`, `uv tool update-shell`, and the
repository hook installer. It removes inherited gitleaks autoinstall permission
from that child and does not provision gitleaks.

## SessionStart

SessionStart may run only:

```text
<python-launcher> .claude/skills/codex-project-bootstrap/scripts/bootstrap.py --session-start
```

This performs the lightweight check, always exits zero, and emits at most one
route message per repository/session. It never changes trust.

Since Issue #23245 it also performs ONE repair: when a required `git_hook_*` row is
BLOCKED it reinstalls the missing hooks, forwarding the installer's own output so the
report says what the machine actually did. That repair is required-hook-only (an
unrelated incomplete row leaves the machine untouched), **hooks-only** (it runs the
installer with provisioning disabled, so SessionStart rewrites hook files but never
installs software), and fail-open (a failed install still exits zero). It exists
because a machine with no `prepare-commit-msg` hook produces unattributable commits
silently, which is invisible precisely on the machines nobody is watching.

First-time `pre-commit` provisioning belongs to the runtime-neutral gate
`.claude/hooks/checks/provenance_selfcheck.py` instead — it runs from an explicit
skill step (issue-start Phase 0.4d / pr-create) rather than a 60s-budgeted session
hook, so a slow network install cannot be killed midway and leave a half-finished
`uv` grandchild behind.
