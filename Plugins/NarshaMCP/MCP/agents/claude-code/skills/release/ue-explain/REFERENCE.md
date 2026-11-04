# UE Explain — Reference

**Version**: 2.2.0
**Parent**: [SKILL.md](SKILL.md)
**Walkthroughs**: [EXAMPLES.md](EXAMPLES.md)

This file holds the lookup-on-demand detail for `/ue-explain`. SKILL.md keeps
only the hot-path one-liners and links to the section anchors below for the
full text. Issue #10510 (Epic #10451 Tier 3-B) — modularization moved
~22 KB of verbose discovery / verification / quality-rule content here.

---

## Table of Contents

1. [Verification Gates Detail](#verification-gates-detail)
2. [Common Failure Patterns](#common-failure-patterns)
3. [Step 0-PRE TOPIC_INDEX Navigation](#step-0-pre-topic_index-navigation)
4. [Step 0 Discovery Detail](#step-0-discovery-detail)
5. [Step 2 Routing Detail](#step-2-routing-detail)
6. [Multi-Query Search Planning](#multi-query-search-planning)
7. [Engine Source Verification](#engine-source-verification)
8. [Answer Quality Rules Full](#answer-quality-rules-full)
9. [Output Format Templates](#output-format-templates)
10. [Domain Routing Matrix](#domain-routing-matrix)
11. [Domain-Specific Secondary Discovery](#domain-specific-secondary-discovery)
12. [Source Verification Checklist](#source-verification-checklist)
13. [Error Recovery](#error-recovery)
14. [Evaluation Criteria](#evaluation-criteria)
15. [Self-Test Mode](#self-test-mode)
16. [Auto-Trigger Phrases](#auto-trigger-phrases)
17. [Step 1 Routing Dict Full](#step-1-routing-dict-full)

---

## Verification Gates Detail

> Hot-path one-liners live in SKILL.md § MANDATORY VERIFICATION GATES.
> Full rationale, evidence requirements, and failure-mode taxonomy here.

**Gate 1: NAME AUDIT** — List every UE class/function name in your answer.
Each name MUST come from one of:
- (a) MCP tool result from this session
- (b) `Read()` / `Grep()` output from this session
- (c) the question text itself

If a name has no tool-verified source → replace with `"a class in [Module]"`.
If 3+ names lack verification → your answer is likely hallucinated; re-run targeted searches before submitting.

**Gate 2: ABSENCE CHECK** — If you assert "you can do X", do you have source evidence?
If `engine_docs` + `grep` both returned 0 results → "Not confirmed in engine source" IS a valid answer.
Do NOT fabricate workarounds for features that may not exist. Document what you searched for.

**Gate 3: ALTERNATIVE CHECK** — Did you only find one approach? Search for competing alternatives.
- Grep for `"deprecated"`, `"legacy"`, `"prefer X over Y"` near your finding
- Report the current-generation approach, not the first-found one
- If two implementations coexist (legacy + modern), report BOTH and recommend the modern one

**Gate 4: engine_docs CROSS-CHECK** — Did you call `ue_engine_docs(search, "<topic>")`?
This is MANDATORY. If `engine_docs` describes a different approach than `grep` findings, report BOTH.
Prefer: designed solution > workaround > training knowledge.

**Tool Call Budget** — Reserve your LAST 2 tool calls for verification:
- V1: `ue_engine_docs(operation="search", query="<main topic>")` — official docs cross-check
- V2: `ue_grep` for the EXACT primary class/function name you cite as the answer — confirm it exists

---

## Common Failure Patterns

| Pattern | What Goes Wrong | Correct Approach |
|---------|-----------------|------------------|
| Overconfident assertion | Assert "X is possible" without source evidence | Verify with `engine_docs` + `grep` before asserting |
| First-hit bias | Query 1 returns results, skip remaining queries | Execute ALL 3 planned queries (Multi-Query Planning, below) |
| Training knowledge as fact | Present training knowledge as verified fact | Always verify with tools before citing |
| Wrong file type | Search only `.cpp` / `.h` | Also search `.usf`, `.ush`, `.cs`, `.ini`, `.xml` |

---

## Step 0-PRE TOPIC_INDEX Navigation

> **ALWAYS execute this BEFORE Step 0.** This identifies the correct `engine_docs`
> module for the question's domain. Without this, you may search the wrong module
> and miss key information. This step is the #1 differentiator between 10.0 and 5.0 scores.

```python
# 0-PRE-a. Read the topic index to identify candidate modules
ue_engine_docs(operation="read", path="TOPIC_INDEX.md", level=2)

# 0-PRE-b. From the result, identify the PRIMARY module for this question:
#   - Match the question topic to "Topic → Module Mapping" sections
#   - If a class name is known, use "Class Prefix → Module" table
#   - If a CVar is mentioned, use "CVar Prefix → Subsystem" table
#   Example: "Zen cooked data" → TOPIC_INDEX says "ZenDashboard"
#   Example: "UMutable*" class → TOPIC_INDEX says "Mutable, HairStrandsMutable"

# 0-PRE-c. Read the PRIMARY module's TROUBLESHOOT.md (MUST DO — do NOT skip)
ue_engine_docs(operation="read", module="<identified_module>", doc_type="TROUBLESHOOT")

# 0-PRE-d. Check "Related Modules" section at bottom → note cross-references for Step 2
```

> **If `TOPIC_INDEX.md` returns "not found"**: skip this step entirely and proceed to Step 0.
> **If module is unclear**: pick the 2 most likely modules and read both TROUBLESHOOTs.

---

## Step 0 Discovery Detail

> **Purpose**: 질문 유형에 따라 최적의 첫 번째 도구가 다릅니다.
> 아래 기준으로 판단하여 가장 적합한 도구를 먼저 호출하세요.
> **강제 순서가 아닌 판단 기준** — 질문에서 유형 신호를 읽고, 가장 적합한 첫 도구를 고르세요.
> Step 0-PRE에서 모듈이 식별됐으면 그 모듈을 primary search target으로 사용하세요.

### 도구 선택 기준 (8가지 유형)

| # | 질문 유형 | 신호 키워드 | 첫 번째 도구 | 보충 도구 | 이유 |
|---|-----------|-----------|-------------|----------|------|
| 1 | **개념/API/방법** | "뭐야", "어떻게", "사용법", "What is", "How to" | `ue_engine_docs(search)` | `ue_grep` | 공식 문서에 API/솔루션 정리 |
| 2 | **에러/버그 조사** | "크래시", "에러", "안 돼", "breaks", "ensure", "assert" | `ue_grep(증상 키워드)` | `ue_analyze_symbols` | 증상→소스 역추적 필요 |
| 3 | **소스 구현 탐색** | "구현 어디", "소스 보여줘", "implementation" | `ue_grep(클래스/함수명)` | `ue_read` | 직접 코드 위치 |
| 4 | **설정/CVar** | "r.", "sg.", "au.", "CVar", "설정값" | `ue_analyze_config` | `ue_grep(TAutoConsoleVariable)` | config 전용 |
| 5 | **워크플로우/설정 방법** | "set up", "configure", "workflow", "설정 방법" | `ue_engine_docs(search)` + `ue_analyze_config` | `ue_grep` | 문서+설정 동시 필요 |
| 6 | **엔진 서브시스템 내부** | 특정 플러그인명 (Mutable, nDisplay, Zen, Horde) | `ue_grep(scope=engine, path_pattern=plugin경로)` | `ue_engine_docs` | 플러그인 소스 직접 탐색 |
| 7 | **빌드/자동화** | "BuildGraph", "cook", "commandlet", "UAT", ".xml" | `ue_grep(scope=engine, path_pattern="*.cs,*.xml")` | `ue_engine_docs` | non-C++ 파일 검색 필수 |
| 8 | **성능/최적화** | "느려", "성능", "bottleneck", "delayed", "latency" | `ue_grep(증상 키워드)` | `ue_analyze_config(관련 CVar)` | 원인 코드 + 튜닝 CVar |

### 유형 판단 우선순위

```text
질문 분석 순서:
1. CVar/설정 이름 포함? (r., sg., au.) → 유형 4
2. 에러/버그 증상 언급? (크래시, assert, ensure, "doesn't work") → 유형 2
3. 빌드/자동화 키워드? (BuildGraph, cook, UAT, commandlet) → 유형 7
4. 특정 플러그인 이름? (Mutable, nDisplay, RiverMax, Zen) → 유형 6
5. 성능 문제 언급? (느려, delayed, bottleneck, packet loss) → 유형 8
6. 워크플로우/설정? (how to set up, configure, workflow) → 유형 5
7. 소스 위치 질문? (구현 어디, source, implementation) → 유형 3
8. 나머지 (개념, API, 설명) → 유형 1
```

### 도메인별 엔진 경로 힌트

> 유형 6, 7에서 `ue_grep`에 `scope=engine` + 경로 힌트를 제공하면 정확도가 높아집니다.

| 도메인 키워드 | 엔진 경로 | 파일 유형 |
|-------------|----------|----------|
| nDisplay, ICVFX, VP | `Engine/Plugins/Runtime/nDisplay/` | `.cpp`, `.h` |
| Mutable, CustomizableObject | `Engine/Plugins/Mutable/` | `.cpp`, `.h` |
| Zen, IoStore, Cooked Data | `Engine/Source/Programs/`, `Engine/Source/Developer/` | `.cpp`, `.cs` |
| BuildGraph, Horde, UAT | `Engine/Source/Programs/AutomationTool/` | `.cs`, `.xml` |
| MetaSound, Audio | `Engine/Plugins/Runtime/Metasound/`, `Engine/Source/Runtime/AudioMixer/` | `.cpp`, `.h` |
| WorldPartition, HLOD | `Engine/Source/Runtime/Engine/Private/WorldPartition/` | `.cpp`, `.h` |
| Media, Electra | `Engine/Plugins/Media/`, `Engine/Source/Runtime/Media/` | `.cpp`, `.h` |
| Landscape, Foliage | `Engine/Source/Runtime/Landscape/` | `.cpp`, `.h` |

### Step 0 Workflow

```python
# 0a. 질문에서 유형 신호 탐지 → 위 우선순위 순서로 판단
# 0b. 판단된 유형의 '첫 번째 도구' 호출
#   - 유형 1: ue_engine_docs(operation="search", query="<topic>")
#   - 유형 2: ue_grep(query="<error keyword or class>", limit=10)
#   - 유형 3: ue_grep(query="<TargetName>", limit=10)
#   - 유형 4: ue_analyze_config(operation="search_config", key="<cvar>")
#   - 유형 5: ue_engine_docs(search) + ue_analyze_config(search) 동시
#   - 유형 6: ue_grep(query="<keyword>", scope="engine") — 경로 힌트 활용
#   - 유형 7: ue_grep(query="<keyword>", scope="engine", path_pattern="*.cs")
#   - 유형 8: ue_grep(query="<symptom>") + ue_analyze_config(관련 CVar)

# 0c. 결과가 부족하면 '보충 도구' 호출
#   - engine_docs 부족 → ue_grep 으로 소스 보충
#   - ue_grep 부족 → engine_docs 로 문서 보충

# 0d. Step 0 결과로 domain 확정 → Step 1 (Domain Auto-Routing)으로 진행
```

### Step 0 Decision Logic

```text
Step 0 result → Decision:
  TOPIC_INDEX module identified → Use that module as primary search target (Step 0-PRE)
  engine_docs hit    → Use doc content as primary answer base, grep for code details
  grep hit (symbols) → Proceed to cpp_class workflow (confirmed)
  grep hit (assets)  → Proceed to blueprint/material/niagara workflow
  grep hit (source)  → Use source path for targeted Read
  grep hit (plugin)  → Read plugin source directly (유형 6 fast path)
  config hit         → Use config detail directly (full bypass — see Step 2)
  Not found anywhere → Report "not found" early, suggest alternatives
```

---

## Step 2 Routing Detail

> SKILL.md § Step 2 has the compact one-line-per-domain table. The full version
> below includes Step 0 bypass conditions and notes on when downstream calls can
> be eliminated to preserve the tool-call budget.
>
> **Step 0 Integration**: Use Step 0 results to **skip** unnecessary operations.
> The "Step 0 Bypass" column shows when downstream calls can be eliminated.

| Domain | Primary Tool | Operations | Step 0 Bypass |
|--------|-------------|------------|---------------|
| `cpp_class` | `ue_analyze_symbols` | `search_symbols` + `get_methods` + `trace_hierarchy` + `find_virtual_overrides` | `ue_read`가 parent_class+child_count 반환 → `trace_hierarchy` 스킵. grep이 method 매치 반환 → `get_methods` 스킵. `find_virtual_overrides`는 사용자가 "override"/"재정의" 명시 시에만 |
| `blueprint` | `ue_manage_blueprint` | `get_structure` + `get_raw_nodes` + `search_variables` | `ue_read`가 variables_preview 포함 → `search_variables` 스킵. `get_raw_nodes`는 구조 질문 시에만 |
| `material` | `ue_manage_material` | `get_nodes` + `get_hierarchy` + `analyze_performance` | `ue_read`가 parameter_count 반환 → `get_hierarchy` 조건부 스킵 |
| `niagara` | `ue_manage_niagara` | `get_structure` + `get_parameters` | `ue_read`가 emitter/parameter count 반환 → 개요만 필요 시 스킵 |
| `ability` | `ue_manage_gameplay` | `trace_abilities` | 스킵 불가 — ability flow는 Step 0에서 확인 불가 |
| `config` | `ue_analyze_config` | `search_config` + `explain_config` | **전체 바이패스**: `ue_read`가 cvar_type, default_value, help_text, flags 반환 → 설명에 충분. override layer 질문 시에만 `ue_analyze_config` 호출 |
| `ai` | `ue_manage_ai` | `get_structure` + `find_transitions` | `ue_read`가 state/node count 반환 → 개요만 필요 시 detail 스킵. **Authoring (#11397)**: `add_state` / `add_transition` / `set_task` / `modify_state` / `modify_transition` / `remove_state` / `create` 가 RC HTTP로 5.7+5.8 동작. 모두 `dry_run:true` 미리보기 지원, 에디터 undo 가능. `set_task`는 5.8+ + `binding.completion_condition` 시 task-completion binding 자동 연쇄 (#9590 read-side와 round-trip). |
| `pcg` | `ue_manage_pcg` | `get_structure` + `trace_flow` | 스킵 불가 — PCG 그래프는 binary |
| `sequencer` | `ue_sequencer_structure` | `get_structure` + `list_tracks` | `ue_read`가 track/binding count 반환 → 개요만 필요 시 스킵 |
| `audio` | `ue_analyze_symbols` | `search_symbols` (FMetaSoundSource, FAudioDevice, FDelay) | `ue_engine_docs(search)` + `ue_analyze_config(search_config, key="au.<keyword>")` 보충. 스킵 불가 — audio 심볼은 PDB 필수 |
| `control_rig` | `ue_manage_rigging` | `get_structure` + `get_nodes` + `analyze_rig` (rig_type="control_rig") | 물리/충돌 질문 → `ue_analyze_symbols(search_symbols, "FAnimNode_RigidBody")` 보충. Engine Plugins/Animation/ 경로 |
| `media` | `ue_analyze_symbols` | `search_symbols` (IVideoRecordingSystem, IMediaPlayer, FElectraPlayer) | 코덱/포맷 질문 → `ue_grep(query="<codec>", scope="engine", path_pattern="*.cpp")` Engine/Plugins/Media/ 경로 |
| `datatable` | `ue_search_assets` | search by name | `ue_read`가 에셋 정보 반환 → `ue_search_assets` 스킵 가능 |
| `fallback` | `ue_search_assets` | broad search | Step 0 `ue_grep`이 이미 cross-domain 검색 수행 → `ue_search_assets` 불필요 |

For `ue_analyze_symbols(operation="trace_hierarchy")`, retain known C++/Blueprint
relations on a partial response, but do not claim there are no descendants when
`bp_descendants_capped`, `bp_descendants_depth_clamped`, or
`bp_descendants_source:"unavailable"` is reported.

### Step 2b: Non-C++ Source Search (Issue #6680)

> When explaining rendering, build systems, or infrastructure topics, also
> search non-C++ engine files for authoritative implementation details.

- Rendering / Shader questions: also search `*.usf`, `*.ush` in `Engine/Shaders/`
- Build system questions: also search `*.xml`, `*.cs` in `Engine/Build/`, `Engine/Source/Programs/`
- Config questions: also search `*.ini` in `Engine/Config/`

```python
# Example: Explaining ComposeSeparateTranslucency
# C++ grep alone misses the actual shader implementation
ue_grep(query="ComposeSeparateTranslucency", scope="engine", path_pattern="*.usf")
# → Finds Engine/Shaders/Private/ComposeSeparateTranslucency.usf

# Example: Explaining BuildGraph automation XML
ue_grep(query="BuildGraph", scope="engine", path_pattern="*.xml")
# → Finds Engine/Plugins/.../AutoPerfTests.xml, Engine/Build/Turnkey/*.xml
```

### CVar Exact Name Search (Issue #6681)

Always search with the EXACT CVar name from the question first. If not found,
try partial name variations by dropping the prefix hierarchy
(e.g., `"AllowCardSharing"` instead of `"r.LumenScene.SurfaceCache.AllowCardSharing"`).
Also try `ue_grep(query="<partial_name>")` to locate the CVar declaration in engine source.

```python
# Fallback search strategy for CVars:
# 1. Exact:   ue_analyze_config(search_config, key="r.LumenScene.SurfaceCache.AllowCardSharing")
# 2. Partial: ue_analyze_config(search_config, key="AllowCardSharing")
# 3. Source:  ue_grep(query="AllowCardSharing", limit=10)
```

---

## Multi-Query Search Planning

> Before executing any engine-source search in Step 4, plan **3 diverse queries** first.
> Do NOT rely on a single search — the first query choice is probabilistic and may miss the key class.

**Query Planning Template**:

| # | Type | Strategy | Example |
|---|------|----------|---------|
| 1 | **EXACT** | Specific class/function name from the question | `"UWorldPartitionNavigationDataBuilder"` |
| 2 | **RELATED** | Helper/Manager/Utility in the same module | `"FWorldPartitionHelpers"`, `"FNavigationManager"` |
| 3 | **SYMPTOM** | The symptom as engine-internal keywords | `"AllowCardSharing"`, `"PostRun serialize package"` |

Execute ALL 3 via `ue_analyze_symbols` or `ue_grep`. Merge results before forming the explanation.

**⚠️ MANDATORY: Execute ALL planned queries even if the first query returns results.**
First-hit bias causes 50 % of bimodal failures — the model stops searching after
finding "something plausible" instead of verifying with additional queries.

After ALL queries complete, COMPARE results:
- Multiple queries converge on same source file? → High confidence
- Queries point to different files? → Read BOTH, choose the more specific one
- Only 1 query returned results? → That result needs extra verification (Step 4)

If ALL 3 return empty → try 3 more:

| # | Type | Strategy |
|---|------|----------|
| 4 | **PREFIX** | Add/remove UE type prefix (F, U, A, E, I, T) |
| 5 | **BROADER** | Search parent directory instead of specific subsystem |
| 6 | **SYNONYM** | `"save"→"serialize"→"package"`, `"load"→"import"→"mount"` |

Max budget: **6 search calls** before proceeding to answer synthesis.

### engine_docs Cross-Check (Step 0에서 미호출 시 MANDATORY, Issue #6002)

> Step 0에서 `ue_engine_docs`를 이미 호출했으면 스킵 가능.
> Step 0에서 `ue_grep`만 사용했으면 여기서 반드시 `engine_docs` cross-check.

```python
# Step 0에서 engine_docs를 안 썼으면 MANDATORY
ue_engine_docs(operation="search", query="<primary topic from question>")
```

**Decision matrix**:
- `engine_docs` returns results → compare with `grep` findings. If different, report BOTH. Prefer `engine_docs`.
- `engine_docs` returns nothing → proceed with `grep` findings only.
- `engine_docs` contradicts `grep` → the designed solution (`engine_docs`) is likely correct over a workaround found via `grep`.

---

## Engine Source Verification

> **When**: After Step 3 MCP tool calls. Verify your explanation against actual
> engine source code using domain-specific paths.

After MCP tools return results, verify and supplement with direct engine source search:

1. **Read `.env`** to get `UECODEGEN_ENGINE_DIR` path
2. **Grep for exact names** found in MCP results or the question:
   - CVar mentioned? → Grep exact CVar name in source to find declaration + default value
   - Class mentioned? → Grep class name in engine source to find implementation details
   - Use SPECIFIC names from MCP results or the question, not broad domain keywords:
     - ✅ Specific class/function/CVar names that MCP returned or the question mentions
     - ❌ Broad domain keywords like module names or feature categories — too many irrelevant results
3. **Discover missing details** (Exact Name Discovery):
   - Read source file to find exact CVar string (e.g., `r.LumenScene.SurfaceCache.AllowCardSharing`)
   - Look for changelist references (CL#) in comments near the feature code
4. **Include in answer**: Exact file paths, CVar names, CL# references

⚠️ Only cite line numbers you actually `Read()`. Never fabricate source locations.

### Engine Source Paths by Domain

| Domain | Search Paths |
|--------|-------------|
| Audio | `Engine/Plugins/Runtime/Metasound/`, `Engine/Source/Runtime/SignalProcessing/`, `Engine/Plugins/Runtime/Metasound/Source/MetasoundFrontend/`, `Engine/Source/Runtime/AudioMixer/Private/`, `Engine/Source/Runtime/Engine/Private/SoundWave*.cpp` |
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

### Verification Workflow

1. Identify the domain from the target entity (use Step 1 routing)
2. Search engine source using ALL paths for that domain via `ue_grep`
3. Read the most relevant source file to confirm behavior
4. Include source file references in the "Key Files" output section

### Domain-specific secondary discovery (Issue #6002)

- **Animation retargeting** (body proportions, bone scale, translation mode):
  → TWO separate retargeting systems — check BOTH:
    (a) Legacy per-bone: `ue_grep("EBoneTranslationRetargetingMode")` in Skeleton.h — all mode enums
    (b) Modern IK Retargeter: `ue_manage_ik_rig` for chain retargeting settings
  → "Same skeleton, different proportions" → legacy per-bone system usually relevant
  → Root/pelvis often need different mode than spine/extremities in both systems
- **MetaSound sync/interface** (subtitle, random output, data access):
  → Call `ue_engine_docs("MetaSound")` for MetaSound Blueprint integration APIs
  → Search `MetaSoundSource` + the specific node type (Random, Output, WavePlayer) together
- **Editor Plugin / Customizable Object** (Mutable, ray tracing, mesh visibility):
  → After finding primary file, also search `*ConversionUtils*` and `*Private.h` in the same plugin module
  → Secondary implementation files often hold the actual propagation logic
- **Audio 3D spatialization** (ITD, delay, ping-pong, stereo positioning):
  → Search delay line implementation classes in `Engine/Source/Runtime/SignalProcessing/`
  → Search `au.*` CVars via `ue_analyze_config` for spatialization runtime settings

---

## Answer Quality Rules Full

> SKILL.md § Answer Quality Rules keeps the 5 hot-path checks. The remaining
> 5 rules below cover architecture validation, name verification, and exhaustive
> absence verification — applied when the question allows time for deeper analysis.

Before writing your final answer, apply these rules:

1. **CONFIDENCE CHECK** — Did you `Read()` a source file containing the SPECIFIC
   class/function/mechanism? If you only found a high-level wrapper, the real
   implementation is likely deeper. Use remaining tool calls to `Grep` for the
   function IT calls.

2. **No fabrication** — Only cite class names, function names, file paths, and
   line numbers from:
   - (a) MCP tool results from this session
   - (b) Source files you `Read()` in this session
   - (c) The question text itself
   Never fabricate or guess names. If `Grep` found no matches, report that —
   don't invent.

3. **Absence is an answer** — If your search found a RELATED but NOT EXACT match
   for what the question asks, consider that the feature may not exist yet. State
   "not currently supported" with evidence (no `Grep` matches for the exact API)
   rather than presenting a different mechanism as the answer.

4. **Synthesized output** — Your answer must be a coherent synthesis of findings,
   not a narration of search steps ("Now let me search...", "Let me check..."). If
   you ran out of tool calls, synthesize what you found so far.

5. **Complete answer** — Always write a detailed text answer with specific class
   names, function names, CVar names, and file paths found. Include What it is /
   How it works / When/Why to use it. Never end with just tool calls or an empty
   response.

6. **Version/CL context** — For version history, users can invoke
   `/ue-cl-tracker` directly.

7. **Architecture validation** — When multiple approaches exist for the same
   problem (e.g., legacy vs modern, plugin vs built-in), verify which is
   current-generation before recommending:
   - Grep for competing alternatives in the same domain
   - Check for "deprecated", "legacy", "prefer X over Y" in source comments near your finding
   - Report the current approach, not the first-found approach

8. **Name existence verification** — Every UE class/function name in your answer
   MUST have a verifiable source: (a) MCP tool result from this session,
   (b) `Read`/`Grep` output from this session, or (c) the question text itself.
   If your answer cites more than 5 specific UE names, re-verify each one actually
   appeared in tool results. Say "a class in [module]" rather than inventing a
   plausible-sounding name.

9. **engine_docs cross-check** — After MCP/grep search, cross-check with
   `ue_engine_docs(operation="search", query="<topic>")` for the relevant module
   documentation. If `engine_docs` describes a different recommended approach than
   what you found via grep, report BOTH and prefer:
   designed solution > workaround > training knowledge guess.

10. **Exhaustive absence verification** — When all searches return 0 results
    after 3+ keyword variations (exact, partial, synonym), conclude "Not found in
    engine source" with evidence of what you searched. Do NOT fabricate workarounds
    for features that don't exist. State the queries you tried and the domains you
    checked.

---

## Output Format Templates

> SKILL.md § Output Format keeps the section skeleton. The per-domain tree
> templates below are domain-specific examples of what should fill the
> "Structure" section.

For C++ class:

```text
ACharacter
+-- Parent: APawn -> AActor -> UObject
+-- Components: UCharacterMovementComponent, UCapsuleComponent
+-- Key Methods: Jump(), Crouch(), OnLanded()
```

For Blueprint:

```text
BP_PlayerController
+-- Parent Class: APlayerController
+-- Variables: Health (float), MaxHealth (float)
+-- Functions: InitializePlayer, HandleDamage
+-- Event Graph: 12 nodes
```

For Material:

```text
M_Rock
+-- Parent: M_BaseMaterial
+-- Parameters: BaseColor, Roughness, Normal
+-- Nodes: 8 expression nodes
```

---

## Domain Routing Matrix

> Condensed canonical reference. SKILL.md § Step 1 has the prefix-routing dict
> (Python form); the table below is the human-readable equivalent.

| Prefix/Pattern | Domain | Primary MCP Tool | Secondary Tool |
|----------------|--------|------------------|----------------|
| `A*`, `U*`, `F*`, `E*`, `I*` (C++ naming) | cpp_class | `ue_analyze_symbols(smart)` | `ue_grep` for engine source |
| `BP_*` | blueprint | `ue_manage_blueprint(get_structure)` | `ue_analyze_symbols` for parent class |
| `M_*`, `MI_*` | material | `ue_manage_material(search_materials)` | `ue_manage_material(get_hlsl)` |
| `NS_*` | niagara | `ue_manage_niagara(search_systems)` | `ue_manage_niagara(get_parameters)` |
| `GA_*`, `GE_*` | gas | `ue_manage_gameplay(smart)` | `ue_trace_execution(trace_ability_flow)` |
| `r.*`, `sg.*`, `gc.*`, `net.*` | config | `ue_analyze_config(search_config)` | `ue_engine_docs(search)` |
| `ST_*` | ai_statetree | `ue_manage_ai(smart)` | `ue_manage_ai(get_structure)` |
| `BT_*` | ai_behaviortree | `ue_manage_ai(smart)` | `ue_manage_ai(find_tasks)` |
| `PCG_*` | pcg | `ue_manage_pcg(get_structure)` | `ue_manage_pcg(get_parameters)` |
| `LS_*`, `LevelSequence` | sequencer | `ue_sequencer_structure(smart)` | `ue_sequencer_tracks` |
| `CR_*`, `IK_*` | rigging | `ue_manage_rigging(smart)` | `ue_manage_rigging(get_structure)` |

---

## Domain-Specific Secondary Discovery

| Domain | What to check additionally |
|--------|--------------------------|
| Animation | TWO retargeting systems — check BOTH legacy (`URetargetSource`) AND modern (IK Retargeter) |
| Audio/MetaSound | Check delay line implementation classes, Blueprint integration APIs |
| C++ Classes | Trace hierarchy up AND down — `direction="up"` for parent chain, `direction="down"` for subclasses |
| Blueprint | Always check parent C++ class for inherited methods not visible in BP graph |
| Material | Check for Material Instances that override parameters |
| Config/CVar | Check `ECVF` flags, scalability group membership, platform overrides |

---

## Source Verification Checklist

When MCP tools return a source file:

1. Check file size — very small files (<50 lines) may be wrappers/forwards
2. Look for `#include` or `using` that delegates to another file
3. If wrapper detected → follow the include chain to find the real implementation
4. Verify the found file contains actual logic (function bodies, not just declarations)

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Step 0 returns empty | Target doesn't exist in project | Report "not found", suggest similar names via fuzzy grep |
| Domain detection ambiguous | Name doesn't match any prefix | Use Step 0 grep results to determine domain |
| MCP tool returns empty | Asset not loaded or PDB not indexed | Try `ue_engine_docs(search)` for engine entities |
| Binary asset unreadable | Using built-in `Read` on `.uasset` | Switch to MCP tool (`ue_manage_blueprint`, etc.) |

---

## Evaluation Criteria

### Activation Test Cases

**Positive (5)** — Should activate:
1. "ACharacter 어떻게 동작해?" → Activate (C++ class)
2. "Explain BP_PlayerController" → Activate (Blueprint)
3. "M_BasicRock 설명해줘" → Activate (Material)
4. "How does NS_Fire work?" → Activate (Niagara)
5. "r.Shadow.MaxResolution 뭐야?" → Activate (Config)

**Negative (3)** — Should NOT activate:
1. "BP_Player의 실행 흐름 추적해줘" → Use `blueprint-flow` skill
2. "GA_Attack 전체 흐름" → Use `cross-domain-flow` skill
3. "M_Rock 성능 최적화해줘" → Use `material-analysis` skill

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Domain detection accuracy | > 95 % |
| MCP tool selection accuracy | 100 % (hardcoded routing) |
| Response time (single domain) | < 30 s |
| Response completeness | All 5 output sections populated |

---

## Self-Test Mode

`/ue-explain --self-test` runs focused validation of this skill:

1. **Structure check**: Verify `SKILL.md`, `EXAMPLES.md`, `REFERENCE.md` exist
2. **Frontmatter check**: Parse YAML frontmatter for required fields (`name`, `description`, `argument-hint`, `metadata.version`)
3. **Routing check**: Verify `routing.json` entry exists for `ue-explain` with matching trigger patterns
4. **Tool reference check**: All MCP tools referenced in SKILL.md (`ue_analyze_symbols`, `ue_grep`, `ue_read`, `ue_manage_blueprint`, `ue_manage_material`, `ue_manage_niagara`, `ue_manage_gameplay`, `ue_manage_ai`, `ue_manage_pcg`, `ue_analyze_config`, `ue_engine_docs`, `ue_search_assets`, `ue_sequencer_structure`, `ue_manage_rigging`, `ue_trace_execution`) are valid tool names
5. **Example validity**: All `/ue-explain` invocations in EXAMPLES.md use the correct skill name and reference valid domain prefixes
6. **Anchor liveness**: All `[text](REFERENCE.md#anchor)` and `[text](EXAMPLES.md#anchor)` links in `SKILL.md` resolve to a real heading in the target file

**Output**: `PASS` / `FAIL` with per-check results. Does not execute the full explain workflow.

---

## Auto-Trigger Phrases

> Moved from SKILL.md (cold). The live router matches on [routing.json](../routing.json),
> not these phrases — kept here for human-readable browsing.

### Korean
- "X가 어떻게 동작해?", "X 설명해줘"
- "X 구조 분석해줘", "X가 뭐야?"
- "X 내부 구조 보여줘", "X 동작 원리"

### English
- "How does X work?", "Explain X"
- "What is X?", "Tell me about X"
- "Show me the structure of X", "How X works"

---

## Step 1 Routing Dict Full

> SKILL.md § Step 1 keeps the compact prefix→domain dict. The full Python form
> below preserves the per-prefix comments and the keyword/final fallback chain.

```python
ROUTING_RULES = {
    # Asset prefix (ordered by specificity)
    "BP_":   "blueprint",  "WBP_":  "blueprint",  "ABP_":  "blueprint",
    "GA_":   "ability",    "GE_":   "ability",
    "NS_":   "niagara",
    "M_":    "material",   "MI_":   "material",
    "ST_":   "ai",         "BT_":   "ai",
    "PCG_":  "pcg",
    "LS_":   "sequencer",
    "DT_":   "datatable",
    "MS_":   "audio",      "Sound": "audio",
    "CR_":   "control_rig",
    "IK_":   "ik_rig",
    # Config prefix
    "r.":    "config",  "gc.":   "config",  "net.":  "config",  "fx.":   "config",
    # UE C++ type prefixes (single letter + uppercase)
    "A":     "cpp_class",  # Actor (ACharacter, APawn)
    "U":     "cpp_class",  # UObject (UGameplayAbility)
    "F":     "cpp_class",  # Plain struct (FVector, FHitResult)
    "E":     "cpp_class",  # Enum (ECollisionChannel)
    "I":     "cpp_class",  # Interface (IAbilitySystemInterface)
}

# Keyword fallback (no prefix match)
KEYWORD_ROUTING = {
    "control rig":  "control_rig",
    "ik rig":       "ik_rig",
    "media player": "media",  "video": "media",  "electra": "media",
}

# Final fallback: ue_search_assets to discover type
```
