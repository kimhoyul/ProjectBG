---
name: ue-debug
model: opus
description: "Purpose: Systematic UE debugging assistant with symptom-to-cause diagnosis. Answers 'Why is X happening?' by classifying symptoms (compile error, runtime crash, unexpected behavior, performance, build_system) and routing to optimal diagnostic tools. Medical 4-step model: Symptoms -> Diagnosis -> Root Cause -> Treatment. Triggers: 'debug', 'why is', 'error', 'crash', 'not working', '디버그', '왜 이런', '에러', '크래시', 'build error detail', 'compiler results'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
argument-hint: "symptom description or error message | --self-test | --cl-search"
metadata:
  version: "2.1.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Debug — Systematic Debugging Assistant

**Issue**: #4800, #5290, #5291, #5293, #6002, #6352, #10452 (modularization)
**Purpose**: Answer "Why is X happening?" with systematic diagnosis from symptom to root cause
**Reference material**: Full classification table, per-path diagnostic catalogs (Paths A–H), engine source search guidelines, output template, answer quality rules, error recovery, evaluation criteria, BP debugger reference, crash doctor reference are in [REFERENCE.md](REFERENCE.md). The optional Epic GitHub CL search (`--cl-search`) is in [CL_SEARCH.md](CL_SEARCH.md).

---

## Prerequisites

- `gh` CLI (>= 2.0.0) — GitHub operations (PR, issue, workflow API).
  Install: `winget install GitHub.cli` (Windows) | `brew install gh` (macOS) | `apt install gh` (Linux)
- `jq` (>= 1.6) — JSON parser, used for state file ops and `gh api ... --jq` filtering.
  Install: `winget install jqlang.jq` (Windows) | `brew install jq` (macOS) | `apt install jq` (Linux)

