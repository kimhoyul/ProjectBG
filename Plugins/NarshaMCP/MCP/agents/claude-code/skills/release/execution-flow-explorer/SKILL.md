---
name: execution-flow-explorer
model: opus
description: "Purpose: code understanding. Bidirectional call graph — traces callers (who calls this?) AND callees (what does this call?) with Mermaid visualization and hotspot detection. Not for refactoring safety (use caller-graph-visualizer). Not for Input→Animation chains (use blueprint-flow or cross-domain-flow). Triggers: '호출 관계 보여줘', 'show call graph', 'dependency trace', '양방향 추적'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Execution Flow Explorer

**Purpose**: Bidirectional caller + callee tracing with unified visualization

---

## 🎯 Purpose

Trace execution flow in **both directions** from a target function:
- **Callers** (upstream): Who calls this function?
- **Callees** (downstream): What does this function call?
- **Unified visualization**: Single Mermaid graph with C++/Blueprint integration

**4 Key Differentiators**:
1. ✅ **Bidirectional tracing**: caller + callee simultaneously
2. ✅ **Unified visualization**: C++/BP/Delegate in one graph
3. ✅ **Interactive exploration**: depth control, filtering, hotspots
4. ✅ **Performance optimized**: parallel execution, session caching

---

## 🔍 Auto-Trigger Phrases

**Korean**:
- "이 함수 어디서 호출하나?" / "어디서 불리는지 궁금해"
- "이 함수가 뭘 호출하나?" / "내부에서 뭐 하나?"
- "호출 관계 보여줘" / "양방향 추적"
- "영향 분석" / "의존성 그래프"

**English**:
- "Where is this function called?" / "What calls this?"
- "What does this function call?" / "Internal execution flow"
- "Show me call graph" / "Bidirectional trace"
- "Impact analysis" / "Dependency graph"

---

## 🚀 Quick Example (30 seconds)

**User**: "TakeDamage 함수 호출 관계 보여줘"

**Agent Stage 1**: "양방향 추적할까요? (Callers + Callees)"
**User**: "네"

**Agent Stage 2** [~1.2s execution]:
```text
Executing parallel trace...
- Finding callers (ue_analyze_symbols)
- Finding callees (ue_trace_execution)
```

**Agent Stage 3** [Result]:
```text
🎯 Execution Flow: ACharacter::TakeDamage

📍 Callers (5 total):
  - APlayerController::ProcessDamage (C++)
  - BP_EnvironmentalHazard::OnOverlap (Blueprint)
  - [+3 more at depth 2]

📍 Callees (6 total):
  - ApplyDamageModifiers (C++)
  - PlayHitAnimation (Blueprint)
  - [+4 more at depth 2]

[Mermaid diagram with 12 nodes]

⚡ Hotspot: TakeDamage (11 connections)
```

**Total Time**: ~1.5s (vs ~3s sequential)

---

## 🔧 Implementation

This skill uses a **reference implementation** approach:
- **skill_logic.py**: Contains detailed orchestration logic for Claude Code to follow
- **Not executed directly**: Claude reads this file to understand how to combine MCP tools
- **Key functions**:
  - `execute_bidirectional_trace()`: Parallel caller + callee orchestration
  - `merge_caller_callee_graphs()`: Unified graph with negative/positive depth
  - `generate_bidirectional_mermaid()`: Subgraph-based visualization
  - `generate_text_summary()`: Statistics, hotspots, critical paths

**How it works**:
1. Claude detects auto-trigger phrase (e.g., "호출 관계 보여줘")
2. Reads skill_logic.py to understand orchestration pattern
3. Executes `ue_analyze_symbols` + `ue_trace_execution` **in parallel**
4. Merges results following the documented merge logic
5. Generates Mermaid diagram and text summary

**MCP Tools Used**:
- `ue_analyze_symbols(find_callers)`: Find direct callers. The skill builds deeper caller
  levels by explicit client-side frontier expansion with a visited set and round cap.
- `ue_trace_execution(trace_execution_flow)`: Find what the target function calls

**Visualization Output** (Issue #4955):
- Use `output_format="mermaid"` in `ue_trace_execution` for native Mermaid diagram output
- Use `output_format="html"` for interactive browser-based visualization
- This replaces manual Mermaid generation in `generate_bidirectional_mermaid()`

---

## 📚 Next Steps

- **EXAMPLES.md**: 3 complete scenarios (bug tracing, code understanding, impact analysis)
- **ADVANCED.md**: Filtering, depth control, performance tuning
- **REFERENCE.md**: Complete API reference, all parameters

---

## ⚠️ Common Failures

| 에러 유형 | 원인 | 해결 방법 |
|-----------|------|-----------|
| `ue_analyze_symbols` caller 결과 0건 | PDB 인덱스 미빌드 또는 함수명 오타 | `ue_cache_control(operation="clear_all")` 실행 후 정확한 함수명으로 재시도 |
| `ue_trace_execution` Blueprint 결과 누락 | Editor가 실행 중이 아니거나 Remote Control 비활성 | UE Editor 실행 상태 확인, Remote Control 플러그인 활성화 |
| 병렬 실행 시 한쪽만 timeout | MCP 서버 동시 요청 처리 한계 | 순차 실행으로 전환하여 안정성 확보 |
| Mermaid 다이어그램 렌더링 깨짐 | 노드명에 특수문자 (`<`, `>`, `::`) 포함 | 노드 ID를 sanitize하여 특수문자 이스케이프 처리 |
| depth 3+ 추적 시 30초+ 소요 | 대규모 호출 그래프 탐색으로 combinatorial explosion | `--depth 2`로 제한, 필요 시 특정 경로만 drill-down |
| C++/Blueprint 통합 그래프에서 노드 중복 | 동일 함수가 C++과 BP에서 각각 발견 | merge 로직에서 함수명 기반 중복 제거 확인 |

---

## Output Format

```
=== Execution Flow: {ClassName}::{FunctionName} ===

Callers (upstream, depth {N}):
  - {CallerClass}::{CallerFunc} (C++)
  - {BP_Name}::{EventNode} (Blueprint)
  - [+N more at depth 2+]

Callees (downstream, depth {N}):
  - {CalleeFunc} (C++)
  - {BP_EventName} (Blueprint)
  - [+N more at depth 2+]

Hotspot: {FunctionName} ({M} total connections)

[Mermaid diagram with {X} nodes]
```

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `ue_analyze_symbols` returns 0 callers | PDB index not built or function name misspelled | Run `ue_cache_control(validate_paths)` to verify index; confirm exact function signature |
| `ue_trace_execution` Blueprint results missing | Editor not running or Remote Control disabled | Report offline limitation; show C++ callers only from PDB data |
| Parallel execution timeout on one direction | MCP server concurrent request limit reached | Fall back to sequential execution (callers first, then callees) |

## ✅ Benefits

**vs Manual Tool Calls**:
- **Single command** instead of 2 separate calls
- **Unified graph** showing both directions
- **40% time savings** via parallel execution
- **Automatic merging** of C++ + Blueprint results

**vs Other Skills**:
- **Blueprint Flow Tracer**: Forward-only (input → animation)
- **Execution Flow Explorer**: Bidirectional (callers ↔ target ↔ callees)

---

**Status**: ✅ Phase 1 MVP Ready (Issue #3033)
**Last Updated**: 2026-01-01
## MCP Tool Examples

```python
# Trace execution flow through symbols
ue_analyze_symbols(operation="smart", function_name="BeginPlay")
```
