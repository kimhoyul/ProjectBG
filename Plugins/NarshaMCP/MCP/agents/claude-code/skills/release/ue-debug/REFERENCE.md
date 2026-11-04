# UE Debug — Reference

**Issue**: #4800, #5290, #5291, #5293, #6002, #6352, #6679, #6680, #6681, #10452
**Parent Skill**: [SKILL.md](SKILL.md)

This file is the complete reference catalog for `/ue-debug`. SKILL.md keeps only the imperative hot-path executor; everything else — full trigger lists, full Step 1 classification table, the per-path Step 2 diagnostic catalog (Paths A–H), engine source search guidelines, output template, answer quality rules, error recovery table, evaluation criteria, and the legacy `BP_DEBUGGER_REFERENCE` / `CRASH_DOCTOR_REFERENCE` material — lives here. The optional Epic GitHub CL search is in [CL_SEARCH.md](CL_SEARCH.md).

---

## Auto-Trigger Phrases

### Korean
- "왜 X가 발생해?", "X 에러 해결해줘"
- "빌드 에러 고쳐줘", "크래시 원인 찾아줘"
- "X가 안돼", "X가 작동 안해"
- "성능이 느려", "FPS 드랍 원인"
- "어빌리티가 안 발동돼"
- "빌드 에러 분석해줘", "컴파일 에러 상세히 보여줘", "빌드 왜 실패했어"
- "컴파일 결과 확인", "빌드 로그 분석", "빌드 에러 목록"
- "원인이 뭐야", "원인 찾아줘", "코드 레벨에서"
- "읽지 못합니다", "생성하지 않는", "동작하지 않"
- "해결 방법이 뭐야", "해결하나요", "원인과 해결 방법"

### English
- "Why is X happening?", "Fix this error"
- "Debug this crash", "Why doesn't X work?"
- "Build error help", "What's causing this?"
- "Performance is slow", "FPS drop cause"
- "Ability not activating"
- "Analyze build errors", "Show compiler results", "Why did the build fail?"
- "Build error detail", "Compiler log analysis", "Get all build errors"
- "What is the root cause?", "Code-level cause and fix"
- "doesn't read", "doesn't cast", "doesn't generate"
- "How to fix?", "What is the cause and fix?"

---

## Step 0d: File Type Diversity — Search Beyond .cpp/.h (Issue #6680)

> When initial grep returns no results OR the symptom involves rendering, build system, or infrastructure,
> expand search to non-C++ engine files that often contain root causes.

```python
# Method 3 — File Type Diversity: Search beyond .cpp/.h
#   Shader bugs: Grep in Engine/Shaders/Private/*.usf, *.ush
#   Build system: Grep in Engine/Build/**/*.xml, Engine/Source/Programs/**/*.cs
#   Config issues: Grep in Engine/Config/*.ini, Engine/Programs/**/Config/*.ini
#   WRONG: Only searching .cpp/.h — misses shaders, build scripts, configs
```

**When to trigger**:

| Symptom Domain | File Types to Search | Search Paths |
|---|---|---|
| Rendering, shader, post-process, translucency | `*.usf`, `*.ush` | `Engine/Shaders/Private/`, `Engine/Shaders/Public/` |
| Build system, automation, Horde, build graph | `*.xml`, `*.cs` | `Engine/Build/`, `Engine/Source/Programs/` |
| Config, platform settings, INI | `*.ini` | `Engine/Config/`, `Engine/Programs/**/Config/` |
| Any symptom with 0 C++ grep results | All above | Broaden progressively |

```python
# Use ue_grep with scope="engine" for engine-wide search
ue_grep(query="<keyword>", scope="engine", path_pattern="*.usf")
ue_grep(query="<keyword>", scope="engine", path_pattern="*.xml")
ue_grep(query="<keyword>", scope="engine", path_pattern="*.cs")
```

---

## Symptom Classification — Full Table

```python
SYMPTOM_CLASSIFIERS = {
    "compile_error": {
        "keywords": ["error C", "LNK", "undeclared", "undefined", "unresolved",
                     "cannot convert",
                     "GENERATED_BODY", "missing", "expected"],
        "tool": "ue_fix_errors",
        "operation": "smart"
    },
    "runtime_crash": {
        "keywords": ["crash", "assert", "ensure", "access violation", "nullptr",
                     "segfault", "fatal error", "크래시", "런타임 에러",
                     "unhandled exception", "stack overflow"],
        "tool": "ue_fix_errors",
        "operation": "crash_analysis"
    },
    "unexpected_behavior": {
        "keywords": ["doesn't work", "not working", "wrong", "안돼", "안 돼",
                     "작동 안", "이상해", "unexpected", "broken", "bug",
                     "not activating", "not triggering", "안 발동"],
        "tool": "ue_trace_execution",  # + ue_analyze_config
        "operation": "smart"
    },
    "performance": {
        "keywords": ["slow", "FPS", "hitch", "lag", "느려", "프레임", "성능",
                     "bottleneck", "frame drop", "GPU", "CPU bound",
                     "memory", "메모리"],
        "tool": "ue_analyze_insights",
        "operation": "diagnose_bottleneck"
    },
    "gas_issue": {
        "keywords": ["ability", "gameplay tag", "gameplay effect", "attribute",
                     "어빌리티", "태그", "이펙트", "GAS",
                     "not granting", "not activating", "blocked by"],
        "tool": "ue_manage_gameplay",  # + ue_analyze_config
        "operation": "trace_abilities"
    },
    # audio_issue: Audio/MetaSound-specific debugging.
    # Triggers for MetaSound interface bugs, spatialization artifacts, looping/attenuation issues.
    # Routes to Path G which searches audio-specific engine source directories.
    "audio_issue": {
        "keywords": ["MetaSound", "metasound", "메타사운드", "OneShot", "원샷",
                     "looping sound", "루핑", "attenuation", "감쇠", "spatialization",
                     "공간화", "ITD", "binaural", "바이노럴", "ping-pong",
                     "delay artifact", "사운드 안 재생", "sound not playing",
                     "sound not restarting", "오디오 버그", "audio bug",
                     "AudioComponent", "SoundCue", "SoundWave"],
        "tool": "ue_analyze_symbols",  # + ue_analyze_config + engine source search
        "operation": "search_symbols"
    },
    # build_system: diagnostic intent without specific error text.
    # Distinct from compile_error (which has specific error codes like "C2065", "LNK2019").
    # Triggers when user wants to *query / enumerate / analyze* the build state.
    # Priority: lower than compile_error. If specific error text is present, use compile_error.
    "build_system": {
        "keywords": ["빌드 에러", "컴파일 에러", "링커 에러", "빌드 에러 분석", "컴파일 에러 상세", "빌드 왜 실패", "빌드 로그 분석",
                     "컴파일 결과 확인", "빌드 에러 목록", "컴파일 에러 목록", "빌드 분석",
                     "build error detail", "compiler results", "analyze build", "build failure",
                     "compilation log", "get all build errors", "build errors list",
                     "why did the build fail", "show compiler output"],
        "tool": "ue_fix_errors",
        "operation": "smart"  # smart auto-routes: Editor ON → get_compiler_results, OFF → scan_build_log
    },
    # build_pipeline: BuildGraph / Horde / UAT build pipeline issues.
    # For questions about BuildGraph node execution, Horde step outcomes,
    # IssueHandlers, task dependencies, and local vs Horde behavior differences.
    # Routes to Path F which searches engine docs for BuildGraph parameters.
    "build_pipeline": {
        "keywords": ["BuildGraph", "Horde", "UAT", "BuildCookRun", "IssueHandler",
                     "LogParser", "Spawn task", "step outcome", "node execution",
                     "dependent nodes", "Failed Outcome", "ErrorLevel",
                     "빌드그래프", "호드", "빌드 파이프라인", "build pipeline"],
        "tool": "ue_engine_docs",  # + ue_analyze_symbols
        "operation": "search"
    },
    # Engine internal root cause analysis — deep source code investigation.
    # For questions asking "what is the code-level root cause?" or "why doesn't X work
    # in engine code?". Uses symbol search + engine docs to find the specific code path.
    "engine_internal": {
        "keywords": ["원인", "root cause", "코드 레벨", "code-level",
                     "소스 코드", "엔진 내부", "engine source",
                     "읽지 못", "생성하지 않", "doesn't cast",
                     "doesn't read", "doesn't generate",
                     "해결 방법", "해결하나요", "cause and fix"],
        "tool": "ue_analyze_symbols",  # + ue_engine_docs in parallel
        "operation": "search_symbols"
    },
    # Blueprint runtime debugging — requires PIE active. Combines runtime inspection
    # with static analysis for "why is my BP doing X?" questions.
    "blueprint_debug": {
        "keywords": ["BP 디버깅", "블루프린트 디버그", "브레이크포인트", "변수 값 확인",
                     "왜 이 분기", "실행 추적", "BP 실행 경로", "런타임 변수",
                     "Blueprint debug", "watch variable", "step through", "execution trace",
                     "why this branch", "BP runtime", "watched pins", "step into BP"],
        "tool": "ue_editor_debug",
        "operation": "get_pie_state"  # First check PIE, then branch to watched values / trace
    }
}
```

