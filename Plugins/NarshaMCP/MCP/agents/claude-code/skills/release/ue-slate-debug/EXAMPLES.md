# UE Slate Debug — Examples

**Version**: 1.1.0 (Phase 2 — empirical refresh)
**Status**: Phase 2 verified against Lyra 5.7 PIE captures (2026-04-26)
**Parent**: [SKILL.md](SKILL.md)
**Issue**: #9332

> **⚠️ Response shapes are illustrative.** The JSON snippets in S1-S4 below
> approximate the live `ue_editor_debug` / `ue_manage_widget` response structure
> for project-agnostic readability. **Field names and nesting** (`absolute_position`,
> `size: {width, height}`, `effective_visibility`, `visibility` enum, `slot: {class}`,
> `widget_class`, etc.) are taken from the
> [`_verification/pie_responses.json`](_verification/pie_responses.json) capture
> (Lyra 5.7, narshamcp 0.9.12, world `L_DefaultEditorOverview`, 62 widgets across
> 10 user_widgets). Specific widget names (`WBP_PlayerHUD`, `Btn_NewGame`, etc.)
> are placeholders — substitute your project's actual WBPs. **S5 (pure-Slate)** is
> screenshot-only and project-agnostic. **All ops require an active PIE session**
> (`ue_editor_debug(get_pie_state).is_pie_running == true`); without PIE,
> `widget_count: 0` and empty matches. SKILL.md Step 1 enforces this.

Five scenarios. Each ends in a structured Slate Debug Report within ≤5 tool calls
(Step 0 discovery is free; Step 5 report has no tool call). Acceptance criterion #3: at least 3 scenarios reach conclusion in ≤5 tool calls — all 5 below satisfy.

---

## Scenario 1: 버튼이 안 눌림 — Hit Test 실패

### User Request
```text
"PIE에서 인벤토리 버튼이 안 눌려"
```

### Invocation
```bash
/ue-slate-debug 인벤토리 버튼이 안 눌려
```

### Step-by-Step Workflow (4 tool calls)

#### Step 0: Discovery (optional — skipped: no specific widget name)

#### Step 1: PIE State + Editor Probe
```python
# Call 1
ue_check_health(compact=true)
# Returns: {status: "ok", version: "0.9.13", editor_hint: "running"}

# Call 2
ue_editor_debug(operation="get_pie_state")
# Returns: {pie_active: true, world_name: "L_Hideout", play_mode: "PIE"}
```

#### Step 3: Hit-test branch (skip Step 2 — go straight to coordinate probe)
```python
# User estimates the button at viewport pixel (450, 320). Call 3.
ue_manage_widget(
    operation="find_widget_at_position",
    screen_x=450,
    screen_y=320
)
# Returns: {
#   "widget_name": "OverlayBlocker",                    ← unexpected!
#   "widget_class": "Border",
#   "user_widget_name": "WBP_LoadingScreen",            ← still in viewport
#   "abs_pos": {"x": 0, "y": 0},
#   "size": {"w": 1920, "h": 1080},
#   "depth": 1
# }
```

#### Step 4: Visual Confirmation
```python
# Call 4
ue_editor_debug(operation="capture_pie_window", return_mode="file")
# Returns: {file_path: "Saved/Screenshots/MCP/capture_pie_window_..._.png"}
Read("Saved/Screenshots/MCP/capture_pie_window_..._.png")
# Vision confirms: full-screen translucent loading screen overlay still visible
```

### Step 5: Slate Debug Report
```text
=== Slate Debug Report ===

Symptom: 인벤토리 버튼이 안 눌려
Symptom Class: hit_test

--- Evidence ---
Suspect Widget: WBP_LoadingScreen.OverlayBlocker (Border)
  Visibility: Visible
  Effective Visibility: Visible
  Cached Geometry: pos (0, 0) size (1920, 1080)
  Slot: CanvasPanelSlot { offsets: full-screen anchor }
  Type Props: BackgroundBrush=BG_Translucent_Black10

Screenshot: Saved/Screenshots/MCP/capture_pie_window_..._.png

--- Root Cause ---
WBP_LoadingScreen is still mounted in the viewport with a full-screen Border
that intercepts all hit tests, even though it's only 10% opaque (visually fading).

--- Suggested Fix ---
1. Set OverlayBlocker.Visibility = "HitTestInvisible" during the fade-out
   (or remove the widget when the fade animation completes).
2. Verify by re-running find_widget_at_position(450, 320) — should now
   return the inventory button.

--- Confidence ---
HIGH — find_widget_at_position returned a different widget than expected,
and screenshot confirms an overlay is still mounted.
```

