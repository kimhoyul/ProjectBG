# UE Security Lint — Examples

## Scenario 1: Full Project Scan (No Arguments)

**User**: "보안 린트 실행" / "Run security lint"

### Expected Flow

**Phase 1 — Discovery**:
```python
ue_grep(params={"query": "UFUNCTION.*Server|_Implementation|WithValidation", "domain": "source", "regex": true})
# → 12 files with Server RPCs

ue_grep(params={"query": "password|token|api.?key|secret|credential", "domain": "source", "regex": true, "-i": true})
# → 5 files with sensitive keyword patterns

ue_grep(params={"query": "FMemory::Memcpy|memcpy|strcpy|FCString::Strcpy", "domain": "source", "regex": true})
# → 3 files with buffer operations

ue_grep(params={"query": "FMath::Rand|FMath::RandRange|rand\\(\\)", "domain": "source", "regex": true})
# → 8 files with random usage

ue_glob(pattern="*.Build.cs", domain="source")
# → 4 Build.cs files (ue_grep cannot scan .cs content — use ue_glob → ue_read)
```

**Phase 2A — Structured Analysis**:
```python
ue_analyze_source(operation="find_macro_invocations", macro_name="UE_LOG", class_filter="AMyCharacter")
# → 15 UE_LOG invocations, 2 with interpolated sensitive variables (SEC-007)

ue_analyze_source(operation="extract_pattern", specifier_type="UFUNCTION", class_filter="AMyCharacter")
# → 3 Server RPCs found, check _Implementation for input validation (SEC-004)
```

**Phase 2B — Pattern Matching**:
```python
ue_read(identifier="AMyCharacter")
# → ServerApplyDamage_Implementation uses client Damage directly → SEC-004
# → UE_LOG contains *AuthToken → SEC-007

ue_read(identifier="ANetworkUtils")
# → memcpy with user-controlled size → SEC-002
```

### Expected Output

```text
═══════════════════════════════════════════
 Security Lint Report
═══════════════════════════════════════════
 Scanned:    32 files
 Violations: 7 total (2 critical, 1 error, 3 warning, 1 info)
 Strict:     off
 Rules:      SEC-001~012

 [Critical] AMyCharacter.cpp:145   SEC-004  Server RPC trusts client damage value
 [Critical] GameConfig.h:23        SEC-008  Hardcoded API key "sk_live_Abc..."
 [Error]    ANetworkUtils.cpp:89   SEC-002  memcpy with unbounded user size
 [Warning]  AMyCharacter.cpp:67    SEC-007  Auth token logged via UE_LOG
 [Warning]  AMovement.cpp:34       SEC-006  Client-side SetActorLocation without authority
 [Warning]  AAuthManager.cpp:112   SEC-009  FMath::Rand for session token generation
 [Info]     MyGame.Build.cs:18     SEC-011  Unknown module "ThirdPartyAuth"

 Suppressed: 1 items (NOLINT)
═══════════════════════════════════════════
```

---

## Scenario 2: Class-Scoped Scan

**User**: "보안 린트 --class ALyraPlayerState" / "security lint --class ALyraPlayerState"

### Expected Flow

**Phase 1** narrows discovery to `ALyraPlayerState` files only:
```python
ue_grep(params={"query": "class ALyraPlayerState", "domain": "source"})
# → LyraPlayerState.h, LyraPlayerState.cpp

ue_grep(params={"query": "UFUNCTION.*Server|_Implementation", "domain": "source", "path_pattern": "*LyraPlayerState*"})
# → Server RPCs in this class only
```

**Phase 2A** analyzes the single class:
```python
ue_analyze_source(operation="extract_pattern", specifier_type="UFUNCTION", class_filter="ALyraPlayerState")
# → 2 Server RPCs found

ue_analyze_source(operation="find_macro_invocations", macro_name="UE_LOG", class_filter="ALyraPlayerState")
# → 4 UE_LOG calls, none with sensitive data interpolation
```

### Expected Output