---

## Diagnostic Tool Catalog (Step 2 — full per-path catalog)

### Path A: Compile Error
```python
ToolSearch("select:mcp__narshamcp__ue_fix_errors")

# Smart mode auto-parses error and suggests fix
# > Step 0 Enhancement: grep이 소스 경로(.h/.cpp) 반환 시 error_message에 포함하여
# > `ue_fix_errors`의 스캔 범위를 프로젝트 전체 → 특정 파일로 축소.
# > 예: step0_source_path = "Source/MyGame/MyCharacter.cpp:42"
# >     → error_message에 파일 경로 포함하여 정밀 진단
result = ue_fix_errors(mode="smart", params={
    "error_message": "<paste error here>",
    "project_root": "<project_path>"
})
# Returns: error classification, root cause, suggested fix, code changes
```

### Path B: Runtime Crash

> **⚠️ MANDATORY**: For crash diagnosis, you MUST use `ue_engine_docs` alongside
> `ue_analyze_symbols`. PDB gives function names; engine docs give ownership
> semantics, known bugs, and memory patterns. **Skipping engine_docs = incomplete diagnosis.**

```python
ToolSearch("select:mcp__narshamcp__ue_fix_errors,mcp__narshamcp__ue_analyze_symbols,mcp__narshamcp__ue_engine_docs")

# 1. Crash log scan
result = ue_fix_errors(mode="crash_analysis")
# Optional: specify crash_path for a specific crash directory
# result = ue_fix_errors(mode="crash_analysis", crash_path="<path>/Saved/Crashes/<crash_dir>")

# Alternative: runtime log analysis (scans Saved/Logs/*.log)
result = ue_fix_errors(mode="runtime_analysis")
# Optional: specify log_path for a specific log file

# 2. Symbol search + Engine docs cross-check (PARALLEL — both mandatory)
# > Step 0 Skip: `ue_read`가 이미 심볼 정보(parent_class, source_file 등) 반환 시 스킵.
# > Step 0에서 symbols 도메인 매치가 없으면 스킵 (BP 전용 크래시).
symbol_info = ue_analyze_symbols(operation="search_symbols", params={
    "query": "<crash function name>"
})
docs = ue_engine_docs(operation="search", query="<crash class/function> crash cause")
ue_engine_docs(operation="grep", params={"pattern": "<crash class name>"})

# 3. MANDATORY Engine Source Search — 3 Methods (Issue #6002)
#    ⚠️ ALWAYS execute this step. Skip ONLY if step 2 already returned
#    specific file paths + line numbers from engine source code.
#    Budget: Max 5 tool calls. Narrow to relevant subsystem directory.
engine_dir = Read(".env")  # Extract UECODEGEN_ENGINE_DIR path
#
# SEARCH — Use these 3 Methods (prioritize Method 1):
#
# Method 1 — Function-First (MOST IMPORTANT): Use SPECIFIC class/function names
#   Extract exact function/class names from the question or from MCP results in Step 2.
#   ✅ Good: Grep("<specific class or function name from question or MCP results>")
#   ❌ Bad:  Grep("<broad domain keyword>") — too broad, finds wrong files
#   Extract exact names from the question text or from MCP tool results in Step 2.
#
# Method 2 — Call-Chain (FOLLOW THE DATA): After finding one function, trace the data flow
#   Found function F1 in FileA? → Grep("F1") across broader path to find callers in FileB
#   Found FileB calls FileA? → Read FileB to understand the full chain
#   Don't stop at first file — root cause is often 2-3 files deep in the call chain
#   KEY INSIGHT: If F1 writes a file/config → search what READS it + what could BLOCK the read
#     e.g., CopyPlatformSpecificFiles writes ini → FileExists checks it → PakPlatformFile blocks it
#   If F1 sets a variable → search who reads that variable and under what conditions
#
# Method 3 — Exact Name Discovery: Grep for CVar/flag/CL# exact strings
#   Grep("r.LumenScene", path=".../Renderer/") → find exact CVar names
#   Grep("CL#", path=".../Renderer/") → find changelist references
#
#   Domain-specific paths:
#   - Audio: Engine/Plugins/Runtime/Metasound/, Engine/Source/Runtime/SignalProcessing/, Engine/Source/Runtime/AudioMixer/
#   - Rendering: Engine/Source/Runtime/Renderer/Private/, Engine/Shaders/Private/ (.usf/.ush)
#   - Build/Config: Engine/Source/Programs/Shared/, Engine/Source/Runtime/PakFile/ (pak filesystem, FileExists blocking),
#     Engine/Source/Runtime/Core/Private/Misc/ (config, crash context), Engine/Build/Graph/ (.xml/.cs)
#   - Animation: Engine/Source/Runtime/Engine/Private/Animation/, Engine/Source/Editor/AnimGraph/
#   - Navigation: Engine/Source/Runtime/NavigationSystem/, Engine/Source/Runtime/Engine/Private/WorldPartition/NavigationData/
#   - WorldPartition: Engine/Source/Runtime/Engine/Private/WorldPartition/
#     WP Builder issues (modifications not saved, PostBuild/PostRun changes lost):
#       → Search Helper/Utility classes (e.g., FWorldPartitionHelpers) for package load/modify patterns
#       → WP = actor-per-package: modifications need LoadPackage + Modify flag + explicit save
#       → Builder pipeline: RunInternal → PostRun — check if save happens BEFORE your modifications
#       → Search ForEachActorDescInstance, LoadPackage in builder source
#
Grep(pattern="<SPECIFIC function/class name from question>", path=f"{engine_dir}/Engine/Source/Runtime/<subsystem>/")
# HYPOTHESIZE: Read the most relevant match, form a hypothesis
Read("<matched source file>")
# VERIFY: Grep to confirm — AND trace callers if multi-system issue (Method 2)
Grep(pattern="<function from hypothesis>", path=f"{engine_dir}/Engine/...")
# Read verification result if needed (optional, within 5-call budget)
Read("<verification file>")
# ⚠️ NEVER fabricate line numbers or code. Only cite lines you actually Read().

# 4. Runtime variable inspection (optional, requires Editor running)
#    ABI: actor_path + properties[] — not actor/variable, not nested params={}
ToolSearch("select:mcp__narshamcp__ue_editor_debug")
runtime = ue_editor_debug(
    operation="start_watch",
    actor_path="<world object path or actor path>",
    properties=["<property to watch>"],
)

# 5. ⚠️ STOP HERE — write your Debug Report with findings so far
# (For version/CL history, user can invoke /ue-cl-tracker separately)
```

### Path C: Unexpected Behavior

> **⚠️ MANDATORY**: If the user mentions a **C++ class or function name** (e.g., `UAnimStateNodeBase::GetTransitionList`),
> you MUST use `ue_analyze_symbols` to search for it AND `ue_engine_docs` for context.
> Built-in Grep/Read cannot replace PDB-indexed symbol search for engine internals.
>
> **Step 0 Domain-Based Selection**: 현재 3개 순차 호출을 Step 0 도메인에 따라 1개만 선택.
> - `step0_domains`에 assets 포함 → `ue_trace_execution`만 (BP 실행 추적)
> - `step0_domains`에 symbols 포함 → `find_delegate_refs`만 (C++ 이벤트/델리게이트)
> - `step0_domains`에 configs 포함 → `search_config`만 (설정 오버라이드)
> - 여러 도메인 매치 시 → 가장 관련 높은 1-2개만 선택 (3개 전부 호출 금지)

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_symbols,mcp__narshamcp__ue_engine_docs,mcp__narshamcp__ue_trace_execution,mcp__narshamcp__ue_analyze_config")

# 1. If C++ class/function is mentioned — search symbols FIRST (MANDATORY)
symbols = ue_analyze_symbols(operation="search_symbols", params={
    "query": "<class or function name from user question>"
})
# Also search callers/hierarchy if relevant
callers = ue_analyze_symbols(operation="find_callers", params={
    "function_name": "<function name>"
})

# 2. Engine docs for design context and known issues
docs = ue_engine_docs(operation="search", query="<class/function> behavior")

