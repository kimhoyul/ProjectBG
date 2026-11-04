# `/ue-dev-cycle` Reference

**Skill**: [SKILL.md](SKILL.md) | **Examples**: [EXAMPLES.md](EXAMPLES.md) | **Code-Exec (opt-in)**: [CODE_EXEC.md](CODE_EXEC.md)

Reference material extracted from SKILL.md per Issue #10454 (Epic #10451 SKILL.md hot-context bloat reduction). Hot-path execution stays in [SKILL.md](SKILL.md); this file holds output formats, error tables, anti-patterns, and evaluation criteria — none of which need to load on every `/ue-dev-cycle` invocation.

---

## Output Format

```text
==============================================================
 Dev Cycle Report
==============================================================

Feature: <feature description>
Editor:  <Connected / Offline>

Phase 1 ANALYZE:     OK (project: <name>, <N> related files)
Phase 2 IMPLEMENT:   OK (<N> files generated, <M> deps added)
Phase 3+4 BUILD-FIX: <PASS/FAIL> (iterations: <N>, errors fixed: <M>)
Phase 5 TEST:        <PASS/GENERATED/SKIPPED/FAIL>

--- Generated Files ---
  [x] Source/MyGame/Characters/MyHero.h
  [x] Source/MyGame/Characters/MyHero.cpp
  [x] Source/MyGame/Tests/MyHeroSpec.cpp

--- Build.cs Dependencies Added ---
  - "GameplayAbilities"
  - "GameplayTags"

--- Symbol Verification ---
  AMyHero: FOUND in PDB index
  UMyHeroAttributeSet: FOUND in PDB index

--- Remaining Issues ---
  (none)

--- Next Steps ---
  1. Open Editor and verify in Content Browser
  2. Create Blueprint derived from AMyHero
  3. Set up Input Mapping Context in Project Settings
  4. Test in PIE (Play In Editor)
==============================================================
```

---

## Error Recovery

| Phase | Error | Cause | Fallback |
|-------|-------|-------|----------|
| 0 | `ue_check_health` fails | MCP server not running | Report: "Run `ue_check_health()` to verify MCP connection" |
| 1 | No project context | Project path not configured | Use `ue_check_health()` project_root field |
| 1 | Base class not found in PDB | Invalid class name or PDB not loaded | `ue_analyze_symbols(search_symbols)` with partial name |
| 2 | `suggest_class` returns empty | Ambiguous description | Ask user to clarify feature type |
| 2 | `derive_class` fails | Missing output directory | Create directory, retry |
| 2 | Target file already exists | File conflict | WARN and skip (do not overwrite) |
| 3 | Build timeout | Large project (>5 min) | Report partial result, suggest incremental build |
| 3 | UBT not found | Engine path not configured | Check `UECODEGEN_ENGINE_DIR` in `.env` |
| 4 | `ue_fix_errors` returns no suggestions | Unknown error pattern | Present raw error for manual diagnosis |
| 4 | Auto-fix makes error worse | Wrong fix applied | Revert via `Edit`, stop iteration |
| 4 | 3 iterations exhausted | Complex cascading errors | Report remaining errors, suggest `/ue-debug` |
| 5 | `generate_test` fails | Unsupported class pattern | Skip test, report in summary |
| 5 | Test execution fails | Editor not running | Report: test code generated, run manually |
| 5 | New build fails after test added | Test file has compile errors | Remove test file, report issue |

---

## Anti-Patterns

| DO NOT | DO INSTEAD |
|--------|------------|
| Call `ue_generate_code` in parallel | Always sequential (connection reset risk) |
| Skip Phase 1 analysis | Discovery prevents wrong base class selection |
| Skip `dependency_check` after codegen | Missing modules = guaranteed build failure |
| Iterate build-fix more than 3 times | Report remaining errors for manual fix |
| Run `run_automation_tests` without Editor | Generate test code only, skip execution |
| Overwrite existing files silently | WARN and skip if target files exist |
| Use `gh`, `cargo test`, or CI tools | This skill is for UE developers, not NarshaMCP development |

---

## Evaluation Criteria

### Difficulty Scenarios (from Issue #6730)

