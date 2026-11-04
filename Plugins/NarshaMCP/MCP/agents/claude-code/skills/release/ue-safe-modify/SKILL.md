---
name: ue-safe-modify
model: opus
description: "Purpose: AI 변경 전 자동 스냅샷 + 검증 실패 시 원클릭 롤백. 5 modes: start(스냅샷), verify(검증), commit(확정), rollback(복원), auto(전자동). Git + filesystem dual-layer backup for C++/BP/Config. Triggers: '안전하게 수정', '롤백', '되돌려', 'safe modify', 'with rollback', 'undo changes', 'safely change', '안전장치', '백업하고 수정'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x project. Git must be initialized."
allowed-tools: [Bash, Read, Grep, Glob, Edit, Write]
argument-hint: "start|verify|commit|rollback|auto [file1 file2 ...] [--description 'what to do']"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  issue: "#7592"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

## Current State (auto-collected)
- Branch: !`git branch --show-current`
- Status: !`git status --short`
- Project Root: !`echo $UECODEGEN_PROJECT_DIR`

> If the above data is empty, collect manually before proceeding.

# UE Safe Modify — Snapshot + Rollback Safety Net

**Issue**: #7592, #7580
**Purpose**: AI가 프로젝트 파일을 수정하기 전에 자동 스냅샷을 만들고, 검증 실패 시 원클릭 롤백.
**Requires**: Git initialized project (preferred — filesystem-only fallback works without git), NarshaMCP v0.7.0+.

> **Reference**: Trigger phrases (KO+EN), architecture diagram, manifest format (incl. Rust callers), error recovery, integration points (`/ue-pre-commit`, `/ue-architecture-lint`, `/ue-debug`), **7 Safety Invariants (frozen)**, activation test cases — all in [REFERENCE.md](REFERENCE.md). MODE 5 (AUTO) full lifecycle walkthrough — [EXAMPLES.md](EXAMPLES.md).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the requested mode automatically**.
> Do NOT just display documentation — actually run the commands!

### Mode Selection

Parse `$ARGUMENTS` to determine mode:

| Argument | Mode | Description |
|----------|------|-------------|
| `start [files...]` | **MODE 1: START** | 스냅샷 생성 (대상 파일 지정 또는 auto-detect) |
| `verify` | **MODE 2: VERIFY** | 변경 사항 검증 (빌드, BP 무결성, Config 유효성) |
| `commit` | **MODE 3: COMMIT** | 검증 통과 후 백업 삭제, 확정 |
| `rollback` | **MODE 4: ROLLBACK** | 모든 변경을 스냅샷 시점으로 정밀 복원 |
| `auto [--description '...']` | **MODE 5: AUTO** | start → 수정 → verify → commit/rollback 전자동 ([EXAMPLES.md](EXAMPLES.md)) |

> If `$ARGUMENTS` is empty or unrecognized, default to `start`.

---

## MODE 1: START — 스냅샷 생성

### Input

- `$ARGUMENTS`: `start [file1 file2 ...]`
- If no files specified: auto-detect target files in Source/ and Content/.

### Workflow

```bash
# Phase 1: Generate session ID and detect git availability
SESSION_ID=$(date +%Y%m%d_%H%M%S)
BACKUP_DIR=".ue-safe-modify-backup/${SESSION_ID}"
HAS_GIT=false
git rev-parse --git-dir > /dev/null 2>&1 && HAS_GIT=true
HEAD_SHA=""
$HAS_GIT && HEAD_SHA=$(git rev-parse HEAD)

# Phase 2: Collect target files
# Option A: Files specified in arguments
# Option B: Auto-detect
#   - With git: git status --short | filter relevant extensions
#   - Without git: scan Source/ and Content/ for specified files

# Phase 3: Backup ALL files via filesystem copy (PRIMARY method)
mkdir -p "${BACKUP_DIR}"
for file in all_target_files:
    target_dir="${BACKUP_DIR}/$(dirname $file)"
    mkdir -p "$target_dir"
    cp "$file" "$target_dir/"
    hash=$(sha256sum "$file" | cut -d' ' -f1)
    # Record in manifest with backup_path and hash

# Phase 4: Write manifest.json
# Include: session_id, head_sha (if git), has_git, files[], created_at
# Format reference: REFERENCE.md § Architecture > Manifest Format

# Phase 5: Ensure .gitignore includes backup directory (Safety Invariant #1)
if $HAS_GIT; then
    grep -q '.ue-safe-modify-backup/' .gitignore 2>/dev/null || \
        echo '.ue-safe-modify-backup/' >> .gitignore
fi
```

