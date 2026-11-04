---
name: ue-slate-debug
model: opus
description: "Purpose: Runtime Slate / UMG widget debugging — focus loss, hit-test failure, layout overflow, visibility, designer-vs-PIE drift. Chains ue_editor_debug(get_widget_tree, search_widgets, capture_*) with ue_manage_widget(find_widget_at_position, get_runtime_geometry, get_preview, capture_widget_screenshot) into a 5-step diagnostic workflow. Triggers: 'slate debug', 'widget hit test', 'widget not visible', 'widget no focus', 'umg runtime', 'widget tree', 'focus chain', '슬레이트 디버그', '위젯 안 보임', '포커스 안 받음', '위젯 클릭 안됨', 'hit test 실패', 'UMG 디버그', '런타임 위젯', '위젯 트리'. Negative: does NOT author/validate widgets offline (use ue-widget-authoring) or analyze static .uasset only (use ue_manage_widget directly)."
compatibility: "Requires NarshaMCP MCP server (v0.9.12+) with Unreal Editor running. PIE session required for runtime tree dump (Step 2); offline-only `.uasset` analysis is out of scope (use `ue_manage_widget` directly). Pure-Slate (non-UMG) editor panels are screenshot-only — see Out of Scope in plan #9332."
argument-hint: "symptom description (e.g. '버튼이 안 눌려', 'Slate hit test failing') or empty to prompt for symptom"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Slate Debug — Runtime Slate / UMG Widget Diagnostic

**Issue**: #9332 (Epic Watch #9328 — Domain gap 2/4: SlateInspector)
**Purpose**: Single-prompt debugging for runtime widget failures (focus / hit-test / layout / visibility / designer-vs-PIE drift)

---

## Scope (runtime-only — offline authoring is out of scope)

This skill diagnoses **runtime** Slate/UMG failures in a live PIE (or editor-panel)
session. It deliberately does **not** cover the offline widget authoring/analysis
surface:

