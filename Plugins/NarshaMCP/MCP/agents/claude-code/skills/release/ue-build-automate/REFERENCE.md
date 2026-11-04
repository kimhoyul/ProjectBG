# ue-build-automate — Reference

## Editor Preflight

Before any build operation, the server detects a running Unreal Editor, saves dirty assets,
requests graceful shutdown, and waits for exit. When `auto_restart=true`, the existing restart
path owns shutdown so it is not requested twice.

## Build History

Completed results are saved to `build_history.jsonl` and shown in the daemon TUI. Query recent
records with `ue_build_pipeline(operation="get_build_history", limit=10)` and aggregate success
rate/duration with `ue_build_pipeline(operation="get_build_stats")`.

Cold companion to [SKILL.md](SKILL.md). The build-report format, error-recovery table, activation test cases, per-operation time estimates, and the smart-mode routing table live here so the SKILL.md body stays under the 1000-word progressive-disclosure budget (Issue #15438; split in #19403 when the tool-call gate was added).

## Time Estimates

**Time estimates by operation**:

| Operation | Typical Duration | Default Timeout |
|-----------|-----------------|-----------------|
| build_editor | 2-10 min | 300s |
| cook | 5-30 min | 900s |
| package | 5-20 min | 600s |
| buildcookrun | 15-60+ min | 1800s |
| run_commandlet | 1-10 min | 300s |
| gauntlet_run | 5-30 min | 900s |

---

## Smart Mode Routing Reference

`smart` routes by checking params in this priority order:
1. `commandlet_name` present → `run_commandlet`
2. `cook=true && package=true` → `buildcookrun`
3. `cook=true` → `cook`
4. `package=true` → `package`
5. `target_name` present → `build_target`
6. **No params → `build_editor` (full compile!)**

**Always specify `operation` explicitly** to avoid unintended smart routing.

---

## Output Format

```text
=== Build Pipeline Report ===

Pipeline: <build|cook|package|bcr|commandlet>
Platform: <Windows|Linux|Android|iOS>
Configuration: <Development|Shipping|Debug>

--- Result ---
Status: SUCCESS / FAILED
Duration: Xm Xs
Warnings: N
Errors: M

--- Details ---
[If success]
  Output: <output_path_or_binary>
  Size: X MB

[If failed]
  Error Summary:
    1. <file>:<line> — <error_message>
    2. <file>:<line> — <error_message>

  Auto-Fix Suggestion:
    ue_fix_errors(operation="smart") → <suggested_fix>

--- Warnings (top 5) ---
| # | File | Warning |
|---|------|---------|
| 1 | MyActor.cpp:42 | Unused variable 'TempVal' |
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Build fails with compile errors | Code errors | `ue_fix_errors(operation="smart")` for auto-diagnosis and fix suggestions |
| Cook fails with missing assets | Asset references broken | `ue_search_assets(operation="find_dependencies")` to identify missing refs |
| Package fails with signing error | Certificate not configured | Report error with platform-specific signing setup instructions |
| Commandlet not found | Typo or module not loaded | `ue_build_pipeline(operation="smart")` to list available commandlets |
| Timeout on large project | Build takes >10 minutes | **Prevention**: 1) Use explicit `operation` (avoid smart fallback to build_editor) 2) Set `timeout` parameter 3) Use `target_name` to scope build. **Recovery**: Report partial progress; suggest incremental build with specific targets |

---

## Activation Test Cases

**Positive (5)** - Should activate:
1. "프로젝트 빌드해줘" -> editor build
2. "Windows로 패키징" -> package
3. "CompileAllBlueprints 커맨드릿 실행" -> commandlet
4. "Cook for Windows" -> cook
5. "BuildCookRun 전체 파이프라인" -> bcr

**Negative (3)** - Should NOT activate:
1. "빌드 에러 해결해줘" -> ue-debug skill
2. "C++ 클래스 만들어줘" -> ue-scaffold skill
3. "라이브 코딩 실패" -> ue-livecoding-fix skill