**Easy**: UPROPERTY 1개 추가
- Input: "AMyCharacter에 float MaxHealth 프로퍼티 추가해줘"
- Expected: Phase 2 uses `Read` + `Edit` (no `derive_class`), Phase 3+4 builds, Phase 6 verifies via `ue_analyze_symbols`
- Pass: Build success + property exists in PDB

**Normal**: ACharacter 파생 클래스 생성
- Input: "AMyHero 캐릭터 클래스 만들어줘, BeginPlay에서 로그 출력"
- Expected: Phase 2 generates 2 files, Phase 3+4 builds in 1-2 iterations
- Pass: Build success + `ue_analyze_symbols(search_symbols, "AMyHero")` returns result

**Hard**: GAS Ability 시스템
- Input: "GAS 기반 공격 어빌리티 시스템 만들어줘 (ASC + GA_Attack)"
- Expected: Phase 2 generates 4-5 files with `features: ["gas_setup"]`, Phase 3+4 may iterate 2x
- Pass: Build success + test passes + 2+ class symbols found

**Expert**: 멀티클래스 무기 시스템
- Input: "무기 시스템 만들어줘 (AWeaponBase + AMeleeWeapon + ARangedWeapon + 테스트)"
- Expected: Phase 2 calls `derive_class` 3+ times sequentially, Phase 3+4 iterates 2-3x
- Pass: Build success + 4+ class symbols found + test passes

### Activation Test Cases

**Positive (5)** — Should activate:
1. "개발 루프 돌려줘, AMyHero 캐릭터" -> Activate
2. "Dev cycle: create GAS combat character" -> Activate
3. "이 기능 구현하고 빌드까지 해줘" -> Activate
4. "코드 빌드 테스트 자동화" -> Activate
5. "Implement and build a weapon system" -> Activate

**Negative (5)** — Should NOT activate:
1. "캐릭터 만들어줘" -> `/ue-scaffold` (no build/test intent)
2. "빌드 에러 해결해줘" -> `/ue-debug` (error fix only)
3. "풀사이클 6730" -> `/full-cycle` (internal issue lifecycle)
4. "테스트 생성해줘" -> `/ue-test-gen` (test only)
5. "핫 리로드 에러" -> `/ue-livecoding-fix` (hot reload only)

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Easy scenario pass rate | >95% |
| Normal scenario pass rate | >90% |
| Hard scenario pass rate | >80% |
| Expert scenario pass rate | >70% |
| Build-fix convergence (3 iters) | >85% |
| Time to complete (Normal) | <120s |

---

## Known Limitations

1. **Editor dependency**: Phase 5 test execution requires Editor. Without Editor, tests are generated but not run.
2. **Build time**: Large projects may take 2-5+ minutes per build iteration. Total cycle time scales with project size.
3. **Context window**: Expert scenarios (8-10+ files) may approach context limits. Use `--skip-test` to reduce pressure.
4. **New .cpp files**: Require full UBT build (Live Coding cannot hot-reload new translation units).
5. **GAS complexity**: Hard/Expert scenarios may produce build errors that `ue_fix_errors` cannot auto-resolve. Manual intervention may be needed.
6. **UBT stdout parsing**: `ue_fix_errors(scan_build_log)` reads Editor log only, not UBT stdout. In Editor-offline mode, Claude reads Bash output directly to identify and fix errors.
7. **Test flags**: `ue_generate_code(generate_test)` defaults for `simple` / `bdd_spec` omit `SmokeFilter` (Issue #27969). That filter runs in `FEngineLoop::PreInitPostStartupScreen` via `RunSmokeTests()`, before `UTypedElementRegistry` registers `Components`; an actor `NewObject` tagged SmokeFilter crashes the editor at boot. The generator also strips an operator-supplied `SmokeFilter` from A* `simple`/`bdd_spec` tests. `cqtest` still uses a single `EngineFilter` bit (#8183). Legacy invalid combos (e.g. SmokeFilter+CriticalPriority on older templates) are still auto-corrected by the build-fix loop on iteration 2.
8. **BDD Spec registration**: BDD Spec tests may not appear in Editor automation test list immediately after rebuild. `run_automation_tests` may timeout; manual refresh or Editor restart needed.

---

**Related**: Issue #6730 (skill origin), Issue #10454 (this file extracted from SKILL.md), Epic #10451 (hot-context bloat reduction).