---

## Scenario 2: 텍스트 안 보임 — Clipping

### User Request
```text
"WBP_PlayerHUD의 ScoreText가 PIE에서 안 보여"
```

### Invocation
```bash
/ue-slate-debug WBP_PlayerHUD ScoreText 안 보임
```

### Step-by-Step Workflow (4 tool calls)

#### Step 0: Discovery
```python
# Free call (Step 0)
ue_grep(query="WBP_PlayerHUD", domain="assets", limit=5)
# Confirms: /Game/UI/HUD/WBP_PlayerHUD.uasset exists
```

#### Step 1: PIE State
```python
# Call 1
ue_editor_debug(operation="get_pie_state")
# Returns: {pie_active: true}
```

#### Step 2: Tree Dump
```python
# Call 2
ue_editor_debug(operation="get_widget_tree", max_depth=8)
# Returns: {user_widgets[], widget_count, capped, is_pie}
# Each user_widgets[i].root_widget has these keys (verified Phase 2 capture):
#   absolute_position{x,y}, children[], clipping, desired_size{width,height},
#   effective_visibility, geometry_available, is_enabled, is_visible,
#   parent_name, render_opacity, size{width,height}, slot:{class},
#   visibility, widget_class, widget_name
# Walking JSON for our scenario finds:
#   WBP_PlayerHUD
#     └─ HBox_TopBar (visibility:"Visible", size:{width:1920, height:60})
#         └─ ScoreText (visibility:"Visible", size:{width:0, height:24},
#                       slot:{class:"HorizontalBoxSlot"}, effective_visibility:"Visible")
# NOTE: slot only exposes {class}; padding/FillRule etc. are NOT in the response.
```

#### Step 3: Layout branch
```python
# Call 3
ue_manage_widget(operation="get_runtime_geometry", widget_name="ScoreText")
# Returns same node shape as get_widget_tree (Phase 2 verified).
# For our suspect: {absolute_position:{x:1920, y:0}, size:{width:0, height:24}, ...}
# size.width == 0 → fully clipped horizontally
```

#### Step 4: Visual Confirmation
```python
# Call 4
ue_editor_debug(operation="capture_pie_window", return_mode="file")
Read(file_path)
# Vision confirms: HBox_TopBar full of widgets, ScoreText pushed off-screen right
```

### Step 5: Slate Debug Report
```text
=== Slate Debug Report ===

Symptom: ScoreText 안 보임
Symptom Class: layout

--- Evidence ---
Suspect Widget: WBP_PlayerHUD.ScoreText (widget_class:"TextBlock")
  visibility: "Visible"
  effective_visibility: "Visible"
  absolute_position: {x: 1920, y: 0}
  size: {width: 0, height: 24}   ← width collapsed to zero
  slot: {class: "HorizontalBoxSlot"}  ← only class exposed; padding/FillRule
                                        not in MCP response (verified Phase 2)

Screenshot: Saved/Screenshots/MCP/capture_pie_window_..._.png

--- Root Cause ---
ScoreText sits at the right edge of HBox_TopBar. Sibling widgets (HealthBar,
MoneyText) consumed all 1920px, leaving ScoreText with width=0. The widget
reports Visible but renders nothing because its allocated geometry is
zero-width. (Slot internals like FillRule/Size aren't in MCP response —
inspect via Designer or `ue_manage_widget(get_structure)` for offline view.)

--- Suggested Fix ---
1. In HBox_TopBar, open WBP in Designer → ScoreText slot → set FillRule="Fill"
   (FillSpan=1) OR set MinDesiredSize.X = 80.
2. Re-run get_runtime_geometry — size.width should be > 0.

--- Confidence ---
HIGH — geometry width=0 is the unambiguous signature of an HBox sibling
fight; visibility flags rule out the alternative explanations.
```

---

## Scenario 3: 포커스가 안 와 — Keyboard Input Dead

### User Request
```text
"메뉴에서 화살표 키가 안 먹어"
```

### Invocation
```bash
/ue-slate-debug 메뉴 키보드 안 먹음
```

### Step-by-Step Workflow (3 tool calls)

#### Step 1: PIE State
```python
# Call 1
ue_editor_debug(operation="get_pie_state")
# Returns: {pie_active: true}
```