- Static `.uasset` widget-tree structure inspection — use `ue_manage_widget(get_structure)` directly.
- Widget DSL and MVVM (ModelViewViewModel): `ue_manage_widget` offers offline round-trip
  `export_dsl`/`validate_dsl`/`import_dsl`/`apply_dsl` (#19998) and live
  `inspect_mvvm_bindings` (read-only). Full offline authoring/validation orchestration
  is in [`ue-widget-authoring`](../ue-widget-authoring/SKILL.md).
- UMG animation: `ue_manage_widget(get_animations` / `get_animation_tracks)` reads metadata;
  animation authoring is out of scope.

This skill stays silent on those surfaces. The dedicated offline widget-authoring/validation
routing skill is [`ue-widget-authoring`](../ue-widget-authoring/SKILL.md) (Epic #19989 "A4",
Issue #20001) — export→validate→authoring round-trip lives there.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the diagnostic workflow automatically**.
> Do NOT just display documentation — actually run the MCP tools!

### Execution Requirements

1. **Symptom**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user one disambiguating question:
   > "Slate 디버그 중 어떤 증상인가요? (focus / hit-test / layout / visibility / designer-vs-PIE)"
2. **Probe** PIE state and editor connectivity (Step 1)
3. **Dump** runtime widget tree at appropriate scope (Step 2)
4. **Targeted probe** matching symptom class (Step 3)
5. **Capture** visual confirmation (Step 4)
6. **Emit** structured Slate Debug Report (Step 5)

---

## Auto-Trigger Phrases

### Korean
- "슬레이트 디버그", "Slate 디버그"
- "위젯 안 보임", "UI 안 보임", "버튼 안 눌림", "위젯 클릭 안됨"
- "포커스 안 받음", "키보드 입력 안됨", "포커스 체인"
- "hit test 실패", "위젯 hit test"
- "UMG 디버그", "런타임 위젯", "위젯 트리 디버그"
- "디자이너랑 PIE랑 달라", "WBP가 다르게 보여"

### English
- "slate debug", "umg runtime debug"
- "widget not visible", "widget hit test", "widget no focus"
- "focus chain", "keyboard dead", "input dead"
- "widget tree", "umg runtime"
- "wbp designer vs pie diff", "runtime widget mismatch"

---

## Workflow

### Step 0: Discovery Pre-Exploration

> **Purpose**: Confirm the symptom target exists before live probing.
> Skip Step 0 for generic symptoms ("위젯 안 보임" without a name) — go directly to Step 1.

```python
# 0a. If symptom mentions a specific widget/class (e.g. "BP_HUD focus", "WBP_Inventory"):
ue_grep(query="<widget_name>", domain="assets", limit=5)
# Discovers: WBP path under Content/, related .uasset files

# 0b. If symptom mentions a viewport pixel (e.g. "버튼 위치 (450, 320)"):
#    No Step 0 — proceed directly to Step 3 hit-test branch
```

**Decision Logic**:
- Widget found in assets → carry path forward to Step 2 `widget_path` parameter
- Widget not found → ask user to confirm name or check `Content/UI/` subdir
- Generic symptom → skip Step 0, proceed to Step 1

---

### Step 1: PIE State + Editor Probe

```python
# 1a. Editor connectivity (~2ms, Issue #9241)
ue_check_health(compact=true)
# Must return success; if fails → "Editor is not running. Launch via /launch-editor first."

# 1b. PIE state (runtime widget tree only meaningful in-PIE)
ue_editor_debug(operation="get_pie_state")
# Decision: if pie_active=false → "Start PIE first; runtime widget tree requires a live game world."
```

**Decision Tree**:
- `pie_active == false` → STOP and instruct user to start PIE; OR if symptom is editor-panel-related (asset editor, content browser), skip to Step 2 pure-Slate branch
- `pie_active == true` → continue to Step 2 UMG branch

### Step 2: Tree Dump (scope-appropriate)

**Branch A — UMG-rooted runtime tree (PIE active)**:
```python
ue_editor_debug(operation="get_widget_tree", max_depth=8)
# Returns: {user_widgets[], widget_count, capped, is_pie}
# - user_widgets[].root_widget = {absolute_position{x,y}, children[],
#   clipping, desired_size{width,height}, effective_visibility,
#   geometry_available, is_enabled, is_visible, parent_name,
#   render_opacity, size{width,height}, slot:{class}, visibility,
#   widget_class, widget_name}
# - visibility enum: Visible, Hidden, Collapsed, HitTestInvisible, SelfHitTestInvisible
# - slot.class examples: OverlaySlot, BorderSlot, ButtonSlot, SizeBoxSlot, VerticalBoxSlot
# Hard cap: 500 widgets per response.
```

**Branch B — Pure-Slate editor panel (PIE not active OR target is editor window)**:
```python
ue_editor_debug(operation="capture_all_panels", return_mode="file")
# OR for a named panel:
ue_editor_debug(operation="capture_editor_panel", panel_name="<panel>", return_mode="file")
# Returns: file_path to PNG. For a STRUCTURAL (not screenshot) walk of the pure-Slate
# tree, prefer Branch C inspect_slate when the Epic MCP server is live.
```

**Branch C — Epic SlateInspectorToolset (live Slate accessibility tree; UMG + pure-Slate)**:
```python
ue_editor_debug(operation="inspect_slate", action="Snapshot")   # action defaults to Snapshot
# Cascades to Epic's SlateInspectorToolset.Snapshot when a UE 5.8 launcher editor is
# running with Epic's MCP server (-ModelContextProtocolStartServer) AND NARSHAMCP_LIVE_EPIC_MCP=1:
#   backend="epic"   → text-accessibility snapshot of the FULL Slate tree (reaches
#                      pure-Slate panels that Branch A's UMG walker cannot).
#   backend="native" → {editor_required:true, message} guidance (no offline equivalent).
# Other inspection actions: Observe/Unobserve/ListObservers (live subtree watch),
#   WaitFor (text present/absent), Windows (top-level window list), Screenshot.
# This is the Issue #9332 "inspect_slate_tree" capability, delivered via Epic cascade (Issue #15832).
```

> **`ue_editor_debug(get_widget_tree)`'s `include_slate` is a no-op (empirically
> verified, narshamcp 0.9.12, 2026-04-26).** Its schema description claims it adds
> "raw SWidget data (verbose)", but a byte-diff against
> `_verification/pie_responses.json` confirms `include_slate=true` and
> `include_slate=false` produce identical responses (both 65121 bytes, modulo the
> `total_time_ms` timing field). Do NOT set this flag on `get_widget_tree` — it does
> not save round-trips and creates false expectations of SWidget data.
>
> **A *separate*, distinct `include_slate` param exists on
> `ue_manage_widget(get_runtime_geometry)`** (schema: "Include underlying Slate widget
> details", default false). The byte-diff above covers `get_widget_tree` **only** —
> `get_runtime_geometry`'s `include_slate` was **not** part of that verification, so do
> not assume it is also a no-op. Treat the two as independent params; the
> `get_runtime_geometry` variant is unverified here.
>
> True SWidget walking is available via the Epic-cascading `inspect_slate` op
> (Branch C, Issue #15832 — the long-deferred Issue #9332 capability), requiring a live
> UE 5.8 launcher editor + Epic MCP server.

### Step 3: Targeted Probe by Symptom Class

| Symptom | Tools | Why |
|---|---|---|
| **Hit-test fail** ("버튼 안 눌림", "click misses") | `ue_manage_widget(find_widget_at_position, screen_x=X, screen_y=Y, user_widget_name?)` + `ue_manage_widget(get_runtime_geometry, widget_name=W)` | Issue #9239: viewport-absolute pixels, deepest visible widget under cursor (matches `capture_pie_window` coord space). Issue #9654: pass `user_widget_name` to disambiguate when 2+ user widgets share a root. |
| **Focus loss** ("키보드 안 먹음", "focus dead") | `ue_editor_debug(search_widgets, widget_class="<focusable>", visibility_filter="visible")` + walk focus chain in `get_widget_tree` JSON | Compare visible focusable widgets vs. expected focus owner; check `bIsFocusable` and parent `Visibility` (HitTestInvisible kills focus). |
| **Layout overflow / clipping** | `ue_manage_widget(get_anchors)` (CanvasPanel-rooted only) + `ue_manage_widget(get_preview, widget_name=full/asset/path)` + `ue_editor_debug(capture_pie_window)` | `get_preview` captures the active editor window (1286×760 PNG); `capture_pie_window` is the reliable runtime baseline. `capture_widget_screenshot` is a conditional single-attempt verification probe while #23632 is open; verify its real file + bounds, and on timeout pivot immediately to the full-frame fallback. |
| **Designer vs PIE drift** ("WBP랑 게임 화면이 달라") | `ue_manage_widget(get_preview, widget_name=W)` + `ue_editor_debug(capture_pie_window)` | Side-by-side WBP Designer vs full PIE frame while #23632 remains open. Use `capture_widget_screenshot` only as a single-attempt verification probe; on timeout pivot immediately to the full-frame fallback. |
| **Visibility ("Hidden vs Collapsed vs HitTestInvisible")** | `ue_editor_debug(get_widget_tree)` + walk `effective_visibility` field (computed via parent chain) | `WidgetTreeHelpers::ComputeEffectiveVisibility` (CUWebSocketEditorSubsystem.cpp) returns the most restrictive visibility along the parent chain. |
| **Pure-Slate panel ("inspect editor menu")** | `ue_editor_debug(inspect_slate, action="Snapshot")` (Epic MCP live) → else `ue_editor_debug(capture_editor_panel, panel_name=P)` | `inspect_slate` gives a STRUCTURAL Slate accessibility tree when `backend="epic"` (Branch C, Issue #15832); when Epic is offline (`backend="native"`, editor_required) fall back to the panel screenshot as evidence. |

### Step 4: Visual Confirmation

```python
# Always capture the live viewport for ground-truth comparison
ue_editor_debug(operation="capture_pie_window", return_mode="file")
# OR for editor (no PIE):
ue_editor_debug(operation="capture_all_panels", return_mode="file")
# Returns: file_path → use Read(file_path) to view the PNG inline (vision)
```

After capture, **always** `Read(file_path)` the PNG so the model can vision-confirm the
diagnosis matches what the user sees on screen.

### Step 5: Slate Debug Report (Conclusion)

Emit a structured report. Maximum 5 tool calls total (Step 0-4) before this point —
acceptance criterion #3.

---

## Output Format

```text
=== Slate Debug Report ===

Symptom: <user's stated symptom, normalized>
Symptom Class: <hit_test | focus | layout | visibility | designer_drift | pure_slate>

--- Evidence ---
Suspect Widget: <user_widget_name>.<widget_name> (<widget_class>)
  Visibility: <Visible|Hidden|Collapsed|HitTestInvisible>
  Effective Visibility: <computed via parent chain>
  Cached Geometry: pos (<X>, <Y>) size (<W>, <H>)
  Slot: <CanvasPanelSlot|HBoxSlot|...> { offsets/anchors/padding }
  Type Props: <text content | brush asset | percent | hover state>

Screenshot: <file_path to PNG, viewable via Read>

--- Root Cause ---
<one-sentence diagnosis grounded in evidence above>

--- Suggested Fix ---
1. <UMG property change | anchor edit | focus override | binding fix>
2. <verification step>

--- Confidence ---
<HIGH | MEDIUM | LOW> — <evidence summary>

--- Deferred ---
<empty | "Pure-Slate (non-UMG) panel — structural walk via `inspect_slate` (Branch C, Issue #15832) needs a live UE 5.8 launcher editor + Epic MCP server; screenshot-only here">
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `ue_check_health` fails | Editor offline | Instruct user: "Editor must be running. Launch via /launch-editor or open the project manually." |
| `get_pie_state` returns `pie_active=false` | PIE not started | If symptom is in-game widget → instruct user to start PIE. If symptom is editor panel → switch to Step 2 Branch B (pure-Slate path). |
| `get_widget_tree` returns `widget_count=0` | No UUserWidgets in viewport | Symptom is likely a pure-Slate panel; switch to Branch B (`capture_all_panels` / `capture_editor_panel`). |
| `get_widget_tree` returns `capped=true` | Hit 500-widget hard cap | Re-call with `widget_path=<specific UUserWidget>` to scope down (Issue #7915 pattern). |
| `find_widget_at_position` returns null | No widget at coordinates OR widget is HitTestInvisible | Try `ue_editor_debug(search_widgets)` with the suspected class instead; manually walk focus chain. |
| `get_preview` opens BP Editor but capture fails | Designer hasn't fully drawn | Wait `settle_ms=800` (Issue #9251); retry once. |
| `capture_pie_window` returns base64 instead of file | `return_mode` defaulted to base64 | Re-call with `return_mode="file"` for full-resolution evidence. |
| Pure-Slate panel symptom (e.g. "asset editor menu") | No UMG root | Try `inspect_slate(action="Snapshot")` for a structural walk (Branch C); if `backend="native"` (Epic MCP offline), emit Output Format with `Deferred` populated + screenshot. Do NOT fabricate widget tree data. |

---

## Common Failures

| Symptom | Likely Cause | Quick Check |
|---------|--------------|-------------|
| Button visible but doesn't click | Parent `HitTestInvisible` OR overlapping widget on top | `find_widget_at_position` returns a *different* widget than expected |
| Text widget invisible despite `Visibility=Visible` | Parent `Collapsed`/`Hidden` (effective visibility) | Walk parent chain in `get_widget_tree` JSON |
| Keyboard input ignored | Button-class siblings exist but no focus owner | `search_widgets(widget_class="Button", visibility_filter=visible)` then walk widget tree for active focus owner (no API field — inferred) |
| Layout breaks at runtime but Designer is fine | Anchor/offset binding changed at runtime | `get_preview` (active-window PNG) vs `capture_pie_window` (PIE PNG); manual visual diff |
| Widget appears at wrong position | `CanvasPanelSlot` offsets vs anchors mismatch | `get_anchors` shows actual layout values (CanvasPanel-rooted WBPs only; returns `count: 0` otherwise) |

## Conditional PIE Ops (single attempt; fallback while #23632 is open)

Empirically verified in Phase 2.5 (Lyra 5.7, narshamcp 0.9.12, 2026-04-26).
See `_verification/pie_responses.json` for capture evidence.

**Issue #23632 (open tracker)**: the live PIE round-trip for these two ops is being
re-verified on UE 5.8. `PIE_NOT_RUNNING` is an ordinary actionable error that preserves
`error_code` and points to `ue_editor_automation(play_in_editor)`. Only a real response timeout
is marked `known_limitation:true`; it carries `tracking_issue:#23632` and the fallback below.
The caller makes exactly one attempt and never retries a timeout. This preserves the
historical dead-wait fix (`10s×3+3s` / `15s×3+3s` before single-attempt routing)
without misclassifying connection failures or C++ precondition errors.

| Op | Failure Mode | Workaround |
|----|--------------|------------|
| `ue_manage_widget(capture_widget_screenshot)` | Conditional single-attempt probe. Success must include a real image file and `widget_bounds`; a response timeout is tracked by #23632. | Use `ue_editor_debug(capture_pie_window)` for full-frame PIE capture; manually crop if needed. |
| `ue_manage_widget(pie_list_active_widgets)` | Conditional single-attempt probe. Default `max_widgets=500`, cap 2000; a response timeout is tracked by #23632. | Use `ue_editor_debug(get_widget_tree)` and walk the response. |

## Op Naming Inconsistency (read-time gotcha)

| Op | Position field | Size field |
|----|---------------|------------|
| `ue_editor_debug(get_widget_tree)` | `absolute_position: {x, y}` | `size: {width, height}` |
| `ue_manage_widget(find_widget_at_position).widget_at_position` | `abs_pos: {x, y}` | `size: {height, width}` |
| `ue_manage_widget(get_preview)` | (n/a — capture op) | `width` / `height` (top-level, not nested) |

When chaining ops, **explicitly map field names** rather than assuming sibling ops share a schema.

---

## Tool Budget

> Maximum **5 tool calls** from Step 1 through Step 4 (acceptance criterion #3).
> Step 0 is optional discovery (free).
> Step 5 is the report (no tool call).

Typical 4-call budget for a hit-test fail:
1. `ue_check_health(compact=true)`
2. `ue_editor_debug(get_pie_state)`
3. `ue_manage_widget(find_widget_at_position, screen_x, screen_y)`
4. `ue_editor_debug(capture_pie_window, return_mode=file)` + `Read(png)`

---

## Self-Test

Run `/agent-test skill ue-slate-debug` (eval-channel mode, ~1 min, $0 subscription
cost) to verify trigger matching + workflow ordering without a live Editor.

**Acceptance**: routing.json patterns trigger this skill in a fresh session for the
16 ko + 12 en phrases listed under Auto-Trigger Phrases above. The test exercises
SKILL.md frontmatter parsing, routing.json schema compliance, and skill resolution
— it does **not** invoke MCP ops (those need a live PIE session).

For end-to-end validation against a real PIE session, follow EXAMPLES.md S1-S4
manually (Lyra is a convenient substrate). S5 (pure-Slate) requires only the
editor running, no PIE.

See [docs/development/testing/AGENT_COMPONENT_TESTING_GUIDE.md](../../../docs/development/testing/AGENT_COMPONENT_TESTING_GUIDE.md).

---

## Related

- **Issue #9332**: Skill creation (this file)
- **Issue #9328**: Epic Watch — Domain gap 2/4 (SlateInspector)
- **Issue #7915**: `get_widget_tree` + `search_widgets` (consumed)
- **Issue #9239**: `find_widget_at_position` viewport-absolute coords (consumed)
- **Issue #9251**: `get_preview` for visual baseline (consumed)
- **Issue #9344**: `capture_widget_screenshot`, `pie_list_active_widgets` (consumed)
- **Issue #9654**: `user_widget_name` disambiguation in `find_widget_at_position` (consumed)
- **Skills**: `/ue-debug` (general crash), `/blueprint-generator` (widget creation), `/screenshot-vision-analyzer` (vision pipeline)

---

**Status**: Phase 2 — the runtime-Slate diagnostic now leverages the `inspect_slate` Epic-cascade op on `ue_editor_debug` (op added by Issue #15832, wired into Branch C on 2026-06-19), delivering the long-deferred Issue #9332 structural-Slate-walk capability. Supersedes the Phase 1 "skill-only, no new MCP ops" MVP baseline.
**Last Updated**: 2026-07-13