### Output

```
=== Safe Modify: Snapshot Created ===

Session: {session_id}
HEAD SHA: {head_sha} (short)
Files tracked: {N} ({M} tracked, {K} binary)
Backup dir: .ue-safe-modify-backup/{session_id}/

Tracked files (git restore enhancement available):
  - Source/MyGame/Characters/MyChar.h
  - Source/MyGame/Characters/MyChar.cpp

Binary files (filesystem backup):
  - Content/Blueprints/BP_Player.uasset (2.3 MB)

Next: Make your changes, then run /ue-safe-modify verify
      Or /ue-safe-modify rollback to revert all changes
```

---

## MODE 2: VERIFY — 변경 사항 검증

### Input

- No arguments needed (reads from most recent active manifest).

### Prerequisites

- Active session exists (`.ue-safe-modify-backup/*/manifest.json` with `status: "active"`).

### Workflow

```
Phase 1: Load active manifest → parse file list, classify by type.
Phase 2: Detect what changed
  → git diff --name-only (for tracked files in manifest)
  → Compare binary file hashes (for .uasset files)
  → Classify: cpp_changed, bp_changed, config_changed, other_changed
Phase 3: Tier-based verification (only run tiers relevant to change type)
```

### Verification Tiers

**Tier 1: C++ Static Check** (if cpp_changed)
```python
ue_fix_errors(operation="preflight", project_root="{project_root}")
# <50ms | 90% accuracy | syntax / missing includes / type mismatches
```

**Tier 2: C++ Deep Check** (if Tier 1 passes AND cpp_changed)
```python
ue_fix_errors(operation="preflight_deep", project_root="{project_root}")
# ~30s | 99% accuracy | linker errors, systemic issues, category classification
```

**Tier 3: Blueprint Integrity** (if bp_changed)
```python
ue_manage_blueprint(operation="get_structure", blueprint_name="{bp_name}")
# Checks: node structure parseable, no orphan connections, variables intact
```

**Tier 4: Config Validation** (if config_changed)
```python
ue_analyze_config(operation="search_config", query="{changed_section_or_key}")
# Checks: valid types, no broken references
```

**Tier 5: Architecture Lint** (optional, graceful degradation)
```python
architecture_lint_exists = Glob(".claude/skills/ue-architecture-lint/SKILL.md")
if architecture_lint_exists:
    Skill("ue-architecture-lint")
# Not installed → skip (Tiers 1-4 are sufficient).
```

### Verdict Logic

- **PASS**: All applicable tiers pass → safe to commit.
- **WARN**: Non-critical issues (warnings only) → commit with caution.
- **FAIL**: Critical issues (errors) → recommend rollback.

### Output

```
=== Safe Modify: Verification Report ===

Session: {session_id}
Changed files: {N} modified since snapshot

--- Tier 1: C++ Static Check ---
Result: PASS | {details}

--- Tier 2: C++ Deep Check ---
Result: PASS | {details}

--- Tier 3: Blueprint Integrity ---
Result: PASS | BP_Player: {node_count} nodes, {var_count} variables — structure intact

--- Tier 4: Config Validation ---
Result: SKIP (no config changes)

--- Tier 5: Architecture Lint ---
Result: SKIP (skill not installed)

=== Verdict: PASS ===
Recommendation: Safe to commit. Run /ue-safe-modify commit

=== Verdict: FAIL ===
Recommendation: Rollback recommended. Run /ue-safe-modify rollback
Failures:
  - Tier 2: 3 linker errors detected
  - Details: {error_list}
```

