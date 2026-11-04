---
name: ue-repeat
model: opus
description: "Purpose: 에디터 작업 녹화 → 재현 가능한 스킬 자동 생성. 트랜잭션 로그 기반으로 에디터 편집(배치/이동/프로퍼티/BP그래프)을 캡처하고, 재현 스크립트를 생성하며, 반복 실행 가능한 하위 스킬로 저장. Triggers: '리피트', '반복', '반복 스킬', '녹화', '에디터 녹화', '작업 반복', 'repeat', 'record editor', 'replay edits', 'save as skill', '스킬로 저장'."
compatibility: "Requires NarshaMCP MCP server (v0.9.8+). Editor must be running for start/stop/replay."
argument-hint: "start <label> | stop | replay <label> [--dry-run] [--verify] | batch <label> --assets A,B,C | save <label> [--parameterize \"의도\"] [--portable] | list"
metadata:
  version: "3.4.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  issue: "#8564, #8834, #25707"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# /ue-repeat — 에디터 작업 녹화 + 재현 + 스킬 자동 생성

**Issue**: #8564 Phase K + L-1, #8834
**Purpose**: 에디터에서 한 작업을 녹화하고, 나중에 동일한 작업을 자동으로 반복 실행하거나 **N개 에셋에 배치 적용**할 수 있는 스킬을 생성합니다.
**Coverage**: 🟢 capture 12/13 도메인 (92%) — Blueprint, Material, PCG, Niagara, Sequencer, ControlRig, StateTree/BT, Animation, Widget, GAS, DataTable, World/Level. **Replay execute** (Issue #25694): single-path handles `set_pin_value` / `connect_nodes` / `disconnect_nodes` / `set_object_property` / `delete_node` / `add_node` / `add_key`; dry-run lists any remaining unsupported ops.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST parse `$ARGUMENTS` and execute the corresponding command**.
> Do NOT just display documentation — actually run the workflow!

---

## Commands

### `start <label>` — 녹화 시작

에디터 트랜잭션 기록을 시작합니다. 그래프 에디터 편집이 있는 경우 스냅샷도 자동 캡처됩니다.

```python
# Step 1: 현재 열린 Blueprint 자동 감지 (선택적)
blueprints = []
# --blueprints 인자가 있으면 사용, 없으면 자동 감지 시도

# Step 2: 마커 시작
ue_manage_project_ops(
    operation="level_mark_transaction",
    action="start",
    label="<label>",
    blueprints=blueprints,
    graph_name="EventGraph",
)
```

**출력**:
```text
🔴 녹화 시작: "<label>"
에디터에서 작업한 후 `/ue-repeat stop`을 실행하세요.
트랜잭션은 자동으로 기록됩니다 (always-on).
```

### `stop` — 녹화 종료 + replay script 생성

활성 마커를 종료하고, 기록된 트랜잭션에서 재현 스크립트를 생성합니다.

```python
# Step 1: 활성 마커 확인
status = ue_manage_project_ops(operation="level_snapshot_status")
# → 활성 마커의 label 추출

# Step 2: 마커 종료 (스냅샷 diff 자동 계산)
ue_manage_project_ops(
    operation="level_mark_transaction",
    action="end",
    label=active_label
)

# Step 3: 재현 스크립트 생성 (default summary; --raw → full)
projection = "full" if "--raw" in args else "stats"
script = ue_manage_project_ops(
    operation="level_generate_replay_script",
    label=active_label,
    projection=projection,
)
```

**출력** (v3.0: 도메인별 통계 포함):

`level_generate_replay_script` 응답의 `stats.domain_counts` HashMap을 파싱하여 도메인별 통계를 표시합니다.

```text
⏹️ 녹화 종료: "<label>"

도메인별 통계:
  blueprint: 3개 | material: 2개 | level_editor: 1개

합계: Python N개 | MCP M개 | 스킵 K개

`/ue-repeat replay "<label>"` 로 재현하거나
`/ue-repeat save "<label>"` 로 반복 스킬로 저장하세요.
```

> **참고**: `domain_counts`가 비어있으면 (Python 전용 레벨 에디터 작업) 도메인별 통계 줄을 생략합니다.

### `replay <label> [--dry-run] [--verify]` — 녹화된 작업 재현

기록된 트랜잭션을 에디터에서 실행합니다.

```python
# --dry-run이면 미리보기만, --verify이면 실행 후 값 검증
dry_run = "--dry-run" in args
verify = "--verify" in args

script = ue_manage_project_ops(
    operation="level_generate_replay_script",
    label="<label>",
    projection="full" if "--raw" in args else "calls",
)

ue_manage_project_ops(
    operation="level_replay_transactions",
    label="<label>",
    dry_run=dry_run,
    verify=verify  # Phase L-2: 실행 후 read-back 검증
)
```

**혼합 녹화 (Issue #25703) — Hidden federation is skill-layer, not a Python fence:**
Walk `timeline[]` **in order** — it is the capture-ordered view (`python_script` /
`mcp_calls` each preserve order only *within* their own bucket, so replaying from
those two loses the interleaving; that is what the #25703 post-merge audit caught).
Emit `prelude` once first, then for each step:
- `{"kind":"python","lines":[…]}` → run those lines (the shared imports are in `prelude`).
- `{"kind":"mcp","call":{…}}` with `tool == "ue_federate_epic"` (the Hidden Epic federation
  proxy) → re-issue it:
  `operation=call_tool`, `toolset`/`tool` from `params`, and
  `tool_args=json.loads(params.tool_args)` when that field is a JSON string.
- any other `mcp` step → it belongs to the Rust executor's transaction range.

**Do NOT hoist every federation call ahead of `level_replay_transactions`.** A
recording that interleaved Editor work with Epic tools must replay interleaved, or
the result differs from what was recorded. The executor skips federation steps it
sees (`skip_reason=federate_skill_layer`) instead of failing `call_tool` as an
unknown op, so a contiguous transaction run can still be handed to it wholesale.
짝지어진 에디터 트랜잭션은 `skip_reasons.paired_transaction` 으로 접혀 이중 재생되지 않습니다.
**캡처 조건:** UnrealMCP는 반드시 그 Hidden proxy 경유. 직결 호출은 데몬이 보지 못해 조용히 빠집니다.

**`--dry-run` 출력** (v3.0: 도메인별 그룹핑):
dry-run 모드에서는 MCP 호출을 도메인별로 그룹화하여 표시합니다:
```text
▶️ 재현 미리보기: "<label>"

--- domain: blueprint ---
  1. set_pin_value: BP_Hero.EventGraph.BlendWeight = 0.75
  2. add_node: K2Node_IfThenElse (Branch)

--- domain: material ---
  3. set_object_property: M_Rock.Roughness = 0.8
  4. set_object_property: M_Rock.Metallic = 0.2

Python 액션: 2개 | MCP 호출: 4개 | 스킵: 0개
```

**`--verify` 플래그** (v3.1: 정확도 검증):
실행 후 각 `set_object_property` 결과를 read-back하여 expected vs actual 비교:
```text
▶️ 재현 + 검증: "<label>"
  성공: N개 | 실패: M개

  검증 결과:
    ✅ Roughness: expected=0.8, actual=0.8
    ❌ Metallic: expected=0.5, actual=0.3
    ⚠️ BaseColor: read-back 실패 (object not found)
    ➖ keyframe:Track@frame30: 확인 불가 (read-back 경로 없음)

  정확도: 90% (9/10 matched, 1 unreadable, 1 unverifiable)
```

> **4-버킷 판정 (Issue #25697)**: `matched`(확인됨) / `mismatched`(다름) / `unreadable`(읽기 실패) /
> **`unverifiable`**(애초에 조회 경로가 없어 판정 자체를 안 함 — `verification_mode: "call-status-only"`).
> 정확도는 `matched / (matched + mismatched)`이며 뒤 두 버킷은 **분자·분모 어디에도 들어가지 않는다**.
> 검사 근거가 0건이면 `accuracy` 키 자체가 생략된다 — 아무것도 확인하지 않은 실행이 100%로 보이지 않게.
> `total_checked`는 검증 대상 4개 op(`set_object_property`/`connect_nodes`/`delete_node`/`add_key`)만
> 포함하며, 적용된 스텝 전체가 아니다.
>
> **참고**: `--verify`는 `--dry-run`과 함께 사용 불가. 실행 모드에서만 작동합니다.
> Guid/내부 프로퍼티(LightingGuid, ParameterStateId)는 자동 스킵됩니다.

**제거됨**: `--retry`는 `level_replay_transactions`가 받지 않는 인자여서 문서에서 철회되었습니다 (#25696).

**실행 출력**:
```text
▶️ 재현 완료: "<label>"
  성공: N개 | 실패: M개 | 연쇄 스킵: K개
```

### `save <label> [--parameterize "<의도>"] [--portable]` — 반복 스킬로 저장

녹화된 작업을 독립 실행 가능한 Claude Code 스킬로 저장합니다.

**`--parameterize` 없이** (기본): 녹화 그대로 재현하는 스킬 생성.

**`--parameterize "<의도>"` 포함**: 사용자 의도를 분석하여 파라미터화된 스킬 생성.
- `repeat-intent-analyzer` 서브에이전트를 호출하여 각 프로퍼티를 분류:
  - `fixed`: 녹화된 값 그대로 (사용자 미언급)
  - `param`: 스킬 인자로 받기 ("내가 말한걸 넣어")
  - `ask`: 실행 시 사용자에게 질문 ("물어봐")

**`--portable` 포함** (v3.3.0, Phase L-4): 에셋 경로도 파라미터로 추출하여 **다른 프로젝트에서 실행 가능**한 스킬 생성.
- `extract_asset_paths()` (Rust) 로 에셋 경로를 역할별 분류:
  - **Primary** (편집 대상) → `$TARGET_ASSET` 인자 (필수)
  - **Spawn** (스폰 클래스) → `$SPAWN_CLASS` 인자 (선택)
  - **Supporting** (텍스처 등) → `fixed` (엔진 공통 에셋)
- 생성된 스킬에서 `substitute_asset_paths()` 로 실행 시 경로 치환

**사용 예시**:
```bash
# 프로퍼티 + 에셋 경로 모두 파라미터화
/ue-repeat save "머티리얼 세팅" --parameterize "태그는 인자로" --portable

# 다른 프로젝트에서 실행
/repeat-material-setup --target-asset /Game/OtherProject/MI_Stone --tag "Enemy"
```

**혼합 타임라인 save (Issue #25703):** 스크립트에 `ue_federate_epic` 스텝이 있으면 생성되는
`repeat-*` SKILL.md 는 `timeline[]` 을 **기록된 순서 그대로** 훑도록 씁니다 — federation 을 앞으로
몰지 않습니다. `prelude` 를 먼저 한 번 방출한 뒤, python 스텝은 그 줄들을 실행하고 federation 스텝은
`ue_federate_epic(call_tool)` 로 재발행하며, 연속된 트랜잭션 구간만 `level_replay_transactions` 에
넘깁니다(§ 혼합 녹화 참조 — 두 절의 지시는 동일합니다). 스킬 상단에 전제를 명시하세요:

```text
## Prerequisites
- Unreal Editor running (same engine as recording)
- Epic ModelContextProtocol server: `-ModelContextProtocolStartServer`
- NarshaMCP `ue_federate_epic` enabled (`UECODEGEN_TOOL_ENABLE` includes it if gated)
- Target Epic tool exists on that UE version
```

**동작**:
1. `level_generate_replay_script(label=<label>, projection="full")` 로 스크립트와 `asset_references` 읽기
2. `--parameterize`이면 → `repeat-intent-analyzer` 서브에이전트 호출:
   - MCP 호출 목록 + 사용자 의도 + 응답의 `asset_references` 전달
   - 각 프로퍼티를 fixed/param/ask로 분류
   - 파라미터화된 SKILL.md 생성
3. `--parameterize` 없으면 → 녹화 그대로 SKILL.md 생성
4. portable 여부와 무관하게 응답의 `asset_references`를 생성 스킬의
   `metadata.asset_references`에 그대로 기록합니다. 참조가 없으면 빈 배열 `[]`을 기록합니다.
5. `.claude/skills/repeat-<label>/SKILL.md` 파일 생성
6. `routing.json`에 트리거 자동 등록

`asset_references`는 서버 `extract_asset_paths()`의 결과가 SSOT입니다. 본문이나 생성된
Python 문자열에서 경로를 다시 정규식 추출하지 마세요. 각 행은 최소한 `path`, `name`,
`role` (`primary|supporting|spawn`), `occurrences`, `param_name`을 보존합니다.

**생성되는 스킬 예시** (`.claude/skills/repeat-encounter-setup/SKILL.md`):
```yaml
---
name: repeat-encounter-setup
description: "자동 생성된 반복 스킬: 인카운터 세팅 (2026-04-14 녹화). 에디터에서 NPC 배치 + SM Blueprint 편집 작업을 재현합니다."
argument-hint: "--dry-run | --execute (default)"
metadata:
  version: "1.0.0"
  recorded_at: "2026-04-14T12:00:00Z"
  source_label: "인카운터 세팅"
  generated_by: "ue-repeat"
  asset_references:
    - path: "/Game/Blueprints/BP_Enemy"
      name: "BP_Enemy"
      role: "primary"
      occurrences: 3
      param_name: "$TARGET_ASSET"
---

# Repeat: 인카운터 세팅

## 포함된 작업
- PointLight 배치 (500, 500, 200) rotation P=0 Y=45 R=0
- NodeIconSize = (X=42, Y=99)
- bDefaultToParallel = True
- BP graph: 2 pin changes, 1 connection

## Auto-Execution
level_replay_transactions(label="인카운터 세팅", dry_run=$DRY_RUN)
```

**routing.json 자동 등록**:
```json
{
  "skill": "repeat-encounter-setup",
  "triggers": ["인카운터 세팅", "encounter setup", "repeat encounter"]
}
```

### `batch <label> --assets <list> [--source <path>] [--dry-run]` — 녹화를 N개 에셋에 배치 적용

녹화된 작업을 여러 대상 에셋에 한 번에 적용합니다. 에셋 경로를 자동 치환합니다.

```python
# --assets: 쉼표 구분 대상 에셋 경로
# --source: 녹화 시 편집한 에셋 (미지정 시 자동 감지)
# --dry-run: 미리보기만

target_assets = ["<asset1>", "<asset2>", ...]  # parse from --assets
source_asset = "<source>"  # from --source, or None for auto-detect
dry_run = "--dry-run" in args

ue_manage_project_ops(
    operation="level_replay_transactions",
    label="<label>",
    target_assets=target_assets,
    source_asset=source_asset,
    dry_run=dry_run,
    include_full_scripts=("--raw" in args),
)
```

**사용 예시**:
```bash
# 머티리얼 설정을 3개 에셋에 적용
/ue-repeat batch "머티리얼 세팅" --assets /Game/Materials/MI_Stone,/Game/Materials/MI_Dirt,/Game/Materials/MI_Sand

# 소스 명시 + 미리보기
/ue-repeat batch "NPC 세팅" --assets /Game/BP_Boss,/Game/BP_Minion --source /Game/BP_Enemy --dry-run
```

**출력** (`--dry-run`):
```text
🔄 배치 미리보기: "<label>"
소스: /Game/Materials/MI_Rock (자동 감지)
대상: 3개 에셋

  [1/3] /Game/Materials/MI_Stone
    - set_object_property: MI_Stone.Roughness = 0.8
    - set_object_property: MI_Stone.Metallic = 0.2

  [2/3] /Game/Materials/MI_Dirt
    - set_object_property: MI_Dirt.Roughness = 0.8
    - set_object_property: MI_Dirt.Metallic = 0.2

  [3/3] /Game/Materials/MI_Sand
    ...
```

**출력** (실행):
```text
🔄 배치 완료: "<label>"
  대상: 3개 | 성공: 3개 | 실패: 0개
```

> **참고**: 소스 에셋 자동 감지는 녹화에서 가장 빈번하게 등장하는 에셋 경로를 사용합니다.
> 복수 에셋이 녹화에 포함된 경우 `--source`를 명시하세요.
>
> **Batch op limits (Issue #25694)**: batch execute supports
> `set_pin_value`, `set_object_property`, `connect_nodes`, `delete_node`, `add_node`.
> `disconnect_nodes` / `add_key` (and material-only disconnect) still need single-path
> execute or skill-layer dispatch via `call.tool`.

### `list` — 저장된 반복 스킬 목록

```python
# 1. 활성 마커 확인
ue_manage_project_ops(operation="level_snapshot_status")

# 2. 저장된 repeat 스킬을 스캔하고 각 SKILL.md frontmatter를 읽기
skill_files = Glob(".claude/skills/repeat-*/SKILL.md")
# Read each file; use only metadata.asset_references[].path.

# 3. 모든 추적 경로를 중복 제거하고 최대 512개씩 오프라인 검사
checks = ue_manage_project_ops(
    operation="level_check_asset_paths",
    asset_paths=unique_saved_paths,
)

# 4. checks.assets를 원래 스킬별로 매핑
# present|builtin만 있으면 healthy, redirected|missing|invalid가 하나라도 있으면 stale.
# metadata.asset_references 필드 자체가 없는 구버전 스킬은 untracked.
```

`level_check_asset_paths`는 에디터와 트랜잭션 로그 없이 프로젝트 디스크를 검사합니다.
`/Game/Foo/Bar`는 `Content/Foo/Bar.uasset` 또는 `.umap`으로 확인하며 `/Engine`, `/Script`
참조는 `builtin`으로 표시합니다. `.uasset`이 UE `ObjectRedirector`이면 옛 경로를
`redirected`/stale로 표시하고, 바이너리 참조에서 목적지 후보를 찾으면 `redirect_to`로 제공합니다.
512개를 넘으면 여러 호출로 나누세요.

`redirected`의 `redirect_to`는 휴리스틱으로 찾은 **후보**입니다. 에디터 Asset Registry에서
`DestinationObject`를 확인하기 전에는 스킬 파일을 자동 수정하지 마세요. 확인 뒤 저장 경로 갱신
후보로 안내할 수 있습니다. `missing`은 목적지를 확정할 수 없으므로 스킬 파일을 자동 수정하지 말고
**재녹화 또는 저장 경로 갱신 필요**라고 안내하며, 에디터/Asset Registry를 사용할 수 있을 때
redirector 확인을 후속 제안으로 표시합니다.

**출력**:
```text
📋 반복 스킬 목록:
  🔴 활성 녹화: "인카운터 세팅" (진행 중)

  저장된 스킬:
  1. ✅ /repeat-encounter-setup — 인카운터 세팅 (2026-04-14)
  2. ⚠️ /repeat-npc-dialogue — stale: /Game/BP_Enemy 없음
     재녹화 또는 저장 경로 갱신 필요 (에디터에서 redirector 확인 가능)
  3. ❔ /repeat-legacy — untracked: asset_references 메타데이터 없음
```

---

## When to Use

- 에디터에서 반복적인 레벨 디자인 작업을 자동화하고 싶을 때
- "아까 한 작업을 다른 레벨에서도 똑같이 해줘"
- 팀원에게 에디터 작업 절차를 스킬로 공유하고 싶을 때
- 프로토타이핑: 빠르게 배치 → 녹화 → 다른 곳에 재현

## 노이즈 프로퍼티 필터 (v3.0)

`stop`과 `replay --dry-run` 출력 시, 기본 모드에서 다음 **에디터 내부 프로퍼티**는 출력에서 숨깁니다.
`--raw` 플래그 사용 시 모든 프로퍼티가 표시됩니다.

> **중요**: 필터링은 **출력 표시에만** 적용됩니다. 실제 replay 실행 시에는 모든 프로퍼티가 포함됩니다.
> 내부 프로퍼티가 필요한 디버깅이나 특수한 경우 `--raw`를 사용하세요.
> `--raw` on `stop` / `replay` maps to `projection="full"`; on `batch --dry-run` it sets `include_full_scripts=true`.
> Generate default is `projection="stats"` (Issue #25700) — `save` must pass `projection="full"`.

**숨겨지는 프로퍼티 패턴**:
- **에디터 전용 컴포넌트**: `BillboardComponent*` 전체
- **내부 ID**: `LightGuid`, `OriginalLightGuid`, `PreviewShadowMapChannel`
- **시각화**: `Sprite`, `SpriteInfo`, `bIsVisualizationComponent`, `bIsEditorOnly`, `bIsScreenSizeScaled`
- **자동 참조**: `RootComponent`, `LightComponent`, `PointLightComponent`, `StaticMeshComponent` (값이 컴포넌트 이름과 동일)
- **기본값**: `OpacityMaskRefVal`, `AlwaysLoadOnClient`, `IntensityUnits`, `AttachParent`

**사용법**:
```
/ue-repeat stop              ← 필터링된 출력 (기본)
/ue-repeat stop --raw        ← 모든 프로퍼티 표시
/ue-repeat replay <label> --dry-run --raw  ← raw dry-run
```

## v3.0 통계 강화

`stop` 출력에 추가된 통계 필드:
- `domain_mcp_counts`: 도메인별 실제 MCP 호출 수 (entries보다 높을 수 있음)
- `skip_reasons`: 스킵 사유별 카운트 (`no_spawn_info`, `metasound`, `no_values`, `empty_path`, `unhandled_event`)
- `deleted` 이벤트 → `destroy_actor` Python 코드 자동 생성
- `asset_created` → `object_changed` 결합 시 spawn 자동 재구성

## Known Limitations (Issue #8834)

### UE Python `set_editor_property()` — Transaction Capture 불가

UE Python의 `set_editor_property()`는 `PreEditChange`/`PostEditChange`만 호출하고 `GEditor->BeginTransaction()`을 호출하지 않습니다. 결과적으로 `OnTransactionStateChanged` 델리게이트가 발동하지 않으며, `FCoreUObjectDelegates::OnObjectPropertyChanged`도 Python 바인딩에서 브로드캐스트되지 않습니다.

**영향받는 API** (모두 `ScopedEditorTransaction` 없이 호출 시):
- `obj.set_editor_property("prop", value)` — 컴포넌트, 액터 모두
- `actor.set_actor_location()`, `set_actor_rotation()`, `set_actor_scale3d()`
- `actor.set_actor_hidden_in_game()`
- `modify()` 단독 (ScopedEditorTransaction 없이)

**캡처되는 패턴**:
```python
# ✅ 이 패턴만 JSONL에 기록됨
with unreal.ScopedEditorTransaction("Replay: Edit"):
    obj.modify()
    obj.set_editor_property("prop", value)

# ❌ 이 패턴은 기록 안 됨
obj.set_editor_property("prop", value)
```

**replay 스크립트 생성 시**: H3 Python fallback 경로는 자동으로 `ScopedEditorTransaction + modify()` 래핑 생성 (commit `f828f591`).

**에디터 UI 작업**: 100% 캡처됨 — UE가 자동으로 `FScopedTransaction`으로 래핑.

**MCP RC 함수**: 100% 캡처됨 — 모든 C++ 핸들러에 `FScopedTransaction + Modify()` 명시적 래핑.

## When NOT to Use

- PIE 게임플레이 입력 녹화 → `/ue-test-record`
- 성능 프로파일 재생 → `/ue-perf-replay`
- C++ 코드 생성 → `/ue-scaffold` 또는 `/code-generation`
- 테스트 시나리오 → `/ue-test-scenario`

---

## 기술 기반

| 기능 | MCP Operation | 구현 |
|------|--------------|------|
| 트랜잭션 로깅 | always-on (CUModule.cpp) | ExportText 값 + spawn info + scale + context 필드 |
| 도메인 라우팅 | `DomainKind` enum | 12개 에디터 컨텍스트 → 올바른 replay 경로 |
| 마커 관리 | `level_mark_transaction` | ActiveMarker + 스냅샷 자동 캡처 |
| 스크립트 생성 | `level_generate_replay_script` | Python + MCP 하이브리드 + `domain_counts` |
| 재현 실행 | `level_replay_transactions` | #25694: pin/connect/disconnect/property + BP delete_node/add_node + sequencer add_key; material connect via Python |
| 그래프 diff | `broadcast_consumer` | 도메인 인식 — BP/Anim만 스냅샷, 나머지 ExportText |
| 노드 추가 | `GraphDiff.added_nodes` | class/title 보존 → `add_node` → `CreateBlueprintNode` RC |
| 프로퍼티 복원 | `SetObjectPropertyByPath` | FScopedTransaction + Modify() (sub-object 캡처) |
| AnimNotify | *(capture only)* | transaction_log generator has **no** anim_notify → MCP ops yet (#25694 honesty) |
| Material 그래프 | connect via Python / property ExportText | material `delete_node` still skill-layer |

### 도메인별 replay 라우팅 (Phase K → v3.0)

| 도메인 | 컨텍스트 | replay 경로 |
|--------|---------|------------|
| Blueprint/Animation | `BlueprintEditor`/`AnimationBlueprintEditor` | `set_pin_value` + `add_node` + `delete_node` |
| Material | `MaterialEditor` | `set_object_property` (ExpressionCollection 배열). 순서: Add → Connect → Modify |
| PCG/Niagara/Sequencer/ControlRig/StateTree/BT | 각 도메인 Editor | `set_object_property` (ExportText) |
| AnimNotify | `AnimationBlueprintEditor` + anim_notifies_changed | **Not replay-executed yet** (no generator MCP ops; do not claim Move/Add/Remove RC replay) |
| MetaSound | `MetaSoundEditor` | 스킵 (🔴 #8833) |

### v3.0 AnimNotify 파라미터화

`--parameterize` 사용 시 AnimNotify 녹화에서 다음 필드를 자동 추출합니다:
- `time` → `$TRIGGER_TIME` (param, 보통 실행 시 변경)
- `class_name` → `fixed` (AnimNotify 클래스는 고정)
- `notify_index` → `fixed` (순서 보존)

예시:
```
/ue-repeat save "히트 이펙트" --parameterize "트리거 타임은 인자로 받아"
```
→ 생성된 스킬에서 `--trigger-time 0.5` 으로 실행 가능

### v3.0 Material 노드 순서 보존

Material 그래프 편집 시 K-4 RC 함수의 호출 순서를 보존합니다:
1. `AddMaterialNode` — 노드 생성
2. `ConnectMaterialNodes` — 핀 연결
3. `ModifyMaterialNode` — 파라미터 설정

JSONL에 기록된 순서대로 재현되며, 이 순서가 깨지면 연결 실패가 발생할 수 있습니다.
