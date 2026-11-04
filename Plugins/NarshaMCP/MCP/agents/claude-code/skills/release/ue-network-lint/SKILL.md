---
name: ue-network-lint
model: opus
description: "Purpose: Static network anti-pattern scanner for UE multiplayer C++ code. Scans RPCs, replication, and architecture for NET-001~010 rules. Triggers: '네트워크 린트', 'network lint', 'RPC 안티패턴', 'multiplayer lint', '멀티플레이어 코드 검사'."
compatibility: "Requires NarshaMCP MCP server (v0.9.7+)"
argument-hint: "(no args = full scan), --class <name>, --rules NET-001,NET-005, --strict, --self-test"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  issue: "#7598"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Network Lint — Static Multiplayer Anti-Pattern Scanner

**Issue**: #7598 (community-scrape #7580 follow-up)
**Purpose**: Scan UE C++ source code for 10 networking anti-patterns (NET-001~010) without requiring runtime data

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the scan workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Scope**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, scan all project source files.
   > `--class AMyCharacter` scans a specific class.
   > `--rules NET-001,NET-005` scans only specific rules.
   > `--strict` treats any warning as a blocking issue (net_strict=true).
2. **Execute the 3-phase workflow** below
3. **Generate severity-ranked report** with file:line, rule ID, message, and suggestion

---

## Auto-Trigger Phrases

### Korean
- "네트워크 린트", "멀티플레이어 린트", "RPC 안티패턴 검사"
- "네트워크 코드 검사", "멀티플레이어 코드 검사"
- "NET 규칙 검사", "네트워크 규칙 스캔"

### English
- "network lint", "multiplayer lint", "RPC anti-pattern check"
- "network code scan", "multiplayer code check"
- "check NET rules", "scan network rules"

### Disambiguation

| Skill | Purpose | When to Use |
|-------|---------|-------------|
| `/ue-network-lint` | **Per-line violation scanning** (NET-001~010 rules, like a linter) | Find specific anti-patterns in RPC/replication code |
| `/ue-network-audit` | **Health overview** (bandwidth estimate, DOREPLIFETIME validation, RPC inventory) | Architecture review, pre-deployment health check |
| `/ue-insights-profiler` | **Runtime profiling** (requires .utrace) | FPS drops, live network performance analysis |

Key differentiator: **lint = per-line violations with rule IDs, audit = aggregate health metrics**.

---

## Rule Reference (10 rules)

### RPC Patterns (NET-001~004)

| Rule | Name | Severity | Description |
|------|------|----------|-------------|
| NET-001 | RPC ownership mismatch | Warning | Server RPC called on an actor the client does not own, without `IsLocallyControlled()` or ownership guard |
| NET-002 | Multicast RPC for persistent state | Warning | Multicast RPC modifies persistent state (door/switch/inventory) instead of using replicated properties — late joiners miss it |
| NET-003 | `GetPlayerController(0)` usage | Error | `GetPlayerController(0)` returns different results on listen-server vs dedicated-server vs client — unguarded usage breaks dedicated servers |
| NET-004 | BeginPlay RPC without Authority check | Warning | RPC call inside `BeginPlay` without `HasAuthority()` or `GetLocalRole()` guard — fires on all instances including clients |

### Replication Safety (NET-005~007)

| Rule | Name | Severity | Description |
|------|------|----------|-------------|
| NET-005 | OnRep C++/BP behavior difference | Warning | C++ `OnRep_*` is NOT called on server automatically (unlike Blueprints). Server must manually call `OnRep_*` after direct property assignment |
| NET-006 | Late-joiner unsafe RPC for initial state | Warning | Client RPC used in BeginPlay/PostInitializeComponents to set initial state without a corresponding `Replicated` property as fallback — late joiners miss the RPC |
| NET-007 | Non-atomic compound state replication | Warning | Multiple related primitive members (e.g., Health, MaxHealth, bIsDead) replicated individually instead of in a single `USTRUCT` — can arrive in different frames causing transient invalid states |

### Architecture (NET-008~010)

| Rule | Name | Severity | Description |
|------|------|----------|-------------|
| NET-008 | Push Model unused for frequent changes | Info | Replicated property modified in `Tick` without `MARK_PROPERTY_DIRTY` macro (UE 5.1+ Push Model). Wastes bandwidth on unchanged properties |
| NET-009 | Bandwidth excess: unconditional Tick replication | Critical | Replicated property assigned unconditionally every frame in `Tick` — floods bandwidth without dirty checking or delta comparison |
| NET-010 | GameState/PlayerState data abuse | Warning | Single State class has >15 replicated properties, or contains non-essential data (cosmetic, UI-only) that belongs in a separate component |

