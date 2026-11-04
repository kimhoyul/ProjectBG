---
name: ue-architecture-lint
description: "Purpose: Static architecture violation scanner for AI-generated UE C++ code. 11 rules (ARCH-001~011): inheritance depth, God Class, missing components, circular deps, Runtime/Editor xref, excessive deps, replication mismatch, GAS bypass, Tick abuse, missing UPROPERTY, unguarded FSlateApplication::Get(). Triggers: '아키텍처 린트', 'architecture lint', '구조 검사', 'design check', '코드 구조 분석'."
compatibility: "Python 3.10+. MCP server NOT required (standalone script). MCP optional for --fix mode."
argument-hint: "(no args = full scan), --scope staged|all|module:<Name>, --fix, --threshold ARCH-XXX:param=val, --disable ARCH-XXX"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  issue: "#7590"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Architecture Lint — AI-Generated Code Architecture Validator

**Issue**: #7590 (based on #7580 community data — 14 critical/high "AI code hides architectural violations")
**Purpose**: Detect 10 architecture anti-patterns (ARCH-001~010) in UE C++ code using 6 existing MCP tools

> **Reference**: Trigger phrases, full threshold config schema, ARCH-001~010 rule definition tables, MCP tool examples, error recovery, output format templates, standalone Python CLI usage, CI output-format routing (Issue #8413 Phase 4) — all in [REFERENCE.md](REFERENCE.md).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the arch_lint operation automatically**.
> Do NOT just display documentation — call the MCP tool!

### Execution Protocol

0. **PDB-first pre-pass for ARCH-001/002/007 (Issue #20393)** — call the native
   whole-project validate op BEFORE the grep/heuristic paths:
   ```python
   ue_analyze_symbols(operation="symbol_health",
                      params={"health_scope": "project",
                              "checks": ["god_class", "deep_inheritance", "unregistered_replication"]})
   ```
   - `god_class` → **ARCH-002** (op threshold methods>30 = `max_methods`; members>40 is an op-only v1 constant)
   - `deep_inheritance` → **ARCH-001** (op reports full-chain depth>6 and carries the actual `depth`;
     re-filter with this skill's project-relative `max_inheritance_depth` (default 3) using the carried depth)
   - `unregistered_replication` → **ARCH-007** (same validator as `validate_replication_registration`)
   - For ARCH-001/002 report rows, use each finding's `file`/`line` first.
     `location_precision="class"` means the file is authoritative but the
     declaration line is unavailable (`line:0`); only consult Phase 1's
     class→file map when `location_precision="unavailable"`.
   These findings are PDB-derived (deterministic method counts / inheritance depth / registration
   cross-validation — no grep false positives). **Fallback (coverage-preserving)**: if the call
   fails, fall back to step 1's `arch_lint` / grep paths for all three rules. Otherwise consult
   `summary.checks_unavailable`: `deep_inheritance` names ARCH-001.
   **Narrow the fallback ONLY on `summary.checks_unavailable_explains_degrade`.** `degraded:true` is
   one boolean over SEVERAL independent sources (an unusable DetailStore, the unpublished inheritance
   maps, an incomplete `dead_ufunction` cross-ref), so it alone cannot tell you what to re-derive —
   and enumerating the sources yourself will drift as sources are added. The response publishes the
   predicate directly:
   - `checks_unavailable_explains_degrade: true` — the inheritance maps are the **sole** reason this
     response is degraded. Only here may you narrow: fall back for just the rules the named checks
     map to (`deep_inheritance`→ARCH-001) and keep ARCH-002 / ARCH-007. This is safe because every
     check that can fail to answer feeds the predicate — a `capped`/`skipped`
     `unregistered_replication` sweep (ARCH-007) or an incomplete `dead_ufunction` cross-ref clears
     it too, so a `true` here really does mean the kept rules are answerable.
   - `detail_store_unavailable: true` — the granular row-check signal this rule used to lack. It is
     set when the DetailStore is missing, bounded, empty, or its row scan is truncated; any of those
     can make `god_class` partial, so ARCH-002 must fall back whenever it is set.
   **Fail-safe floor — fall back for all three rules whenever any of these holds**: `degraded:true`
   with `checks_unavailable_explains_degrade` **false** (something else degraded it too — the
   ordinary cold start degrades on both axes at once, and narrowing there would accept an empty
   `god_class` array as an authoritative ARCH-002 clean bill); either key **absent** (an older server
   binary); or the call failed. Absence must never be read as "nothing unavailable".
   Never treat an unavailable check's zero count as authoritative.
1. **Call MCP tool** (ARCH-003~006 / 008~011 — and the ARCH-001/002/007 fallback above):
   ```python
   ue_fix_errors(operation="arch_lint", scope="all")
   ```
   > Scope options: `"all"` (default), `"staged"`, `"module:Combat"`
   > Disable rules: `disabled_rules=["ARCH-003", "ARCH-010"]`
2. **Read the JSON response**: violations array with rule, severity, file, line, description
3. **Explain findings**: Group by severity (ERROR > WARNING > INFO), suggest fixes; merge the
   step-0 symbol_health findings into the same report (map check→ARCH id per the table above;
   dedupe a class reported by both sources, preferring the PDB finding)
4. **If `--fix` requested**: For ARCH-007, use built-in `Read` + `Edit` to add missing DOREPLIFETIME

### Fallback: Python Script

If MCP server is not available, fall back to the standalone Python script:
```bash
python scripts/quality/ue_arch_lint.py --project "$PROJECT_PATH" $ARGUMENTS
```

### MCP is Single Source of Truth

The Rust implementation in `ue_fix_errors(operation="arch_lint")` is the primary path.
Python script (`scripts/quality/ue_arch_lint.py`) is the standalone fallback.

---

## Default Thresholds (Phase 0 needs these inline)

모든 규칙에는 기본 임계값이 있으며, 프로젝트별로 오버라이드할 수 있습니다 (config schema +
override CLI 사용법: [REFERENCE.md § Threshold Configuration](REFERENCE.md#threshold-configuration)).

| Rule | Parameter | Default |
|------|-----------|---------|
| **ARCH-001** | `max_inheritance_depth` | 3 |
| **ARCH-002** | `max_methods` / `max_lines` | 30 / 1500 |
| **ARCH-003** | `min_components` | 1 |
| **ARCH-006** | `max_public_deps` | 10 |
| **ARCH-008** | `health_patterns` | `["Health\\s*[-+]=", "SetHealth", "->Health\\s*="]` |
| **ARCH-009** | `tick_violation_threshold` | 1 |

> ARCH-004, ARCH-005, ARCH-007, ARCH-010, **ARCH-011** are pattern-based (no numeric threshold).
> Full rule definitions: [REFERENCE.md § Rule Reference](REFERENCE.md#rule-reference-10-rules).

### ARCH ↔ `symbol_health` check mapping (Issue #20393)

| ARCH rule | symbol_health check | threshold relationship |
|---|---|---|
| ARCH-001 (inheritance depth) | `deep_inheritance` | op: full-chain depth>6 (carries `depth`); skill re-filters project-relative `max_inheritance_depth`=3 |
| ARCH-002 (God Class) | `god_class` | op methods>30 == skill `max_methods`; op members>40 is a **new op-only v1 constant** (no skill counterpart); skill `max_lines` stays grep-side |
| ARCH-007 (replication mismatch) | `unregistered_replication` | identical validator (`validate_replication_registration` full-Source sweep) |
| (no ARCH id) | `dead_ufunction`, `virtual_no_override` | op-only checks — surfaced as extra INFO sections in the report |

Rules NOT covered by the op (ARCH-003~006, 008~011) keep their existing arch_lint/grep paths.
`summary.checks_unavailable` names inheritance checks whose zero counts are non-authoritative;
fall back for their mapped ARCH rules. `summary.detail_store_unavailable` is the granular
row-check signal for `god_class`, so ARCH-002 must fall back whenever it is set.
`summary.checks_unavailable_explains_degrade` permits a narrowed inheritance-only fallback only
when it is `true`; if either granular key is absent, or the predicate is `false` while
`degraded:true`, fall back for 001/002/007 (backward-compatible fail-safe, no coverage loss).
>
> **ARCH-011** (Issues #10856 / #10892 / #10984) flags unguarded `FSlateApplication::Get()` calls — cook commandlet crash risk per Issue #10756 RCA / PR #10793. **ERROR** severity (graduated from Warning in #10984; override via `severity_overrides`). v2 brace-depth-aware guard-stack + 10-line lookback fallback (PR #10934). Remaining limitations (multi-line early-return, cross-function `ShouldCreateSubsystem` analysis) documented in REFERENCE.md.

---

## 5-Phase Workflow

### Phase 0: Parse Arguments & Load Config

```text
1. Parse $ARGUMENTS:
   - (empty) → --scope all (full project scan)
   - --scope staged → git diff --cached --name-only | filter *.h *.cpp
   - --scope all → scan all Source/**/*.h and Source/**/*.cpp
   - --scope module:Combat → scan Source/**/Combat/**
   - --fix → enable auto-fix for ARCH-007, ARCH-009
   - --threshold ARCH-006:max_public_deps=20 → one-off override
   - --disable ARCH-003,ARCH-010 → skip specific rules

2. Load config (priority: CLI > project file > defaults):
   a. Read defaults from "Default Thresholds" table above
   b. If .ue-arch-lint.json exists in project root → merge overrides
   c. If --threshold or --disable CLI args → apply on top
   d. Log effective thresholds in report header
```

### Phase 1: Scope Discovery (Step 0 — lightweight pre-assessment)

Determine which files and classes to scan:

```python
# For --scope staged:
git diff --cached --name-only -- '*.h' '*.cpp'
# OR if no staged changes, use recent commits:
ue_analyze_symbols(operation="detect_changes", scope="staged")

# For --scope all:
ue_grep(params={"query": "UCLASS(", "domain": "source", "limit": 200})

# For --scope module:Name:
ue_grep(params={"query": "UCLASS(", "domain": "source", "path_filter": "Name"})
```

Collect candidate classes and their file paths. If 0 classes found, report "No UE classes in scope" and exit.

### Phase 2: Data Collection (6 MCP tools, parallelize where possible)

**Group A — class-level**:
```python
# ARCH-001: Inheritance hierarchy for each candidate class
ue_analyze_symbols(operation="trace_hierarchy", class_name="<ClassName>", direction="up")

# ARCH-002: Method count
ue_analyze_symbols(operation="smart", class_name="<ClassName>")
# → get_methods result includes method list; count them

# ARCH-003: Component usage check
ue_grep(query="CreateDefaultSubobject", path_filter="<ClassName>.cpp")
```

**Group B — module-level**:
```python
# ARCH-004: Module dependency graph
ue_analyze_source(operation="map_dependencies")

# ARCH-005/006: Build.cs analysis
ue_grep(query="PublicDependencyModuleNames", domain="source", file_filter="*.Build.cs")
# Then ue_read each Build.cs to check module type and count deps
```

**Group C — pattern-level**:
```python
# ARCH-007: Replication mismatch
ue_diff(operation="replication_audit", class_name="<ClassName>")
# AND/OR:
ue_analyze_source(operation="cross_check", class_name="<ClassName>")

# ARCH-008: GAS bypass detection (only if GAS module present)
ue_grep(query="GameplayAbilitiesModule\\|GameplayAbilities", domain="source", file_filter="*.Build.cs")
ue_grep(query="Health\\s*[-+]=\\|SetHealth\\|->Health\\s*=", domain="source")

# ARCH-009: Tick abuse (heavy ops in Tick body)
ue_grep(query="::Tick(", domain="source")
# Then for each Tick implementation, check for: NewObject, SpawnActor, LoadObject,
# FindObject, GetAllActorsOfClass, GetAllActorsWithTag inside function body

# ARCH-010: Missing UPROPERTY/UFUNCTION
ue_analyze_source(operation="extract_pattern", specifier_type="UPROPERTY", class_filter="<ClassName>")
# Compare against raw member declarations in header
```

### Phase 3: Rule Evaluation

For each ARCH rule, evaluate collected data:

```text
For each class in scope:
  ARCH-001: hierarchy_depth > 3 → WARNING
  ARCH-002: method_count > 30 OR file_lines > 1500 → WARNING
  ARCH-003: is_actor_subclass AND component_count == 0 → WARNING
  ARCH-004: dependency_cycles.length > 0 → ERROR
  ARCH-005: runtime_module includes editor_header → ERROR
  ARCH-006: public_deps_count > 10 → WARNING
  ARCH-007: replicated_without_registration.length > 0 → ERROR
  ARCH-008: gas_project AND direct_health_manipulation → WARNING
  ARCH-009: perf_lint_tick_violations.length > 0 → WARNING
  ARCH-010: undeclared_bp_members found → INFO

  Check NOLINT suppression before adding to report
  ( `// NOLINT(ARCH-XXX)` for specific rule, `// NOLINT` for all ARCH rules)
```

### Phase 4: Report Generation

Generate severity-ranked Markdown report (Errors → Warnings → Info → Suppressed → Summary).
Full template: [REFERENCE.md § Output Format](REFERENCE.md#output-format).

> **CI / automation mode**: when `--output-format json|sarif|junit` is passed OR
> `CI=true` env is set, skip Markdown rendering and shell out to `narshamcp analyze`
> for format conversion. Full routing rules: [REFERENCE.md § Output Format Routing](REFERENCE.md#output-format-routing-issue-8413-phase-4).

### Phase 5: Auto-Fix (--fix only)

If `--fix` flag is set, attempt automatic fixes for supported rules:

| Rule | Auto-Fix | Method |
|------|----------|--------|
| **ARCH-007** | Add missing DOREPLIFETIME registration | Edit .cpp `GetLifetimeReplicatedProps` function |
| **ARCH-009** | Suggest Timer conversion | Generate timer-based replacement code snippet |
| Others | Manual fix guide only | Display suggestion in report |

For ARCH-007 auto-fix:
```python
# Read the .cpp file, find GetLifetimeReplicatedProps, add missing DOREPLIFETIME lines
ue_read(identifier="<ClassName>.cpp")
# Then use built-in Edit tool to insert the missing registration
```

---

## See Also

- [REFERENCE.md](REFERENCE.md) — Trigger phrases, full threshold config schema, ARCH-001~010 rule definition tables, MCP tools used, MCP examples, limitations, error recovery, output format templates (Markdown + console), standalone Python CLI guide, related skills, Output Format Routing (Issue #8413 Phase 4)
