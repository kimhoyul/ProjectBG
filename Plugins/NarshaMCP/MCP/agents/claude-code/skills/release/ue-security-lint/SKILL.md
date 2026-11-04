---
name: ue-security-lint
model: opus
description: "Purpose: AI-generated UE C++ code security vulnerability scanner. Scans for SEC-001~012 rules: input validation, UE-specific security, general code security, supply chain. Triggers: '보안 린트', 'security lint', '취약점 검사', 'SEC 규칙 검사', 'code security audit'."
compatibility: "Requires NarshaMCP MCP server (v0.9.7+)"
argument-hint: "(no args = full scan), --class <name>, --rules SEC-001,SEC-004, --strict, --self-test, --output-format json|sarif|junit, --output <path>"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  issue: "#7599"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Security Lint — AI-Generated Code Security Vulnerability Scanner

**Issue**: #7599 (community-scrape #7580 follow-up)
**Purpose**: Scan UE C++ source code for 12 security anti-patterns (SEC-001~012) without requiring runtime data

> Cold-path detail (full rule descriptions, MCP parameter examples, FP analysis, hard-case detection patterns):
> [REFERENCE.md](REFERENCE.md). SKILL.md keeps only the dispatch/routing logic.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the scan workflow automatically**.
> Do NOT just display documentation — actually run the MCP tools!

### Execution Requirements

1. **Scope**: `$ARGUMENTS`
   > Empty → scan all project source files.
   > `--class AMyCharacter` → scan a specific class.
   > `--rules SEC-001,SEC-004` → scans only specific rules.
   > `--strict` → any warning becomes blocking (`sec_strict=true`).
2. **Execute the 3-phase workflow** below.
3. **Generate severity-ranked report** with file:line, rule ID, message, and suggestion.

---

## Auto-Trigger Phrases

### Korean
- "보안 린트", "보안 검사", "시큐리티 린트"
- "취약점 검사", "보안 취약점 스캔"
- "SEC 규칙 검사", "코드 보안 검사"

### English
- "security lint", "security scan", "vulnerability scan"
- "security check", "code security audit"
- "check SEC rules", "scan security rules"

### Disambiguation

