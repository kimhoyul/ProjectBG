# UE Security Lint — Rule Reference

**Version**: 1.0.0
**Parent**: [SKILL.md](SKILL.md)

---

## Rule Detail Table (SEC-001~012)

| Rule | Name | Severity | Grep Pattern | Analysis Method | FP Risk | Fix Suggestion |
|------|------|----------|-------------|-----------------|---------|----------------|
| SEC-001 | Unvalidated external input | Warning | `UFUNCTION.*Server` + `_Implementation` in *.cpp | `ue_read` body → check if RPC params used directly without `FMath::Clamp`, `IsValid()`, `ensure()`, range check | Medium | Add input validation: `FMath::Clamp(Param, Min, Max)` or `if (!IsValid(Param)) return` |
| SEC-002 | Buffer overflow risk | Error | `FMemory::Memcpy\|memcpy\|strcpy\|FCString::Strcpy\|char\[\|TCHAR\[` | `ue_read` context → check if size param is user-controlled without `FMath::Min(Size, MaxSize)` or `sizeof()` bound | Low | Add bounds: `FMemory::Memcpy(Dst, Src, FMath::Min(UserSize, sizeof(Buffer)))` |
| SEC-003 | Format string injection | Error | `UE_LOG.*\*.*Input\|Printf.*\*.*String` | `ue_analyze_source(find_macro_invocations, UE_LOG)` → check if format param is a variable, not string literal | Low | Use as argument: `UE_LOG(LogTemp, Log, TEXT("%s"), *UserInput)` instead of `UE_LOG(LogTemp, Log, *UserInput)` |
| SEC-004 | RPC authority bypass | Critical | `UFUNCTION.*Server` in *.h | `ue_analyze_source(extract_pattern, UFUNCTION)` → filter Server specifier → `ue_read` `_Implementation` → check for param validation | Medium | Clamp/validate: `float Safe = FMath::Clamp(ClientDamage, 0.f, MaxDamage); Health -= Safe;` |
| SEC-005 | Insecure deserialization | Warning | `NetSerialize\|Serialize.*FArchive` | `ue_read` NetSerialize body → check for `SetNum(Size)` or array allocation without bounds check on `Size` | Low | Add bounds: `Size = FMath::Clamp(Size, 0, MAX_PAYLOAD_SIZE)` before `SetNum()` |
| SEC-006 | Missing cheat prevention | Warning | `SetActorLocation\|TakeDamage\|AddMoney\|SetHealth\|AddCurrency\|SetScore` | `ue_read` caller → check for `HasAuthority()` or `GetLocalRole() == ROLE_Authority` guard | Medium | Add guard: `if (HasAuthority()) { SetActorLocation(Pos); }` or move to Server RPC |
| SEC-007 | Sensitive data in logs | Warning | `password\|token\|api.?key\|secret\|credential\|session.?id` (case-insensitive) | `ue_analyze_source(find_macro_invocations, UE_LOG)` → check if sensitive keyword in interpolated variable (`%s`, `*Var`), not plain text | High | Remove sensitive data from logs or redact: `UE_LOG(LogAuth, Log, TEXT("Auth attempt for user %s"), *UserId)` |
| SEC-008 | Hardcoded secrets | Critical | `TEXT\("(sk_\|pk_\|api_\|key_\|secret_\|token_\|Bearer \|Basic )[^"]{8,}"\)` | `ue_grep` regex + `ue_read` → exclude `Tests/`, `Mock*`, `Test*` dirs and `test_placeholder` context | Medium | Move to config: `GConfig->GetString(TEXT("/Script/MyGame"), TEXT("ApiKey"), ApiKey, GGameIni)` |
| SEC-009 | Insecure random for security | Warning | `FMath::Rand\|FMath::RandRange\|rand\(\)\|srand\(` | `ue_read` context → flag if assigned to `*Token*`, `*Nonce*`, `*SessionId*`, `*EncryptionKey*`; safe if `*Position*`, `*Spawn*`, `*Color*` | Medium | Use crypto random: `FPlatformMisc::GetUniqueEncryptionKey()` or OS CSPRNG |
| SEC-010 | Resource leak on error path | Warning | `IFileHandle\|FSocket\|new.*\[\|FPlatformFileManager` | `ue_read` function body → track open/create → verify cleanup on ALL exit paths (early returns, error branches) | Low | Use RAII: `TUniquePtr<IFileHandle> File(PlatformFile.OpenRead(*Path))` |
| SEC-011 | Unknown Build.cs module | Info | `*.Build.cs` files (via `ue_glob`) | `ue_read` Build.cs → extract `PublicDependencyModuleNames`/`PrivateDependencyModuleNames` → cross-check against engine module list | High | Verify module source and origin; document in Build.cs comments |
| SEC-012 | Suspicious plugin dependency | Info | `*.uplugin` files (via `ue_glob`) | `ue_read` .uplugin → check plugin refs against `Engine/Plugins/`, project `Plugins/`, Marketplace | High | Verify plugin integrity; ensure trusted source |

