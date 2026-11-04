# `_verification/` — Empirical Grounding for `ue-cinematics-audit`

These files prove that the skill's field and coverage rules were checked against
real public MCP responses instead of inferred response shapes.

## Captured environment

- NarshaMCP `0.12.0`, fresh STDIO session
- Unreal Engine 5.8 Lyra project
- Editor disconnected
- Read-only calls only (`use_editor=false` on detail reads)
- Capture date: 2026-07-22

`op_responses.json` contains all six operations used by the workflow:

- `ue_sequencer_structure`: `list_sequences`, `validate_settings`, `list_bindings`
- `ue_sequencer_tracks`: `sequence_audit`, `list_tracks`
- `ue_sequencer_keyframes`: `get_curve_data`

The capture found five Level Sequences through filesystem discovery and exercised
the detail reads on `SEQ_LobbyScreen`. All four detail responses succeeded through
the offline `binary_parser`, including a 14-track inventory. The keyframe envelope's
top-level `key_count` is 31 while its requested-track curves sum to 9; this captured
counterexample is the regression anchor for ignoring the stale pre-filter total.

## Corrections driven by the capture

1. Current `list_sequences` responses expose `name` and absolute `file_path`, not a
   mount `path`. The absolute `.uasset` path is not a valid offline `sequence_path`.
   The workflow now uses a future mount `path` when present and otherwise the unique
   `name`; ambiguous names fail closed.
2. `sequence_audit` returned `success=true` with `sequences_analyzed=0` while discovery
   found five sequences because the metadata category was not loaded. The workflow now
   treats a baseline row count below the selected count as partial coverage instead of
   reporting a false clean baseline.

## Reproduce

From the repository root on a host with the sample project and UE 5.8 installed:

```powershell
python .claude/skills/ue-cinematics-audit/_verification/_capture.py
.venv/Scripts/python.exe .claude/hooks/checks/validate_skill_evidence.py --skill ue-cinematics-audit --json
.venv/Scripts/python.exe .claude/skills/ue-cinematics-audit/_verification/trigger_test.py .claude/skills/ue-cinematics-audit/evals/trigger-grading.json
```

`_capture.py` starts and terminates its own STDIO server. It does not connect the
Editor or modify any asset. `trigger_test.py` checks the 20 routing fixtures without
a model channel.