# 2.4 Tool Selection by Symptom Type (Issue #6002)
#   Choose the RIGHT MCP tool based on the symptom, not just the domain:
#   - Visual artifact / flickering / rendering bug → ue_grep first (source context),
#     then ue_analyze_config (CVar workaround). NOT ue_analyze_symbols (wrong granularity).
#   - Crash / assertion → ue_analyze_symbols first (callstack symbols),
#     then ue_grep (source context around crash site).
#   - Feature not working / config issue → ue_analyze_config first,
#     then ue_grep if config alone doesn't explain.
#   - Plugin / subsystem integration → ue_analyze_symbols (class hierarchy),
#     then ue_grep (module-specific patterns like delegates, callbacks).
#   - Version regression ("worked in X, broken in Y") → ue_grep in the subsystem source
#     for version-guarded code, then ue_analyze_config for new CVars added as workarounds.

# 2.5 MANDATORY: Multi-Query Search Planning (Issue #6002)
#   Before executing ANY engine source search, plan 3 diverse queries first.
#   Do NOT pick just one query — different paths lead to different results.
#
#   Query 1 — EXACT: The specific class/function name from the question
#     e.g., "UWorldPartitionNavigationDataBuilder"
#   Query 2 — RELATED: A Helper/Manager/Utility class in the same module
#     e.g., append "Helper", "Helpers", "Utils", "Manager" to the module name
#     e.g., "FWorldPartitionHelpers", "FNavigationDataBuilder"
#   Query 3 — SYMPTOM: The symptom described as engine-internal keywords
#     e.g., "PostRun serialize package save" (if issue is about changes not persisting)
#
#   Execute ALL 3 queries (ue_analyze_symbols or ue_grep). Merge results before hypothesis.
#
#   ⚠️ MANDATORY: Execute ALL planned queries even if the first query returns results.
#   First-hit bias causes 50% of bimodal failures — the model stops searching
#   after finding "something plausible" instead of verifying with additional queries.
#
#   After ALL queries complete, COMPARE results:
#   - Multiple queries converge on same source file? → High confidence
#   - Queries point to different files? → Read BOTH, choose the more specific one
#   - Only 1 query returned results? → That result needs extra verification (Step 3)
#
#   If ALL 3 return empty → try 3 MORE with variations:
#   Query 4 — PREFIX: Add/remove UE type prefix (F, U, A, E, I, T)
#     e.g., "WorldPartitionNavigationDataBuilder" → "FWorldPartitionNavigationDataBuilder"
#   Query 5 — BROADER: Search parent directory instead of specific subsystem
#     e.g., "Engine/Source/Runtime/" instead of "Engine/Source/Runtime/Engine/Private/WorldPartition/"
#   Query 6 — RELATED CONCEPT: Synonym search
#     e.g., "save" → "serialize" → "package"; "load" → "import" → "mount"
#
#   Max budget: 6 search calls before moving to hypothesis.
#   Never skip to hypothesis with fewer than 3 search attempts.

# 3. MANDATORY Engine Source Search — 3 Methods (Issue #6002)
#    ⚠️ ALWAYS execute this step. Skip ONLY if steps 1-2 already returned
#    specific file paths + line numbers from engine source code.
#    Budget: Max 5 tool calls. Narrow to relevant subsystem directory.
engine_dir = Read(".env")  # Extract UECODEGEN_ENGINE_DIR path
#
# SEARCH — Use these 3 Methods (prioritize Method 1):
#
# Method 1 — Function-First (MOST IMPORTANT): Use SPECIFIC class/function names
#   ✅ Good: Grep("<specific class or function name from question or MCP results>")
#   ❌ Bad:  Grep("<broad domain keyword>") — too broad, finds wrong files
#   Extract exact names from the question text or from MCP tool results in Step 2.
#
# Method 2 — Call-Chain (FOLLOW THE DATA): After finding one function, trace the data flow
#   Found F1 in FileA? → Grep("F1") broader to find callers in FileB
#   Don't stop at first file — root cause is often 2-3 files deep
#   KEY INSIGHT: If F1 writes a file/config → search what READS it + what could BLOCK the read
#
#   VERIFICATION after finding a candidate source:
#   1. Is this the IMPLEMENTATION or just a WRAPPER/DECLARATION?
#      → If header only: Grep the function name to find the .cpp implementation
#   2. Does this file reference another system for the actual behavior?
#      → If yes: follow that reference deeper (e.g., Manager → Impl → Core)
#   3. Are there CVars or config settings that modify this behavior?
#      → Quick check: ue_analyze_config with the subsystem name from the question
#   If F1 sets a variable → search who reads that variable and under what conditions
#
# Method 3 — Exact Name Discovery: Grep for CVar/flag/CL# exact strings
#   Grep("r.LumenScene", path=".../Renderer/") → exact CVar names
#
#   Domain-specific paths:
#   - Audio: Engine/Plugins/Runtime/Metasound/, Engine/Source/Runtime/SignalProcessing/, Engine/Source/Runtime/AudioMixer/
#     MetaSound sync/interface issues (subtitle, random output, data access):
#       → Use ue_engine_docs("MetaSound") for MetaSound Blueprint integration APIs
#       → Search MetaSoundSource + the specific node type (Random, Output, WavePlayer)
#     3D spatialization issues (ITD, delay, ping-pong, positioning):
#       → Search delay line classes in Engine/Source/Runtime/SignalProcessing/ for delay implementation
#       → Search au.* CVars via ue_analyze_config for spatialization settings
#
#   - Rendering: Engine/Source/Runtime/Renderer/Private/, Engine/Shaders/Private/ (.usf/.ush)
#   - Build/Config: Engine/Source/Programs/Shared/, Engine/Source/Runtime/PakFile/ (pak filesystem, FileExists blocking),
#     Engine/Source/Runtime/Core/Private/Misc/ (config, crash context), Engine/Build/Graph/ (.xml/.cs)
#
#   - Animation: Engine/Source/Runtime/Engine/Private/Animation/, Engine/Source/Editor/AnimGraph/
#     Retargeting issues (different body proportions, bone scale):
#       → TWO separate retargeting systems exist — check BOTH:
#         (a) Legacy per-bone: Search "EBoneTranslationRetargetingMode" or "TranslationRetargetingMode"
#             in Engine/Source/Runtime/Engine/Classes/Animation/Skeleton.h — enum defines all available modes
#         (b) Modern IK Retargeter: Use ue_manage_rigging to inspect chain retargeting settings
#       → Root/pelvis often need different mode than spine/extremity bones in BOTH systems
#       → For "same skeleton, different proportions" questions, the legacy per-bone system is usually relevant
#     Animation State Machine issues (Conduit, Transition priority):
#       → Search UAnimStateNodeBase, UAnimStateConduitNode in AnimGraph/
#       → ALWAYS include in answer: (1) WHY the behavior occurs, (2) WHERE in source, (3) HOW to override
#
#   - Editor/Plugin systems (Mutable, Customizable Object, mesh visibility properties):
#     After finding primary implementation file, ALWAYS also search:
#       → *ConversionUtils* or *Conversion* helpers in the same plugin directory
#       → *Private.h headers in the same module (propagation logic often lives in Private headers)
#
#   - Navigation: Engine/Source/Runtime/NavigationSystem/, Engine/Source/Runtime/Engine/Private/WorldPartition/NavigationData/
#   - WorldPartition: Engine/Source/Runtime/Engine/Private/WorldPartition/
#     WP Builder issues (modifications not saved, PostBuild/PostRun changes lost):
#       → Search Helper/Utility classes (e.g., FWorldPartitionHelpers) for package load/modify patterns
#       → WP = actor-per-package: modifications need LoadPackage + Modify flag + explicit save
#       → Builder pipeline: RunInternal → PostRun — check if save happens BEFORE your modifications
#       → Search ForEachActorDescInstance, LoadPackage in builder source
#
Grep(pattern="<SPECIFIC function/class name from question>", path=f"{engine_dir}/Engine/Source/Runtime/<subsystem>/")
# HYPOTHESIZE: Read the most relevant match, form a hypothesis
Read("<matched source file>")
# VERIFY: Grep to confirm — AND trace callers if multi-system issue (Method 2)
Grep(pattern="<function from hypothesis>", path=f"{engine_dir}/Engine/...")
# Read verification result if needed (optional, within 5-call budget)
Read("<verification file>")
# ⚠️ NEVER fabricate line numbers or code. Only cite lines you actually Read().

# 4. Trace execution to find where behavior diverges (if BP involved)
# > Step 0 Skip: step0_domains에 assets가 없으면 스킵 (C++ 심볼이나 config 문제)
trace = ue_trace_execution(operation="smart", params={
    "target": "<blueprint or function name>",
    "output_format": "html"  # Interactive HTML for visual debugging in browser
})
# output_format options: "json" (default), "html" (interactive), "mermaid" (diagram)