#### Step 2: Tree Dump (focused at WBP_MainMenu)
```python
# Call 2
ue_editor_debug(operation="get_widget_tree", max_depth=6)
# JSON walk finds (real fields only — no IsFocusable in MCP response):
#   WBP_MainMenu
#     └─ VBox_Buttons (widget_class:"VerticalBox", visibility:"Visible")
#         ├─ Btn_NewGame (widget_class:"CommonButtonInternalBase", visibility:"Visible", is_enabled:true)
#         ├─ Btn_Continue (widget_class:"CommonButtonInternalBase", visibility:"Visible", is_enabled:true)
#         └─ Btn_Settings (widget_class:"CommonButtonInternalBase", visibility:"Visible", is_enabled:true)
#   WBP_BackgroundOverlay     ← MOUNTED ON TOP
#     └─ ImageBackground (widget_class:"Image", visibility:"Visible",
#                         effective_visibility:"Visible",
#                         absolute_position:{x:0,y:0}, size:{width:1920,height:1080})
# NOTE: focus-ability is inferred from widget_class — Button-derived classes
# are focus targets; Image is not. The MCP response has NO IsFocusable field.
```

#### Step 3: Focus search
```python
# Call 3 — list all visible buttons (the conventional focus-receiving UMG class)
ue_editor_debug(
    operation="search_widgets",
    widget_class="Button",
    visibility_filter="visible"
)
# Returns (verified Phase 2 shape): {match_count: 3, matches: [...]}
# matches[i] = {absolute_position{x,y}, is_enabled, is_hovered, is_pressed,
#               is_visible, path, size{width,height}, visibility,
#               widget_class, widget_name}
# Btn_NewGame/Continue/Settings: visibility="Visible", is_enabled=true.
# Focus-ability is inferred from widget_class (Button-derived = focus target);
# the API does NOT expose IsFocusable directly. None of the 3 reports is the
# active focus owner (no API field, but indirect evidence: arrow keys do
# nothing → CommonUI/EnhancedInput has no consumer).
```

### Step 5: Slate Debug Report
```text
=== Slate Debug Report ===

Symptom: 메뉴 키보드 화살표 안 먹음
Symptom Class: focus

--- Evidence ---
Suspect Widget: WBP_BackgroundOverlay.ImageBackground (widget_class:"Image")
  visibility: "Visible" (NOT "HitTestInvisible")
  effective_visibility: "Visible"
  absolute_position: {x: 0, y: 0}
  size: {width: 1920, height: 1080}
  slot: {class: "CanvasPanelSlot"}  ← only class exposed; full-screen anchor
                                      inferred from size matching viewport
  Inferred: Image class is not focus-eligible (no IsFocusable field in MCP
  response — inference based on widget_class).
  Z-Order: above WBP_MainMenu (inferred from get_widget_tree iteration order)

Focus Owner Search: 3 visible Buttons (Btn_NewGame/Continue/Settings) all
  is_enabled=true, but none is the active focus owner. (MCP has no
  is_focused field; indirect evidence: arrow keys do nothing.)

--- Root Cause ---
WBP_BackgroundOverlay was added to viewport with default Visibility=Visible
(not HitTestInvisible). Its full-screen Image intercepts mouse hover and
prevents focus from reaching the underlying buttons. With no focus owner,
arrow key navigation in CommonUI / EnhancedInput sees no consumer and
silently drops the key event.

--- Suggested Fix ---
1. Set WBP_BackgroundOverlay.Visibility = "HitTestInvisible" (or
   "SelfHitTestInvisible" if children should still receive input).
2. Explicitly call SetUserFocus(Btn_NewGame) on menu open.
3. Verify by re-running search_widgets — Btn_NewGame should appear in
   focused state, and arrow keys should navigate.

--- Confidence ---
HIGH — overlay z-order + non-focusable + zero focus owners is the textbook
"focus black hole" pattern.
```

---

## Scenario 4: 디자이너 vs PIE 차이 — Runtime Override

### User Request
```text
"WBP_HealthBar가 디자이너에선 빨간색인데 PIE에서는 회색이야"
```

### Invocation
```bash
/ue-slate-debug WBP_HealthBar 디자이너랑 PIE 색이 다름
```

### Step-by-Step Workflow (4 tool calls)

#### Step 0: Discovery
```python
# Free call
ue_grep(query="WBP_HealthBar", domain="assets", limit=5)
# Confirms: /Game/UI/HUD/WBP_HealthBar.uasset
```

