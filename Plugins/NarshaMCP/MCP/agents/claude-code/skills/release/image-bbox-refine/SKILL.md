---
name: image-bbox-refine
model: opus
description: "Purpose: stage1_analysis.json bbox closed-loop visual refinement -> pixel-accurate (bbox-refinement-checker -> re-render -> converge). Triggers: 'bbox 보정', 'bbox refine'. (routing.json is SSOT.)"
allowed-tools:
  - Read
  - Write
  - Edit
  - Bash
  - Task
  - AskUserQuestion
argument-hint: "--workflow-id <wf_id> [--max-iterations N=10] [--consecutive-clean K=3] [--auto-apply] [--zoom-mode] [--zoom-pad-pct N=4] [--render-only] [--self-test] [--force]"
metadata:
  version: "0.5.0"
  author: "Next-Stage-Inc"
  cluster: "design-from-image"
  added: "2026-04-30"
  parent_issue: "#9998"
  changelog: |
    0.5.0 (2026-05-14): evals/trigger-grading.json (12 cases, 100% routing F1) + _verification/
                         (op_responses.json + trigger_test.py + README.md) per Empirical Grounding
                         (Issue #9965). Phase B.2 /ns-review round 3.
    0.4.0 (2026-05-01): F-010 mitigation — domain routing gate. Skill skips with informative
                         message when stage1.domain_distribution.ui_mockup < 0.5 OR
                         len(components) == 0. system_diagram / concept_art / mixed-non-UI
                         workflows no longer waste a Vision call. Validated on
                         wf_20260501_34bf6c (system_diagram, components=[]) — N=2 D34 pilot.
                         Override: --force flag (escape hatch for edge cases).
    0.3.0 (2026-04-30): two-tier mode — per-component zoom-inspect via crop_for_zoom.py
                         catches drift the full-frame loop misses (rect-vs-circle, sub-row gaps,
                         text-row miss, etc.). Validated on wf_20260429_d2e9a7 D34 pilot.
    0.2.0 (2026-04-30): convergence rule = N consecutive clean (default 3), not 1. --max-iterations
                         default raised 5→10 to give 3-streak room.
    0.1.0 (2026-04-30): initial skill (full-frame closed-loop only, single-clean termination).
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# /image-bbox-refine

`stage1_analysis.json` 의 bbox 좌표를 closed-loop visual refinement 로 보정합니다.

기존 `/image-to-design-spec` 의 Stage 1 결과는 9-grid coarse(top-left/center/right ...) 정확도. Stage 3 의 `ui_mockup → bbox→Slot` 변환은 픽셀 단위 정밀도를 요구하므로, 이 Skill 이 그 갭을 메웁니다.

---

## Arguments

| Arg | Default | Description |
|---|---|---|
| `--workflow-id <wf_id>` | (required) | 대상 workflow 디렉토리 (`.claude/state/design-workflow/<wf_id>/`) |
| `--max-iterations <N>` | `10` | closed-loop 수렴 한도 (3-연속-clean 충족까지 여유) |
| `--consecutive-clean <K>` | `3` | **수렴 정의**: K번 연속 verdict=clean 이어야 종료. 1번 clean 으로는 부족 — Vision 노이즈/일시 false negative 흡수 |
| `--auto-apply` | `true` | 매 iteration 사용자 확인 없이 적용 (false 시 AskUserQuestion 분기) |
| `--threshold <pct>` | `5` | 평균 위치 오차 < threshold% 시 수렴 보강 신호 (clean 판정의 보조 지표) |
| `--render-only` | flag | 보정 없이 현재 bbox 만 PNG 로 렌더 (디버그) |
| `--self-test` | flag | 더미 이미지 + 합성 결함으로 dry-run 검증 (CI 용) |
| `--force` | flag | F-010 도메인 라우팅 gate 우회 (escape hatch — system_diagram 도메인이지만 components 가 surface 됐다고 우길 때) |

---

## 실행 흐름

### Phase 0: 환경 검증

```bash
state_dir=".claude/state/design-workflow/${wf_id}"
test -f "${state_dir}/stage1_analysis.json" || die "stage1_analysis.json not found"
test -f "${state_dir}/manifest.json" || die "manifest.json not found"

# Source image path (manifest 에서 추출)
src_img=$(jq -r '.image_inputs[0].source_path' "${state_dir}/manifest.json")
test -f "${src_img}" || die "source image not accessible"

# bbox_refinement/ sub-dir 생성 (idempotent)
mkdir -p "${state_dir}/bbox_refinement"
```

### Phase 0.5 (v0.4+): 도메인 라우팅 gate (F-010 mitigation)

bbox refinement 은 **ui_mockup 도메인 + components 가 있는 경우에만** 의미 있음. system_diagram (graph topology only) / concept_art (raster art) / mixed-non-UI 는 components 자체가 부재하거나 N/A. 이런 워크플로우는 Vision 호출을 낭비하므로 **gate 로 차단**.

```python
import json
analysis = json.load(open(f"{state_dir}/stage1_analysis.json"))

# Domain check
domain_dist = analysis.get("domain_distribution", {})
ui_share = domain_dist.get("ui_mockup", 0.0)

# Components check (image[0] 기준)
images = analysis.get("images", [])
components = images[0].get("vision_extract", {}).get("components", []) if images else []

# Gate decision
if not args.force:
    if ui_share < 0.5:
        # 도메인이 ui_mockup 이 아님
        dominant = max(domain_dist.items(), key=lambda kv: kv[1], default=("unknown", 0))
        print(f"⏭️  /image-bbox-refine SKIP — 도메인 라우팅 gate (F-010)")
        print(f"   - dominant domain: {dominant[0]} ({dominant[1]:.0%})")
        print(f"   - ui_mockup share: {ui_share:.0%} < 0.5 threshold")
        print(f"   - bbox refinement 은 ui_mockup 도메인 전용 (graph topology / raster art 에는 N/A)")
        print()
        print(f"   다음 단계: /image-to-design-spec --workflow-id {wf_id} --stage 2")
        print(f"   강제 실행: /image-bbox-refine --workflow-id {wf_id} --force")
        # manifest 에 skip 기록 (D34 telemetry 용)
        write_manifest_skip_record(state_dir, reason="domain_not_ui_mockup", domain=dominant[0])
        exit(0)

    if len(components) == 0:
        print(f"⏭️  /image-bbox-refine SKIP — 도메인 라우팅 gate (F-010)")
        print(f"   - components count: 0")
        print(f"   - bbox refinement 은 components > 0 인 경우에만 의미 있음")
        print(f"   - system_diagram 도메인은 diagram_nodes / diagram_edges 사용 (bbox refinement N/A)")
        print()
        print(f"   다음 단계: /image-to-design-spec --workflow-id {wf_id} --stage 2")
        print(f"   강제 실행: /image-bbox-refine --workflow-id {wf_id} --force")
        write_manifest_skip_record(state_dir, reason="no_components", components_count=0)
        exit(0)

print(f"✅ Phase 0.5 gate PASS — domain=ui_mockup ({ui_share:.0%}), components={len(components)}")
```

**Gate 조건 요약**:

| 조건 | 동작 | 사유 |
|---|---|---|
| `ui_mockup share ≥ 0.5` AND `len(components) > 0` | **PROCEED** to Phase 1 | bbox refinement 의미 있음 |
| `ui_mockup share < 0.5` (other domain dominant) | **SKIP** with informative message | system_diagram / concept_art / mixed-non-UI |
| `len(components) == 0` | **SKIP** with informative message | bbox refinement 대상 부재 |
| `--force` flag | **PROCEED** unconditionally | escape hatch (테스트 / 엣지케이스) |

**Skip 기록 (manifest)**:
```jsonc
{
  "stages": {
    "bbox_refinement": {
      "status": "skipped",
      "skip_reason": "domain_not_ui_mockup" | "no_components",
      "gate_version": "F-010 v0.4",
      "skipped_at": "<ISO>"
    }
  }
}
```

**검증 케이스**:
- 1st pilot wf_20260429_d2e9a7 (mixed=1.0, ui_mockup component 10개) → **PROCEED** (legacy 검증)
- 2nd pilot wf_20260501_34bf6c (system_diagram=1.0, components=[]) → **SKIP** (F-010 신규 검증 — 이 도메인에서 bbox refinement 호출 자체가 없었음)

### Phase 1: 초기 overlay 렌더 (iter0 = baseline)

```bash
python .claude/skills/image-bbox-refine/scripts/render_overlay.py \
  --stage1 "${state_dir}/stage1_analysis.json" \
  --source "${src_img}" \
  --out "${state_dir}/bbox_refinement/overlay_iter0.png"
```

`--render-only` flag 시 여기서 종료.

### Phase 2: closed-loop 반복 — N 연속 clean 수렴

**핵심 변경 (Skill v0.2 +)**: 1번 clean 으로 종료하지 않음. K = `--consecutive-clean` (기본 3) 회 **연속** 으로 verdict=clean 이 나와야 수렴 인정. 중간에 dirty 가 끼면 카운터 리셋.

```text
clean_streak = 0
for iter in 1..max_iterations:
  # (a) bbox-refinement-checker SubAgent 호출 (격리)
  Task(
    subagent_type="bbox-refinement-checker",
    prompt=f"""
workflow_id: {wf_id}
state_dir: {state_dir}
iteration: {iter}
source_image_path: {src_img}
current_overlay_png: {state_dir}/bbox_refinement/overlay_iter{iter-1}.png
current_stage1_analysis_json: {state_dir}/stage1_analysis.json
schema_path: .claude/skills/image-bbox-refine/schemas/refinement.schema.json
clean_streak_so_far: {clean_streak}      # SubAgent 가 자체 strictness 조정 가능
required_clean_streak: {consecutive_clean}
"""
  )

  refinement = Read(f"{state_dir}/bbox_refinement/refinement_iter{iter}.json")

  # (b) streak 갱신
  if refinement.verdict == "clean":
    clean_streak += 1
    if clean_streak >= consecutive_clean:
      print(f"Converged after {clean_streak} consecutive clean (iter{iter}).")
      break
    print(f"clean ({clean_streak}/{consecutive_clean}) — continuing for confirmation iters")
    # clean 이어도 corrections 가 비어있을 가능성 큼 → 다음 iter 도 그냥 verify pass
    # (재렌더 step 은 stage1 가 안 바뀌면 skip 가능 — 최적화는 후속)
  else:
    clean_streak = 0   # 리셋
    print(f"verdict={refinement.verdict} — streak reset to 0")

  # (c) 사용자 확인 (auto_apply=false 또는 structural_changes 존재 시)
  if not auto_apply or len(refinement.structural_changes) > 0:
    response = AskUserQuestion(...)
    if response != "apply":
      print("aborted by user")
      break

  # (d) corrections 적용 (verdict != clean 인 경우만 실제 변경)
  if refinement.verdict != "clean" or len(refinement.discrepancies) > 0:
    Bash:
      python .claude/skills/image-bbox-refine/scripts/apply_corrections.py \
        --stage1 ${state_dir}/stage1_analysis.json \
        --refinement ${state_dir}/bbox_refinement/refinement_iter{iter}.json \
        --snapshot-to ${state_dir}/stage1_analysis.iter{iter}.json

    # (e) 재렌더 (변경 발생 시에만)
    Bash:
      python .claude/skills/image-bbox-refine/scripts/render_overlay.py \
        --stage1 ${state_dir}/stage1_analysis.json \
        --source ${src_img} \
        --out ${state_dir}/bbox_refinement/overlay_iter{iter}.png

  if iter == max_iterations:
    print(f"Hit max_iterations={max_iterations}. Final clean_streak={clean_streak}/{consecutive_clean}.")
    if clean_streak < consecutive_clean:
      print("주의: 3-연속-clean 미달성. 사용자 수동 검토 권장.")
```

**왜 N 연속?** 단발 clean 은 SubAgent 가 다음 iter 에서 시각 노이즈 / 한 widget 의 미세 drift 발견으로 다시 minor_drift 가 될 수 있음. 같은 stage1 상태에 대해 K 회 연속 clean 이 나와야 진짜 수렴. test-iterate skill 의 N-consecutive-pass 패턴과 동일한 발상.

**clean iter 비용**: 보정 없으므로 apply/render 스킵, Vision 호출만 발생 (~$0.5 / iter). 3 연속 clean 추가 비용 ~$1.5.

### Phase 2.5 (v0.3): two-tier zoom-inspect (선택, --zoom-mode 시 활성)

**문제**: 전체 이미지 1장에 대한 Vision 호출은 effective resolution이 낮음. 9-grid coarse 정렬은 OK 지만 픽셀 단위 (사각 vs 원형, sub-row gap, text-row 누락 등)는 full-frame view 에서 surface 안 됨. wf_20260429_d2e9a7 파일럿에서 user 가 직접 zoom 스크린샷 보여줘서 발견.

**해법**: 컴포넌트별 crop + bbox overlay PNG 를 만들고, 그것을 Vision 입력으로 한 sub-loop 추가.

```text
# Phase 2 closed-loop 가 verdict=clean 3-streak 도달한 후 (또는 --zoom-mode 직진 시)

for component in stage1_analysis.images[0].vision_extract.components:
  Bash:
    python .claude/skills/image-bbox-refine/scripts/crop_for_zoom.py \
      --stage1 ${state_dir}/stage1_analysis.json \
      --source ${src_img} \
      --out-dir ${state_dir}/bbox_refinement/zoom \
      --component "${component.name_guess}" \
      --pad-pct ${zoom_pad_pct}
  # → bbox_refinement/zoom/<sanitized_name>_iter<N>.png

# 그 후 zoom-inspect closed-loop (Tier 2)
clean_streak = 0
for iter in (Phase 2 last iter + 1) .. max_iterations:
  Task(
    subagent_type="bbox-refinement-checker",
    prompt=f"""
... (same as Phase 2)
zoom_mode: true
zoom_inputs: {state_dir}/bbox_refinement/zoom/*.png   # 모든 컴포넌트 zoom
"""
  )
  # SubAgent 가 Read() 로 source + 모든 zoom PNG 들을 Vision 1-pass 비교
  # → discrepancies 가 zoom 에서만 보이는 미세 drift 도 surface
  # → Phase 2 와 동일하게 apply + render + 3-streak 체크
```

**Phase 2 vs Phase 2.5 결함 패턴**:
| 결함 종류 | Phase 2 (full-frame) | Phase 2.5 (zoom) |
|---|---|---|
| 9-grid 위치 어긋남 (5pp+) | ✅ catch | ✅ catch |
| 컴포넌트 merge / split 필요 | ✅ catch | ✅ catch |
| 사각 bbox vs 원형 widget (Minimap) | ❌ miss | ✅ catch |
| sub-row gap 안의 빈 공간 (PartyPanel) | ❌ miss | ✅ catch |
| text-row 1줄 누락 (QuestTracker title) | ❌ miss | ✅ catch |
| widget 경계 1-2pp drift | ❌ miss | ✅ catch |

**비용**: zoom 9개 × Vision 1-pass per iter ≈ Phase 2 의 ~1.5x 토큰. 그 대신 sub-pp 정밀도 확보. 권장 사용 시점: Stage 3 ui_mockup → UMG Slot 변환 직전.

---

### Phase 3: 최종 산출물

```text
- stage1_analysis.json                              <- 최종 보정 적용
- stage1_analysis.iter{1..N}.json                   <- 각 iter 스냅샷 (D34 누적)
- bbox_refinement/refinement_iter{1..N}.json        <- SubAgent corrections JSON
- bbox_refinement/overlay_iter{0..N}.png            <- 시각화 PNG (iter0 = baseline)
- bbox_refinement/converged.txt                     <- 최종 iter 번호 + verdict
- stage1_bbox_overlay.png                           <- 최종 overlay (UI 노출 용)
```

`bbox_refinement/converged.txt` 형식:
```text
iteration=3
verdict=clean
convergence_score=0.92
total_corrections_applied=12
structural_changes_count=1
elapsed_seconds=180
cost_estimate_usd=4.50
```

### Phase 4: 사용자 안내

```text
✅ /image-bbox-refine 완료: {wf_id}

iterations: {N}
final verdict: {verdict}
convergence score: {score}/1.0
corrections applied: {total}
structural changes: {struct_count}

산출물:
  {state_dir}/stage1_analysis.json (보정 반영)
  {state_dir}/bbox_refinement/ (전 iter 산출)
  {state_dir}/stage1_bbox_overlay.png (최종 시각화)

다음 단계:
  /image-to-design-spec --workflow-id {wf_id} --stage 2
  (또는 /design-doc-iterate / /design-doc-ab-test 직접 호출)
```

---

## --self-test 모드

CI/QA 용 dry-run:

1. 더미 1px PNG 대신 합성 mockup PNG 생성 (`tests/fixtures/synthetic_hud.png`)
2. 합성 stage1_analysis.json 생성 (의도적으로 bbox 5% 어긋난 상태)
3. SubAgent 호출 (mock 또는 실제)
4. 1 iteration 후 corrections 적용 → verdict 검증
5. 모든 산출물 schema PASS 확인

---

## 에러 처리

| 에러 | 대응 |
|---|---|
| `stage1_analysis.json` 미존재 | exit + "/image-to-design-spec 먼저 실행하세요" |
| `manifest.json` 미존재 | 동일 |
| `source image` 접근 불가 | warning + render-only 모드로 fallback |
| SubAgent 호출 실패 | iter 중단 + 마지막 정상 iter로 rollback |
| `refinement_iter{N}.json` schema 위반 | iter 중단 + JSON 파싱 결함 보고 |
| max_iterations 도달했는데 verdict != clean | warning + 사용자 결정 (수동 보정 / 다음 phase / Phase 1 재실행) |
| corrections 적용 시 component name_guess 미존재 | warning + 해당 correction 스킵 |

---

## 관련 스킬 및 파일

- **선행**: `/image-to-design-spec` (Stage 1 — 초기 bbox 생성)
- **후행**: `/image-to-design-spec --stage 2` (Stage 2 — brief 작성 시 보정된 bbox 반영)
- **상태 파일**: `.claude/state/design-workflow/<wf_id>/bbox_refinement/`
- **스크립트**: `scripts/render_overlay.py`, `scripts/apply_corrections.py`
- **스키마**: `schemas/refinement.schema.json`
- **SubAgent**: `bbox-refinement-checker`
- **Hook**: `.claude/hooks/design_workflow_gate.py` (bbox_refinement/ sub-dir은 stage1 위임 — 추가 gate 없음)
- **참고 plan**: `phase-2-velvety-peach.md` §Phase 1.5