# 5. Check if delegate/event binding is broken ("event not firing" debugging)
# > Step 0 Skip: step0_domains에 symbols가 없으면 스킵 (BP나 config 문제)
delegates = ue_analyze_symbols(operation="find_delegate_refs", params={
    "target": "<event or delegate name>"
})

# 6. Check if config is overriding expected behavior
# > Step 0 Skip: step0_domains에 configs가 없으면 스킵 (코드/BP 문제)
config = ue_analyze_config(operation="search_config", params={
    "key": "<relevant config key>"
})

# 7. ⚠️ STOP HERE — write your Debug Report with findings so far
# (For version/CL history, user can invoke /ue-cl-tracker separately)
```

### Path D: Performance
```python
ToolSearch("select:mcp__narshamcp__ue_analyze_insights")

# Diagnose bottleneck
result = ue_analyze_insights(operation="diagnose_bottleneck", params={
    "trace_path": "<.utrace file if available>"
})

# Or get stats overview
result = ue_analyze_insights(operation="get_stats", params={})

# MANDATORY for performance regression ("slower in 5.x", "regression", "was fast before"):
# Cross-check CVars that control the subsystem mentioned in the symptom.
ToolSearch("select:mcp__narshamcp__ue_analyze_config")
ue_analyze_config(operation="search_config", params={
    "key": "<subsystem keyword>",  # e.g., "VT", "Landscape", "Nanite", "Shadow"
})
# Performance regressions are often caused by new CVars with different defaults.
# Search for CVars matching the subsystem, then check if defaults changed between versions.
```

### Path E: GAS Issue
```python
ToolSearch("select:mcp__narshamcp__ue_manage_gameplay")

# Trace ability chain to find where it breaks
# > Step 0 Enhancement: grep이 정확한 GAS 태그 (예: "Ability.Attack.Melee") 반환 시
# > 사용자 쿼리 텍스트 대신 정확한 태그를 trace_abilities에 전달하여 정밀 추적.
# > step0_read_detail에서 태그 → Ability 매핑 확인 시 직접 사용.
result = ue_manage_gameplay(operation="trace_abilities", params={
    "tag": "<ability tag or class name>"  # Step 0 grep 결과의 정확한 태그 우선 사용
})

# Check if config is blocking
# > Step 0 Skip: step0_domains에 configs가 없으면 스킵 (config 문제 아닐 가능성 높음).
# > GAS 이슈 대부분은 태그/바인딩 문제 — config 체크는 보충적.
ToolSearch("select:mcp__narshamcp__ue_analyze_config")
config = ue_analyze_config(operation="search_config", params={
    "key": "AbilitySystem"
})
```

### Path F: BuildGraph / Horde / Build Pipeline

> **⚠️ MANDATORY**: If the question mentions **BuildGraph**, **Horde**, **UAT**, **BuildCookRun**,
> **IssueHandlers**, **LogParser**, or **Spawn task**, you MUST search engine docs for
> BuildGraph task parameters and Horde execution semantics.

```python
ToolSearch("select:mcp__narshamcp__ue_engine_docs,mcp__narshamcp__ue_analyze_symbols")

# 1. Search engine docs for BuildGraph/Horde behavior and parameters
docs = ue_engine_docs(operation="search", query="BuildGraph Spawn task ErrorLevel parameter")
ue_engine_docs(operation="grep", params={"pattern": "ErrorLevel"})
ue_engine_docs(operation="search", query="Horde IssueHandler LogParser step outcome")

# 2. Search for relevant BuildGraph/Horde source classes
symbols = ue_analyze_symbols(operation="search_symbols", params={
    "query": "BuildGraph"
})

# 3. Search for Horde agent issue handler source
ue_engine_docs(operation="grep", params={"pattern": "IssueHandler"})
```

### Path F (alt): Engine Internal Root Cause (Issue #6002)

For questions asking "what is the code-level root cause?" or investigating engine-internal
behavior (e.g., "why doesn't X work in Test/Shipping builds?"). These require searching
engine source code via PDB index rather than analyzing build logs.

> **⚠️ MANDATORY FIRST STEP**: Before ANY symbol search, you MUST call:
> `ue_cache_control(operation="load_engine_runtime")`
> This loads 50K-200K engine symbols needed for engine-internal analysis.
> Skipping this step will cause you to miss critical shader/rendering classes.
> **Do NOT skip this even if you think you already have enough symbols.**

```python
# 1. ALWAYS load engine PDBs first — Path F requires engine-internal analysis
ToolSearch("select:mcp__narshamcp__ue_cache_control,mcp__narshamcp__ue_analyze_symbols,mcp__narshamcp__ue_engine_docs")
ue_cache_control(operation="load_engine_runtime")  # 30-60s, adds 50K-200K engine symbols

# 2. Search for key classes/functions mentioned in the question (PARALLEL)
symbols = ue_analyze_symbols(operation="search_symbols", params={
    "query": "<key class or function from question>"
})
docs = ue_engine_docs(operation="search", query="<key class or function>")

# 3. Read engine source files directly — PDB has names but NOT shader params/bool flags
# Use Glob to find .cpp files, then Read them for the actual filtering logic
engine_files = Glob("**/Engine/Source/Runtime/**/<filename from symbols>.cpp")
if engine_files:
    Read(engine_files[0])  # Look for bool params, shader dispatch code, filtering logic

# 4. Trace callers/hierarchy for deeper understanding
ue_analyze_symbols(operation="find_callers", params={
    "function_name": "<relevant function>"
})

# 5. Check related config if applicable
ToolSearch("select:mcp__narshamcp__ue_analyze_config")
ue_analyze_config(operation="search_config", params={
    "key": "<relevant config key>"
})
```

> **IMPORTANT**: PDB symbols show class/function NAMES but not shader parameters or bool flags.
> If `ue_analyze_symbols` finds related classes but the exact filtering mechanism is unclear,
> you MUST use `Glob` to find the engine source `.cpp` file and `Read` it directly.
> Parameters like `bDrawNaniteMeshes` are only visible in source code, not in PDB.
>
> **⚠️ MANDATORY FINAL STEP**: After investigation (max 15 turns), you MUST produce a
> **Debug Report** with these sections:
> 1. **Symptoms** — what the user reported
> 2. **Root Cause** — code-level explanation with file:line references
> 3. **Fix** — concrete code modification or workaround
>
> If investigation is incomplete after 15 turns, report what you found and what remains unknown.
> An incomplete answer with partial findings is ALWAYS better than no answer.

### Path G: Build System (Issue #5291)

Use when the user wants to **enumerate / analyze / diagnose** the build state, not when a
specific error code is already in the query (that's Path A: Compile Error).

**Routing logic**:
- Editor ON (RC available) → `get_compiler_results` — reads FCompilerResultsLog directly,
  returns tokenized errors with Blueprint node annotations and engine-suggested fixes
- Editor OFF → `scan_build_log` — auto-detects Saved/Logs/*.log, parses with BuildLogParser

```python
ToolSearch("select:mcp__narshamcp__ue_fix_errors")

# smart auto-routes based on Editor connectivity:
#   Editor ON  → get_compiler_results (FCompilerResultsLog via RC)
#   Editor OFF → scan_build_log (Saved/Logs/ auto-detect)
result = ue_fix_errors(operation="smart", params={
    "project_root": "<project_path>"
})
# Returns: errors[], summary{total_errors, by_source}, token_enrichment, next_actions

# If user specifies a Blueprint to focus on (Editor ON only):
result = ue_fix_errors(operation="get_compiler_results", params={
    "blueprint_path": "/Game/MyBlueprint.MyBlueprint",
    "include_warnings": True
})

# If user wants to explicitly scan an offline log:
result = ue_fix_errors(operation="scan_build_log", params={
    "project_root": "<project_path>",
    "include_warnings": False,
    "max_errors": 50
})

# If user wants to trigger a fresh build and capture errors:
result = ue_fix_errors(operation="build_and_fix", params={
    "project_root": "<project_path>",
    "platform": "Win64",
    "configuration": "Development"
})
```

**Output to report**:
- Error count by source (msvc / linker / blueprint / unknown)
- Token-enriched errors: Blueprint node annotations, engine-suggested fixes (confidence 0.99)
- Staleness warning if log file is >1 hour old
- `next_actions` from the tool result (e.g. "Fix 3 compile errors", "Navigate to BP node X")

### Path G (alt): Blueprint Debug (Issue #5293)

Combines runtime inspection (watched values, execution trace) with static flow analysis
for diagnosing "why is my Blueprint doing X?" questions.

> **ABI note (Path G dogfood 2026-08-11):** `ue_editor_debug` reads **top-level** argument keys
> (`blueprint_path`, `node_guid`, `max_depth`, …). Do **not** nest them under `params={…}` —
> nested form returns `requires 'blueprint_path'` without reaching Remote Control.
> There is **no** `watch_variable` op. Use `get_watched_values` (BP watched pins) or
> `inspect_variable` / `start_watch` (live actor properties).
> Start PIE via `ue_editor_automation(operation="play_in_editor")` — not `start_pie`.

```python
# Step 1: Check PIE state
ToolSearch("select:mcp__narshamcp__ue_editor_debug")
pie_state = ue_editor_debug(operation="get_pie_state")