---

## Severity Levels

| Level | Meaning | Action Required |
|-------|---------|-----------------|
| **Critical** | Active exploit risk — data theft, privilege escalation, or economy manipulation | Must fix before shipping |
| **Error** | Will cause security vulnerability under specific conditions | Should fix in current sprint |
| **Warning** | Potential issue depending on context; may be intentional design choice | Review and decide; suppress with NOLINT if intentional |
| **Info** | Informational — supply chain awareness, not a confirmed vulnerability | Consider for security-sensitive contexts |

---

## NOLINT Suppression Syntax

```cpp
// Suppress specific rule on this line
Health -= ClientDamage; // NOLINT(SEC-004)

// Suppress all SEC rules on this line
GEngine->Exec(GetWorld(), *Command); // NOLINT

// Multi-rule suppression
FMemory::Memcpy(Buf, Data, Size); // NOLINT(SEC-001,SEC-002)
```

**Scope**: Line-level only. No file-level or block-level suppression.

---

## MCP Tool Parameter Reference

### ue_grep (Phase 1: Discovery)

```python
# RPC declarations (regex mode)
ue_grep(params={
    "query": "UFUNCTION.*Server|_Implementation|WithValidation",
    "domain": "source",
    "regex": true,
    "limit": 50
})

# Sensitive data patterns (case-insensitive)
ue_grep(params={
    "query": "password|token|api.?key|secret|credential|auth.?key|session.?id",
    "domain": "source",
    "regex": true,
    "-i": true,
    "limit": 50
})

# Hardcoded secret patterns
ue_grep(params={
    "query": "TEXT\\(\"(sk_|pk_|api_|key_|secret_|token_|Bearer |Basic )[^\"]{8,}\"\\)",
    "domain": "source",
    "regex": true
})

# Buffer operations
ue_grep(params={
    "query": "FMemory::Memcpy|memcpy|strcpy|FCString::Strcpy|char\\[|TCHAR\\[",
    "domain": "source",
    "regex": true
})

# Random usage
ue_grep(params={
    "query": "FMath::Rand|FMath::RandRange|rand\\(\\)|srand\\(",
    "domain": "source",
    "regex": true
})

# Resource handles
ue_grep(params={
    "query": "IFileHandle|FSocket|new.*\\[|FPlatformFileManager",
    "domain": "source",
    "regex": true
})
```

### ue_read (Phase 2B: Pattern Matching)

```python
# Read source file for context-aware analysis
ue_read(identifier="AMyCharacter")
```

### ue_analyze_source (Phase 2A: Structured Analysis)

```python
# SEC-003/007: UE_LOG macro analysis — format string + sensitive data
# NOTE: Returns .h results only. For .cpp, use ue_grep fallback.
ue_analyze_source(operation="find_macro_invocations",
    macro_name="UE_LOG",
    class_filter="AMyCharacter")

# SEC-004: UFUNCTION specifier extraction — Server RPC identification
ue_analyze_source(operation="extract_pattern",
    specifier_type="UFUNCTION",
    class_filter="AMyCharacter")

# SEC-005: USTRUCT extraction — custom serialization identification
ue_analyze_source(operation="extract_pattern",
    specifier_type="USTRUCT",
    class_filter="AMyCharacter")
```

### ue_analyze_symbols (Phase 2A Enhanced: Call Chain Tracing)

