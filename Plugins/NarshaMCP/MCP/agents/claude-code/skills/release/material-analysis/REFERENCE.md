# Material Analysis — Reference

Cold-path detail for `/material-analysis`. The hot-path workflow lives in [SKILL.md](SKILL.md);
this file holds the Part 4 flow-stage-split example + the read-only stage-split algorithm
(Issue #14979) so SKILL.md stays under the progressive-disclosure word budget.

## Part 4: Flow Stage Split (흐름 단계 분할, Issue #14979)

> **Goal**: 노드가 수백 개인 머티리얼을 하나하나 안 보고, 그래프를 의미 단위 **단계(stage)**로 잘라
> "이 구간 = 이런 역할"로 설명한다. 읽기 전용 — 필요한 데이터(노드+연결 엣지)는 `get_nodes`에 전부 존재.

```text
M_Example 흐름 요약 — 노드 124개 → 단계 5개

[공용 입력부]
  단계 0 (노드 12개): Time + UV 스크롤 + 파라미터 주입 — 아래 모든 단계가 공유

[출력: BaseColor — 색감]
  단계 1 (노드 18개): 노이즈 텍스처 2장 샘플 → 교차 곱 — 내부 패턴 값을 구함
  단계 2 (노드 7개): Lerp 체인 + ColorA/ColorB — 이 구간 연산으로 색감이 결정됨

[출력: OpacityMask — 보임/안 보임]
  단계 3 (노드 15개): 프레넬 × 마스크 텍스처 — 어디가 보일지 결정

[출력: WorldPositionOffset — 모양]
  단계 4 (노드 12개): 사인파 정점 오프셋 — 이 구간을 통해 모양이 흔들림
```

분류 사전(출력 핀 → 역할): BaseColor/EmissiveColor → **색감**, OpacityMask/Opacity → **보임**,
WorldPositionOffset/Normal/Tangent → **모양**, Roughness/Metallic/Specular → **질감**.

출력은 다음 형식: `[출력: <핀> — <역할>] 단계 N (노드 K개): <구간 한 줄 설명>`.

## Stage-Split Algorithm (Step 4 detail)

읽기 전용 그래프 순회. 입력은 SKILL.md Step 1의 `nodes`(노드: guid·node_type·position·parameters·outputs +
연결 엣지 from_node/pin → to_node/pin). 추가 MCP 호출/파서 불필요.

```python
# 출력 핀 → 역할 분류 사전 (출력 핀의 의미가 곧 서브그래프의 역할 = 공짜 1차 분류)
OUTPUT_ROLE = {
    "BaseColor": "색감", "EmissiveColor": "색감",
    "OpacityMask": "보임", "Opacity": "보임",
    "WorldPositionOffset": "모양", "Normal": "모양", "Tangent": "모양",
    "Roughness": "질감", "Metallic": "질감", "Specular": "질감",
}

# 1) 출력 핀 역추적: 각 머티리얼 출력 핀에서 업스트림 서브그래프를 역방향(BFS) 수집
def upstream_subgraph(output_node, edges):
    seen, frontier = set(), [output_node]
    while frontier:
        n = frontier.pop()
        for e in edges:
            if e["to_node"] == n and e["from_node"] not in seen:
                seen.add(e["from_node"]); frontier.append(e["from_node"])
    return seen

# 2) 단계 절단: 절단점 후보 = 다입력 합류 노드(Lerp/Blend), TextureSample,
#    MaterialFunctionCall 경계, 파라미터 주입점. 절단점 사이 구간 = "단계".
# 3) 공용 구간 분리: 여러 출력 핀 서브그래프의 교집합(Time/UV 등) = "공용 입력부" 단계 0.
# 4) 구간 라벨링(LLM 프롬프트 레이어): 구간 내 노드 타입 시퀀스 + 파라미터 이름 + MF 이름으로
#    "이 구간은 무엇을 구함" 한 줄 생성. (보조) 노드 position 으로 공간 클러스터 교차 검증.
```

## Part 5: 기능 카탈로그 (Issue #16326)

> **Goal**: 복합 머티리얼을 **기능군**(=무엇을 조절하는 노브 묶음)으로 정리해 "어떤 파라미터가 → 무엇을 → 기본값/실측범위 →
> 제작자들이 실제로 어떻게 조합했고 → 어떻게 끄는가"를 한 표로 보여준다. 입력은 Phase 1 enriched `get_nodes`(노드/엣지) +
> Phase 2 `compare_mi_family`(MI 패밀리 파라미터 diff). **읽기 전용** — 두 도구 출력 필드만 합성, 추가 파서 없음.

### 입력 필드 (실측 확인된 출력만 사용)

`compare_mi_family(base_material=…)` → `comparison.<kind>`(kind = `scalar`/`vector`/`texture`/`static_switch`):

- `varied[]` = `{name, base_default, per_child:{<MI라벨>:값}}` — 자식들이 값이 갈리는 파라미터. inherited override는 `<inherited>` 센티넬.
- `common[]` = `{name, value, base_default}` — 모든 자식이 같은 유효값(`value`). **`common[]`엔 `per_child` 없음.**
- `base_default` = base 머티리얼 참조값. **`common[].value`(유효값)와 구분.** `static_switch`는 오프라인 메타데이터에 base default가
  없어 `base_default=null`/`base_defaults_available=false`인 경우가 흔함 → 기본값 칸은 'inherit/unknown'으로 렌더(허위 기본값 금지).
- `prefix_clusters` = 파라미터명을 언더스코어 첫 토큰으로 묶은 맵(kind 횡단 union). 예: `ScanLine_Width`+`ScanLine_Speed` → `"ScanLine"`.

`get_nodes` → 파라미터 노드 + 엣지(`from_node`/`to_node`). 단 **노드 NAME/title는 라이브 에디터에서만 채워짐**
(#16324 `dispatcher.rs:133-141`: 바이너리 .uasset은 node NAME/DefaultValue/name-based edge 불가). 오프라인은 GUID 엣지만.

### 합성 알고리즘 (열 → 출처)

```python
# 1) 기능군 = prefix_clusters. 각 군에 출력 핀 역할(Part 4 OUTPUT_ROLE) 부착:
#    군의 파라미터가 흘러드는 머티리얼 출력 핀을 get_nodes 엣지로 추적 → BaseColor=색감 / OpacityMask=보임 / Normal=모양 / Roughness=질감.
# 2) 담당 파라미터 = 군 내 파라미터명(scalar/vector/texture/static_switch).
# 3) 기본값 = comparison.<kind>.{common,varied}[].base_default. static_switch가 null이면 'inherit/unknown'.
# 4) 실측 범위 = varied[].per_child 값들의 min~max(패밀리 across). common[]은 단일 value.
# 5) 제작자 조합 = varied[].per_child 맵(어느 MI가 어떤 값 조합). <inherited>는 "부모값 그대로".
# 6) 끄는 법:
#    [offline-core, 항상 가능]
#      · static_switch off: comparison.static_switch.varied[].per_child(개별 MI가 false) + common[].value(전부 동일 off).
#      · scalar/vector 0·중립값: 해당 파라미터가 0인 MI 식별(= 그 기능 비활성).
#    [enriched-attribution, 라이브 전용 → 오프라인 graceful-degrade]
#      · "이 0값이 어느 출력역할을 죽이나"는 파라미터 노드를 get_nodes에서 찾아 Part 4 upstream_subgraph로 출력 핀 역추적.
#        노드 NAME이 필요(라이브 전용) → 오프라인에선 "param=0 → 기능 off" 사실만 보고, 출력역할 귀속은 "라이브 필요"로 표기.
```

### 출력 예시 (기능 카탈로그)

```text
M_HoloShield 기능 카탈로그 — 기능군 3개 / MI 패밀리 4종 (MI_Blue, MI_Crimson, MI_Green, MI_Red)

[기능군: TeamColor — 색감(BaseColor/EmissiveColor)]
  담당 파라미터: TeamColor(vector)
  기본값: (0.5, 0.5, 0.5)
  실측 범위(4종): R 0.1~0.9 / G 0.1~0.7 / B 0.1~0.9
  제작자 조합: MI_Blue=(0.1,0.2,0.9) · MI_Crimson=(0.9,0.1,0.2) · MI_Green=(0.1,0.7,0.2) · MI_Red=(0.9,0.1,0.1)
  끄는 법: 단색이므로 끄는 스위치 없음 — 중립값 (0.5,0.5,0.5)로 두면 무채색

[기능군: ScanLine — 보임(OpacityMask)]
  담당 파라미터: ScanLine_Intensity(scalar), ScanLine_Speed(scalar), bUseScanLine(static_switch)
  기본값: Intensity=0.8, Speed=1.5, bUseScanLine=inherit/unknown(base default 없음)
  실측 범위(4종): Intensity 0.0~1.0, Speed 1.5~3.0
  제작자 조합: MI_Crimson{Intensity=1.0,Speed=3.0}(강한 고속 주사선) · MI_Green{Intensity=0.0}(주사선 0)
  끄는 법(offline-core): bUseScanLine=false(MI_Green·MI_Red) → 기능군 전체 off / ScanLine_Intensity=0 → 효과만 0
  끄는 법(역할 귀속): Intensity=0이 OpacityMask(보임) 경로를 죽임 — 라이브 에디터에서 노드명 해석 시 확정(오프라인 graceful-degrade)

[기능군: Fresnel — 모양/질감(Normal·Roughness)]
  담당 파라미터: Fresnel_Power(scalar), Fresnel_Tint(vector)
  기본값: Power=2.0, Tint=(0.2,0.6,1.0)
  실측 범위(4종): Power 1.0~4.0
  제작자 조합: 4종 모두 Power만 조정, Tint는 <inherited>(부모값 유지)
  끄는 법(offline-core): Fresnel_Power=0 → 림 강조 제거(중립)
```

### MI 패밀리 diff 표 예시

`compare_mi_family.comparison`을 표로 평탄화. `<inherited>`=부모값 그대로, 빈칸 없음(정직 표기).

| 파라미터 | kind | base 기본값 | MI_Blue | MI_Crimson | MI_Green | MI_Red |
|---|---|---|---|---|---|---|
| TeamColor | vector | (0.5,0.5,0.5) | (0.1,0.2,0.9) | (0.9,0.1,0.2) | (0.1,0.7,0.2) | (0.9,0.1,0.1) |
| ScanLine_Intensity | scalar | 0.8 | 0.8 | 1.0 | 0.0 | `<inherited>` |
| ScanLine_Speed | scalar | 1.5 | 1.5 | 3.0 | `<inherited>` | `<inherited>` |
| bUseScanLine | static_switch | inherit/unknown | true | true | false | false |
| Fresnel_Power | scalar | 2.0 | 2.0 | 4.0 | 1.0 | 3.0 |

> 위 값은 형식 예시(illustrative). 실제 카탈로그는 `ue_cache_control(operation="regenerate_metadata")` 후
> `compare_mi_family(base_material="M_TeamColorBasic")`(자식 4종 `MI_Team*` 자동 발견) + `get_nodes`로 산출.
> 노드명 기반 출력역할 귀속이 필요하면 라이브 에디터 연결 시 full resolution(오프라인은 graceful-degrade).
