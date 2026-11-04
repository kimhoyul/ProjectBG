# UE Diff — Examples

## 1. Blueprint Asset Comparison

```
User: "BP_Player_v1.uasset vs BP_Player_v2.uasset 에셋 비교"

→ ue_diff(operation="compare_assets", asset_a="BP_Player_v1.uasset", asset_b="BP_Player_v2.uasset")

Result: typed diff (nodes, variables, functions) + generic diff (names, imports, exports)
```

## 2. Niagara System Comparison

```
User: "NS_Confetti vs NS_Explosion 비교해줘"

→ ue_diff(asset_a="NS_Confetti.uasset", asset_b="NS_Explosion.uasset")

Smart routing detects asset_a/asset_b → compare_assets
Result: typed diff (emitters, parameters, renderers) + generic diff
```

## 3. Material Comparison with Type Hint

```
User: "M_Rock 두 버전 머테리얼 비교"

→ ue_diff(operation="compare_assets", asset_a="M_Rock_v1.uasset", asset_b="M_Rock_v2.uasset", asset_type="material")

Type hint overrides only the response's `asset_type` label. Native-class extraction still auto-detects the typed extractor. Result: typed diff (parameters, connections, nodes)
```

## 4. Same File Comparison (Zero Diff)

```
User: "이 에셋이 바뀌었는지 확인해줘"

→ ue_diff(asset_a="/path/to/BP_Player.uasset", asset_b="/path/to/BP_Player.uasset")

Result: total_changes=0, typed_changes=0, generic_changes=0
```

## 5. SVN Revision Comparison

```
User: "SVN 리비전 5948에서 5950 사이 변경사항 보여줘"

→ ue_diff(operation="compare_revision", revision_a="5948", revision_b="5950")

Auto-detects SVN from project root. Classifies each file:
- .uasset → compare_assets (typed+generic binary diff)
- .cpp/.h → text diff (line count)
- .ini → config hint
Result: per-file diff summaries with type classification
```

## 6. Git Commit Range Comparison

```
User: "git commit abc1234와 def5678 사이 변경 비교"

→ ue_diff(operation="compare_revision", revision_a="abc1234", revision_b="def5678", vcs_type="git")

Explicit vcs_type for Git. Uses `git diff --name-status` + `git show` for file extraction.
```

## 7. Revision Diff with File Filter

```
User: "리비전 5948~5950 사이 uasset만 비교"

→ ue_diff(operation="compare_revision", revision_a="5948", revision_b="5950", filter="*.uasset")

Only processes .uasset files, skips .cpp/.h/.ini.
```

## 8. Smart Mode — Asset Routing

```
User: "GA_Dash_old.uasset GA_Dash_new.uasset"

→ ue_diff(asset_a="GA_Dash_old.uasset", asset_b="GA_Dash_new.uasset")

Smart mode detects asset_a + asset_b → routes to compare_assets automatically.
```

## 9. Smart Mode — Revision Routing

```
User: "리비전 5948 변경점"

→ ue_diff(revision_a="5948")

Smart mode detects revision_a → routes to compare_revision.
revision_b defaults to working copy.
```

## 10. Exact-file BlueprintUE HTML Render

```
User: "이 Blueprint 파일을 재현 가능한 BlueprintUE HTML로 렌더해줘"

→ ue_diff(operation="render_blueprintue", asset="C:/Lore/Uploads/BP_Player.uasset")

Requirements:
- `asset` is an absolute path to an existing native Blueprint-family `.uasset`.
- Do not pass `format`, `project_root`, or `asset_type`.
- The operation is explicit-only; smart/auto never routes to it.

Legacy protocol result includes inline `html_content`. Protocol 2025-06-18
returns a `resource_link` plus `ui_resource_uri`; call `resources/read` for the
same deterministic HTML bytes. Source SHA-256, byte size, graph/node/pin/edge
counts, graph names, and provider version are included in the result.
```

## 11. Error Cases

```
User: "존재하지 않는 에셋 비교"

→ ue_diff(asset_a="/nonexistent/a.uasset", asset_b="/nonexistent/b.uasset")

Result: { "success": false, "error": "Asset file not found: /nonexistent/a.uasset" }

User: "VCS 없는 프로젝트에서 리비전 비교"

→ ue_diff(operation="compare_revision", revision_a="100")

Result: { "success": false, "error": "No VCS detected at project root. Provide vcs_type explicitly." }
```
