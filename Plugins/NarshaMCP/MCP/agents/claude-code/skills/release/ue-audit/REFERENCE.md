# /ue-audit — Reference Material

> Companion to [SKILL.md](SKILL.md). Reference content (trigger phrases, scope enum,
> Check 0 explanatory prose, output format template, error recovery, evaluation
> criteria, CI output-format routing) lives here. The hot-path SKILL.md keeps Step 0
> Discovery + the 6-point Check 1-6 protocols.

---

## Auto-Trigger Phrases

### Korean
- "프로젝트 건강 검진", "시스템 감사"
- "프로젝트 상태 확인", "전체 점검"
- "빌드 전 검증", "품질 확인"
- "서버 상태 확인", "MCP 헬스체크"
- "메시지 로그 확인", "경고 요약", "자동 수정"
- "자동 수정 가능한 경고", "MessageLog 진단"

### English
- "Audit the project", "Health check"
- "Is the project healthy?", "System status"
- "Pre-build validation", "Quality check"
- "Check server status", "Project diagnostic"
- "Message log", "Warning summary", "Auto fix warnings"
- "MessageLog warnings", "Show fixable warnings"

---

## 6-Point Audit Framework — Scope Selection

```python
AUDIT_SCOPES = {
    "full":        [1, 2, 3, 4, 5, 6],  # Default: all 6 checks
    "--health":    [1],                   # Server health only
    "--config":    [2],                   # Config audit only
    "--assets":    [3],                   # Asset quality only
    "--perf":      [4],                   # Performance only
    "--code":      [5],                   # Code quality only
    "--messages":  [6],                   # Message log diagnostics only
}
```

### Check 1 Signal Set — UE 5.7 vs 5.8+ (Issue #11392)

