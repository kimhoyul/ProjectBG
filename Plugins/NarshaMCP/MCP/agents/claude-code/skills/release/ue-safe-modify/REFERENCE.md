# /ue-safe-modify — Reference Material

> Companion to [SKILL.md](SKILL.md). Reference content (trigger phrases, architecture
> diagrams, manifest format, error recovery, integration points, **7 Safety Invariants
> (frozen)**, activation test cases) lives here. SKILL.md keeps the imperative MODE 1-4
> protocols. MODE 5 (AUTO) walkthrough is in [EXAMPLES.md](EXAMPLES.md).

---

## Auto-Trigger Phrases

### Korean
- "안전하게 수정해줘", "스냅샷 찍고 수정"
- "롤백 가능하게 수정", "되돌릴 수 있게"
- "수정 전 백업해줘", "안전장치 걸고 수정"
- "원래대로 돌려줘", "백업하고 수정해줘"
- "안전 모드로 수정"

### English
- "Safely modify", "Modify with rollback"
- "Snapshot before changing", "With undo support"
- "Rollback changes", "Undo AI changes"
- "Safely change", "Backup before change"
- "Safe mode modify"

---

## Architecture: Unified Filesystem Backup (with Git Enhancement)

All files are backed up via **filesystem copy** — this works universally, with or without git.
When git IS available, tracked files get an additional git-based restore path for extra safety.

```
┌─────────────────────────────────────────────────┐
│ PRIMARY: Filesystem Copy (ALL files)            │
│ .h, .cpp, .ini, .cs, .json, .uasset, .umap     │
│ Method: cp to backup dir, cp back on rollback   │
│ Works: ALWAYS (git or no git)                   │
│ Precision: per-file, sha256-verified            │
├─────────────────────────────────────────────────┤
│ ENHANCEMENT: Git Restore (when git available)   │
│ Tracked text files only (.h, .cpp, .ini, .cs)   │
│ Method: git checkout {head_sha} -- <files>      │
│ Benefit: faster, leverages git object store     │
│ Fallback: filesystem copy if git unavailable    │
├─────────────────────────────────────────────────┤
│ Manifest: .ue-safe-modify-backup/{id}/manifest  │
│ Records every file, type, hash, backup path     │
│ Enables precise rollback of THIS session only   │
└─────────────────────────────────────────────────┘
```

> **IMPORTANT**: Many UE projects are NOT git-initialized (e.g., downloaded Lyra, marketplace projects).
> The skill MUST work without git. Filesystem copy is the primary mechanism; git is a bonus.

### Backup Directory Structure

```
{project_root}/
└── .ue-safe-modify-backup/
    └── {session_id}/                    # e.g., 20260329_143022
        ├── manifest.json                # File tracking manifest
        └── Content/                     # Mirrors project structure
            ├── BP_Player.uasset
            └── Maps/
                └── MainMap.umap
```

### Manifest Format

```json
{
  "session_id": "20260329_143022",
  "head_sha": "abc123def456...",
  "created_at": "2026-03-29T14:30:22",
  "project_root": "D:/MyProject",
  "status": "active",
  "files": [
    {
      "path": "Source/MyGame/Characters/MyChar.h",
      "type": "tracked",
      "original_hash": "def456...",
      "backup_method": "git"
    },
    {
      "path": "Content/Blueprints/BP_Player.uasset",
      "type": "binary",
      "original_hash": "789abc...",
      "backup_method": "filesystem",
      "backup_path": ".ue-safe-modify-backup/20260329_143022/Content/Blueprints/BP_Player.uasset"
    }
  ]
}
```

> **Rust callers (Issue #9249)**: The same manifest format is now produced
> by the native Rust API at `narshamcp::tools::safe_modify` and the
> `narshamcp safe-modify {start|rollback|commit|list}` CLI subcommand.
> Either side can roll back the other's sessions — covered by
> `MCP/native/mcp_server/tests/safe_modify_cli_parity.rs`. Use the Rust API
> from inside Rust auto-fix tools (#9145, #9146, #9147) to avoid shelling out.
> Schema differences vs this skill: Rust always writes `backup_method:"filesystem"`
> and always populates `backup_path` (even for tracked files), but parses
> `backup_method:"git"` for forward compatibility.

---

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| No active session found | `verify`/`commit`/`rollback` without prior `start` | Run `start` first |
| Manifest corrupted | JSON parse failure | Fallback: `git checkout HEAD -- .` for tracked files, warn about binary files |
| Backup directory missing | Manually deleted or disk error | For tracked files: `git checkout {head_sha} -- <files>` still works. Binary files: unrecoverable, warn user |
| Editor locks .uasset | UE Editor has file handle | Warn: "Close Unreal Editor before rollback", retry after |
| Hash mismatch after restore | External modification during session | Report mismatch, recommend manual review via `git diff` |
| `.gitignore` not writable | Permission issue | Warn user to manually add `.ue-safe-modify-backup/` to .gitignore |
| Git not initialized | No `.git` directory | Filesystem copy still works (primary path). Git enhancement is skipped automatically. |
| MCP tools unavailable | Server not running | Verify tiers 1-4 skip gracefully, warn "verification incomplete" |

---

## Integration Points

### With /ue-pre-commit (Issue #5299)
```
/ue-safe-modify start → (modifications) → /ue-safe-modify verify
                                          → /ue-pre-commit (impact analysis)
                                          → /ue-safe-modify commit
                                          → git commit
```

### With /ue-architecture-lint (Issue #7590)
```
/ue-safe-modify verify
  → Tier 1-4: MCP-based checks
  → Tier 5: Glob for ue-architecture-lint skill
    → Found: invoke for architecture validation
    → Not found: skip gracefully (INFO message only)
```

### With /ue-debug (Issue #7591)
```
/ue-safe-modify auto → FAIL → rollback
  → User can then: /ue-debug to diagnose the specific error
  → Fix the root cause
  → /ue-safe-modify auto (retry)
```

---

## Safety Invariants

> **Frozen** — these 7 invariants are the safety contract. Any change to them must be
> reflected in the rollback unit tests (manifest_round_trip, hash_verification, session_isolation).

1. **Backup directory is NEVER committed** — `.gitignore` entry enforced at `start`
2. **Rollback is always per-file** — never `git checkout -- .` (which would revert unrelated changes)
3. **Manifest is the single source of truth** — no implicit file tracking
4. **Hash verification after rollback** — guarantees restoration accuracy
5. **Binary files use filesystem copy** — git cannot selectively restore binary diffs
6. **Session isolation** — multiple sessions can coexist without interference
7. **No destructive operations on unrelated files** — only files in the manifest are touched

---

## Activation Test Cases

### Positive (should activate this skill)
1. "안전하게 수정해줘" → `/ue-safe-modify`
2. "롤백 가능하게 AMyChar.h 수정" → `/ue-safe-modify`
3. "Safely modify BP_Player" → `/ue-safe-modify`
4. "Modify with rollback support" → `/ue-safe-modify`
5. "백업하고 수정해줘" → `/ue-safe-modify`

### Negative (should NOT activate)
1. "AMyChar를 ABaseChar로 리네임" → `/safe-class-rename` (rename, not safe-modify)
2. "BP_Player 일괄 수정" → `/asset-modification-wizard` (bulk modification)
3. "빌드 에러 해결해줘" → `/ue-debug` (error fixing, not safe modification)
4. "커밋 전 검사해줘" → `/ue-pre-commit` (pre-commit analysis, not modification)
5. "프로젝트 진단해줘" → `/ue-audit` (project health, not modification)
