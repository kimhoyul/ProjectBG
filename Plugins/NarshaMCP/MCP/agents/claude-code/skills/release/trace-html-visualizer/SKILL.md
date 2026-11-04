---
name: trace-html-visualizer
model: opus
description: "Purpose: interactive trace HTML export. Generate Blueprint trace HTML visualization file. Discovers nodes, traces path, saves 600KB+ HTML to file. Features: Tab/Unified View, Node Detail panel, C++ boundary markers, data flow badges. Triggers: 'trace HTML', 'HTML로 보여줘', 'trace를 HTML로', '시각화 파일', 'interactive trace', 'open trace in browser'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Trace HTML Visualizer

**Purpose**: Generate interactive Blueprint trace HTML visualization files

---

## Purpose

Generate self-contained HTML files that visualize Blueprint execution trace paths using the BlueprintUE renderer. Saves files locally for browser viewing.

**Why separate from `blueprint-flow`**: HTML output requires fundamentally different handling:
1. File I/O required (600KB+ HTML must be saved to file, never returned inline)
2. `end_node` is mandatory (JSON works with start_node alone)
3. Browser-based interaction (Tab/Unified View, Node Detail panel, animation controls)

---

## Trigger Phrases

**HTML/visual-specific only** (avoids collision with `blueprint-flow`, `execution-flow-explorer`):

### Korean
- "trace HTML로 보여줘", "HTML로 시각화해줘"
- "브라우저에서 보기", "시각화 파일 만들어줘"
- "trace를 HTML로", "인터랙티브 트레이스"

### English
- "trace to HTML", "open trace in browser"
- "interactive visualization", "visual trace file"
- "HTML blueprint trace", "generate trace HTML"

---

## Workflow (4 Phase)

### Phase 0: Prerequisite (optional)

```python
ue_check_health()
```
- Verify metadata is loaded
- If failed: "MCP 서버 연결 확인 필요"

### Phase 1: Node Discovery

**Skip if**: User provides both `start_node` AND `end_node` → go directly to Phase 2.

**Option A (recommended)**: Get blueprint structure to find entry points
```python
ue_manage_blueprint(
    operation="get_structure",
    blueprint_name="GA_Weapon_Fire"
)
# → Pick start from entry points (Event nodes)
# → Pick end from leaf nodes (EndAbility, PrintString, etc.)
```

**Option B**: JSON trace to explore reachable nodes
```python
ue_trace_execution(
    operation="trace_execution_flow",
    start_node="EventActivateAbility",
    output_format="json"
    # end_node omitted → returns max_reachable_depth
)
```

**CRITICAL**: `output_format="html"` **requires** `end_node`. Without it, only a hint message is returned (see `analysis_trace.rs:514-526`).

### Phase 2: HTML Generation

```python
ue_trace_execution(
    operation="trace_execution_flow",
    start_node="EventActivateAbility",
    end_node="EndAbility",
    blueprint_name="GA_Weapon_Fire",
    output_format="html",
)
```

**Response structure** (`html_generator.rs:391-403`):
```json
{
    "success": true,
    "operation": "trace_execution_flow",
    "output_format": "html",
    "html_content": "<600KB+ HTML string>",
    "blueprint_name": "GA_Weapon_Fire",
    "path_length": 6,
    "node_count": 45,
    "graph_count": 3,
    "total_time_ms": 45.2
}
```

### Phase 3: File Save + Report

1. **Save** `html_content` to file using Write tool
2. **File naming**: `trace_{blueprint_name}_{description}.html` in project root
   - Sanitize BP name: spaces/slashes → underscores
   - Example: `trace_GA_Weapon_Fire_ability_flow.html`
   - Note: These files are untracked by git (not in `.gitignore` — add `trace_*.html` if needed)
3. **CRITICAL**: NEVER output `html_content` inline in conversation (600-717KB = context explosion). Always save to file, report path only.
4. **Report** (path + metrics only):
   ```text
   HTML 시각화 저장됨: trace_GA_Weapon_Fire_ability_flow.html
   - 경로 길이: 6 노드
   - 전체 노드: 45개 (3개 그래프)
   - 생성 시간: 45.2ms
   - 기능: Tab View(기본) / Unified View 토글, Node Detail 패널
   ```