if pie_state.is_paused or pie_state.is_running:
    # Step 2: Get watched pin values (runtime) — requires pins watched in Editor UI
    watches = ue_editor_debug(
        operation="get_watched_values",
        blueprint_path="<blueprint_path>",  # e.g. /Game/Blueprints/BP_Hero.BP_Hero
        max_depth=5,
    )

    # Step 3: Get execution trace (what nodes actually ran)
    trace = ue_editor_debug(
        operation="get_execution_trace",
        max_entries=100,
        blueprint_filter="<blueprint_name>",
    )

    # Step 4: Get debug state (breakpoint info, current node)
    debug_state = ue_editor_debug(operation="get_debug_state")

    # Step 5: Combine with static flow analysis
    ToolSearch("select:mcp__narshamcp__ue_trace_execution")
    static_flow = ue_trace_execution(operation="trace_execution_flow", params={
        "blueprint_name": "<blueprint_name>"
    })

    # Step 6: Compare runtime trace vs static flow to find divergence point
    # Report: which path was taken, which was expected, variable values at divergence
else:
    # PIE not running — set breakpoint, then play, then re-run Path G
    ue_editor_debug(
        operation="set_breakpoint",
        blueprint_path="<blueprint_path>",
        node_guid="<target_node_guid>",  # or node_title=
    )
    # ue_editor_automation(operation="play_in_editor")  # not start_pie
```

**Output to report**:
- PIE state (running/paused/stopped)
- Watched variable values with runtime types
- Execution trace (which nodes actually ran, in order)
- Static vs runtime flow comparison (divergence point)
- Suggested next debugging step (set breakpoint, step into, inspect variable)

> See [BP Debugger Reference](#bp-debugger-reference) below for the full Blueprint runtime debugging workflow, scenarios, and report format.

### Path H: Audio Issue (Issue #6002)

For audio-specific debugging: MetaSound interface bugs, spatialization artifacts,
looping/attenuation issues, AudioComponent problems.

> **⚠️ MANDATORY**: For audio issues, you MUST search audio-specific engine source
> directories. Generic symbol search often misses audio implementation details that
> live in plugin directories (`Engine/Plugins/Runtime/Metasound/`).

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_symbols,mcp__narshamcp__ue_analyze_config,mcp__narshamcp__ue_engine_docs")

# 1. Search for audio-related classes/functions mentioned in the question
symbols = ue_analyze_symbols(operation="search_symbols", params={
    "query": "<audio class from question>"  # FDelay, FParameterInterface, FMetaSoundSource, etc.
})
docs = ue_engine_docs(operation="search", query="<audio keyword> MetaSound behavior")

# 2. Search audio CVars (prefix: au.)
config = ue_analyze_config(operation="search_config", params={
    "key": "au.<relevant keyword>"  # e.g., au.DisableBinauralSpatialization
})

# 3. MANDATORY Engine Source Search — Bottom-Up principle (Issue #6002)
engine_dir = Read(".env")  # Extract UECODEGEN_ENGINE_DIR path
#
# ⚠️ PRINCIPLE: Search LOW-LEVEL modules BEFORE high-level modules.
# Audio bugs often originate in signal processing primitives, not in the
# high-level nodes/spatializers that call them. The high-level file may look
# relevant (name matches the symptom) but the actual bug is in the utility it calls.
#
# SEARCH ORDER (bottom-up):
#   1. SignalProcessing/ — low-level DSP primitives (delay lines, filters, interpolation)
#   2. AudioMixer/       — mixing, voice management, AudioComponent
#   3. Metasound/        — MetaSound node implementations
#   4. Spatialization/   — spatializer plugins (ITD, HRTF)
#
# ❌ WRONG: Jump straight to the file whose name matches the symptom keyword
# ✅ RIGHT: Start from the lowest-level module, read what's there, then trace upward
#
# Step A: Search the lowest-level audio module first
Grep(pattern="<keyword from question>", path=f"{engine_dir}/Engine/Source/Runtime/SignalProcessing/")
# Step B: ⚠️ MANDATORY — Read matched file, don't just note the filename
Read("<matched source file>")  # Read the FULL file, find the key function
# Step C: ⚠️ 2-HOP RULE — Trace what Step B's function CALLS internally
# The root cause is almost never in the first file. Read 1-2 levels deeper.
Grep(pattern="<function found in Step B>", path=f"{engine_dir}/Engine/Plugins/Runtime/")
Read("<callee source file>")  # Read THIS file too — this is where root causes live
# Step D: If needed, continue up to MetaSound/Spatialization
Grep(pattern="<ExactFunctionName>", path=f"{engine_dir}/Engine/Plugins/Runtime/Metasound/")
Grep(pattern="<ExactFunctionName>", path=f"{engine_dir}/Engine/Source/Runtime/AudioMixer/")
# ⚠️ NEVER fabricate line numbers, variable names, or bug descriptions not found in source.
#
# ⚠️ 2-HOP MINIMUM RULE (applies to ALL debug paths, not just Audio):
# After finding a matching source file, you MUST:
#   Hop 1: Read the file. Identify the key function related to the symptom.
#   Hop 2: Search for what that function calls internally. Read the callee file.
# Root causes live in the IMPLEMENTATION, not the API surface.
# If you stop at Hop 1, you'll describe the symptom but miss the mechanism.
```