| Skill | Purpose | When to Use |
|-------|---------|-------------|
| `/ue-security-lint` | **Per-line security vulnerability scanning** (SEC-001~012: trust boundaries, data exposure, secrets, supply chain) | Find security anti-patterns in AI-generated UE C++ code |
| `/ue-network-lint` | **Per-line network anti-pattern scanning** (NET-001~010: RPC correctness, replication safety) | Find multiplayer networking correctness issues |
| `/ue-perf-lint` | **Per-line performance anti-pattern scanning** (PERF-001~023: Tick/allocation/replication overhead) | Find performance anti-patterns in hot code paths |
| `/ue-architecture-lint` | **Structural anti-pattern scanning** (ARCH rules: coupling, layering, module organization) | Find architectural design issues (#7590) |
| `/ue-audit` | **Project-wide health check** (broader scope: config, assets, build, code) | General project health diagnosis |

Key differentiator: **security-lint = trust boundary violations, data exposure, secret leaks, supply chain risks**.

> SEC-004 vs NET-001 fire independently on the same Server RPC (different trust models).
> No deduplication — both provide independent value. Full comparison:
> [REFERENCE.md § SEC-004 / NET-001 Deduplication](REFERENCE.md#sec-004-net-001-deduplication)

---

## Rule Reference (12 rules)

| Rule | Severity | One-line |
|------|----------|----------|
| SEC-001 | Warning  | Unvalidated external input (RPC params, network/file I/O) used without range/IsValid/sanitization |
| SEC-002 | Error    | Buffer overflow risk: `Memcpy`/`strcpy`/fixed-size arrays with unbounded user-controlled size |
| SEC-003 | Error    | Format string injection: user `FString` as format param to `UE_LOG`/`Printf`/`Format` |
| SEC-004 | Critical | RPC authority bypass: Server RPC trusts client values without server-side validation |
| SEC-005 | Warning  | Insecure deserialization: `NetSerialize`/`Serialize` without bounds check or array size limit |
| SEC-006 | Warning  | Missing cheat prevention: client-authoritative game-critical logic without server check |
| SEC-007 | Warning  | Sensitive data in logs: passwords/tokens/keys in `UE_LOG`/screen messages |
| SEC-008 | Critical | Hardcoded secrets: API keys/passwords/tokens as string literals |
| SEC-009 | Warning  | Insecure random: `FMath::Rand`/`rand()` for tokens/nonces/encryption keys |
| SEC-010 | Warning  | Resource leak on error path: file handles/sockets/`new[]` without cleanup on all exits |
| SEC-011 | Info     | Unknown Build.cs module dependency (not in engine or project Source/) |
| SEC-012 | Info     | Suspicious plugin dependency (not in Engine/Plugins, project Plugins, or Marketplace) |

> Full descriptions, grep patterns, analysis methods, FP risks, fix suggestions per rule:
> [REFERENCE.md § Rule Detail Table](REFERENCE.md#rule-detail-table-sec-001012)

**Suppression**: `// NOLINT(SEC-XXX)` (rule-specific) or `// NOLINT` (all rules) on the same line. Line-scope only — see [REFERENCE.md § NOLINT Suppression Syntax](REFERENCE.md#nolint-suppression-syntax).

---

## 3-Phase Workflow

### Phase 0: Parse Arguments

```text
Parse $ARGUMENTS:
- (empty)                → full project scan
- --class AMyCharacter   → scan only that class
- --rules SEC-001,SEC-004 → filter to specific rules
- --strict               → any warning blocks (sec_strict=true)
- --self-test            → run 4-step self-test
- --output-format <fmt>  → CI mode (see § Output Format Routing)
- --output <path>        → output file path (with --output-format)
```

### Phase 1: Discovery — Find Candidate Files

Use `ue_grep` for security-relevant patterns. The discovery queries map rules to grep
patterns; full executable examples in [REFERENCE.md § ue_grep (Phase 1: Discovery)](REFERENCE.md#ue_grep-phase-1-discovery).

| Rules | Pattern category | Tool |
|-------|------------------|------|
| SEC-001~003 | Input handling, format strings, buffer ops | `ue_grep` regex |
| SEC-004~006 | RPC declarations, NetSerialize, authority calls | `ue_grep` regex |
| SEC-007~009 | Sensitive keywords (case-insensitive), random calls | `ue_grep` regex `-i` |
| SEC-010 | Resource handles | `ue_grep` regex |
| SEC-011~012 | Build.cs / .uplugin (cannot grep `.cs` content) | `ue_glob` → `ue_read` |

Collect unique `.h`/`.cpp` file pairs. **Exclude** `.generated.h` and `.gen.cpp` (auto-generated, not authored code). If `--class` specified, narrow scope with class-name grep.

### Phase 2: Analysis — Run Security Rules

#### Phase 2A: Structured Rules (SEC-003, SEC-004, SEC-005, SEC-007) — `ue_analyze_source` + `ue_analyze_symbols`

**Always prefer `ue_analyze_source` for macro/UFUNCTION-driven rules**:

| Rule | Tool & Operation | Purpose |
|------|------------------|---------|
| SEC-003, SEC-007 | `ue_analyze_source(find_macro_invocations, UE_LOG)` | Format string + sensitive data in logs |
| SEC-004 | `ue_analyze_source(extract_pattern, UFUNCTION)` | Identify Server RPCs by specifier |
| SEC-004 enhanced | `ue_analyze_symbols(smart, function_name=Server*_Implementation)` | Find direct callers; client-expand if needed |
| SEC-005 | `ue_analyze_source(extract_pattern, USTRUCT)` | Cross-ref custom serialization |

> **Known limitation**: `find_macro_invocations(UE_LOG)` scans `.h` only — `.cpp` `UE_LOG` calls require Phase 2B fallback.

Full code samples and tool parameters:
[REFERENCE.md § ue_analyze_source (Phase 2A: Structured Analysis)](REFERENCE.md#ue_analyze_source-phase-2a-structured-analysis)

**Fallback** (if `ue_analyze_source` fails or PDB not loaded): `ue_read` + manual pattern matching.

#### Phase 2B: Pattern-Based Rules (SEC-001, SEC-002, SEC-006, SEC-008~012) — `ue_grep` + `ue_read`

For each candidate file pair:

| Rule | Detection | Validation patterns recognized as safe |
|------|-----------|----------------------------------------|
| SEC-001 | Server RPC `_Implementation` body | `FMath::Clamp`, `FMath::Min`/`Max`, `IsValid()`, `ensure()`, `check()`, `if (Param ><=)` |
| SEC-002 | Unbounded `Memcpy`/`strcpy`/array | `sizeof()` as size, `FMath::Min(UserSize, MaxSize)`, constant-size with static source |
| SEC-006 | Client-side game-critical calls | Inside `if (HasAuthority())`, inside `Server*_Implementation`, inside `#if WITH_SERVER_CODE` |
| SEC-008 | Secret-pattern string literals | (FP) Tests/Mock/Test dirs, "example"/"placeholder"/"TODO" context |
| SEC-009 | `FMath::Rand` / `rand()` usage | Gameplay context (Position/Rotation/Damage/Spawn/Color) → safe; security context (Token/Nonce/SessionId/EncryptionKey) → flag |
| SEC-010 | Open without cleanup | `TUniquePtr`, `TSharedPtr`, `TAutoDeleteAsyncTask`, RAII wrappers |
| SEC-011 | Build.cs module names not in engine list | `ue_glob` *.Build.cs → `ue_read` → extract `*DependencyModuleNames` arrays |
| SEC-012 | .uplugin plugin refs | `ue_glob` *.uplugin → `ue_read` → check refs against Engine/project/Marketplace |

Full per-rule detection logic (BAD/GOOD examples, FP context, RAII detection, lambda/delegate authority scan):
[REFERENCE.md § Hard Case Detection Patterns](REFERENCE.md#hard-case-detection-patterns) and the per-rule rows in
[REFERENCE.md § Rule Detail Table](REFERENCE.md#rule-detail-table-sec-001012).

**NOLINT check**: For every candidate violation, check if `// NOLINT(SEC-XXX)` or `// NOLINT` is on the same line. Suppress and record in Suppressed list.

### Phase 3: Report — Severity-Ranked Output

Sort violations by severity (Critical > Error > Warning > Info). Output Markdown:

```markdown
## Security Lint Report

**Scanned**: N files | **Violations**: M total | **Critical**: C | **Strict mode**: on/off

### Critical (C items)
| # | File:Line | Rule | Pattern | Suggestion |
|---|-----------|------|---------|------------|
| 1 | AMyChar.cpp:45 | SEC-004 | Server RPC trusts client damage | Add `FMath::Clamp()` and server-side validation |

### Error (E items)
| ... |

### Warning (W items)
| ... |

### Info (I items)
| ... |

### Suppressed (S items)
- AMyChar.cpp:89: SEC-009 FMath::Rand for spawn randomization (NOLINT)

### Summary
- FP reduction: context-aware filtering + NOLINT suppression
- Target FP rate: < 15%
```

Severity → action mapping (Critical=must-fix-before-ship, Error=fix-this-sprint, Warning=review-decide, Info=consider-context):
[REFERENCE.md § Severity Levels](REFERENCE.md#severity-levels).

---

## Output Format

Default human-readable Markdown report (header above). Compact text-block also acceptable:

```text
═══════════════════════════════════════════
 Security Lint Report
═══════════════════════════════════════════
 Scanned:    N files
 Violations: M total (C critical, E error, W warning, I info)
 Strict:     on/off
 Rules:      SEC-001~012 (or filtered subset)

 [Critical] AMyChar.cpp:45   SEC-004  Server RPC trusts client value
 [Error]    NetUtils.cpp:89  SEC-002  memcpy unbounded size
 [Warning]  Auth.cpp:67      SEC-007  Token in UE_LOG
 [Info]     MyGame.Build.cs  SEC-011  Unknown module dep

 Suppressed: S items (NOLINT)
═══════════════════════════════════════════
```

---

## Output Format Routing (Issue #8413 Phase 2)

> **Additive feature** — when `$ARGUMENTS` does NOT contain `--output-format`,
> behavior is **identical** to the Markdown report above (AC #8). The integration
> only triggers when CI/automation explicitly opts in.

**Activation**: parse `$ARGUMENTS` for both flags:
- `--output-format json|sarif|junit` (required to activate; case-sensitive value)
- `--output <path>` (optional — defaults to stdout if absent)

When activated:
1. Skip Markdown rendering. Collect findings as a list of `AnalysisResult` objects (`{tool, rule_id, severity, file_path, line, column?, message, suggestion?}`); `severity` ∈ `{error, warning, info}` lowercase (Critical → `error`).
2. Write the array to `TMPFILE="${TMPDIR:-/tmp}/security-lint-findings-$$.json"`.
3. Shell out: `narshamcp analyze --source ue-security-lint --input "$TMPFILE" --format <fmt> --output <path?>`.
4. Report only the file path or stdout indicator (no Markdown summary).

**Argument forms supported**: `--output-format json` (space-separated). `--output-format=json` (equals form) is **not supported** — emit error: `Use --output-format json (space-separated, not equals)`.

Full activation walkthrough, JSON schema, examples, backward-compatibility contract:
[REFERENCE.md § Output Format Routing (Issue #8413 Phase 2)](REFERENCE.md#output-format-routing-issue-8413-phase-2).

---

## Self-Test

`/ue-security-lint --self-test` validates 4 steps (MCP health → source grep → structured macro analysis → NOLINT parsing). Full spec and pass criteria:
[REFERENCE.md § Self-Test](REFERENCE.md#self-test).

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `ue_grep` empty | No UE source files | Display "No C++ source files found" |
| `ue_read` not found | File path changed since grep | Skip file, continue scanning |
| `ue_analyze_source` fails | Tool unavailable or PDB not loaded | Use `ue_read` + manual pattern matching |
| `ue_analyze_symbols` fails | PDB not loaded | Skip SEC-004/006 enhanced call tracing |
| Too many files (>100) | Large project | Scan only files matching high-signal patterns |
| `ue_grep` timeout | Query too broad | Narrow to `path_pattern` matching `--class` |

> Limitations (static-only, heuristics, no data flow, supply-chain informational):
> [REFERENCE.md § Limitations](REFERENCE.md#limitations).

---

## Related Skills

- [`/ue-network-lint`](../ue-network-lint/SKILL.md) — Network anti-pattern scanning (NET-001~010, complementary)
- [`/ue-perf-lint`](../ue-perf-lint/SKILL.md) — Performance anti-pattern scanning (PERF-001~023, complementary)
- [`/ue-audit`](../ue-audit/SKILL.md) — Project-wide health audit (broader scope)
- [`/ue-pre-commit`](../ue-pre-commit/SKILL.md) — Pre-commit safety analysis (can include security)
- `/ue-architecture-lint` — Structural anti-pattern scanning (ARCH rules, #7590)
