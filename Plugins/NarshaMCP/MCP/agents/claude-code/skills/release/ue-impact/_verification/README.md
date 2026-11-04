# `_verification/` — Empirical Grounding for `ue-impact`

> **Purpose**: Change impact analysis for UE entities — traces callers, subclasses, Blueprint references, config deps.

This directory holds **evidence** that `ue-impact` was authored against
*real* MCP tool responses, not hallucinated shapes. It is the standard
"empirical grounding" pattern introduced in Issue #9965 Fix 4 after the #9332 session burned ~60-90 min iterating on fictional response fields.

## Layout

| File | Role |
|------|------|
| `README.md` | This file — explains the pattern |
| `op_responses.json` | Captured tool responses keyed by tool name (schema v1.0) |
| `_capture.py` | Template script: run real MCP tools and write into `op_responses.json` |
| `trigger_test.py` | Static routing.json substring matcher (no Claude channel needed) |

## MCP tools this skill uses

- `ue_grep`
- `ue_read`
- `ue_analyze_symbols`
- `ue_manage_blueprint`

## How to populate `op_responses.json`

1. Stand up a live NarshaMCP environment (open the target UE project).
2. Edit `_capture.py` — uncomment the calls for the operations you actually use.
3. Run `python _capture.py` from `<skill>/_verification/`.
4. Inspect `op_responses.json`. The `_meta` block should now have
   `capture_date`, `narshamcp_version`, `ue_version` populated. Each tool's
   `operations` dict should contain real captured responses.
5. Update `SKILL.md` and `EXAMPLES.md` to reference the *actual* fields you
   see — not what you assumed they would be.

## Validation

The hook `.claude/hooks/checks/validate_skill_evidence.py` confirms every
MCP tool mentioned in `SKILL.md` (or the `mcp_tools_used` array in
`routing.json`) has a corresponding key in `op_responses.json["tools"]`.
