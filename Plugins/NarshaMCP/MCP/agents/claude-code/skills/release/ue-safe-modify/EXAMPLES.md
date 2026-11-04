# /ue-safe-modify — Examples

> Companion to [SKILL.md](SKILL.md). Walkthroughs — not protocol. SKILL.md keeps the
> imperative MODE 1-4 protocols. This file holds the MODE 5 (AUTO) full-lifecycle
> walkthrough including success and auto-rollback failure outputs.

---

## MODE 5: AUTO — full lifecycle walkthrough

`/ue-safe-modify auto` chains START → modify → VERIFY → COMMIT/ROLLBACK in a single
operator-confirmed call. Use this when the modification is small and well-described,
and when an auto-rollback on verify failure is the desired safety behavior.

### Input

- `auto --description "AMyCharacter.h에 Jump 기능 추가"`
- `auto Source/MyGame/MyChar.h --description "Add health regeneration"`

### Workflow

```
Phase 1: START
  → Execute MODE 1 (start) with specified files or auto-detect
  → Report snapshot created

Phase 2: MODIFY
  → Claude performs the modifications described in --description
  → Uses standard Edit/Write tools
  → All changes tracked by manifest

Phase 3: VERIFY
  → Execute MODE 2 (verify)
  → Evaluate verdict

Phase 4: DECISION GATE
  → PASS or WARN: Execute MODE 3 (commit)
  → FAIL: Execute MODE 4 (rollback) + error report

Phase 5: REPORT
  → Final summary of what happened
```

### Output (Success)

```
=== Safe Modify: Auto Complete (SUCCESS) ===

Description: "AMyCharacter.h에 Jump 기능 추가"
Session: {session_id}

Phase 1 (Start):   ✅ Snapshot created — 2 files tracked
Phase 2 (Modify):  ✅ Changes applied
Phase 3 (Verify):  ✅ All tiers passed
Phase 4 (Commit):  ✅ Backup cleaned up

Modified files:
  - Source/MyGame/Characters/MyChar.h (+15 lines)
  - Source/MyGame/Characters/MyChar.cpp (+42 lines)
```

### Output (Failure + Auto-Rollback)

```
=== Safe Modify: Auto Complete (ROLLED BACK) ===

Description: "AMyCharacter.h에 Jump 기능 추가"
Session: {session_id}

Phase 1 (Start):    ✅ Snapshot created — 2 files tracked
Phase 2 (Modify):   ✅ Changes applied
Phase 3 (Verify):   ❌ FAILED
  - Tier 2: 3 linker errors
    - LNK2019: unresolved external symbol "UCharacterMovementComponent::Jump"
    - LNK2019: unresolved external symbol "ACharacter::CanJump"
    - LNK2001: unresolved external symbol "ACharacter::JumpMaxCount"
Phase 4 (Rollback): ✅ All files restored to pre-modification state

Files restored:
  ✅ Source/MyGame/Characters/MyChar.h — hash verified
  ✅ Source/MyGame/Characters/MyChar.cpp — hash verified

Recommendation:
  The modification caused linker errors. Likely missing module dependency
  in Build.cs. Consider adding "Engine" to PublicDependencyModuleNames.
```

### When to use AUTO vs. manual MODE 1-4

- **AUTO** — small, well-described changes where rollback-on-failure is acceptable.
  Operator does NOT review intermediate verify output before commit.
- **Manual MODE 1-4** — larger changes, exploratory work, or when you want to
  inspect verify output and consciously choose commit/rollback. Verbose but precise.