**Common audio debugging patterns** (always search bottom-up):
- Audio artifact bugs → Start from **SignalProcessing/** (lowest-level DSP primitives like delay lines, filters, interpolation), then trace upward to callers
- MetaSound node issues → Start from **Metasound/** plugin source. MetaSounds use an interface system — search for the interface class hierarchy when debugging registration or lifecycle issues
- Playback/lifecycle bugs → Start from **AudioMixer/** (voice management, virtualization, AudioComponent). UE's audio virtualization system controls when sounds are stopped/restarted based on distance — search for virtualization-related code when sounds don't restart
- Spatialization bugs → Start from **SignalProcessing/** (DSP primitives used by spatializers including delay lines for ITD), then check spatialization plugin code. Audio positioning artifacts often originate in low-level DSP, not the spatializer itself
- AudioComponent issues → Search `UAudioComponent`, `FAudioDevice` in **AudioMixer/**
- MetaSound subtitle/caption issues → MetaSounds and SoundCues have different subtitle architectures. Search in **Engine/Private/** for subtitle management and in **Metasound/** for output handling

**Audio debugging tips**:
- Always search the specific class/function name mentioned in the error or symptom using `ue_analyze_symbols` first
- Audio CVars use the `au.` prefix — search with `ue_analyze_config` for relevant settings
- When debugging MetaSound interface issues, search for the interface base classes and registration code in the Metasound plugin
- For sound lifecycle bugs (not restarting, premature stop), investigate the voice management and virtualization system in AudioMixer

---

## Engine Source Paths by Domain (Issue #6679)

> **Used by**: Path B (Runtime Crash — engine symbol lookup), Path C (Unexpected Behavior — engine source tracing)
>
> When searching engine source code with `ue_grep` or `ue_analyze_symbols`, use these
> domain-specific paths to ensure comprehensive coverage. Include **all** listed paths
> for the relevant domain — the "+" paths are commonly missed but contain critical files.

| Domain | Search Paths |
|--------|-------------|
| Audio | `Engine/Plugins/Runtime/Metasound/`, `Engine/Source/Runtime/SignalProcessing/`, `Engine/Plugins/Runtime/Metasound/Source/MetasoundFrontend/` |
| Rendering | `Engine/Source/Runtime/Renderer/`, `Engine/Shaders/Private/` |
| Build | `Engine/Source/Programs/`, `Engine/Build/Graph/` |
| Animation | `Engine/Source/Runtime/Engine/Private/Animation/`, `Engine/Source/Editor/AnimGraph/`, `Engine/Plugins/Animation/` |
| Navigation | `Engine/Source/Runtime/NavigationSystem/` |
| WorldPartition | `Engine/Source/Runtime/Engine/Private/WorldPartition/` |
| Physics | `Engine/Source/Runtime/Engine/Private/PhysicsEngine/`, `Engine/Source/Runtime/PhysicsCore/` |
| Sequencer | `Engine/Source/Runtime/MovieScene/`, `Engine/Source/Runtime/Engine/Private/Animation/` (Montage) |
| Editor Plugins | `Engine/Plugins/Mutable/`, `Engine/Plugins/Experimental/` |
| VP/nDisplay | `Engine/Plugins/Runtime/nDisplay/`, `Engine/Source/Runtime/Media/`, `Engine/Plugins/VirtualProduction/` |
| Platforms/XR | `Engine/Source/Runtime/ApplicationCore/Private/`, `Engine/Plugins/Runtime/OpenXR/` |
| Build Pipeline | `Engine/Source/Programs/AutomationTool/`, `Engine/Source/Programs/UnrealBuildTool/`, `Engine/Source/Programs/Shared/` |
| UI/Slate | `Engine/Source/Runtime/Slate/`, `Engine/Source/Runtime/UMG/` |
| General/Core | `Engine/Source/Runtime/Core/`, `Engine/Source/Runtime/Engine/` |

---

## Engine Source Search Guidelines (Issue #6002)

**When to escalate to engine source search** (Paths B, C, F):

Engine source search via `Grep`/`Read` on `UECODEGEN_ENGINE_DIR` is the **last resort** when
MCP tools (`ue_engine_docs`, `ue_analyze_symbols`) and config searches don't explain the behavior.

**Trigger conditions** (any one is sufficient):
1. MCP tools returned results but they don't explain the **root cause** of the symptom
2. The symptom involves a specific engine subsystem (Audio, Rendering, Animation, etc.)
3. The user mentions a specific engine class/function name not found in docs
4. The question describes behavior that requires reading the actual implementation

**How to search effectively**:
- Read `.env` to get `UECODEGEN_ENGINE_DIR` path
- **Narrow the search directory** to the relevant subsystem to avoid scanning 10GB+:
  - Audio: `Engine/Plugins/Runtime/Metasound/`, `Engine/Source/Runtime/AudioMixer/`
  - Rendering: `Engine/Source/Runtime/Renderer/`, `Engine/Shaders/Private/**/*.{usf,ush}`
  - Animation: `Engine/Source/Editor/AnimGraph/`, `Engine/Source/Runtime/Engine/Private/Animation/`
  - Build/Horde: `Engine/Source/Programs/Shared/**/*.cs` (Horde is C#, not C++)
  - General: `Engine/Source/Runtime/<ModuleName>/`
- **Search ALL relevant file types** — NOT just .cpp/.h:
  - Shaders: `.usf`, `.ush` (rendering bugs often live in shader code)
  - Build scripts: `.cs` (Horde, UBT, BuildGraph are C#)
  - Configs: `.ini`, `.uplugin`, `.uproject`
- Search for **class names, function names, or error messages** from the question
- Read matched files — look for **comments, ensure conditions, version checks** that explain the behavior

**Anti-fabrication rules** (Issue #6002):
1. NEVER cite specific line numbers unless you `Read()` that file in this session
2. NEVER invent function names, variable names, or bug descriptions not found in source
3. If Grep returns no matches → report "not found in engine source", don't guess
4. If you found a file but the specific function isn't there → search broader, don't fabricate

**Turn budget**: Engine source search MUST NOT exceed 5 tool calls (2 Grep + 2-3 Read).
After the verification step, **immediately write the Debug Report**. Do NOT continue searching for
more files — an answer based on partial findings is far better than no answer.

**Search methodology** (Search → Hypothesize → Verify):
1. **SEARCH**: Grep for symptom keywords in engine source — search ALL relevant file types,
   not just .cpp/.h (include .usf/.ush shaders, .ini configs, .cs build files)
2. **HYPOTHESIZE**: Read the best-matching file, form a hypothesis about the root cause
3. **VERIFY**: Before writing your answer, do ONE verification check:
   - Grep for the specific function/type name you're about to cite — confirm it exists
   - If your hypothesis relies on prior knowledge (not found in source), search source for that claim
   - If the code you found doesn't directly explain the EXACT symptom, your hypothesis is likely wrong

**Source code is ground truth**:
- If engine source shows a DIFFERENT mechanism than your prior knowledge, **trust the source**
- Read type names, template parameters, and comments EXACTLY — similar names ≠ same type
  (e.g., `TObjectPtr` ≠ `TStrongObjectPtr`, these have completely different semantics)
- If you cannot confirm your hypothesis in source, explicitly state "not confirmed in source"
  rather than presenting prior knowledge as verified fact

**⚠️ MANDATORY VERIFICATION (before writing Debug Report)**:
1. Grep for the EXACT class/function name you are about to cite as root cause
2. If found → confirm file path and line context match your hypothesis
3. If NOT found → revise your hypothesis or explicitly state "not confirmed in source"
4. **NEVER** cite a class, function, or code snippet without verifying it exists
5. **NEVER** fabricate line numbers, code paths, or source file contents
6. If you cannot locate the specific source in the indexed codebase, say so —
   an honest "could not locate [X]" is far more valuable than a fabricated answer

**Graceful fallback**: If `UECODEGEN_ENGINE_DIR` is not set in `.env`, skip this step and report
"Engine source not available for deep analysis" in the debug report.

---

## BP Debugger Reference

> **Source**: Was a separate `BP_DEBUGGER_REFERENCE.md`. Folded here per Epic #10451 Tier 1-A (Issue #10452).
> **Issue**: #5293 — Parent Skill: `/ue-debug` v1.3.0 (`blueprint_debug` classifier)

This reference extends `/ue-debug` with Blueprint runtime debugging capabilities.
Activates when keywords include: "BP 디버깅", "Blueprint debug", "BP 브레이크포인트",
"변수 값 확인", "왜 이 분기".

Combines static flow analysis (ue_trace_execution) with runtime state inspection
(ue_editor_debug) for comprehensive BP debugging.

### Blueprint Debug Workflow

```text
Step 1: Static Flow Analysis
   │ ue_manage_blueprint(get_structure) → understand BP graph
   ▼
Step 2: Execution Tracing
   │ ue_trace_execution(trace_execution_flow) → find execution path
   ▼
Step 3: Runtime State Inspection (requires PIE)
   │ ue_editor_debug(get_watched_values) → BP watched pins
   │ ue_editor_debug(inspect_variable)   → live actor property
   ▼
Step 4: C++ Cross-Reference (if needed)
   │ ue_analyze_symbols → find underlying C++ implementation
   ▼
[Blueprint Debug Report]
```

### Step 1: Static Flow Analysis

```python
ToolSearch("select:mcp__narshamcp__ue_manage_blueprint")

# Get full BP structure to understand graph layout
structure = ue_manage_blueprint(operation="get_structure", params={
    "blueprint_name": "<BP_Name>"
})
# Returns: graphs, nodes, pins, connections, variables

# Search for specific variables/functions
vars = ue_manage_blueprint(operation="search_variables", params={
    "blueprint_name": "<BP_Name>",
    "pattern": "<variable pattern>"
})
```

### Step 2: Execution Tracing

```python
ToolSearch("select:mcp__narshamcp__ue_trace_execution")

# Trace full execution flow
trace = ue_trace_execution(operation="trace_execution_flow", params={
    "blueprint_name": "<BP_Name>",
    "output_format": "html"  # Interactive visualization
})

# Trace from a specific event
trace = ue_trace_execution(operation="trace_execution_flow", params={
    "blueprint_name": "<BP_Name>",
    "start_node": "EventBeginPlay",
    "output_format": "mermaid"  # Diagram format
})
```

### Step 3: Runtime State Inspection

**Prerequisite**: Editor must be running with PIE (Play In Editor) active.

```python
ToolSearch("select:mcp__narshamcp__ue_editor_debug")

# A) BP watched pins (set watches in Editor UI first) — top-level args, not params={}
pin_values = ue_editor_debug(
    operation="get_watched_values",
    blueprint_path="/Game/Blueprints/<BP_Name>.<BP_Name>",
    max_depth=5,
)

# B) One-shot property read on a live actor (NOT watch_variable — that op does not exist)
runtime_state = ue_editor_debug(
    operation="inspect_variable",
    actor_path="/Game/Maps/Level.Level:PersistentLevel.<ActorName>",
    property_name="<PropertyName>",
)