> If either CLI is missing, scripts in this skill exit with code 127 and print platform-specific install commands.

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the diagnostic workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!
> **Non-negotiable diagnosis gate (#17830 pattern).** A debug/crash request — **however terse, and even with NO crash log, class name, or error code provided** ("런타임 크래시 원인 찾아줘", "why is it crashing") — is a **tool-driven diagnosis** request: EVERY invocation MUST *actually call* the classified path's **entry diagnostic MCP tool** BEFORE writing the Debug Report. For `runtime_crash` that means actually calling **`ue_fix_errors(operation="crash_analysis")`** — it auto-scans `Saved/Crashes/*` and `Saved/Logs/*.log`, so **no log-path argument is required and you must NOT ask the user for a log first** — or, when the crash is a live-PIE / Blueprint symptom, **`ue_editor_debug(operation="get_pie_state")`**. `ue_check_health` is a connectivity probe, **NOT** a diagnosis tool: stopping after `ue_check_health` and answering from memory is a **FAILED debug**. A prose / general-knowledge answer with no diagnostic tool call is a **FAILED debug**, even when it names the correct cause. In a non-interactive run, do NOT pause for confirmation — call the entry tool, then report.

### Mandatory Response Receipt (reserve before calling tools)

Every invocation must end with these literal English labels and numbered step labels. **Do not translate,
rename, merge, or omit them**, even when the user asks in Korean or the diagnostic tool
returns no records:

```text
Classification: <compile_error|linker|build_system|runtime_crash|other routed category>
Evidence: <diagnostic tool and its concrete finding, or no diagnostic record found>
Root Cause: <confirmed cause or undetermined>
Fix:
Step 1: <first evidence-backed action>
Step 2: <next evidence-backed action>
Verification: <command/build/PIE reproduction that proves the result>
```

For a generic `빌드 에러`/`컴파일 에러` request without an error code, write
`Classification: build_system` and `Root Cause: undetermined` unless the tool supplies causal
evidence. For a concrete `LNK####` request, write `Classification: linker` and keep the Build.cs
module-dependency/include checks under literal `Step 1:` and `Step 2:` lines.
For a `NullPtr`/null-pointer request, repeat `NullPtr` or `null pointer` in Classification/Evidence
and include an evidence-backed `IsValid` or null-check guard in the numbered fix; do not translate
away the diagnostic category token.

### Execution Requirements

1. **Step 0: Discovery Pre-Exploration** (lightweight context gathering)
2. **Symptom**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user what error or symptom to debug.
3. **Classify symptom** into one of 10 categories — see [REFERENCE.md § Symptom Classification](REFERENCE.md#symptom-classification--full-table) for the full table
4. **Execute diagnostic tools** based on classification — see [REFERENCE.md § Diagnostic Tool Catalog](REFERENCE.md#diagnostic-tool-catalog-step-2--full-per-path-catalog) for per-path Paths A–H
5. **Generate debug report** using the [REFERENCE.md § Output Format (Medical Model)](REFERENCE.md#output-format-medical-model)

> **Optional flag** `--cl-search`: when the symptom mentions a specific UE version OR source-level investigation has located the buggy area but not the mechanism, also load [CL_SEARCH.md](CL_SEARCH.md) for the Epic GitHub commit-search workflow. Default invocation does NOT load CL_SEARCH.md.

---

## MANDATORY VERIFICATION GATES (check BEFORE writing Debug Report)

> **CRITICAL**: These gates override all other instructions. Check ALL gates before writing your final answer.
> If any gate fails, revise your answer before submitting.

**Gate 0: DIAGNOSTIC TOOL FIRST (check before Gates 1–4 and before the Tool Call Budget)** — Confirm you have *actually CALLED* the classified path's **entry diagnostic tool** before any name/absence/engine_docs verification below and before writing the Debug Report: `ue_fix_errors(crash_analysis)` for `runtime_crash`, `ue_fix_errors(smart)` for `compile_error`/`build_system`, `ue_editor_debug(get_pie_state)` for `blueprint_debug`. `ue_check_health` does NOT satisfy this gate. If you have not called it yet → STOP and call it now, then continue with the verification gates.

**Gate 1: NAME AUDIT** — List every UE class/function name in your Debug Report.
Each name MUST come from: (a) MCP tool result, (b) Read()/Grep() output, or (c) the question text.
If a name has no tool-verified source → replace with "a class in [Module]".
If 3+ names lack verification → your answer is likely hallucinated. Re-run targeted searches.

**Gate 2: ABSENCE CHECK** — If you assert "the root cause is X", do you have source evidence?
If engine_docs + grep both returned 0 results for X → "Root cause not confirmed in engine source" IS a valid diagnosis.
Do NOT fabricate root causes or workarounds for mechanisms that may not exist.

**Gate 3: ALTERNATIVE CHECK** — Did you only find one possible root cause? Search for competing explanations.
Grep for related symptoms in other subsystems. The first-found explanation is often wrong.

**Gate 4: engine_docs CROSS-CHECK** — Did you call `ue_engine_docs(search, "<topic>")`?
This is MANDATORY. If engine_docs describes a different mechanism than grep findings, report BOTH.
Prefer: documented behavior > inferred behavior > training knowledge.

**Tool Call Budget**: Reserve your LAST 2 tool calls for verification:
- V1: `ue_engine_docs(operation="search", query="<symptom/module>")` — official docs cross-check
- V2: Grep for the EXACT class/function name you cite as root cause — confirm it exists

### Common Failure Patterns (avoid these)

| Pattern | What Goes Wrong | Correct Approach |
|---------|-----------------|------------------|
| Overconfident diagnosis | Assert root cause without source evidence | Verify with engine_docs + grep before diagnosing |
| First-hit bias | First grep result becomes the diagnosis | Execute ALL planned queries, compare results |
| Training knowledge as fact | Present training knowledge as verified diagnosis | Always verify with tools before citing |
| Wrong file type | Search only .cpp/.h | Also search .usf/.ush/.cs/.ini |

---

## Step 0: Discovery Pre-Exploration (Issue #6352)

> **Purpose**: Before running diagnostic tools, use lightweight Discovery Tools
> to gather context about error-related symbols, files, and patterns.
> Reduces misdiagnosis by confirming which entities exist and where.

### When to Run Step 0

- Run Step 0 when the symptom mentions a **specific class, function, or file name**
- Skip Step 0 for generic symptoms ("빌드 에러", "FPS drop") — go directly to classification

### Step 0 Workflow

```python
# 0a. If symptom mentions a specific entity (e.g., "AMyCharacter crashes"):
ue_grep(query="<EntityName>", limit=10)
# Discovers: which domains contain this entity, related files

# 0b. If grep found source files, read the most relevant one:
ue_read(identifier="<EntityName>")
# Returns: class structure, file location, method list

# 0c. Use Step 0 results to:
#   - Locate the exact file/line where the error occurs
#   - Confirm class hierarchy (is it a Blueprint or C++ class?)
#   - Find related files that might be involved in the error
#   - Provide better context to ue_fix_errors for more accurate diagnosis
```

### Step 0 → Classification Enhancement

Step 0 결과로 분류 정확도를 높이고, 진단 경로에 컨텍스트를 전달합니다:

| Step 0 Result | Classification Override | Step 2에 전달할 컨텍스트 |
|---------------|------------------------|--------------------------|
| 소스에서 발견 (.h/.cpp 경로) | compile_error/runtime_crash C++ 경로 확정 | `step0_source_path` → `ue_fix_errors`에 전달하여 스캔 범위 축소 |
| 에셋에서 발견 (BP_*, GA_* 등) | unexpected_behavior/blueprint_debug BP 경로 확정 | 에셋 경로 → `ue_trace_execution`에 직접 전달 |
| GAS 태그 발견 (grep에서 Ability.* 매치) | gas_issue로 오버라이드 | 정확한 태그 문자열 → `trace_abilities`에 전달 |
| config에서 발견 | config 오류 경로 추가 | config 키 → `ue_analyze_config`로 보충 체크 |
| grep에서 파일+라인 반환 | — | `Read(step0_source_path)` 직접 사용, 광범위 스캔 스킵 |
| 타겟 미발견 | — | 에러 메시지 내 심볼이 오타일 가능성 → grep 부분 매치로 대안 제안 |

### Step 0 Result Variables (downstream 참조용)

- `step0_domains` — 매치된 도메인 목록 (symbols, assets, metadata, configs, source)
- `step0_read_detail` — `ue_read` 구조화 응답 (class 구조, 파일 위치 등)
- `step0_source_path` — 소스 파일 경로 (있을 경우 `Read`/`ue_fix_errors`에 전달)
- `step0_target_exists` — 타겟 존재 여부

> **Step 0d (file-type diversity)** — when initial grep returns no results OR the symptom involves rendering, build system, or infrastructure, expand search to non-C++ files (`.usf`/`.ush` shaders, `.xml`/`.cs` build scripts, `.ini` configs). Full matrix → [REFERENCE.md § Step 0d](REFERENCE.md#step-0d-file-type-diversity--search-beyond-cpph-issue-6680).

---

## Auto-Trigger Phrases (short list)

Most common 5 each. Full KO/EN list → [REFERENCE.md § Auto-Trigger Phrases](REFERENCE.md#auto-trigger-phrases).

**Korean**: "왜 X가 발생해?", "빌드 에러 고쳐줘", "크래시 원인 찾아줘", "X가 안돼", "성능이 느려"

**English**: "Why is X happening?", "Fix this error", "Debug this crash", "Why doesn't X work?", "Performance is slow"

---

## Symptom Classification & Diagnostic Routing

### Step 1: Classify Symptom (10-category summary)

Match the symptom to ONE of the 10 categories below. Full classifier with keyword arrays → [REFERENCE.md § Symptom Classification](REFERENCE.md#symptom-classification--full-table).

| Category | Trigger signal | Primary tool | Path |
|---|---|---|---|
| `compile_error` | specific error code: `error C`, `LNK`, `unresolved`, `undeclared` | `ue_fix_errors(smart)` | A |
| `runtime_crash` | `crash`, `assert`, `access violation`, `nullptr`, `segfault`, `크래시` | `ue_fix_errors(crash_analysis)` + `ue_analyze_symbols` + `ue_engine_docs` | B |
| `unexpected_behavior` | `not working`, `안돼`, `wrong`, `not activating`, no error code | `ue_trace_execution` + `ue_analyze_symbols` + `ue_analyze_config` | C |
| `performance` | `slow`, `FPS`, `lag`, `bottleneck`, `frame drop`, `메모리` | `ue_analyze_insights(diagnose_bottleneck)` | D |
| `gas_issue` | `ability`, `gameplay tag`, `attribute`, `어빌리티`, `not granting` | `ue_manage_gameplay(trace_abilities)` + `ue_analyze_config` | E |
| `audio_issue` | `MetaSound`, `metasound`, `메타사운드`, `spatialization`, `AudioComponent` | `ue_analyze_symbols` + `ue_analyze_config` + engine source bottom-up | H |
| `build_system` | diagnostic intent, no specific error code: `빌드 에러 분석`, `compiler results`, `analyze build` | `ue_fix_errors(smart)` | G |
| `build_pipeline` | `BuildGraph`, `Horde`, `UAT`, `BuildCookRun`, `IssueHandler` | `ue_engine_docs(search)` + `ue_analyze_symbols` | F |
| `engine_internal` | `원인`, `root cause`, `code-level`, `소스 코드`, `엔진 내부` | `ue_cache_control(load_engine_runtime)` → `ue_analyze_symbols` + `ue_engine_docs` | F (alt) |
| `blueprint_debug` | `BP 디버깅`, `Blueprint debug`, `브레이크포인트`, `변수 값 확인`, `watched pins` | `ue_editor_debug(get_pie_state)` → watched values + execution trace | G (alt) |

**Classification boundary**: `compile_error` outranks `build_system` when a specific error code (`C2065`, `LNK2019`) is present. Otherwise, diagnostic-intent queries route to `build_system`.

### Step 2: Execute Diagnostic Tools (dispatcher)

Each path's full tool sequence (with Step-0 enhancements, MANDATORY engine source search procedure, multi-query planning, 2-hop rule, anti-fabrication rules) lives in REFERENCE.md. Use this dispatcher:

| Path | Goal | Entry tool | Full procedure |
|---|---|---|---|
| **A** Compile Error | Parse + suggest fix | `ue_fix_errors(smart)` | [REFERENCE.md § Path A](REFERENCE.md#path-a-compile-error) |
| **B** Runtime Crash | Crash log + symbol + engine docs + source search | `ue_fix_errors(crash_analysis)` then `ue_analyze_symbols` ⫽ `ue_engine_docs` | [REFERENCE.md § Path B](REFERENCE.md#path-b-runtime-crash) |
| **C** Unexpected Behavior | Symbol-first if C++ name mentioned, BP trace if asset, config if no code | `ue_analyze_symbols(search_symbols)` then `ue_engine_docs(search)` | [REFERENCE.md § Path C](REFERENCE.md#path-c-unexpected-behavior) |
| **D** Performance | Diagnose bottleneck + CVar regression check | `ue_analyze_insights(diagnose_bottleneck)` | [REFERENCE.md § Path D](REFERENCE.md#path-d-performance) |
| **E** GAS Issue | Ability/tag chain trace + config block check | `ue_manage_gameplay(trace_abilities)` | [REFERENCE.md § Path E](REFERENCE.md#path-e-gas-issue) |
| **F** Build Pipeline | BuildGraph/Horde docs + source classes | `ue_engine_docs(search, "BuildGraph ...")` | [REFERENCE.md § Path F](REFERENCE.md#path-f-buildgraph--horde--build-pipeline) |
| **F** alt: Engine Internal | Load engine PDBs, deep symbol+source investigation | `ue_cache_control(load_engine_runtime)` then `ue_analyze_symbols` ⫽ `ue_engine_docs` | [REFERENCE.md § Path F (alt)](REFERENCE.md#path-f-alt-engine-internal-root-cause-issue-6002) |
| **G** Build System | Editor ON → compiler results, OFF → scan log | `ue_fix_errors(smart)` | [REFERENCE.md § Path G](REFERENCE.md#path-g-build-system-issue-5291) |
| **G** alt: Blueprint Debug | PIE check → watched values + execution trace + static flow | `ue_editor_debug(get_pie_state)` | [REFERENCE.md § Path G (alt)](REFERENCE.md#path-g-alt-blueprint-debug-issue-5293) — extended scenarios in [BP Debugger Reference](REFERENCE.md#bp-debugger-reference) |
| **H** Audio Issue | Symbol search + au.* CVars + bottom-up engine source | `ue_analyze_symbols(search_symbols)` | [REFERENCE.md § Path H](REFERENCE.md#path-h-audio-issue-issue-6002) |

For a generic build-error request with no specific error code, always emit this safe minimum even
when the tool finds no compiler records. Do not promote an unobserved cause to a diagnosis:

```text
Classification: build_system
Evidence: ue_fix_errors — <finding or no build log found>
Root Cause: undetermined
Fix:
Step 1: <first evidence-gathering or remediation step>
Step 2: <next step, if applicable>
Verification: <build or compiler-result check to run>
```

**Cross-cutting rules** (apply to all paths that touch engine source):
- **2-HOP MINIMUM**: After finding a matching source file, Read it, then Grep what its key function calls and Read the callee. Root causes live in the implementation, not the API surface.
- **Multi-query planning** (Issue #6002): plan 3 diverse queries (EXACT / RELATED / SYMPTOM) BEFORE executing. Execute ALL — first-hit bias causes ~50% of bimodal failures.
- **Tool call budget**: engine source search ≤ 5 calls (2 Grep + 2-3 Read). After verification, write the report immediately.
- **Anti-fabrication**: only cite line numbers/code from files Read in this session. If Grep returns no matches → report "not found in engine source" instead of guessing.

Engine source paths by domain (Audio, Rendering, Animation, WorldPartition, etc.) → [REFERENCE.md § Engine Source Paths by Domain](REFERENCE.md#engine-source-paths-by-domain-issue-6679).

Full search guidelines (when to escalate, narrowing strategies, file-type expansion, mandatory verification step) → [REFERENCE.md § Engine Source Search Guidelines](REFERENCE.md#engine-source-search-guidelines-issue-6002).

---

## Output, Recovery, Quality Rules

After Step 2 completes:

1. Write the Debug Report using the **Medical-model template** → [REFERENCE.md § Output Format](REFERENCE.md#output-format-medical-model). Required sections: Symptoms / Diagnosis / Root Cause / Fix / Prevention.
2. Apply the **11 Answer Quality Rules** before submitting → [REFERENCE.md § Answer Quality Rules](REFERENCE.md#answer-quality-rules). Highlights:
   - **Confidence check** — Did you Read() the SPECIFIC function causing the issue?
   - **No fabrication** — only cite names from MCP results, Read/Grep output, or question text.
   - **Absence is an answer** — "not currently supported" with evidence beats inventing a mechanism.
   - **Symptom-to-API search** (Issue #6681) — extract symptom keywords FIRST, search source SECOND, propose diagnosis THIRD.
3. On any tool failure, consult the **Error Recovery table** → [REFERENCE.md § Error Recovery](REFERENCE.md#error-recovery). Smart mode auto-falls-back (e.g. `get_compiler_results` RC fail → `scan_build_log`).

## ⚠️ Final Response Contract (MUST — eval-gated)

Every final Debug Report must contain these fields, even for a short or under-specified request:

1. **Classification** — state the explicit category (`compile_error`, `linker`, `build_system`,
   `runtime_crash`, `blueprint_debug`, or the routed equivalent).
2. **Evidence** — identify the diagnostic tool plus the concrete error, callstack, file, symbol, or
   log finding it returned.
3. **Root Cause** — label it `confirmed` only when evidence supports it; otherwise write
   `Root Cause: undetermined` and list ranked hypotheses separately.
4. **Ordered remediation** — give numbered Step 1 / Step 2 / Step 3 actions tied to the evidence,
   including exact files/modules only when verified.
5. **Verification** — state the command, build, PIE action, or reproduction that proves the fix.

No crash report, build log, or source match is an honest insufficient-evidence result, not a
license to invent a cause. Ask for the missing artifact and provide the next diagnostic step; do
not claim the issue is resolved until verification evidence exists.

---

## Reference Material

| File | Purpose | Loaded |
|---|---|---|
| [REFERENCE.md](REFERENCE.md) | Full classification table, Paths A–H catalog, engine source guidelines, Medical output template, 11 Answer Quality Rules, Error Recovery table, Evaluation Criteria, BP Debugger Reference, Crash Doctor Reference | Always available — consult by anchor link from this file |
| [CL_SEARCH.md](CL_SEARCH.md) | Optional Epic GitHub commit-search workflow for version-specific bugs | Only when `--cl-search` flag passed (default: NOT loaded) |
| [EXAMPLES.md](EXAMPLES.md) | 3 worked scenarios (compile / runtime / unexpected) | Reference only |

---

## Self-Test Mode

`/ue-debug --self-test` runs focused validation of this skill:

1. **Structure check**: Verify `SKILL.md`, `REFERENCE.md`, `CL_SEARCH.md`, `EXAMPLES.md` exist; verify `BP_DEBUGGER_REFERENCE.md` and `CRASH_DOCTOR_REFERENCE.md` do NOT exist (folded into REFERENCE.md by Issue #10452).
2. **Frontmatter check**: Parse YAML frontmatter for required fields (`name`, `description`, `argument-hint`, `metadata.version`).
3. **Routing check**: Verify `routing.json` entry exists for `ue-debug` with matching trigger patterns.
4. **Tool reference check**: All MCP tools referenced in SKILL.md (`ue_fix_errors`, `ue_analyze_symbols`, `ue_grep`, `ue_read`, `ue_manage_blueprint`, `ue_trace_execution`, `ue_analyze_config`, `ue_editor_debug`, `ue_engine_docs`, `ue_analyze_insights`, `ue_manage_gameplay`, `ue_cache_control`) are valid tool names.
5. **Anchor resolution**: Every `REFERENCE.md#anchor` and `CL_SEARCH.md` link in this file points at a heading that exists in the target file.
6. **Example validity**: All `/ue-debug` invocations in EXAMPLES.md use correct skill name and reference existing files.

**Output**: PASS / FAIL with per-check results. Does not execute full diagnostic workflow.

---

**Status**: Phase 1 MVP + Phase 3 (build_system) + Phase 4 (blueprint_debug) + Tier 1-A modularization (Issue #10452, Epic #10451)
**Related**: Issue #4800 (MCP-First Tool Selection), Issue #5290 (Crash Doctor), Issue #5291 (FCompilerResultsLog), Issue #5293 (Remote BP Debugger), Issue #10440 (`/ci-monitor` template), Issue #10451 (Epic), Issue #10452 (this refactor)