### Suppression

- `// NOLINT(NET-XXX)` on the same line suppresses a specific rule
- `// NOLINT` suppresses all rules for that line

---

## 3-Phase Workflow

### Phase 0: Parse Arguments

```text
Parse $ARGUMENTS:
- (empty) → full project scan
- --class AMyCharacter → scan only that class
- --rules NET-001,NET-005 → filter to specific rules
- --strict → any warning blocks (net_strict=true)
```

### Phase 1: Discovery — Find Candidate Files

Use `ue_grep` to find files containing networking-relevant patterns:

```python
# RPC declarations and implementations
ue_grep(query="UFUNCTION.*Server|UFUNCTION.*Client|UFUNCTION.*NetMulticast", domain="source", regex=true)

# Replication-related code
ue_grep(query="Replicated|DOREPLIFETIME|ReplicatedUsing|GetLifetimeReplicatedProps", domain="source")

# High-signal specific anti-patterns
ue_grep(query="GetPlayerController(0)", domain="source")

# If --class specified, narrow scope
ue_grep(query="class AMyCharacter", domain="source")
```

Collect unique `.h` and `.cpp` file pairs from results. Exclude `.generated.h` and `.gen.cpp` files (auto-generated, not authored code).

### Phase 2: Analysis — Run Network Rules

#### Phase 2A: Replication-Aware Rules (NET-005, NET-007, NET-008, NET-010) — `ue_analyze_source` + `ue_analyze_symbols`

For replication-specific rules, **always use `ue_analyze_source` first** (structured macro parsing):

```python
# NET-005 + NET-007: Cross-check replicated properties and OnRep callbacks
ue_analyze_source(operation="cross_check", class_name="<target_class>")
# → matched[].on_rep_function present → check server-side manual call (NET-005)
# → missing_registration[] → also feeds /ue-network-audit overlap check
# → count related primitives by name prefix → NET-007 if 3+ related

# NET-008 + NET-010: Extract UPROPERTY specifiers for property counting
ue_analyze_source(operation="extract_pattern", specifier_type="UPROPERTY", class_filter="<target_class>")
# → Replicated properties assigned in Tick without MARK_PROPERTY_DIRTY → NET-008
# → *State class with >15 replicated properties → NET-010
```

**NET-005 Enhanced (HARD-005)**: If setter function has no direct `OnRep_X()` call:
```python
# Check if OnRep is called anywhere in the same class (helper function pattern)
ue_grep(query="OnRep_<PropertyName>", domain="source", path_pattern="*<ClassName>*")
# If OnRep_X is called somewhere in the class → suppress NET-005 (indirect call OK)
# If OnRep_X is never called anywhere in the class → flag NET-005
```

**NET-010 Enhanced (HARD-010)**: Count inherited replicated properties:
```python
# Step 1: Get parent class chain for State classes
ue_analyze_symbols(operation="smart", class_name="<StateClass>", direction="up")
# → Returns ancestor chain: AChildState → AParentState → APlayerState → ...

# Step 2: For each project-level parent, count replicated properties
ue_analyze_source(operation="extract_pattern", specifier_type="UPROPERTY",
    class_filter="<ParentClass>")
# → Filter for "Replicated" specifier → count

# Step 3: Sum current class + all parent project classes
# If total > 15 → flag with inheritance breakdown:
#   "AChildState (8) + AParentState (10) = 18 total replicated properties"
```

**Fallback** (if `ue_analyze_source` returns error or tool unavailable):
Use `ue_read` + manual pattern matching for NET-005/007/008/010.

#### Phase 2B: RPC & Pattern-Based Rules (NET-001~004, NET-006, NET-009) — `ue_grep` + `ue_read` + `ue_analyze_symbols`

For each candidate file pair:

1. **NET-001** (Enhanced — HARD-001): Find Server RPC declarations, then trace callers:
   ```python
   # Step 1: Direct call site check (original)
   # Check for IsLocallyControlled(), GetOwner(), or HasAuthority() guard within ~10 lines

   # Step 2: Indirect call chain check (NEW — handles A→B→ServerRPC)
   ue_analyze_symbols(operation="smart", function_name="ServerXxx")
   # find_callers returns direct callers only
   # For each direct caller: check whether it has an ownership guard
   # For deeper evidence, repeat the query with a visited set and explicit round cap
   ```
   **Guard patterns recognized**: `IsLocallyControlled()`, `HasAuthority()`, `GetOwner()`, `GetLocalRole() == ROLE_Authority`, `ROLE_AutonomousProxy`

2. **NET-002** (Enhanced — HARD-002): Find `NetMulticast` UFUNCTION implementations → check if body modifies state:
   - Direct member assignment: `bDoorOpen = bOpen`
   - **Pointer/component mutation (NEW)**: `DoorComp->SetOpen(true)`, `Manager->bState =`, `Component->Set*()`
   - Cross-check: if modified variable/component also has `Replicated` specifier, it is doubly suspicious
   - **Safe patterns (no flag)**: `UGameplayStatics::Spawn*`, `PlaySound*`, `SpawnEmitter*` (transient FX only)

3. **NET-003**: Find `GetPlayerController(0)` → check for guards. **Guard patterns recognized**:
   - `#if WITH_EDITOR` / `#if !UE_SERVER` (compile-time)
   - `IsRunningDedicatedServer()` (runtime)
   - `GetNetMode() != NM_DedicatedServer` / `NM_Client` (net mode check)
   - Unguarded = violation

4. **NET-004** (Enhanced — HARD-004): Find `::BeginPlay(` implementations → scan **entire body including lambda/delegate blocks**:
   - Scan from opening `{` to matching closing `}` (not just immediate lines)
   - **Include lambda bodies**: `[this]() { ServerRPC(); }`, `[&]() { ClientRPC(); }`
   - **Include timer delegates**: `SetTimer(Handle, [this]() { ... })`
   - Flag RPC calls without `HasAuthority()` or `GetLocalRole() == ROLE_Authority` wrap
   - **Guard patterns**: same as NET-001 (HasAuthority, GetLocalRole, ROLE_Authority)

5. **NET-006**: Find Client RPC declarations called in BeginPlay/PostInitializeComponents → check if corresponding `Replicated` property exists as late-joiner fallback

6. **NET-009** (Enhanced — HARD-009): Find replicated property assignments inside `::Tick(`:
   - Flag if unconditional (no dirty check, no delta comparison, no `if` guard)
   - **Default-true detection (NEW)**: If assignment is inside `if (bCondition)` but `bCondition` is initialized with `= true` or `= 1` in the class header → add Info-level warning: "Condition variable initialized to true — may be effectively unconditional"

**NOLINT check**: For every candidate violation, check if `// NOLINT(NET-XXX)` or `// NOLINT` appears on the same line. If so, suppress and record in Suppressed list.

**NET-009/NET-008 dedup**: If NET-009 fires for a property, suppress NET-008 for the same property to avoid double-reporting.

#### Phase 2C (Optional): Deep Replication Analysis — symbols pack first, then `ue_diff`