```text
═══════════════════════════════════════════
 Security Lint Report
═══════════════════════════════════════════
 Scanned:    2 files (ALyraPlayerState)
 Violations: 0 total
 Strict:     off
 Rules:      SEC-001~012

 No security violations found.

 Note: Lyra project follows Epic's security best practices.
 All Server RPCs use WithValidation and parameter clamping.

 Suppressed: 0 items
═══════════════════════════════════════════
```

---

## Scenario 3: Rule-Filtered Scan (Pilot Rules)

**User**: "security lint --rules SEC-004,SEC-008" / "보안 린트 --rules SEC-004,SEC-008"

### Expected Flow

Only SEC-004 (RPC authority bypass) and SEC-008 (hardcoded secrets) are scanned:

**Phase 1** — Targeted discovery:
```python
# SEC-004 only
ue_grep(params={"query": "UFUNCTION.*Server|_Implementation|WithValidation", "domain": "source", "regex": true})
# → 12 files

# SEC-008 only
ue_grep(params={"query": "(Password|ApiKey|Secret|Token|Credential).*=.*TEXT\\(\"[^\"]+\"\\)", "domain": "source", "regex": true})
# → 2 files with potential hardcoded secrets
```

**Phase 2A** — SEC-004 structured analysis:
```python
ue_analyze_source(operation="extract_pattern", specifier_type="UFUNCTION", class_filter="*")
# → Filter for Server specifier
# → For each: check _Implementation for input validation

ue_analyze_symbols(operation="smart", function_name="ServerApplyDamage_Implementation")
# → Trace callers for indirect authority bypass
```

**Phase 2B** — SEC-008 pattern matching:
```python
ue_read(identifier="AGameConfig")
# → FString ApiKey = TEXT("sk_live_abc123def456"); → SEC-008 Critical
# → Exclude: FString TestKey = TEXT("test_placeholder"); → Not flagged (test context)
```

### Expected Output

```text
═══════════════════════════════════════════
 Security Lint Report
═══════════════════════════════════════════
 Scanned:    14 files
 Violations: 3 total (3 critical, 0 error, 0 warning, 0 info)
 Strict:     off
 Rules:      SEC-004, SEC-008 (filtered)

 [Critical] AMyCharacter.cpp:145   SEC-004  Server RPC trusts client damage value
 [Critical] AWeapon.cpp:78         SEC-004  Server RPC trusts client ammo count
 [Critical] GameConfig.h:23        SEC-008  Hardcoded API key "sk_live_..."

 Suppressed: 0 items
═══════════════════════════════════════════
```

---

## Scenario 4: Strict Mode (CI Gate)

**User**: "security lint --strict" / "보안 린트 --strict"

### Expected Flow

Same as Scenario 1, but `sec_strict=true`. Any violation (including Warning and Info) causes a blocking result.

### Expected Output

```text
═══════════════════════════════════════════
 Security Lint Report
═══════════════════════════════════════════
 Scanned:    32 files
 Violations: 7 total (2 critical, 1 error, 3 warning, 1 info)
 Strict:     ON ⚠️
 Rules:      SEC-001~012

 [Critical] AMyCharacter.cpp:145   SEC-004  Server RPC trusts client damage value
 [Critical] GameConfig.h:23        SEC-008  Hardcoded API key "sk_live_Abc..."
 [Error]    ANetworkUtils.cpp:89   SEC-002  memcpy with unbounded user size
 [Warning]  AMyCharacter.cpp:67    SEC-007  Auth token logged via UE_LOG
 [Warning]  AMovement.cpp:34       SEC-006  Client-side SetActorLocation without authority
 [Warning]  AAuthManager.cpp:112   SEC-009  FMath::Rand for session token generation
 [Info]     MyGame.Build.cs:18     SEC-011  Unknown module "ThirdPartyAuth"

 Suppressed: 1 items (NOLINT)

 ⚠️ STRICT MODE: 7 violations found. Security gate BLOCKED.
 Fix all violations or suppress with // NOLINT(SEC-XXX) before proceeding.
═══════════════════════════════════════════
```

### Strict Mode Behavior

- **Default mode**: Only Critical and Error violations are considered blocking
- **Strict mode**: ALL violations (including Warning and Info) are blocking
- Use `// NOLINT(SEC-XXX)` to suppress intentional exceptions
- Recommended for pre-merge CI gates on security-critical codebases