# C) Continuous watch (optional) — requires actor_path + properties array
watch = ue_editor_debug(
    operation="start_watch",
    actor_path="/Game/Maps/Level.Level:PersistentLevel.<ActorName>",
    properties=["Health", "IsLocalPlayer"],
)
# Later: get_watch_results / stop_watch with the returned watch_id
```

### Step 4: C++ Cross-Reference

When a BP node wraps a C++ function, trace into the native implementation:

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_symbols")

# Find the C++ implementation behind a BP node
symbol = ue_analyze_symbols(operation="search_symbols", params={
    "query": "<function node name>"
})

# Check callers of the C++ function
callers = ue_analyze_symbols(operation="find_callers", params={
    "function_name": "<native function name>"
})
```

### Common BP Debug Scenarios

#### Scenario 1: "Why does this branch execute?"
1. `ue_trace_execution(trace_execution_flow)` — see which path executes
2. Identify the Branch node's condition pin
3. `ue_editor_debug(get_watched_values)` if pin is watched in Editor, else `inspect_variable` on the actor property feeding the branch

#### Scenario 2: "Variable has unexpected value"
1. `ue_manage_blueprint(search_variables)` — find all Set nodes for the variable
2. `ue_trace_execution` — trace which Set node executes last
3. `ue_editor_debug(inspect_variable)` (or `get_watched_values` for pin watches) — confirm runtime value

#### Scenario 3: "Event not firing"
1. `ue_manage_blueprint(get_structure)` — verify event node exists
2. Check if event is bound (Custom Event vs. Engine Event)
3. For delegates: `ue_analyze_symbols(find_delegate_refs)` — verify binding

#### Scenario 4: "Cast failing at runtime"
1. Identify the Cast node in the BP flow
2. `ue_editor_debug(inspect_variable)` — check the input object's actual class / related property
3. `ue_analyze_symbols(trace_hierarchy)` — verify class compatibility

### Blueprint Debug Output Format

```text
=== Blueprint Debug Report: [BP_Name] ===

--- Static Analysis ---
Graph: EventGraph
Nodes: 45 | Variables: 12 | Functions: 3
Execution path: BeginPlay → SetupCharacter → [Branch: IsLocalPlayer]
                                                ├── True → EnableInput
                                                └── False → DisableInput

--- Runtime State (PIE) ---
Actor: BP_PlayerCharacter_C_0
  IsLocalPlayer = false    ← This is why False branch executes
  Health = 100.0
  CurrentState = "Idle"

--- Diagnosis ---
The Branch node at EventGraph:Node_45 evaluates `IsLocalPlayer` which is `false`
because this actor instance is a server-spawned replica, not the local player.

--- Fix Suggestion ---
1. Check if IsLocallyControlled() is the intended check instead of IsLocalPlayer
2. Or move the setup logic to OnPossess event which fires only on the controlling client
```

---

## Crash Doctor Reference

> **Source**: Was a separate `CRASH_DOCTOR_REFERENCE.md`. Folded here per Epic #10451 Tier 1-A (Issue #10452).
> **Issue**: #5290 — Parent Skill: `/ue-debug` v1.1.0 (`crash_deep` classifier)

This reference extends `/ue-debug`'s `runtime_crash` path with deep crash analysis.
Activates when crash keywords include: "callstack", "minidump", "콜스택", "덤프 분석", "crash log".

### Deep Crash Analysis Workflow

```text
Step 1: Locate Crash Data
   │ Saved/Crashes/, Saved/Logs/, .dmp files
   ▼
Step 2: Parse Crash Report
   │ FGenericCrashContext fields extraction
   ▼
Step 3: PDB Symbol Resolution
   │ Raw addresses → function names via ue_analyze_symbols
   ▼
Step 4: Call Graph Reconstruction
   │ Crash function → callers → root cause chain
   ▼
Step 5: Pattern Matching
   │ Match against known UE crash patterns
   ▼
[Deep Crash Report with Root Cause]
```

#### Step 1: Locate Crash Data

```python
# Auto-scan crash directories
result = ue_fix_errors(mode="crash_analysis")
# Scans: Saved/Crashes/*, Saved/Logs/*.log

# Manual path for specific crash
result = ue_fix_errors(mode="crash_analysis", crash_path="<path>/Saved/Crashes/<crash_dir>")
```

#### Step 2: Key FGenericCrashContext Fields

| Field | Description | Example |
|-------|-------------|---------|
| `ErrorMessage` | Top-level crash message | `"Access violation - code c0000005"` |
| `CrashType` | Crash category | `"Ensure"`, `"Assert"`, `"GPF"`, `"Fatal"` |
| `CallStack` | Raw call stack frames | `Module!Function+0xOffset` |
| `SourceContext` | Source file + line (if available) | `"MyActor.cpp:145"` |
| `GameName` | Project name | `"LyraStarterGame"` |
| `EngineVersion` | UE version | `"5.4.3-33956291+++UE5+Release-5.4"` |

#### Step 3: PDB Symbol Resolution

```python
# For each unresolved frame in callstack:
symbol = ue_analyze_symbols(operation="search_symbols", params={
    "query": "<function name from crash frame>"
})
# Returns: full qualified name, file, line, module

# For the crash function specifically:
methods = ue_analyze_symbols(operation="get_methods", params={
    "class_name": "<class owning crash function>"
})
```

#### Step 4: Call Graph Reconstruction

```python
# Find who calls the crash function
callers = ue_analyze_symbols(operation="find_callers", params={
    "function_name": "<crash function>",
    "limit": 15
})

# Full impact analysis around crash site
impact = ue_analyze_symbols(operation="impact_analysis", params={
    "target": "<crash function or class>",
    "depth": 2
})
```

### Known UE Crash Patterns

#### Pattern 1: Nullptr Dereference (GPF)
- **Signature**:
  ```text
  Access violation - code c0000005 (first/second chance not available)
  ```
- **Common causes**: Uninitialized pointer, GC'd UObject, invalid weak ptr
- **Diagnosis**: Check if object is `IsValid()`, `IsValidLowLevel()`, or has been GC'd
- **Fix**: Add null check, use `TWeakObjectPtr`, or `UPROPERTY()` to prevent GC

#### Pattern 2: Assert/Ensure Failure
- **Signature**:
  ```text
  Assertion failed: <condition> [File:<path>] [Line:<num>]
  ```
- **Common causes**: Contract violation, invalid state, threading issue
- **Diagnosis**: Read the assertion condition and trace how the violated state was reached
- **Fix**: Fix the state that violated the assertion, or update the assertion if outdated

#### Pattern 3: Stack Overflow
- **Signature**:
  ```text
  Stack overflow
  ```
  or very deep recursive callstack
- **Common causes**: Infinite recursion, deep BeginPlay chains, circular references
- **Diagnosis**: Look for recurring function names in the callstack
- **Fix**: Break the recursion, add depth limit, or restructure call chain

#### Pattern 4: Pure Virtual Call
- **Signature**:
  ```text
  Pure virtual function being called while application was running
  ```
- **Common causes**: Calling virtual function in constructor/destructor
- **Diagnosis**: Check callstack for ctor/dtor frames
- **Fix**: Move virtual call to PostInitializeComponents or BeginPlay

#### Pattern 5: GC-Related Crash
- **Signature**: Crash in `UObject::ProcessEvent`, `FGCObject`, or during garbage collection
- **Common causes**: Accessing UObject after GC, missing UPROPERTY(), raw pointer to UObject
- **Diagnosis**: Check if crash object had `UPROPERTY()` annotation
- **Fix**: Add `UPROPERTY()` or use `TWeakObjectPtr`/`TStrongObjectPtr`

### Deep Crash Output Format

```text
=== Deep Crash Report ===

--- Crash Summary ---
Type: [GPF / Assert / Stack Overflow / Pure Virtual / GC-Related]
Message: [crash error message]
Engine: [UE version]

--- Resolved Callstack ---
  #0  AMyCharacter::TakeDamage (MyCharacter.cpp:145)     ← CRASH SITE
  #1  UDamageType::ProcessDamage (DamageType.cpp:89)
  #2  AActor::TakeDamage (Actor.cpp:2341)
  #3  UGameplayAbility::ApplyDamage (GameplayAbility.cpp:567)

--- Pattern Match ---
Pattern: Nullptr Dereference (GPF)
Confidence: HIGH (matches "Access violation" + nullptr in frame #0)

--- Root Cause ---
AMyCharacter::TakeDamage accesses `HealthComponent` which is nullptr.
The component was not created in the constructor.

--- Fix ---
1. Add null check before accessing HealthComponent
2. Ensure HealthComponent is created in constructor via CreateDefaultSubobject
```

---

## Answer Quality Rules