When NET-005/006/007/008 candidates need class- or diff-level evidence, prefer the
**shipped `ue_analyze_symbols` replication pack** (Issue #20401 / #24721) before
`ue_diff`:

```python
# G2: DOREPLIFETIME / Replicated / push-model surface diff across git revisions
ue_analyze_symbols(operation="replication_diff", scope="staged")  # or unstaged|all
# → registration vs header declaration deltas; reinforces NET-005/007 class work

# G3: push-model MARK_PROPERTY_DIRTY coverage (project .cpp sweep)
ue_analyze_symbols(operation="push_model_coverage")
# → missing dirty marks reinforce NET-008; truncated/degraded flags must be reported honestly
```

Positive replication or dirty-mark findings remain actionable when either pack
operation is partial. A clean/no-findings verdict is allowed only when the
response succeeds, is not degraded or truncated, and
`scan_completeness.complete` is explicitly `true`. Missing completeness evidence
is not clean evidence. Otherwise label the clean result as
unknown and continue with the class-scoped `ue_diff`/source fallback below; never
convert partial absence into a clean networking verdict.

Apply the same rule to `ue_analyze_symbols(operation="trace_hierarchy")`: keep
known descendants, but an empty/capped/depth-clamped result or
`bp_descendants_source:"unavailable"` cannot prove that no replicated subclass
exists.

Fallback / class-scoped audit when pack ops are unavailable:

```python
ue_diff(operation="replication_audit", class_name="<target_class>")
# → push_model_coverage.missing → reinforces NET-008
# → declared_not_registered → additional findings beyond NET rules
```

### Phase 3: Report — Severity-Ranked Output

Generate a markdown report sorted by severity (Critical > Error > Warning > Info):

```markdown
## Network Lint Report

**Scanned**: N files | **Violations**: M total | **Critical**: C | **Strict mode**: on/off

### Critical (C items)

| # | File:Line | Rule | Pattern | Suggestion |
|---|-----------|------|---------|------------|
| 1 | AMyChar.cpp:78 | NET-009 | Replicated `Health` assigned in Tick unconditionally | Add dirty check: `if (NewHealth != Health) { Health = NewHealth; }` |

### Error (E items)

| # | File:Line | Rule | Pattern | Suggestion |
|---|-----------|------|---------|------------|
| 1 | AMyChar.cpp:102 | NET-003 | `GetPlayerController(0)` without dedicated server guard | Wrap in `if (!IsRunningDedicatedServer())` or use `GetOwningPlayerController()` |

### Warning (W items)

| # | File:Line | Rule | Pattern | Suggestion |
|---|-----------|------|---------|------------|
| 1 | AMyChar.cpp:45 | NET-001 | Server RPC `ServerFire` called without ownership check | Add `if (IsLocallyControlled())` guard before call |
| 2 | AMyChar.cpp:120 | NET-002 | Multicast RPC `MulticastSetDoorState` modifies `bDoorOpen` | Use `UPROPERTY(ReplicatedUsing=OnRep_DoorOpen)` instead |

### Info (I items)

| # | File:Line | Rule | Pattern | Suggestion |
|---|-----------|------|---------|------------|
| 1 | AMyChar.h:34 | NET-008 | Replicated `Score` modified in Tick without Push Model | Add `MARK_PROPERTY_DIRTY_FROM_NAME(AMyChar, Score)` after assignment |

### Suppressed (S items)
- AMyChar.cpp:89: NET-003 GetPlayerController(0) (NOLINT)

### Summary
- Rules checked: NET-001~010
- Files scanned: N
- Violations: M (C critical, E error, W warning, I info)
- Suppressed: S (NOLINT)
- False positive reduction: NOLINT suppression available per-line
```

---

## MCP Tools Used

| Tool | Phase | Purpose |
|------|-------|---------|
| `ue_grep` | 1, 2B | Find candidate files with RPC/replication patterns |
| `ue_read` | 2B | Read source files for rule pattern analysis |
| `ue_analyze_source` | 2A (primary) | Cross-check replication (NET-005/007), extract UPROPERTY specifiers (NET-008/010) |
| `ue_analyze_symbols` | 2A, 2B, 2C | **find_callers** (NET-001/005), **trace_hierarchy** (NET-010), **replication_diff** / **push_model_coverage** (NET-005/007/008 pack, Issue #20401) |
| `ue_diff` | 2C (fallback) | Class-scoped `replication_audit` when symbols pack is unavailable |

---

## Limitations

1. **Static analysis only** — cannot detect runtime-only networking issues (actual packet loss, desync, ownership transfer)
2. **Pattern matching** — may miss patterns inside macros or heavily templated code
3. **Push Model (NET-008)** requires UE 5.1+; older projects should suppress this rule via NOLINT
4. **Cross-file call tracing** — `find_callers` returns direct callers only. Deeper checks require explicit client-side frontier expansion with cycle and result caps.

### Known Limitations (Honest Assessment)

| Limitation | Affected Rule | Impact | Workaround |
|-----------|--------------|--------|-----------|
| Semantic property grouping uses name prefix only | NET-007 | May miss `Damage` + `CritMultiplier` as related (different prefix) | Manual review for combat/movement property groups |
| Condition variable runtime value unknown | NET-009 | `if (bAlwaysTrue)` passes dirty check even if always true | Info warning for `= true` default; manual review |
| Pointer-chain mutation detection is shallow | NET-002 | `GetSubsystem()->GetManager()->SetState()` (3+ depth) may be missed | Flag any non-trivial method call in Multicast body |
| Template/macro-heavy code | All rules | Pattern matching inside template instantiations unreliable | Suppress with NOLINT |
| Lambda capture context | NET-004 | Deeply nested lambda-in-lambda may be misattributed | Scan full BeginPlay body including nested blocks |

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `ue_grep` returns empty | No UE networking code in project | Display "No multiplayer/networking code found" |
| `ue_read` file not found | File path changed since grep | Skip file, continue scanning |
| `ue_analyze_source` fails | Source index not loaded or file not found | Fall back to `ue_read` + manual pattern matching |
| `ue_analyze_symbols` fails | PDB not loaded | Skip indirect call chain (NET-001), hierarchy counting (NET-010) |
| Class hierarchy unknown | PDB not loaded | Skip ownership-dependent checks in NET-001 |
| Too many files (>100) | Large project | Scan only files with RPC/Replicated patterns first |

---

## Output Format

```text
════════════════════════════════════════════
 Network Lint Report
════════════════════════════════════════════
 Scanned:    N files
 Violations: M total (C critical, E error, W warning, I info)
 Strict:     on/off
 Rules:      NET-001~010 (or filtered subset)

 [Critical] AMyChar.cpp:78   NET-009  Replicated Health assigned in Tick unconditionally
 [Error]    AMyChar.cpp:102  NET-003  GetPlayerController(0) without dedicated server guard
 [Warning]  AMyChar.cpp:45   NET-001  Server RPC called without ownership check
 [Info]     AMyChar.h:34     NET-008  Replicated Tick property without MARK_PROPERTY_DIRTY

 Suppressed: S items (NOLINT)
════════════════════════════════════════════
```

---

## Related Skills

- [`/ue-network-audit`](../ue-network-audit/SKILL.md) — Replication health overview + bandwidth estimation (complementary: audit first for big picture, then lint for per-line violations)
- [`/ue-perf-lint`](../ue-perf-lint/SKILL.md) — Performance anti-patterns (sibling lint skill, PERF-020~023 cover some replication)
- [`/ue-pre-commit`](../ue-pre-commit/SKILL.md) — Pre-commit safety analysis (may reference NET rules)
- [`/ue-diff`](../ue-diff/SKILL.md) — Replication audit for specific classes (used internally by Phase 2C)

## MCP Tool Examples

```python
# Discovery: find all RPC declarations
ue_grep(query="UFUNCTION.*Server|UFUNCTION.*NetMulticast", domain="source", regex=true)

# Read source for analysis
ue_read(identifier="AMyCharacter")

# Cross-check replication registration
ue_analyze_source(operation="cross_check", class_name="AMyCharacter")

# Extract replicated properties for counting
ue_analyze_source(operation="extract_pattern", specifier_type="UPROPERTY", class_filter="AMyPlayerState")

# Deep replication (preferred symbols pack, then ue_diff fallback)
ue_analyze_symbols(operation="replication_diff", scope="staged")
ue_analyze_symbols(operation="push_model_coverage")
ue_diff(operation="replication_audit", class_name="ALyraPlayerState")
```

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

1. **Collect findings during the NET scan** as an in-memory list of
   `AnalysisResult` objects, one per rule hit:
   ```json
   {
     "tool": "ue-network-lint",
     "rule_id": "NET-003",
     "severity": "error",
     "file_path": "Source/MyGame/MyChar.cpp",
     "line": 42,
     "column": null,
     "message": "Server RPC accepts client-supplied position without validation",
     "suggestion": "Add server-side bounds check before applying input"
   }
   ```
   `severity` MUST be one of `error` / `warning` / `info` (lowercase). Map
   the rule's table severity (Critical/Warning/Info) → `error`/`warning`/`info`.
   `column` and `suggestion` are optional (omit or `null`).

2. **Write to temp file** as a bare JSON array (CLI accepts envelope or array):
   ```bash
   TMPFILE="${TMPDIR:-/tmp}/network-lint-findings-$$.json"
   ```

3. **Shell out to `narshamcp analyze`** for format conversion:
   ```bash
   narshamcp analyze \
     --source ue-network-lint \
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
# Local dev: Markdown
/ue-network-lint

# CI: JSON auto-default + GitHub Code Scanning upload
CI=true /ue-network-lint --output-format sarif --output /tmp/net.sarif
```

### Backward compatibility (AC #8)

The NET scan workflow above is **unchanged**. Trigger phrases, argument
parsing, and Markdown report generation remain byte-identical to the
pre-Phase-4 baseline.