The Server Health check pulls different fields out of `ue_check_health()`
depending on detected engine version. UE 5.7 emits the legacy baseline;
UE 5.8+ adds Epic Unreal MCP coexistence detection (#11501) and four
MCPClientToolset visibility signals from #11371/#11401, plus a name+URL
sample from #11392.

| Engine | Check 1 surface |
|---|---|
| ≤ 5.7 | server health + PDB index age + project path validation + engine version |
| ≥ 5.8 | above + `epic_mcp_coexist.detected` (advisory escalation on `true`) + `capabilities.mcp_servers_configured` + `capabilities.mcp_servers_active` + `capabilities.engine_toolsets_registered` + `capabilities.mcp_servers_sample` (≤ 3 entries) |

The Skill body in [SKILL.md § Check 1](SKILL.md#check-1-server-health)
controls rendering and scoring:
- `epic_mcp_coexist.detected == true` → **Warning** (with `message` +
  `resolution_paths`)
- All four MCPClientToolset signals → **Info** (Pass kept; mostly used
  to surface "MCPServers is empty" and "Editor-off, no live probe" UX
  hints).
- 5.7 projects never see these signals — gating happens in
  `is_58_or_later()` inside `check_health.rs`.

Field name note: the original issue spec proposed
`epic_unreal_mcp_plugin.{enabled, port, source}` and
`mcp_client_toolset_servers.{configured, sample}` as nested top-level
objects. The implementation keeps the flatter `epic_mcp_coexist` +
`capabilities.mcp_*` shape that #11371/#11501 shipped, with `port`
intentionally omitted (no fixed key on the Epic uplugin to parse
statically — RC live query is Out of Scope per the issue body).

---

## Check 0: Discovery Pre-Exploration (alternative description, Issue #6289)

> Explanatory companion to SKILL.md Step 0. Step 0 is the executed pre-flight; Check 0
> is the abstract decision-logic narrative. Both share the same goal: skip clean
> categories early to reduce MCP calls (~50% reduction on healthy projects).

```python
# 0a. Project-wide asset overview
ue_glob(pattern="*", domain="all", limit=50)
# Returns: assets, symbols, configs, source files (~200 tokens)
# → Determines which checks are relevant (skip empty categories)

# 0b. Config baseline (for Check 2)
ue_grep(query="DefaultEngine", domain="configs", limit=10)
# Returns: config key overview for consistency check
```

**Check 0 Decision Logic**:
- No assets found → Skip Check 3 (Asset Quality)
- No symbols loaded → Warn about PDB, skip Check 5 (Code Quality)
- Small project → Use lightweight thresholds for scoring
- Check 0 results feed into subsequent checks for targeted analysis

---

## Actionable Final Response (Issue #25735)

Hot-path contract: [SKILL.md Final Response Contract](SKILL.md). Cold detail:

### Offline / incomplete coverage (do not invent)

| State | Required behavior |
|-------|-------------------|
| Editor offline (Check 6 skipped) | Keep other findings actionable. Message Log: name the gap; action = launch Editor with Remote Control; verification = re-run `/ue-audit --messages` or `ue_editor_debug(operation="get_message_log")` after connect. |
| No `.utrace` (Check 4 skipped) | Action = capture Insights `.utrace`; verification = re-run Check 4 / `/ue-audit --perf`. |
| No build log (Check 5 fallback) | State `dependency_check` / `hotreload_check` was used; verification = re-run with `preflight` once `Saved/Logs/Build.log` exists. |
| MCP/server unreachable | Fail Server Health with evidence; do **not** invent asset/config findings. |

### Minimal example (warning summary)

```text
## Audit status
**Overall status**: warning
**Coverage**: partial — Editor offline

Findings:
1. severity=warning | category=Configuration
   evidence: r.AllowOcclusionQueries present (deprecated)
   action: replace with r.OcclusionQueries in DefaultEngine.ini
   verification: ue_analyze_config(operation="search_config", key="r.AllowOcclusionQueries") returns empty

2. severity=skipped | category=Message Log
   evidence: Editor Remote Control not connected
   action: launch Unreal Editor with Remote Control enabled for this project
   verification: /ue-audit --messages (or ue_editor_debug get_message_log) after Editor is up

Priority actions: (1) config CVar replace, (2) Editor connect + message-log re-audit
```

---

## Output Format

```text
## Audit status
**Overall status**: warning
**Coverage**: partial — Performance and Message Log unverified
**Score**: 8.5/10
Checks: 6 total | 3 Pass | 1 Warning | 2 Skipped

--- 1. Server Health: PASS ---
  MCP Server: Connected (uptime: 2h 15m)
  PDB Index: Loaded (12,450 symbols, updated 30m ago)
  Project Path: D:/MyProject (validated)
  Engine: UE 5.4.3

--- 2. Configuration Audit: WARNING ---
  Deprecated CVars found: 1
    - r.AllowOcclusionQueries (deprecated in 5.3, use r.OcclusionQueries)
  Config conflicts: 0
  Total config keys checked: 15

--- 3. Asset Quality: PASS ---
  Total assets scanned: 245
  Naming compliance: 94% (231/245)
  Violations:
    - Enemy_AI (should be BP_EnemyAI)
    - RockMaterial (should be M_Rock)

--- 4. Performance Check: SKIPPED ---
  No .utrace data available.
  Run: UnrealEditor > Tools > Unreal Insights to capture trace.

--- 5. Code Quality: PASS ---
  Preflight result: Clean
  Errors: 0
  Warnings: 3
    - Unused #include in MyCharacter.h
    - Potential null dereference in WeaponSystem.cpp:145
    - Missing UPROPERTY() on replicated field

--- 6. Message Log Diagnostics: WARNING ---
  Categories scanned: 8
  Total messages: 23
    MapCheck: 5 warnings, 2 fixable (Fix tokens)
    BlueprintLog: 3 errors, 0 fixable
    AssetCheck: 15 info
  Fixable issues: 2 (dry_run previewed)

--- Recommendations (Priority Ordered) ---
  1. Severity=error | Category=Message Log
     Evidence=BlueprintLog has 3 compile errors and no fix tokens
     Action=open the named Blueprints and fix their compiler errors
     Verification=re-query BlueprintLog and confirm error count is 0
  2. Severity=warning | Category=Configuration
     Evidence=search_config found deprecated r.AllowOcclusionQueries
     Action=replace it with r.OcclusionQueries in DefaultEngine.ini
     Verification=search_config no longer returns the deprecated CVar
  3. Severity=warning | Category=Message Log
     Evidence=MapCheck has 2 messages with fix tokens
     Action=preview each token with dry_run, then request approval to apply
     Verification=re-query MapCheck and confirm warning count decreased
  4. Severity=warning | Category=Asset Quality
     Evidence=14 assets violate the recorded prefix rule
     Action=rename the listed assets to their expected prefixes
     Verification=re-run the same prefix queries and reach >90% compliance
  5. Severity=warning | Category=Code Quality
     Evidence=preflight reports 3 warnings in named files/lines
     Action=fix each reported source location before the next build
     Verification=re-run preflight and confirm warning count is <5
  6. Severity=skipped | Category=Performance
     Evidence=no .utrace was found
     Action=capture an Unreal Insights trace for the target scenario
     Verification=re-run /ue-audit --perf and confirm Check 4 is no longer skipped
```

### Terminal shapes and offline coverage (Issue #25735)

The hot contract has three terminal shapes:

1. Findings: include severity, category, concrete evidence, operator action,
   and verification for every Warning/Fail or confidence-blocking skipped check.
2. Clean: state the evidence-backed healthy verdict and `no action required`.
3. Unverified/skipped: name the limitation and missing coverage; never convert
   missing evidence into a healthy claim.

For human Markdown, status severity and coverage are orthogonal: keep
`**Overall status**: warning|fail` when verified findings require it, and add
`**Coverage**: partial` for skipped or unverified requested checks. CI formats
replace this Markdown header with their artifact-only routing.

| State | Required behavior |
|-------|-------------------|
| Editor offline | Keep other findings actionable. For Message Log, name the category; action = connect Editor with Remote Control; verification = re-run `/ue-audit --messages` or `ue_editor_debug(operation="get_message_log")`. |
| No `.utrace` | Action = capture an Insights trace; verification = re-run `/ue-audit --perf`. |
| No build log | State that `dependency_check`/`hotreload_check` was used; verification = run `preflight` when `Saved/Logs/Build.log` exists. |
| MCP/server unreachable | Fail Server Health with evidence; do not invent asset/config findings. |

When a CI finding includes `AnalysisResult.suggestion`, it must be a concrete
action rather than a generic investigation string.

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Server health fails | MCP not connected | Report connection status, stop audit |
| Config search fails | No project configured | Use ue_check_health first to verify |
| Asset search empty | No assets in project | Report: "No assets found - is project path correct?" |
| Performance unavailable | No .utrace data | Skip with note, suggest how to capture |
| Preflight fails | No build_log_path | Use `dependency_check` or `hotreload_check` instead (no build log needed) |
| Message log empty | Editor not connected | Skip with note: "Launch Editor with Remote Control plugin enabled" |
| Fix token execution fails | Token expired or Editor state changed | Re-query messages, report stale state |

---

## Evaluation Criteria

### Activation Test Cases

**Positive (7)** - Should activate:
1. "프로젝트 상태 확인해줘" -> Activate (full audit)
2. "Health check" -> Activate (full audit)
3. "/ue-audit --config" -> Activate (config only)
4. "서버 연결 확인" -> Activate (health only)
5. "빌드 전에 검증해줘" -> Activate (code quality focus)
6. "경고 요약 보여줘" -> Activate (Message Log Diagnostics focus)
7. "/ue-audit --messages" -> Activate (Message Log Diagnostics only)

**Negative (3)** - Should NOT activate:
1. "빌드 에러 해결해줘" -> Use ue-debug skill
2. "APawn 구조 설명해" -> Use ue-explain skill
3. "새 기능 만들어줘" -> Use ue-scaffold skill

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Health check accuracy | 100% |
| Config issue detection | >85% |
| Naming violation detection | >90% |
| Full audit time | <2 minutes |
| MessageLog detection rate | >90% when Editor online |

---

**Status**: Phase 3 (Issue #5458: MessageLog integration)
**Related**: Issue #4800 (MCP-First Tool Selection), Issue #5296 (MessageLog operations), Issue #5458 (MessageLog audit integration)

---

## Output Format Routing (Issue #8413 Phase 4)

> **Additive feature** — when `$ARGUMENTS` does NOT contain `--output-format`
> AND `CI` env var is not set to `true`, behavior is **identical** to the
> pre-Phase-4 Markdown report above (AC #8). Integration only triggers
> when CI/automation explicitly opts in OR the `CI=true` convention is detected.

### When to activate

Parse `$ARGUMENTS` for both flags AND inspect environment:
- `--output-format json|sarif|junit` (explicit opt-in; case-sensitive value)
- `--output <path>` (optional — defaults to stdout if absent)
- **CI auto-detect**: if `$CI` env equals `true` (set by GitHub Actions / GitLab CI / CircleCI / Travis), default to `--output-format json` even when `--output-format` is absent. Explicit `--output-format` always wins.

If neither `--output-format` nor `CI=true` is present, ignore this entire
section and produce the Markdown report defined above.

### When activated (CI / automation mode)

Skip the human-readable Markdown report. Instead:

1. **Collect findings during the audit** as an in-memory list of
   `AnalysisResult` objects, one per finding (one entry per audit
   category that flags an issue):
   ```json
   {
     "tool": "ue-audit",
     "rule_id": "AUDIT-CONFIG-MISSING-DEFAULT",
     "severity": "warning",
     "file_path": "Config/DefaultEngine.ini",
     "line": 1,
     "column": null,
     "message": "DefaultEngine.ini missing required section [/Script/Engine.RendererSettings]",
     "suggestion": "Run UnrealEditor-Cmd -run=ResavePackages to regenerate defaults"
   }
   ```
   `severity` MUST be one of `error` / `warning` / `info` (lowercase). Map
   audit category severities (Health-critical → `error`, drift/inconsistency
   → `warning`, informational → `info`). `column` and `suggestion` are
   optional (omit or `null`). For project-wide findings without a specific
   line, use line=1.

2. **Write to temp file** as a bare JSON array (CLI accepts envelope or array):
   ```bash
   TMPFILE="${TMPDIR:-/tmp}/audit-findings-$$.json"
   ```

3. **Shell out to `narshamcp analyze`** for format conversion:
   ```bash
   narshamcp analyze \
     --source ue-audit \
     --input "$TMPFILE" \
     --format <fmt-from-args-or-json-when-CI=true> \
     --output <path-from-args-or-omit-for-stdout>
   ```

4. **Report only the file path** (or stdout indicator) — no Markdown summary.

### Argument forms supported

- `--output-format json` (space-separated) ✅
- `--output-format sarif` (space-separated) ✅
- `--output-format junit` (space-separated) ✅
- `--output /tmp/out.sarif` (space-separated) ✅
- `--output-format=json` (equals form) ❌ — emit error

### Examples

```bash
# Local dev: Markdown audit dashboard
/ue-audit

# CI: JSON for downstream report-pr or storage
CI=true /ue-audit --output-format json --output /tmp/audit.json
```

### Backward compatibility (AC #8)

The 6-point tool sequence, trigger phrases, argument parsing, and CI routing are
unchanged. Issue #25735 intentionally strengthens human Markdown findings and
recommendations; their wording is not byte-identical to the earlier template.

---

## Step 0: Discovery Pre-Exploration (full workflow, Issue #6352)

> **Purpose**: Before running all 6 audit checks sequentially, use lightweight Discovery Tools
> (`ue_grep` / `ue_glob`) to pre-identify which categories have potential issues.
> This allows skipping clean categories entirely (~50% fewer MCP calls on healthy projects).

### When to Run Step 0

- **ALWAYS** run Step 0 before the 6-point audit (unless user specifies a single `--category`)
- Skip Step 0 if user requests a specific check only (e.g., `--messages`, `--config`)

### Step 0 Workflow

```python
# 0a. Quick grep for error/warning indicators across project
ue_grep(query="Error|Warning|Deprecated", limit=10)
# Returns: matches across configs/source/logs (~50 tokens)

# 0b. Check if trace data exists (for Check 4: Performance Check)
# NOTE: ue_glob searches symbols/assets/configs, NOT filesystem.
# Use built-in Glob for filesystem paths.
Glob(pattern="**/*.utrace")
# Returns: trace file list — empty means skip Check 4

# 0c. Check if build log exists (for Check 5: Code Quality)
Glob(pattern="Saved/Logs/*.log")
# Returns: log file list — determines preflight vs dependency_check fallback

# 0d. Use Step 0 results to:
#   - Skip Check 4 (Performance Check) if no .utrace files found
#   - Route Check 5 to correct mode (preflight vs dependency_check)
#   - Prioritize categories with detected issues
#   - Short-circuit full audit on clean projects
```

### Step 0 Result Variables (downstream 참조용)

각 Check에서 아래 변수를 참조하여 불필요한 호출을 스킵합니다:

- `step0_has_errors` — 에러/경고 매치 존재 여부 (없으면 감사 경량화)
- `step0_has_trace` — .utrace 파일 존재 여부 (없으면 Check 4 스킵)
- `step0_has_build_log` — 빌드 로그 존재 여부 (Check 5 모드 결정)
- `step0_issue_categories` — 이슈가 감지된 카테고리 목록 (우선 검사 대상)

### Step 0 Decision Logic

```text
Step 0 result → Decision:
  No errors/warnings found  → Lightweight audit (skip detailed Check 2/3)
  No .utrace files          → Skip Check 4 (Performance Check), suggest trace capture in report
  No build log              → Check 5 uses dependency_check (not preflight)
  Errors in specific domain → Prioritize that category's detailed check
```

---

## Check 1: Server Health — version-gated surfaces (rationale)

### Issue #11501 — Epic Unreal MCP coexistence surface

When `health.epic_mcp_coexist.detected == true`, **escalate to Warning** and
surface the message to the user verbatim. Include `health.epic_mcp_coexist.resolution_paths`
so the operator can apply the decision tree (port split / disable Epic plugin /
disable NarshaMCP). Issue #25806 replaced the former `doc_url` field: it pointed
at `docs/migration/EPIC_UNREAL_MCP_COEXISTENCE.md` in the private
`Next-Stage-Inc/ue-code-mcp` repo, so it 404'd for every reader outside the org
and the doc does not ship in the package either. The three resolution paths now
travel inside the payload, so this surface is self-contained. No
extra MCP call is required — the field is part of the existing
`ue_check_health()` response. UE 5.7 and earlier never populate `detected=true`
and 5.8+ projects with Epic disabled report `detected=false`, so this surface
is silent in the common case.

### Issue #11392 — 5.8 MCPClientToolset visibility

When `health.engine_version.major == 5 && minor >= 8`, surface (in the report
body, not just as raw JSON) the four 5.8-only capability fields:

- `capabilities.mcp_servers_configured` — count of `MCPServers=` rows
  declared in the project's `EditorPerProjectUserSettings.ini`. Sourced
  offline (no Editor required).
- `capabilities.mcp_servers_active` — count that the live `ToolsetRegistry`
  RC probe reports as actively connected. Editor-off path emits `0`.
- `capabilities.engine_toolsets_registered` — total `ToolsetRegistry`
  toolsets loaded by the engine (≈22 on a stock 5.8 install). Editor-off
  emits `0`.
- `capabilities.mcp_servers_sample` — up to 3 `{name, url}` entries (sorted
  by name) lifted from the same .ini parse. Rendered as a small markdown
  table when non-empty; the count + an "(empty)" hint when the array is
  `[]`.

Scoring rules for this surface:

- `mcp_servers_configured == 0` → **Info** (Pass kept). Render a one-line
  hint: *MCPClientToolset.MCPServers is empty — NarshaMCP↔Editor handshake
  inactive; declare a server in `Editor Preferences > Plugins > MCP Toolset
  Servers` to enable live tooling.*
- `mcp_servers_configured > 0 && mcp_servers_active == 0` (Editor-off
  probe) → **Info** (Pass kept). No remediation — just an observation that
  the count comes from offline parse.
- `mcp_servers_configured > 0 && mcp_servers_active > 0` → no extra
  output; the configured count + sample table is enough context.
- This dimension is evaluated **independently** of the
  `epic_mcp_coexist.detected` Warning above. A 5.8 project can have an
  Epic-plugin conflict AND a configured NarshaMCP server simultaneously;
  both messages should appear.

UE 5.7 and earlier never emit any of these four fields (gated by
`is_58_or_later()` in `check_health.rs`); the 5.8 surface stays silent
on older engines and on source builds whose `Build.version` did not
resolve a major/minor pair.

---

## Check 2: Config Override Cascade example (Issue #7315)

**2b: Config Override Cascade** — if platform configs exist, trace override chain:

```python
ToolSearch(query="select:mcp__narshamcp__ue_diff")
platform_configs = ue_glob(pattern="Config/**/*.ini")
if len(platform_configs) >= 2:
    chain = ue_diff(operation="compare_config_chain", config_files="DefaultEngine.ini,WindowsEngine.ini")
    # Returns: chain steps with overrides/additions, final_resolved values
    # Flag surprising overrides (e.g., platform disabling a Default-enabled feature)
```

---

## Check 3: Naming Convention Rules (full table)

Used in compliance scoring. The `ue_search_assets` prefix pattern (`BP_*`, `M_*`, etc.)
queries each type by its expected prefix:

| Type | Expected Prefix |
|------|----------------|
| Blueprint | `BP_` |
| Material | `M_` |
| Material Instance | `MI_` |
| Niagara System | `NS_` |
| GameplayAbility | `GA_` |
| GameplayEffect | `GE_` |
| StateTree | `ST_` |
| BehaviorTree | `BT_` |
| Animation Montage | `AM_` |
| Widget Blueprint | `WBP_` |

---

## Check 5: Code Quality — option variants (rationale)

The Check 5 modes (`preflight` / `dependency_check` / `hotreload_check`) plus two
symbol-analysis options:

```python
# Option A: build log available → preflight scan
preflight = ue_fix_errors(mode="preflight", build_log_path="<path>/Saved/Logs/Build.log")

# Option B (recommended fallback): dependency check — no build log needed
dep_check = ue_fix_errors(mode="dependency_check")
# Returns: missing modules, circular deps, unused deps

# Option C: Hot-reload risk check — needs git
hotreload = ue_fix_errors(mode="hotreload_check", auto_detect_git=True)

# Option D: Dead UFUNCTION detection (unused BlueprintCallable functions)
ToolSearch("select:mcp__narshamcp__ue_analyze_symbols")
ufunction_stats = ue_analyze_symbols(operation="ufunction_usage")
# Positive usage rows remain useful when detection is partial. Only report an
# authoritative unused/empty set when detection_complete=true and
# detection_truncated=false; otherwise label absence unknown and disclose
# detection_diagnostics.

# Option E: Replication Safety Check (Issue #7315) — for replicated Actors
ToolSearch(query="select:mcp__narshamcp__ue_diff")
for class_name in ["ALyraGameState", "ALyraPlayerState"]:  # Adapt to project
    repl = ue_diff(operation="replication_audit", class_name=class_name)
    # Returns: has_issues, declared_not_registered[], push_model_coverage{}
```

> `preflight` requires `build_log_path`. If absent, fall back to `dependency_check`
> (always works) or `hotreload_check` (needs git).

---

## Check 6: Message Log Diagnostics — full 5-step detail (Issue #5296, #5458)

```python
ToolSearch("select:mcp__narshamcp__ue_editor_debug")

# Step 1: Get category overview (no category = all categories with counts)
overview = ue_editor_debug(operation="get_message_log")

# Step 2: For categories with errors/warnings, get detailed messages
for category in categories_with_issues:
    details = ue_editor_debug(
        operation="get_message_log",
        log_category=category,
        severity_filter="Warning",
        max_messages=50,
        include_tokens=True,
    )

# Step 3: Identify fixable messages (those with Fix tokens)

# Step 4: Preview fixes with dry_run before executing
for fixable_message in messages_with_fix_tokens:
    preview = ue_editor_debug(
        operation="execute_message_fix",
        log_category=category,
        message_index=fixable_message.index,
        fix_token_index=0,
        dry_run=True,
    )

# Step 5: Execute fix (only with user confirmation) and re-query to verify
fix_result = ue_editor_debug(
    operation="execute_message_fix",
    log_category=category,
    message_index=fixable_message.index,
    dry_run=False,
)
```

**Known Categories**: MapCheck, BlueprintLog, AssetCheck, PIE, LoadErrors, LightingResults, DataValidation, PackagingResults, SlateStyleLog, SourceControl, HotReload, LiveCoding, AssetTools.

**SAFETY**: `execute_message_fix` is always called with `dry_run=True` in audit mode. Actual fix execution should only happen when the user explicitly requests it after reviewing the audit report.
