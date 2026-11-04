# `/ue-dev-cycle` Examples

**Skill**: [SKILL.md](SKILL.md) | **Reference**: [REFERENCE.md](REFERENCE.md) | **Code-Exec (opt-in)**: [CODE_EXEC.md](CODE_EXEC.md)

Six canonical walkthroughs covering the four difficulty tiers + the two flag-mode variants. Sources for each example are cited from the original SKILL.md (pre-Issue-#10454) Difficulty Scenarios so reviewers can verify no scenario was invented during the refactor.

---

## Example 1 — Easy: UPROPERTY add to existing class

<!-- Source: original SKILL.md "Easy" Difficulty Scenario (lines 521-525) -->

```bash
/ue-dev-cycle "AMyCharacter에 float MaxHealth 프로퍼티 추가해줘"
```

**Path taken**:
- Phase 2 detects "single property add" → uses `Read` + `Edit` directly on `AMyCharacter.h`/`.cpp` (skips `derive_class`).
- Phase 3+4 builds in 1 iteration.
- Phase 6 verifies via `ue_analyze_symbols(search_symbols, "MaxHealth")`.

**Output**:

```text
==============================================================
 Dev Cycle Report
==============================================================
Feature: AMyCharacter에 float MaxHealth 프로퍼티 추가
Editor:  Connected
Phase 1 ANALYZE:     OK (project: MyGame, 2 related files)
Phase 2 IMPLEMENT:   OK (1 file edited, 0 deps added)
Phase 3+4 BUILD-FIX: PASS (iterations: 1, errors fixed: 0)
Phase 5 TEST:        SKIPPED (simple property add)

--- Symbol Verification ---
  AMyCharacter::MaxHealth: FOUND in PDB index
==============================================================
```

**Pass criterion**: Build success + property exists in PDB.

---

## Example 2 — Normal: ACharacter-derived class

<!-- Source: original SKILL.md "Normal" Difficulty Scenario (lines 527-531) -->

```bash
/ue-dev-cycle "AMyHero 캐릭터 클래스 만들어줘, BeginPlay에서 로그 출력"
```

**Path taken**:
- Phase 2 calls `ue_generate_code(suggest_class)` → `derive_class` → 2 files generated.
- Phase 3+4 builds in 1-2 iterations (typical: 1 iteration).
- Phase 5 generates BDD Spec test; executes if Editor is on.

**Output**:

```text
Phase 1 ANALYZE:     OK (project: MyGame, 4 related files; ACharacter verified in PDB)
Phase 2 IMPLEMENT:   OK (2 files generated, 0 deps added)
Phase 3+4 BUILD-FIX: PASS (iterations: 1, errors fixed: 0)
Phase 5 TEST:        PASS

--- Generated Files ---
  [x] Source/MyGame/Characters/MyHero.h
  [x] Source/MyGame/Characters/MyHero.cpp
  [x] Source/MyGame/Tests/MyHeroSpec.cpp

--- Symbol Verification ---
  AMyHero: FOUND in PDB index
```

**Pass criterion**: Build success + `ue_analyze_symbols(search_symbols, "AMyHero")` returns result.

---

## Example 3 — Hard: GAS Ability system

<!-- Source: original SKILL.md "Hard" Difficulty Scenario (lines 533-537) -->

```bash
/ue-dev-cycle "GAS 기반 공격 어빌리티 시스템 만들어줘 (ASC + GA_Attack)"
```

**Path taken**:
- Phase 2 calls `derive_class` with `features: ["gas_setup"]` → 4-5 files (ASC + GA_Attack + AttributeSet + GameplayEffect skeleton).
- Phase 2d adds `GameplayAbilities`, `GameplayTags`, `GameplayTasks` to Build.cs.
- Phase 3+4 may iterate 2x (GAS forward declarations + virtual override signatures often need a fix-up pass).

**Output**:

```text
Phase 2 IMPLEMENT:   OK (5 files generated, 3 deps added)
Phase 3+4 BUILD-FIX: PASS (iterations: 2, errors fixed: 3)
Phase 5 TEST:        PASS

--- Build.cs Dependencies Added ---
  - "GameplayAbilities"
  - "GameplayTags"
  - "GameplayTasks"

--- Symbol Verification ---
  UMyAbilitySystemComponent: FOUND
  UGA_Attack:                FOUND
  UMyAttributeSet:           FOUND
```

**Pass criterion**: Build success + test passes + 2+ class symbols found.

---

## Example 4 — Expert: Multi-class weapon system

<!-- Source: original SKILL.md "Expert" Difficulty Scenario (lines 539-540) -->

```bash
/ue-dev-cycle "무기 시스템 만들어줘 (AWeaponBase + AMeleeWeapon + ARangedWeapon + 테스트)"
```

**Path taken**:
- Phase 2 calls `derive_class` 3 times sequentially (NEVER parallel — connection reset risk; see [REFERENCE.md § Anti-Patterns](REFERENCE.md#anti-patterns)).
- Phase 3+4 typically iterates 2-3x (cascading errors from inheritance chain are common).
- Approaches context-window limit on Editor-on PIE runs; consider `--skip-test` if tight.

**Output**:

```text
Phase 2 IMPLEMENT:   OK (4 files generated, 0 deps added)
Phase 3+4 BUILD-FIX: PASS (iterations: 3, errors fixed: 5)
Phase 5 TEST:        PASS

--- Generated Files ---
  [x] Source/MyGame/Weapons/WeaponBase.{h,cpp}
  [x] Source/MyGame/Weapons/MeleeWeapon.{h,cpp}
  [x] Source/MyGame/Weapons/RangedWeapon.{h,cpp}
  [x] Source/MyGame/Tests/WeaponSystemSpec.cpp
```

**Pass criterion**: Build success + 4+ class symbols found + test passes.

---

## Example 5 — `--build-only` (code already written)

<!-- Source: original SKILL.md STEP 0 flag table (line 72) + Decision Tree (line 85) -->

```bash
# Operator already wrote AMyHero.h/.cpp by hand and wants the cycle
# to skip codegen, just drive build → fix → test:
/ue-dev-cycle "validate AMyHero hand-written code" --build-only
```

**Path taken**:
- STEP 0 sets `--build-only` → Phase 2 skipped entirely.
- Phase 1 still runs (project context discovery).
- Phase 3+4 starts immediately on the existing source tree.
- Phase 5 generates a test against the hand-written class.

**Output**:

```text
Phase 1 ANALYZE:     OK (project: MyGame, 6 related files)
Phase 2 IMPLEMENT:   SKIPPED (--build-only)
Phase 3+4 BUILD-FIX: PASS (iterations: 1, errors fixed: 0)
Phase 5 TEST:        GENERATED (Editor offline)
```

**Use case**: validate a hand-written or merged-from-VCS change without regenerating files.

---

## Example 6 — `--code-exec` (Phase 2-CE orchestration, opt-in)

<!-- Source: original SKILL.md Phase 2-CE block (lines 177-315), now in CODE_EXEC.md -->

```bash
/ue-dev-cycle "AMyHero with GAS attack" --code-exec
```

**Path taken**:
- STEP 0 detects `--code-exec` → SKILL.md instructs Claude to `Read(.claude/skills/ue-dev-cycle/CODE_EXEC.md)` (this is the "load gate" — see [SKILL.md § Phase 2-CE](SKILL.md#phase-2-ce-code-exec-orchestration-opt-in-via---code-exec-issue-9581) for the gate stub).
- Phase 1 (ANALYZE) runs normally.
- Phase 2-CE replaces Phase 2-4 with a single Claude-authored Python script saved under `<project_root>/Saved/CodeExec/<feature_slug>_<YYYYMMDD_HHMMSS>.py`. The script orchestrates `narsha.analyze` → `narsha.generate.suggest_class` → `narsha.generate.derive_class` → `narsha.fix.preflight` → `narsha.fix.build_and_fix` in one process and prints ONLY the structured summary.
- Runner invocation: `python .claude/skills/ue-dev-cycle/scripts/code_exec_runner.py --script <path> --timeout 1800`.
- Retry-with-fix loop: max 3 iterations, each subsequent script generated with `previous_returncode` + `previous_stderr_tail` injected.
- Phase 5 + Phase 6 consume `phase_2_ce_status` / `phase_2_ce_summary` / `phase_2_ce_iterations` / `phase_2_ce_total_wall_secs` from the runner.

**Output (parsed JSON from runner stdout)**:

```text
phase_2_ce_status:        success
phase_2_ce_iterations:    1
phase_2_ce_total_wall_secs: 412.7

Phase 1 ANALYZE:     OK (project: MyGame, 8 related files)
Phase 2-CE:          PASS (1 iteration, 412.7s wall time)
Phase 3+4 BUILD-FIX: bypassed (Phase 2-CE handled it)
Phase 5 TEST:        PASS
Phase 6 SUMMARY:     OK
```

**Use case**: token-economy A/B vs. the default turn-based path. Status is **PoC** — see `docs/research/CODE_EXEC_UE_DEV_CYCLE_AB.md` for the latest `expand` / `hold` / `pivot` decision.

**Note on the load gate** (Issue #10454 headline regression test): without `--code-exec`, this skill never reads CODE_EXEC.md and the 139-line Phase 2-CE protocol body (lines 13-153 of the 156-line CODE_EXEC.md) stays out of context. With the flag, SKILL.md's Phase 2-CE stub explicitly invokes `Read("CODE_EXEC.md")` before proceeding. Verify by grepping the conversation transcript for the sentinel string `ue-dev-cycle-CODE_EXEC-MARKER-10454` — zero hits without flag, ≥ 1 hit with flag.

---

**Related**: Issue #6730 (skill origin), Issue #9581 (Phase 2-CE PoC), Issue #10454 (this file extracted from SKILL.md), Epic #10451 (hot-context bloat reduction).