```python
# SEC-004 Enhanced: Trace RPC callers for indirect authority bypass
ue_analyze_symbols(operation="smart",
    function_name="ServerApplyDamage_Implementation")

# SEC-006 Enhanced: Trace hierarchy for authority check patterns
ue_analyze_symbols(operation="smart",
    class_name="AMyCharacter",
    direction="up")
```

### ue_glob (Phase 1: Supply Chain Discovery)

```python
# SEC-011: Find Build.cs files (ue_grep cannot scan .cs content)
ue_glob(pattern="*.Build.cs", domain="source")

# SEC-012: Find .uplugin files (ue_grep cannot scan .uplugin content)
ue_glob(pattern="*.uplugin", domain="source")
```

---

## Scoring & FP Rate Calculation

**False Positive Rate** = `FP / (TP + FP)` per rule

| FP Rate | Rating | Action |
|---------|--------|--------|
| < 5% | Excellent | No changes needed |
| 5-10% | Acceptable | Monitor; consider tightening heuristics |
| 10-15% | Needs improvement | Refine grep patterns or add context checks |
| > 15% | Unacceptable | Rule must be redesigned or demoted to Info |

**Target**: < 15% aggregate FP rate across all 12 rules.

---

## SEC-004 / NET-001 Deduplication

When both SEC-004 (security-lint) and NET-001 (network-lint) fire on the same Server RPC:
- **No automatic deduplication** — both provide independent value
- SEC-004 checks **data trust** (server trusts client values without validation)
- NET-001 checks **ownership trust** (RPC called on actor the client doesn't own)
- Same function can have both issues simultaneously

| Aspect | SEC-004 (Security) | NET-001 (Networking) |
|--------|---------------------|----------------------|
| Checks | Server trusts client-provided values | RPC on actor client doesn't own |
| Fix | `FMath::Clamp()` + validation | `if (IsLocallyControlled())` guard |

---

## Hard Case Detection Patterns

### Indirect RPC Call Chain (SEC-004)

```python
# Instead of just checking _Implementation body:
ue_analyze_symbols(operation="smart", function_name="ServerApplyDamage")
# find_callers returns direct callers only
# Repeat unique frontier queries client-side before checking deeper validation evidence
```

### RAII Resource Detection (SEC-010 FP Prevention)

Safe patterns that should NOT trigger SEC-010:
- `TUniquePtr<IFileHandle>` — RAII auto-cleanup
- `TSharedPtr<FSocket>` — reference-counted cleanup
- `TAutoDeleteAsyncTask` — UE async task cleanup
- Scope-based: resource created and deleted within same `{ }` block

### Gameplay vs Security Random (SEC-009 Context)

| Context | Variables | Verdict |
|---------|-----------|---------|
| **Security** (flag) | `*Token*`, `*Nonce*`, `*SessionId*`, `*EncryptionKey*`, `*AuthCode*` | SEC-009 Warning |
| **Gameplay** (safe) | `*Position*`, `*Rotation*`, `*Damage*`, `*Spawn*`, `*Color*`, `*Delay*` | Not flagged |
| **Ambiguous** | Other variable names | Warning with note: "Verify not used for security" |

### Test/Mock File Exclusion (SEC-008 FP Prevention)

Files in these directories are excluded from SEC-008 hardcoded secret scanning:
- `Tests/`, `test_*`, `Mock*`, `Test*` directories
- Strings containing `example`, `placeholder`, `TODO`, `test_`

### Lambda/Delegate Authority Check (SEC-006)

Scan caller function body including:
- `[this]() { SetActorLocation(Pos); }` — lambda capture
- `SetTimer(Handle, ...)` — timer delegate bodies
- Check if enclosing scope has `HasAuthority()` guard

---

## Self-Test

`/ue-security-lint --self-test` validates:

1. **MCP connectivity**: `ue_check_health(compact=true)` responds with status
2. **Source grep**: `ue_grep(query="UFUNCTION.*Server", domain="source", regex=true)` returns results
3. **Structured analysis**: `ue_analyze_source(operation="find_macro_invocations", macro_name="UE_LOG")` returns data
4. **NOLINT parsing**: `ue_read` any .h file → verify `// NOLINT(SEC-XXX)` pattern recognition

**Expected**: Steps 1-2 always pass if NarshaMCP connected and project has C++ source. Step 3 passes if project has UE_LOG calls. Step 4 is offline validation (always passes).

---

## Limitations

1. **Static analysis only** — cannot detect runtime-only vulnerabilities (e.g., TOCTOU, timing attacks).
2. **Heuristic-based** — SEC-007/008/009 use keyword/context heuristics; may miss obfuscated secrets or flag benign patterns.
3. **No data flow analysis** — SEC-001 checks local function scope only; cannot trace tainted data across multiple function calls.
4. **Supply chain rules are informational** — SEC-011/012 lack a comprehensive module whitelist; results require manual verification.
5. **UE macro interference** — heavily macro-wrapped code (`GENERATED_BODY`, `UPROPERTY`) may obscure patterns.
6. **`ue_analyze_source(UE_LOG)` scans `.h` only** — `.cpp` `UE_LOG` calls require Phase 2B fallback (`ue_grep` + `ue_read`).
7. **`ue_grep` cannot scan `.cs`/`.uplugin` content** — SEC-011/012 must use `ue_glob` → `ue_read` path instead.

---

## Output Format Routing (Issue #8413 Phase 2)

> **Additive feature** — when `$ARGUMENTS` does NOT contain `--output-format`,
> behavior is **identical** to the pre-Phase-2 Markdown report (AC #8). The
> integration only triggers when CI/automation explicitly opts in.

### When to activate

Parse `$ARGUMENTS` for both flags:
- `--output-format json|sarif|junit` (required to activate; case-sensitive value)
- `--output <path>` (optional — defaults to stdout if absent)

If `--output-format` is **absent**, ignore this entire section and produce the
Markdown report defined in SKILL.md § Output Format.

### When activated (CI / automation mode)

Skip the human-readable Markdown report. Instead:

1. **Collect findings during the SEC scan** as an in-memory list of
   `AnalysisResult` objects, one per rule hit:
   ```json
   {
     "tool": "ue-security-lint",
     "rule_id": "SEC-001",
     "severity": "error",
     "file_path": "Source/MyGame/Auth.cpp",
     "line": 23,
     "column": null,
     "message": "Hardcoded credential detected",
     "suggestion": "Move secret to FConfigCacheIni or environment variable"
   }
   ```
   `severity` MUST be one of `error` / `warning` / `info` (lowercase). Map
   the rule's table severity (Critical/Warning/Info) → `error`/`warning`/`info`.
   `column` and `suggestion` are optional (omit or `null`).

2. **Write to temp file** as a bare JSON array (no envelope needed; the CLI
   accepts both shapes):
   ```bash
   TMPFILE="${TMPDIR:-/tmp}/security-lint-findings-$$.json"
   # write JSON array to $TMPFILE
   ```

3. **Shell out to `narshamcp analyze`** for format conversion:
   ```bash
   narshamcp analyze \
     --source ue-security-lint \
     --input "$TMPFILE" \
     --format <fmt-from-args> \
     --output <path-from-args-or-omit-for-stdout>
   ```

4. **Report only the file path** (or stdout indicator) — no Markdown summary.
   Example success message: `Wrote 3 findings to /tmp/security.sarif (SARIF v2.1.0)`.

### Argument forms supported

- `--output-format json` (space-separated) ✅
- `--output-format sarif` (space-separated) ✅
- `--output-format junit` (space-separated) ✅
- `--output /tmp/out.sarif` (space-separated) ✅
- `--output-format=json` (equals form) ❌ — not supported in Phase 2; emit error: `Use --output-format json (space-separated, not equals)`

### Examples

```bash
# Activate JSON mode → file
/ue-security-lint --output-format json --output /tmp/sec-findings.json

# Activate SARIF mode → file (for GitHub Code Scanning)
/ue-security-lint --rules SEC-001,SEC-004 --output-format sarif --output /tmp/sec.sarif

# Activate JUnit mode → stdout (Jenkins captures pipe)
/ue-security-lint --output-format junit
```

### Backward compatibility (AC #8)

The SEC scan workflow is **unchanged**. The only addition is a post-scan branch
that, when `--output-format` is provided, serializes findings as JSON and shells
to the CLI instead of formatting Markdown. All existing trigger phrases,
argument parsing, and Markdown report generation remain byte-identical to the
pre-Phase-2 baseline.

---

**Issue**: #7599
**Last Updated**: 2026-05-02 (Issue #10515 — modularization)
