---
name: release-recovery
audience: end-user
description: |
  Diagnose and repair a broken NarshaMCP install — stale `.mcp.json` paths after engine
  swap (scenario F: launcher UE removed, source-built UE adopted), zombie daemons, dead
  registry entries, and bundle a masked support report when auto-recovery fails.
  Triggers on '릴리즈 설치 복구', 'mcp 안 켜져요', 'narshamcp 연결 안됨', 'release recovery',
  'mcp not connecting', 'zombie daemon', '오류 보고', 'support bundle'.
triggers_ko:
  - "MCP 안 켜져요"
  - "릴리즈 설치 복구"
  - "데몬 좀비"
  - "오류 보고"
  - "narshamcp 연결 안됨"
  - "엔진 경로 바꿨는데"
  - "런처 지웠는데"
triggers_en:
  - "mcp not connecting"
  - "release recovery"
  - "zombie daemon"
  - "send error report"
  - "stale engine path"
  - "support bundle"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
---

# release-recovery

Self-service diagnostic + repair flow for end-user installs of NarshaMCP. Issue [#11118](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/11118).

## When to invoke

The user reports any of:
- Claude Code can't reach `narshamcp` after a recent change to the UE install (most common: **launcher UE removed, source-built UE adopted** — scenario F).
- The daemon process is stuck / zombie and a fresh start fails.
- They want to send a useful bug report but don't know what to attach.

## Decision flow

```
START
  │
  ├── Phase A: diagnose.py
  │     - inspect .mcp.json (project + Cursor + Claude Desktop user-global)
  │     - Layer 1: format check (mcp_json_path_guard rules R1-R5)
  │     - Layer 2: filesystem existence (catches scenario F stale paths)
  │     - emit JSON report (severity OK | WARN | CRITICAL, scenarios A-F)
  │
  ├── (if --diagnose-only) → STOP
  │
  ├── Phase B: recover.py --apply (gated)
  │     - resolve fresh engine path (env → ini → uproject hint → prompt)
  │     - run `narshamcp setup --mcp-json-only --project-path X --engine-path Y`
  │     - taskkill zombies (explicit yes/no per PID)
  │     - prune stale daemon registry
  │     - hand off to /mcp-restart
  │
  ├── (if Phase A still FAIL after Phase B) or (if user asks) →
  │     Phase C: bundle.py
  │     - mask + zip support bundle
  │     - optional --send-telemetry → ue_telemetry(send_error_report)
  │     - print report_id + bundle path
  │
  └── END
```

## CLI

```
/release-recovery [--diagnose-only|--bundle-only|--apply] [--send-telemetry] [--no-mask]
```

| Flag | Effect |
|------|--------|
| (none) | Phase A + dry-run plan of Phase B (no mutations) |
| `--diagnose-only` | Phase A only |
| `--apply` | Phase A + execute Phase B (with confirmation prompts) |
| `--bundle-only` | Phase A + Phase C only (no recovery) |
| `--send-telemetry` | Phase C also submits payload via `ue_telemetry` |
| `--no-mask` | Skip masking (developer escape hatch — incompatible with `--send-telemetry`) |

## How the orchestrator runs it

The skill is a thin wrapper that pipes the three scripts:

```bash
# Phase A — diagnose (always)
python .claude/skills/release-recovery/scripts/diagnose.py \
  --project-dir "$PWD" --json > /tmp/rr-report.json

# Read severity from the report
SEV=$(jq -r '.severity' /tmp/rr-report.json)

# Phase B — recover (if --apply and severity != OK)
python .claude/skills/release-recovery/scripts/recover.py \
  --report /tmp/rr-report.json --apply --interactive-engine-resolve

# Re-diagnose
python .claude/skills/release-recovery/scripts/diagnose.py \
  --project-dir "$PWD" --json > /tmp/rr-report-after.json

# Phase C — bundle (if still FAIL or --bundle-only)
python .claude/skills/release-recovery/scripts/bundle.py \
  --report /tmp/rr-report.json [--send-telemetry] [--no-mask]
```

When the bundle script emits a telemetry payload, the orchestrator feeds that JSON to `ue_telemetry(operation="send_error_report")` and reports the returned `report_id` to the user. The skill **never** auto-creates a GitHub issue (Rule 15 + safety) — it prints `gh issue create --body-file <bundle>/report.md` as a copy-paste hint.

## Scenario coverage

| Code | Scenario | Phase A signal | Phase B fix |
|------|----------|----------------|-------------|
| **F** | Launcher UE removed → stale `--engine-path` | `stale_path` + `kind=engine-path` + `exists_on_fs=False` | Resolve via env/ini/uproject/prompt + `setup --mcp-json-only` |
| A | Upgrade left stale `.mcp.json` (Build.cs PostBuildSteps didn't fire) | `mcp_json_invalid` (format) or `stale_path` | `setup --mcp-json-only` |
| B | User hand-edited to relative form | `mcp_json_invalid` + scenario tag "B" | `setup --mcp-json-only` |
| C | Project moved on disk | `stale_path` + `kind=project-path` | `setup --mcp-json-only` from new cwd |
| D | Claude Desktop user-global missing narshamcp entry | (no project-local issue) | `setup --target=claude-desktop` |
| E | Fab install + first build never ran | `.mcp.json` absent | Prompt: build the project first |

## Privacy & telemetry contract

- The masking module (`scripts/masking.py`) is the SSOT — applied to every file before it lands in the bundle and again before any telemetry submission.
- `--send-telemetry` and `--no-mask` are mutually exclusive (hard fail in `bundle.py`). Bundle ZIPs intended for upload **must** be masked.
- The Rust-side `ue_telemetry::send_error_report` re-validates with `masking.contains_obvious_pii` and rejects payloads that still match.
- Telemetry payload TTL: 90 days (per [#11118](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/11118) plan §8c R3). Payload cap: ~1 MB. Client-side rate limit: 1/hour per install.

## Tests

- `scripts/test_scenario_f.py` — 11 cases: healthy baseline, scenario F primary, adjacent stale-path cases, format violation B (must NOT be confused with F), placeholder unresolved, engine-resolve chain, JSON schema contract.
- `scripts/test_masking.py` — 22 cases: static patterns, project/engine label override, case-insensitive Windows drive, idempotency, `contains_obvious_pii` round-trip, edge cases.

```bash
python -m pytest .claude/skills/release-recovery/scripts/ -v
```

## Related skills (do NOT auto-invoke; cross-references)

- `/mcp-doctor` — broader MCP diagnosis (multi-instance, registry, hung process). release-recovery focuses on the `.mcp.json` + scenario F gap.
- `/mcp-restart` — daemon restart after Phase B.
- `/session-health-check` — `ue_check_health` + PDB indexing + orphan check.
- `/release-package-tester` — developer-side release ZIP install verification (audience: developer; not bundled in release).
- `/deploy-test` — developer-side full deploy cycle (audience: developer).

## Out of scope

- Editing `mcp_json.rs` / `paths.rs` (Issue #9291 already covers absolutize on the Rust side; `setup --mcp-json-only` reuses that pipeline).
- Auto-creating GitHub issues — Rule 15 + safety. The skill emits a ready-to-paste body; the user runs `gh issue create`.
- Resolving `.uproject` `EngineAssociation` GUIDs against the Epic Games Launcher registry — deferred to a follow-up (R7 in plan §8c).

## Changelog

- v0.1.0 (2026-05-07, Issue #11118): initial release. Phase A diagnose + scenario F detection, Phase B recover with `setup --mcp-json-only`, Phase C bundle + masking + optional telemetry.