---

## Quick Example

**User**: "GA_Weapon_Fire의 trace를 HTML로 보여줘"

```text
Phase 0: ue_check_health() → metadata loaded
Phase 1: User provided BP name only → get_structure finds entry points
         → start: EventActivateAbility, end: EndAbility
Phase 2: ue_trace_execution(
           operation="trace_execution_flow",
           blueprint_name="GA_Weapon_Fire",
           start_node="EventActivateAbility",
           end_node="EndAbility",
           output_format="html")
         → html_content (667KB) + path_length: 6
Phase 3: Write → trace_GA_Weapon_Fire_ability_flow.html
         → Report: file path + 6 nodes + 45 total + 3 graphs + 45ms
```

---

## Parameter Combinations

| Scenario | blueprint_name | start_node | end_node |
|----------|---------------|------------|----------|
| Ability flow | GA_Weapon_Fire | EventActivateAbility | EndAbility |
| Input handling | BP_HeroCharacter | InputAction_Attack | PlayMontage |
| Widget display | GAB_ShowWidget | EventActivateAbility | RemoveWidget |
| No scope | (omit) | Global search | Cross-BP possible |

---

## Error Handling

| Error | Cause | Recovery |
|-------|-------|----------|
| Hint message returned | `end_node` missing | Go back to Phase 1 for node discovery |
| `path_found: false` | No path exists | Try different end_node or increase max_depth |
| Empty `html_content` | Metadata not loaded | Run `ue_check_health()`, verify connection |
| BP name mismatch | Typo or not loaded | Use `ue_search_assets` to find correct name |

---

## Scope Boundaries

- **This skill**: HTML file generation + save + browser viewing
- **blueprint-flow**: JSON/Mermaid text analysis (no file save needed)
- **execution-flow-explorer**: Bidirectional call graph (Mermaid)
- **cross-domain-flow**: 5-tool cross-domain tracing (Input → Animation)

---

## HTML Features (Issue #4955)

The generated HTML includes:
- **Tab View** (default): One graph per tab, clean navigation
- **Unified View**: All graphs visible simultaneously
- **Node Detail Panel**: Click any node for pin details, data flow
- **Path Highlighting**: Traced nodes glow with animation
- **C++ Boundary Markers**: Visual indicators for BP↔C++ transitions
- **Data Flow Badges**: Show data pin connections between nodes
- **Self-contained**: No external dependencies, opens in any browser

---

## Related Files

- `MCP/native/mcp_server/src/visualization/html_generator.rs:232` — `generate_blueprintue_trace()`
- `MCP/native/mcp_server/src/tools/analysis_trace/execution_flow.rs:59` — `trace_execution_flow()`
- `MCP/native/mcp_server/src/tools/analysis_trace/execution_flow.rs:521` — `end_node` required check
- `MCP/native/mcp_server/src/visualization/template.rs:24-26` — `BLUEPRINTUE_TRACE_TEMPLATE`
- `MCP/native/mcp_server/templates/blueprintue_trace_visualization.html` — HTML template (64KB)

**Related Skills**: `blueprint-flow` (JSON/Mermaid complement), `bp-flow-comprehension-eval` (JSON quality)
**Issue**: #4955 (HTML visualization implementation)

## Output Format

```
HTML 시각화 저장됨: trace_GA_Weapon_Fire_ability_flow.html

Trace Summary:
  Blueprint:   GA_Weapon_Fire
  Start Node:  EventActivateAbility
  End Node:    EndAbility
  Path Length:  6 nodes
  Total Nodes: 45 (3 graphs)
  Generation:  45.2ms
  File Size:   667 KB

Features: Tab View (default), Unified View toggle, Node Detail panel
Open in browser to interact.
```

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| Hint message returned instead of HTML | `end_node` parameter is missing | Go back to Phase 1: use `get_structure` or JSON trace to discover valid end nodes |
| `path_found: false` in response | No execution path exists between start and end nodes | Try a different `end_node` or remove `blueprint_name` scope for cross-BP search |
| Empty `html_content` field | Metadata not loaded or MCP server connection lost | Run `ue_check_health()` to verify server status; regenerate metadata if needed |