> **Merged from two SKILL.md sections** (lines 1001 / 1073, latter added by Issue #6681). Duplicates deduplicated.

Before writing your final answer, apply these rules:

1. **CONFIDENCE CHECK** — Did you Read() a source file containing the SPECIFIC function/mechanism causing the issue? If you only found a high-level caller or wrapper, the real root cause is likely deeper. Use remaining tool calls to Grep for the function IT calls.

2. **No fabrication** — Only cite class names, function names, file paths, and line numbers from:
   - (a) MCP tool results from this session
   - (b) Source files you Read() in this session
   - (c) The question text itself
   Never fabricate or guess names. If Grep found no matches, report that — don't invent.

3. **Absence is an answer** — If your search found a RELATED but NOT EXACT match for what the question asks, consider that the feature may not exist yet. State "not currently supported" with evidence (no Grep matches for the exact API) rather than presenting a different mechanism as the answer.

4. **Synthesized output** — Your answer must be a coherent synthesis of findings, not a narration of search steps ("Now let me search...", "Let me check..."). If you ran out of tool calls, synthesize what you found so far.

5. **Complete answer** — Always write a detailed text answer with specific class names, function names, CVar names, and file paths found. Include root cause, code path, and fix/workaround. Never end with just tool calls or an empty response.

6. **Version/CL context** — For version-specific questions, users can invoke `/ue-cl-tracker` directly. The optional `[CL_SEARCH.md](CL_SEARCH.md)` flow covers Epic GitHub commit-search inside `/ue-debug` itself.

7. **Name existence verification** — Every UE class/function name in your answer MUST have a verifiable source: (a) MCP tool result from this session, (b) Read()/Grep() output from this session, or (c) the question text itself. If your answer cites more than 5 specific UE names, re-verify each one actually appeared in tool results. Say "a class in [module]" rather than inventing a plausible-sounding name.

8. **engine_docs cross-check** — After diagnosing via MCP/grep, cross-check with `ue_engine_docs(operation="search", query="<topic>")` for the relevant module documentation. If engine_docs describes a different root cause or recommended fix, report BOTH and prefer: documented solution > workaround > training knowledge guess.

9. **Exhaustive absence verification** — When all searches return 0 results after 3+ keyword variations (exact, partial, synonym), conclude "Not found in engine source" with evidence. Do NOT fabricate workarounds for features that don't exist.

10. **Symptom-to-API search (Issue #6681)** — When the question describes a symptom (not an explicit error code), extract keywords and search engine source BEFORE proposing a diagnosis:
    - a. Extract SPECIFIC keywords from the question (function names, class names, property names)
    - b. Search for those EXACT names first with `ue_analyze_symbols` or `ue_grep`
    - c. UI behavior symptoms → search for Viewport/Controller/Input/Focus classes (e.g., `LostFocus`, `FlushPressedKeys`)
    - d. Callback not firing → search for the delegate/event name in source
    - e. Property not propagating → search for the property name in source
    - f. Memory leak or ownership issue → search for smart pointer types (`TStrongObjectPtr`, `TSharedPtr`, `TWeakObjectPtr`)
    - g. Loading/packaging pattern → search for `LoadPackage`, `LoadObject`, `StreamableManager`
    - h. Specific UI panel or editor feature name (e.g., "Property Matrix") → search for that exact term in engine source

11. **Source-verified approaches (Issue #6681)** — When multiple solutions exist:
    - a. Search engine source for the EXACT mechanism described in the symptom before proposing alternatives
    - b. If the question hints at a specific API pattern, verify it exists via PDB/symbol search before suggesting workarounds
    - c. Do NOT default to a different methodology when the question implies a specific approach (e.g., direct property setting vs. indirect workaround)

> **Integration with Step 0-2**: Rules 10-11 apply DURING Step 0 (Discovery) and Step 2 (Diagnostic).
> Use them to guide keyword extraction and search strategy before tool calls.

---

## Output Format (Medical Model)

```text
=== Debug Report: [Symptom Summary] ===

--- Step 1: Symptoms ---
Classification: [Compile Error / Runtime Crash / Unexpected Behavior / Performance / GAS Issue / Build System / Blueprint Debug]
Reported symptom: [User's description]
Error details: [Parsed error message if applicable]

--- Step 2: Diagnosis ---
Tool used: [ue_fix_errors / ue_analyze_insights / etc.]
Findings:
  [Detailed tool results — what was found]

--- Step 3: Root Cause ---
[Clear explanation of WHY this is happening]
Example: "The GENERATED_BODY() macro is missing from line 15 of MyActor.h.
         UE requires this macro in every UCLASS to generate reflection data."

--- Step 4: Fix ---
[Step-by-step fix instructions]

1. Open [file path]
2. Add/modify [specific code change]:
   ```cpp
   // Before:
   class AMyActor : public AActor
   {
       UCLASS()

   // After:
   UCLASS()
   class AMyActor : public AActor
   {
       GENERATED_BODY()
   ```
3. Rebuild project

--- Prevention ---
[How to avoid this in the future]
- [Specific practice or check to add]
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| ue_fix_errors smart fails | No error_message provided | Ask user for full error log text |
| ue_fix_errors preflight fails | Requires build_log_path | Use `dependency_check` or `hotreload_check` as fallback |
| crash_analysis empty | No crash reports in Saved/Crashes/ | Use `runtime_analysis` to scan Saved/Logs/ instead |
| Performance trace unavailable | No .utrace file | Use `ue_analyze_insights(get_stats)` for general overview |
| GAS trace empty | Ability not in project | `ue_search_assets` to find similar abilities |
| Symptom classification ambiguous | Multiple keyword matches | Classify as highest priority match (compile > crash > behavior) |
| get_compiler_results RC connection failed | Editor not running | Fall back to `scan_build_log` automatically (smart mode handles this) |
| scan_build_log no log found | No Saved/Logs/ or empty dir | Suggest `build_and_fix` to spawn a fresh build and capture output |
| scan_build_log staleness_warning | Log >1 hour old | Warn user; suggest re-running build or using `build_and_fix` |
| build_system query has specific error code | "C2065", "LNK2019" in query | Re-classify as compile_error → Path A |
| get_watched_values RC failed | C++ plugin unavailable / Editor off | Use `ue_manage_blueprint(get_structure)` for static analysis |
| get_watched_values / set_breakpoint "requires blueprint_path" | Args nested under `params={}` | Pass `blueprint_path` (and `node_guid`) as **top-level** tool arguments |
| Operation 'watch_variable' is not available | Invented op name in old skill prose | Use `get_watched_values` (pins) or `inspect_variable` / `start_watch` (actor props) |
| Operation 'start_pie' is not available | Invented automation name | Use `ue_editor_automation(operation="play_in_editor")` |
| get_execution_trace empty | PIE not running | Suggest `play_in_editor` first, then re-run |
| step_execution RC failed | C++ plugin unavailable | pause/resume fall back to Python; step_into/over/out require plugin |

---

## Evaluation Criteria

### Activation Test Cases

**Positive (8)** - Should activate:
1. "error C2065: 'MyVar': undeclared identifier 해결해줘" -> Activate (compile_error)
2. "게임 실행하면 BeginPlay에서 크래시 발생" -> Activate (runtime_crash)
3. "GA_Attack 어빌리티가 안 발동돼" -> Activate (gas_issue)
4. "프레임이 30FPS 아래로 떨어져" -> Activate (performance)
5. "Why is my character not moving?" -> Activate (unexpected_behavior)
6. "빌드 에러 분석해줘" -> Activate (build_system, NOT compile_error — no specific error text)
7. "컴파일 결과 보여줘" -> Activate (build_system)
8. "Why did the build fail?" -> Activate (build_system)
9. "BP 디버깅 — 변수 값 확인하고 싶어" -> Activate (blueprint_debug)
10. "Step through this Blueprint" -> Activate (blueprint_debug)

**Classification boundary (compile_error vs build_system)**:
- "error C2065 고쳐줘" → compile_error (specific error code present)
- "빌드 에러 분석" → build_system (diagnostic intent, no specific error code)

**Negative (3)** - Should NOT activate:
1. "ACharacter 어떻게 동작해?" -> Use ue-explain skill
2. "APawn 변경 영향 분석" -> Use ue-impact skill
3. "캐릭터 클래스 만들어줘" -> Use ue-scaffold skill

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Symptom classification accuracy | >90% |
| Root cause identification | >80% |
| Fix suggestion accuracy | >75% |
| Compile error auto-fix | >95% (via ue_fix_errors smart mode) |

---

**Status**: Phase 1 MVP + Phase 3 (build_system) + Phase 4 (blueprint_debug) + Tier 1-A modularization (Issue #10452)
**Related**: Issue #4800 (MCP-First Tool Selection), Issue #5290 (Crash Doctor), Issue #5291 (FCompilerResultsLog), Issue #5293 (Remote BP Debugger), Issue #10451 (Epic), Issue #10452 (this refactor)
