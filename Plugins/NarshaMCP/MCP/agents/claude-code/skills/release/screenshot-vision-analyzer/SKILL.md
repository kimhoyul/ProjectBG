---
name: screenshot-vision-analyzer
model: opus
description: "Automated PIE/viewport screenshot capture and Claude Vision analysis pipeline. Captures screenshots, displays via Read(), and provides natural language visual analysis. Triggers: '화면 분석', '스크린샷 분석', 'capture and analyze', 'visual check', 'show me the screen', 'PIE 실행해서 확인'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
allowed-tools:
  - Read
  - mcp__narshamcp__ue_editor_debug
  - mcp__narshamcp__ue_editor_automation
  - mcp__narshamcp__ue_check_health
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  issue: "#7911"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Screenshot Vision Analyzer

**Status**: Beta
**Issue**: #7911

---

## Purpose

Automated screenshot capture and Claude Vision analysis pipeline for Unreal Editor.
Orchestrates: optional PIE start -> timed capture -> `Read(file_path)` -> AI visual analysis.

The key insight: Claude Code's `Read` tool renders images natively. When Claude reads a screenshot file, it can see and analyze the image content using its vision capabilities.

---

## Workflow

### Phase 1: Parse User Intent

Determine capture parameters from user request:

| User Intent | Parameters |
|---|---|
| "화면 분석해줘" / "Analyze the screen" | Default: auto-detect PIE vs viewport |
| "PIE 실행해서 확인" / "Start PIE and check" | `start_pie=true, delay_ms=2000` |
| "3번 캡처해서 비교" / "Capture 3 times and compare" | `capture_count=3, interval_ms=1000` |
| "UI 문제 있는지 확인" / "Check for UI issues" | Default capture + UI analysis prompt |
| "고해상도로 캡처" / "High resolution capture" | `resolution=1920x1080, format=png` |

### Phase 2: Capture

Call `ue_editor_debug` with the `auto_capture_analyze` operation:

```python
# Basic capture (auto-detect PIE vs viewport)
ue_editor_debug(operation="auto_capture_analyze")

# Start PIE first, then capture
ue_editor_debug(operation="auto_capture_analyze", start_pie=true, delay_ms=3000)

# Multi-capture for comparison
ue_editor_debug(operation="auto_capture_analyze", capture_count=3, interval_ms=1000)

# Explicit PIE capture with JPEG
ue_editor_debug(operation="auto_capture_analyze", capture_target="pie", format="jpeg", quality=90)
```

The operation returns:
```json
{
  "captures": [
    { "file_path": "C:/Project/Saved/Screenshots/MCP/capture_20260416.png", "width": 1920, "height": 1080 }
  ],
  "analysis_hint": "Use Read(file_path) on each capture to view and analyze the image"
}
```

### Phase 3: View

For each capture in the response, use `Read` to display the image:

```
Read(file_path="C:/Project/Saved/Screenshots/MCP/capture_20260416.png")
```

Claude can now see the screenshot and analyze it visually.

### Phase 4: Analyze

Provide natural language analysis based on the user's request:

**General State Description**:
> Describe what you see in the viewport: scene composition, lighting, actors visible, overall state.

**UI Validation**:
> Check for UI layout issues: overlapping elements, text readability, widget alignment, missing elements.

**Visual Bug Detection**:
> Look for visual artifacts: z-fighting, missing textures, incorrect lighting, shadow issues, LOD pop-in.

**State Verification**:
> Verify that [specific element] is visible, positioned correctly, and in the expected state.

**Before/After Comparison** (multi-capture):
> Compare the captures chronologically. Identify what changed between frames.

---

## Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `start_pie` | bool | false | Start PIE before capturing |
| `delay_ms` | int | 2000 | Delay before first capture (ms, 0-30000) |
| `capture_count` | int | 1 | Number of captures (1-10) |
| `interval_ms` | int | 1000 | Delay between multi-captures (ms, 100-10000) |
| `capture_target` | string | "auto" | "auto" (detect PIE), "viewport", or "pie" |
| `format` | string | "png" | Image format: "png" or "jpeg" |
| `quality` | int | 75 | JPEG quality (1-100) |
| `resolution` | string | (native) | Resolution override (e.g., "1920x1080") |
| `purpose` | string | (none) | Description of why capture was taken |

---

## Examples

### Analyze current viewport
```
User: "화면 분석해줘"
-> ue_editor_debug(operation="auto_capture_analyze")
-> Read(file_path)
-> "The viewport shows a third-person character standing in an open field with..."
```

### Start PIE and check for visual bugs
```
User: "PIE 실행해서 시각적 버그 확인해줘"
-> ue_editor_debug(operation="auto_capture_analyze", start_pie=true, delay_ms=3000)
-> Read(file_path)
-> "I can see the following potential issues: 1) Z-fighting on the floor plane..."
```

### Multi-frame comparison
```
User: "3번 캡처해서 프레임 비교해줘"
-> ue_editor_debug(operation="auto_capture_analyze", capture_count=3, interval_ms=2000)
-> Read(capture[0].file_path), Read(capture[1].file_path), Read(capture[2].file_path)
-> "Comparing 3 frames over 4 seconds: Frame 1 shows... Frame 2 shows..."
```

---

## Error Handling

| Error | Cause | Recovery |
|-------|-------|----------|
| "No active editor viewport" | Editor not running or no level loaded | Check `ue_check_health()`, open a level |
| "No active PIE session" | PIE not running with `capture_target=pie` | Use `start_pie=true` or `capture_target=auto` |
| "PIE start failed" | Level has compilation errors | Fix errors first via `/ue-debug` |
| "Timeout" | Heavy level or too many captures | Increase `delay_ms`, reduce `capture_count` |