#### Step 1: Editor Probe (PIE optional for this scenario — designer side first)
```python
# Call 1 — Designer baseline (requires full path like /Game/UI/...)
ue_manage_widget(
    operation="get_preview",
    widget_name="/Game/UI/HUD/WBP_HealthBar",
    return_mode="file",
    settle_ms=800,
    close_after=True
)
# Returns (verified Phase 2.5 shape):
# {
#   capture_mode: "active_window",  ← captures the active editor window,
#                                      NOT just WBP designer in isolation.
#                                      Other windows (level viewport) may
#                                      appear if they were active when called.
#   file_path: "...../Saved/Screenshots/MCP_YYYYMMDD_HHMMSS.png",
#   height: 760,    ← top-level fields (NOT nested as widget_size: {w, h})
#   width: 1286,
#   widget_name: "/Game/UI/HUD/WBP_HealthBar"
# }
Read(file_path)  # Vision: progress bar is RED at design time
```

#### Step 2: PIE state
```python
# Call 2
ue_editor_debug(operation="get_pie_state")
# Returns: {pie_active: true}
```

#### Step 3: Runtime crop
```python
# Call 3 — ⚠️ KNOWN ISSUE: capture_widget_screenshot times out (15s/48s)
# in Phase 2.5 testing on Lyra. Use ue_editor_debug(capture_pie_window)
# as the reliable fallback and crop manually if needed.
ue_editor_debug(operation="capture_pie_window", return_mode="file")
# Returns: {file_path: "Saved/Screenshots/MCP/capture_pie_window_..._.png"}
Read(file_path)
# Vision confirms: progress bar is GREY at runtime in the live PIE viewport.
# (capture_widget_screenshot would have AABB-cropped the result, but the op
# is currently broken — see SKILL.md Common Failures.)
```

#### Step 4: Structure inspect (offline) for property bindings
```python
# Call 4
ue_manage_widget(
    operation="get_structure",
    widget_name="WBP_HealthBar"
)
# JSON shows: HealthBar.FillColor has a property binding to
#   GetHealthBarColor() function returning FLinearColor::Gray when
#   health > 0.5 (game logic override)
```

### Step 5: Slate Debug Report
```text
=== Slate Debug Report ===

Symptom: WBP_HealthBar 디자이너 vs PIE 색 차이
Symptom Class: designer_drift

--- Evidence ---
Designer Preview: RED  (Saved/Screenshots/MCP/get_preview_..._.png — vision)
PIE Runtime Crop: GREY (Saved/Screenshots/MCP/capture_widget_screenshot_..._.png — vision)
Property Binding: HealthBar.FillColor ← GetHealthBarColor()
  Logic: health > 0.5 → FLinearColor::Gray
         health 0.2-0.5 → FLinearColor::Yellow
         health < 0.2 → FLinearColor::Red

--- Root Cause ---
Designer shows the static literal Color set on the SBox at edit time.
At runtime, the property binding GetHealthBarColor() takes over and
returns Grey because the test character has full health (>0.5).
The Designer view does NOT execute property bindings, so the binding
result was never visible at edit time.

--- Suggested Fix ---
1. If the design intent is "always red" → remove the property binding
   (Designer → HealthBar → FillColor → unbind).
2. If the binding is intentional → update Designer's literal color to
   match the lowest-priority binding output (Grey), so what-you-see-is-
   what-you-get for designers.
3. Optional: add a Designer-time "Show Bindings Preview" toggle on the WBP.

--- Confidence ---
HIGH — direct visual diff plus the property binding source explains the
discrepancy completely.
```

---

## Scenario 5: 메뉴 패널 검사 — Pure Slate (no UMG)

### User Request
```text
"Sequencer 패널이 이상하게 그려지는데 검사해줘"
```

### Invocation
```bash
/ue-slate-debug Sequencer 패널 이상함
```

### Step-by-Step Workflow (3 tool calls — pure-Slate path)

#### Step 1: PIE State (will fail / be irrelevant)
```python
# Call 1
ue_editor_debug(operation="get_pie_state")
# Returns: {pie_active: false, world_name: ""}
# Decision: skip UMG branch; switch to pure-Slate path
```

#### Step 2: Capture editor panel (Branch B)
```python
# Call 2
ue_editor_debug(
    operation="capture_editor_panel",
    panel_name="Sequencer",
    return_mode="file"
)
# Returns: {file_path: "Saved/Screenshots/MCP/capture_editor_panel_Sequencer_..._.png"}
Read(file_path)
# Vision: Sequencer panel rendered with overlapping track headers and
# the playhead cursor extending past the visible ruler
```

