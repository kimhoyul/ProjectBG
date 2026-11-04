# Unreal Error Doctor - Advanced Features

Advanced usage, integration, and technical details.

---

## Tool Integration

### Path A Tools (Read-First — Compile/Build Errors)

| Tool | Step | Purpose |
|------|------|---------|
| `Read` | 0A-1 | Read build log to find error file:line |
| `Read` | 0A-2 | Read source file at error location |
| `Edit` | 0A-3 | Apply fix directly |
| `Bash` | 0A-4 | [Optional] Verify build compiles |

### Path B Tools (MCP — Asset/Blueprint/Runtime Errors)

| Tool | Step | Purpose |
|------|------|---------|
| `ue_fix_errors(operation="smart")` | 1 | ONE call — auto-detect and classify (max 1 call) |
| `Read` | 2 | Read source file for code context |
| `Edit` | 4 | Apply fix |
| `ue_build_pipeline(operation="build_editor")` | 5 | C++ rebuild (if needed) |
| `ue_editor_automation(operation="compile_blueprints")` | 5 | BP compile (Editor required, if needed) |
| `ue_fix_errors(operation="scan_build_log")` | 6 | Verify fix (if needed) |

### Prohibited Tools (Anti-Patterns)

| Tool/Pattern | Reason |
|-------------|--------|
| `ue_check_health()` | Server health already confirmed at skill load (Issue #7110) |
| `ue_fix_errors(crash_analysis)` then `(runtime_analysis)` then ... | Sequential mode-trying wastes 3-4 calls. Use `smart` only. |
| `ToolSearch(...)` | Skill already loaded — tool schemas available |
| `Glob(**/Saved/Logs/*.log)` | Build log paths are known constants — Read directly |

### Success Criteria

| Metric | Target |
|--------|--------|
| **Total tool calls (Path A)** | **≤ 4** |
| **Total tool calls (Path B)** | **≤ 5** |
| **ue_fix_errors calls** | **≤ 1** |
| **ue_check_health calls** | **0** |
| **ToolSearch calls** | **0** |
| **Session cost (Opus 4.6)** | **< $0.50** |
| Time Savings | >5x faster than manual |
| Diagnosis Accuracy | >85% |
| Fix Completion Rate | >85% |
| Build Success After Fix | >80% |
| Verify Confirms Resolution | >75% |
| Loop Convergence (max 3 iter) | >90% |

---

## Integration with Other Skills

**Works with**:

- **Module Mapper** - For linker errors (module dependency resolution)
- **Blueprint Flow Tracer** - Understand code flow context
- **Performance Health Check** - Validate quality after fix
- **UE Build Automate** - Shares `ue_build_pipeline` tool for build operations

**Called by**:

- **ue-debug** - Routes compile errors to this skill via symptom classification

---

## Build Step Details

### Offline vs Online

| Mode | Build Tool | Verify Tool | Notes |
|------|-----------|-------------|-------|
| Editor running | `compile_blueprints` (BP) or `build_editor` (C++) | `get_compiler_results` | Fastest path for BP errors |
| Editor not running | `build_editor` | `scan_build_log` | Full offline support |

### Timeout Strategy

- `build_editor`: 600s default (10 min), configurable via `timeout` param
- `build_and_fix`: 300s default (5 min), dynamic timeout at dispatcher level (Issue #6231)
- `compile_blueprints`: ~5s, no timeout concern
- Both build tools: user `timeout` param respected, +30s buffer, max 3700s (~1h)
- On timeout: report partial, suggest manual build
- Large projects (Lyra etc.): set `timeout: 900+`

---

## Version History

**v3.0.0** - 2026-03-21 (Issue #7112)

- 2-path workflow: Path A (Read-first, 3-4 calls) for compile errors, Path B (MCP, 3-5 calls) for asset/BP errors
- Step 0 error type classification and path selection
- Explicit anti-pattern rules (no sequential ue_fix_errors, no ue_check_health, no ToolSearch)
- Call budget enforcement (Path A ≤4, Path B ≤5, hard stop at 6)
- New examples: Read-first path, user-pasted errors
- Success criteria updated with call-count metrics

**v2.0.0** - 2026-03-10 (Issue #6231)

- 6-step complete cycle (BUILD + VERIFY added)
- Smart build routing (BP vs C++, Editor ON/OFF)
- Post-fix error count verification
- Loop-back mechanism (max 3 iterations)
- Cumulative progress reporting
- Regression detection (error count increase → .backup restore)

**v1.0.0** - 2025-10-20

- 4-step medical workflow
- 5 core error patterns (100% Top 5)
- Epic Games best practices
- Bilingual support