---

## MODE 3: COMMIT — 검증 후 확정

```
Phase 1: Check verification status
  → If verify was NOT run in this session: auto-run verify first.
  → If verify FAILS: abort commit, recommend rollback.

Phase 2: Verify passed — clean up backup
  → Load manifest. Delete backup dir: rm -rf .ue-safe-modify-backup/{session_id}/
  → If no other active sessions: remove backup root if empty.

Phase 3: Summary report → list confirmed changes, remind user to git commit when ready.
```

### Output

```
=== Safe Modify: Changes Committed ===

Session: {session_id} — CLOSED
Verification: PASSED (all {N} tiers)
Backup: Deleted (.ue-safe-modify-backup/{session_id}/)

Modified files (confirmed):
  - Source/MyGame/Characters/MyChar.h
  - Source/MyGame/Characters/MyChar.cpp
  - Content/Blueprints/BP_Player.uasset

Next: git add + git commit when ready
      Or /ue-pre-commit for impact analysis before committing
```

---

## MODE 4: ROLLBACK — 정밀 복원

### Input

- No arguments needed (rolls back most recent active session).
- Optional: `rollback {session_id}` to target a specific session.

### Workflow

```
Phase 1: Load manifest (most recent active, or specified session_id) → parse file list.

Phase 2: Restore all files from backup (PRIMARY — filesystem copy)
  → For each file in manifest: cp {backup_path} {file_path}
  → Works universally (git or no git).

Phase 3: Verify restoration (sha256 hash comparison) — Safety Invariant #4
  → For each restored file:
    current_hash=$(sha256sum {file_path} | cut -d' ' -f1)
    if current_hash != original_hash:
      WARNING: Hash mismatch (file may have been modified outside this session).

Phase 4: Clean up
  → Update manifest status to "rolled_back"
  → Delete backup directory
  → Report results
```

### Output

```
=== Safe Modify: Rollback Complete ===

Session: {session_id} — ROLLED BACK
HEAD SHA restored to: {head_sha} (for listed files only)

Restored files:
  ✅ Source/MyGame/Characters/MyChar.h — hash verified
  ✅ Source/MyGame/Characters/MyChar.cpp — hash verified
  ✅ Content/Blueprints/BP_Player.uasset — hash verified

All {N} files restored to pre-modification state.
Backup directory cleaned up.
```

### Hash Mismatch Handling

```
⚠️ Hash mismatch: Source/MyGame/MyChar.h
   Expected: def456...
   Got:      abc789...
   Cause: File may have been modified by another process during this session.
   Action: Manual review recommended. Use `git diff` to inspect differences.
```

---

## MODE 5: AUTO

Chains START → modify → VERIFY → COMMIT / ROLLBACK in one call. Full walkthrough
(success + failure outputs + when-to-use guidance) in [EXAMPLES.md § MODE 5: AUTO](EXAMPLES.md#mode-5-auto--full-lifecycle-walkthrough).

```bash
/ue-safe-modify auto --description "AMyCharacter.h에 Jump 기능 추가"
/ue-safe-modify auto Source/MyGame/MyChar.h --description "Add health regeneration"
```

---

## See Also

- [REFERENCE.md](REFERENCE.md) — Trigger phrases (KO+EN), architecture diagram (Filesystem Copy + Git Enhancement), manifest format (incl. Rust caller schema, Issue #9249), error recovery, integration points (`/ue-pre-commit`, `/ue-architecture-lint`, `/ue-debug`), **7 Safety Invariants** (frozen contract), activation test cases (positive 5 + negative 5)
- [EXAMPLES.md](EXAMPLES.md) — MODE 5 (AUTO) full lifecycle walkthrough (success + auto-rollback failure)