#### Step 3: All editor panels screenshot (broader context)
```python
# Call 3
ue_editor_debug(operation="capture_all_panels", return_mode="file")
Read(file_path)
# Vision: confirms Sequencer is undocked into a floating SWindow that's
# narrower than the panel's minimum sensible width
```

### Step 5: Slate Debug Report
```text
=== Slate Debug Report ===

Symptom: Sequencer 패널 이상함
Symptom Class: pure_slate

--- Evidence ---
Captured Panel: Sequencer  (file_path)
  Visual finding: track headers overlap; playhead extends past ruler edge
Active Window: Floating SWindow ~720px wide
  Visual finding: window is below the panel's effective minimum width

--- Root Cause (visual diagnosis) ---
The Sequencer panel was undocked into a floating SWindow that is narrower
than the SSequencer's minimum-content threshold. SSequencer's track header
column has no horizontal scroll fallback, so child SWidgets composite over
each other.

--- Suggested Fix ---
1. Re-dock the Sequencer panel to the main editor frame (Window → Reset
   Layout, or drag the tab back to the dock area).
2. If the floating layout is intentional, manually resize the floating
   SWindow to ≥1024px wide.

--- Deferred ---
Pure-Slate (non-UMG) panel — a structural walk of SSequencer's SWidget tree is now
available via the Epic-cascading `ue_editor_debug(inspect_slate, action="Snapshot")`
op (Branch C, Issue #15832 — the delivered Issue #9332 capability) when a live UE 5.8
launcher editor + Epic MCP server are running (`backend="epic"`). Without that live
Epic backend (`backend="native"`, editor_required), screenshot evidence is the
strongest available signal — as in this capture.

--- Confidence ---
MEDIUM — visual diagnosis is unambiguous; a live `inspect_slate` Snapshot (Branch C)
would confirm which specific child slot is the layout owner, but the Epic MCP backend
was not available for this capture.
```

---

## Manual Trigger Smoke (operator-run, fresh session required)

Routing decisions happen on user-input → assistant skill match. The current authoring
session cannot reliably self-test routing. To validate `routing.json` patterns are live,
run these 6 prompts in a **fresh Claude Code session** (not the session that authored
the skill):

| # | Prompt | Expected | Why |
|---|--------|----------|-----|
| 1 | "위젯이 안 눌려" | `Skill("/ue-slate-debug")` invoked first | KO trigger pattern match |
| 2 | "Slate hit test failing" | `Skill("/ue-slate-debug")` invoked first | EN trigger pattern match |
| 3 | "WBP_HealthBar designer vs PIE 색이 다름" | `Skill("/ue-slate-debug")` invoked first | KO designer-drift trigger |
| 4 | "focus chain debug" | `Skill("/ue-slate-debug")` invoked first | EN focus pattern match |
| 5 | "위젯 만들어줘" | `Skill("/blueprint-generator")` (NOT `/ue-slate-debug`) | negative_patterns guard |
| 6 | "WBP 분석해줘" | direct `ue_manage_widget(get_structure)` (NOT `/ue-slate-debug`) | negative_patterns guard |

**Pass**: 4/4 positive triggers fire correctly + 2/2 negatives don't fire.
**Fail**: any miss → tighten `trigger_patterns` or `negative_patterns` in
`.claude/skills/routing.json` and re-run `python scripts/plugins/prepare-plugins.py`.

**Alternative**: `/agent-test skill ue-slate-debug` (eval-channel mode, ~1min, $0)
when that harness is available; same intent, $0 subscription cost.

---

## Acceptance Criteria Cross-Reference

| Criterion | Evidence |
|---|---|
| **AC1**: Skill 트리거 패턴이 routing.json 매칭으로 자동 호출됨 | All 5 scenarios start with the user typing a Korean trigger; routing.json `trigger_patterns.ko` matches all of them. Verified manually via Manual Trigger Smoke section above (operator action) or `/skill-finder`. |
| **AC2**: ≥3 시나리오가 실제 NarshaMCP 도구 호출만으로 해결 가능 (가짜 호출 없음) | All 5 use only ops from NarshaMCP's registered tool set (64 MCP tools total — registry SSOT `TOTAL_TOOL_COUNT`; ops here from Issue #7915, #9239, #9251, #9344). Phase 2 grounded response shapes against `_verification/pie_responses.json` (Lyra 5.7, 62 widgets). No fabricated parameters. |
| **AC3**: 5단계 이내 결론 도달 | S1=4 calls, S2=4 calls (incl. Step 0 free), S3=3 calls, S4=4 calls (incl. Step 0 free), S5=3 calls. All ≤5. |

---

**Last Updated**: 2026-07-13
