---
name: image-to-design-spec
model: opus
description: "Purpose: AI 생성 이미지 1-N장 → 4단계 디자인 파이프라인 Stage 1(Vision 분석) + Stage 2(기획서 초안 Markdown). Auto-triggers: '이미지로 기획서', 'design from image', 'image to spec', 'Nano Banana 시안', 'concept art to GDD'."
allowed-tools:
  - Read
  - Write
  - Glob
  - Bash
  - Task
  - AskUserQuestion
argument-hint: "<image_paths...> [--stage 1|2|all] [--workflow-id <id>] [--domain ui|system|art|auto] [--resume] [--auto-research] [--self-test]"
metadata:
  version: "0.3.0"
  author: "Next-Stage-Inc"
  cluster: "design-from-image"
  parent_issue: 9998
  changelog:
    - version: "0.3.0"
      date: "2026-05-15"
      changes:
        - "FU-9 (#11861): Phase 3.6 Auto-Research Enrichment branch — --auto-research flag invokes auto-research-comparator SubAgent for industry pattern grounding. Default off; idempotent (skips when stage2_research_brief.json exists)."
        - "Argument-hint extended with [--auto-research] flag."
    - version: "0.2.0"
      date: "2026-05-14"
      changes:
        - "Add evals/trigger-grading.json (18 cases, 100% routing F1) — Phase B.1 /ns-review round 3"
        - "Add _verification/ (op_responses.json + trigger_test.py + README.md) per Empirical Grounding (Issue #9965)"
        - "Add scripts/validate_stage1.py — schema + invariant checks for /image-bbox-refine + /design-doc-finalize handoff"
        - "Compress description (456 → 220 chars) — drop verbose Triggers list (routing.json is SSOT)"
    - version: "0.1.0"
      date: "2026-04-29"
      changes:
        - "Initial release — Stage 1 (Vision analysis) + Stage 2 (brief draft) per Issue #9998"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# /image-to-design-spec

AI 생성 이미지를 4단계 디자인 파이프라인의 Stage 1-2로 처리합니다.

---

## Arguments

```text
<image_paths...>    절대경로 이미지 파일 1-10개 (PNG/JPG/WebP)
--stage 1|2|all    실행할 Stage (default: all = 1+2)
--workflow-id <id> 기존 워크플로우 재개 (wf_YYYYMMDD_xxxxxx 형식)
--domain           ui|system|art|auto (default: auto)
--resume           기존 워크플로우 이어서 실행
--self-test        더미 이미지로 dry-run 검증
--high-res         이미지 최대 해상도 8000×8000px (기본: 2576×2576)
```

---

## 실행 흐름

### Phase 0: 환경 초기화

```bash
# 1. 워크플로우 ID 생성 (신규) 또는 기존 로드 (--resume)
wf_id="wf_$(date +%Y%m%d)_$(python -c 'import secrets; print(secrets.token_hex(3))')"
state_dir=".claude/state/design-workflow/${wf_id}"

# 2. 상태 디렉토리 생성
mkdir -p "${state_dir}/images"
mkdir -p "${state_dir}/gates"
```

이미지 경로가 0개이고 `--self-test` 없으면 → `AskUserQuestion`으로 경로 요청.

### Phase 1: Stage 1 — Vision 분석 (image-to-spec-extractor SubAgent)

이미지 픽셀이 메인 컨텍스트로 새지 않도록 격리된 SubAgent에 위임:

```python
Task(
    subagent_type="image-to-spec-extractor",
    prompt=f"""
workflow_id: {wf_id}
state_dir: {state_dir}
image_paths: {image_paths}
user_domain_hint: {domain}
max_images: 10
"""
)
```

SubAgent 완료 후:
- `<state_dir>/stage1_analysis.json` 존재 확인
- `<state_dir>/manifest.json` 존재 확인
- Gate 1 체크리스트 표시: `<state_dir>/gates/gate_stage1.review_checklist.md` 내용 출력

**사용자에게 안내**:
```text
Stage 1 완료. 다음 단계로 진행하려면 Vision 분석 결과를 검토하세요:

검토 파일: {state_dir}/stage1_analysis.json
체크리스트: {state_dir}/gates/gate_stage1.review_checklist.md

승인 후 Stage 2 시작:
  touch "{state_dir}/gates/gate_stage1.approved"
  /image-to-design-spec --workflow-id {wf_id} --stage 2
```

`--stage 1` 지정 시 여기서 종료.

### Phase 2: Gate 1 확인 (--stage 2 또는 --stage all)

```bash
test -f "{state_dir}/gates/gate_stage1.approved" || echo "GATE_BLOCKED"
```

GATE_BLOCKED 시 → 사용자에게 승인 방법 안내 후 종료.

`gate_stage1.notes.md` 존재 시 내용 로드 → Stage 2 prompt에 주입 (사용자 피드백 반영).

### Phase 3: Stage 2 — 기획서 초안 생성

`stage1_analysis.json`을 읽어 도메인 분포 확인:

```python
Read(f"{state_dir}/stage1_analysis.json")
domain_dist = data["domain_distribution"]
dominant = max(domain_dist, key=domain_dist.get)
```

도메인별 템플릿 선택:
| 우세 (≥60%) | 템플릿 | 경로 |
|-------------|--------|------|
| ui_mockup | UI/UX 기획서 | `schemas/../templates/ui_brief.md.template` |
| system_diagram | 게임 시스템 기획서 | `schemas/../templates/system_brief.md.template` |
| concept_art | 컨셉/스타일 가이드 | `schemas/../templates/concept_brief.md.template` |
| 균등 분포 | 하이브리드 GDD | `schemas/../templates/hybrid_brief.md.template` |

모든 이미지가 mixed → `AskUserQuestion`으로 우세 도메인 선택 요청.

**stage2_brief.md 생성** (게이트 Hook이 gate_stage1.approved 확인):

```markdown
# {title_from_vision}
<!-- workflow-id: {wf_id} -->
<!-- ssot-version: 1.0 -->
<!-- generated-at: {ISO} -->
<!-- stage: 2 -->

## 1. 개요 <!-- section-id: SEC-OVR-001 -->
{global_summary}

## 2. 의도된 플레이어 경험 <!-- section-id: SEC-EXP-001 -->
{design_intent.intended_experience — 이 섹션은 반드시 경험 서술로 시작}

## 3. 화면 목록 / 컴포넌트 <!-- section-id: SEC-UI-NAV -->
...

## 사용자 검토 요청 <!-- section-id: SEC-REVIEW -->
- [ ] 화면 분류가 의도와 맞는가?
- [ ] Stage 3에서 어떤 도메인 깊이가 필요한가?
```

### Phase 3.5: game-design-philosopher SubAgent 실행

```python
Task(
    subagent_type="game-design-philosopher",
    prompt=f"""
workflow_id: {wf_id}
state_dir: {state_dir}
input_files: [stage1_analysis.json, stage2_brief.md]
mode: stage2_review
"""
)
```

결과 `stage2_design_review.json` 로드 → gate_recommendation 확인.

**gate_recommendation == "block"** 시:
- 구체적 이유와 개선 제안 사용자에게 표시
- `gates/gate_stage2.review_checklist.md`에 개선 항목 추가
- Stage 2 종료 (사용자가 수정 후 재실행)

**gate_recommendation == "request_revision"** 시:
- 개선 제안 표시 후 계속 진행 (사용자 결정)

**rejection_reason_for_memory** 있으면 → 향후 동일 사용자 워크플로우에 학습 가능.

### Phase 3.6: Auto-Research Enrichment (FU-9 / #11861, optional)

`--auto-research` flag 활성 + `stage2_research_brief.json` 부재 + Phase 3.5 게이트 통과 (block 아님) 시,
`auto-research-comparator` SubAgent 를 호출해 외부 게임/디자인 사례를 자동 검색.

**트리거 조건** (모두 만족해야 invoke):
1. `--auto-research` flag (default off)
2. `stage2_brief.md` + `stage2_design_review.json` 모두 존재 (Phase 3 + 3.5 완료)
3. `stage2_research_brief.json` 부재 (이미 누적된 research 가 있으면 skip — idempotent)
4. Phase 3.5 gate_recommendation != "block" (block 시 enrichment 의미 없음)

**SubAgent 호출 (main session 에서 Task tool 통해)**:

```python
Task(
    subagent_type="auto-research-comparator",
    prompt=f"""
workflow_id: {wf_id}
state_dir: {state_dir}
input_files: [stage1_analysis.json, stage2_brief.md, stage2_design_review.json]
mode: research_enrichment
output_path: {state_dir}/stage2_research_brief.json
constraints:
  - WebSearch 사용 (외부 게임/디자인 사례 인용 의무)
  - 모든 case 에 source URL 포함
  - 비용 통제: max 3 re-query, max 8 cases
  - SQLite 캐시 활용 (`.claude/skills/design-doc-finalize/_research_cache/`)
"""
)
```

**산출물**: `stage2_research_brief.json` — `design-doc-finalize` 의 Phase 1.5 에서 자동으로 consume (이미 구현됨).

**비용 / 효과**:
- 비용: WebSearch 3-5 회 + WebFetch 3-8 회 + SubAgent 추론 (~5-10분, $2-5)
- 효과: Stage 2 폐쇄 평가 (4축 점수만) → 외부 사례 근거 추가로 Stage 3 설계가 industry pattern grounded
- 권장 사용: production phase (`--phase production`), high-stakes design (large team / vertical slice 직전)
- 기본 동작: flag 미명시 시 skip — 기존 워크플로우 영향 zero

**Skip 시점 (자동, 명시 메시지 없음)**:
- flag off → silent skip
- research_brief 이미 존재 → "[auto-research] 기존 brief 재사용" stderr 로그
- block verdict → "[auto-research] gate block 으로 enrichment skip" stderr 로그

### Phase 4: Gate 2 체크리스트 생성

```markdown
# Gate 2 체크리스트 — {gate_stage1.review_checklist.md 내용 포함}

## game-design-philosopher 검토 결과
- 평균 점수: {average}/10
- gate_recommendation: {recommendation}
- 주요 개선 제안: {top suggestions}

## 승인 명령
touch "{state_dir}/gates/gate_stage2.approved"

## Stage 3 시작
/design-doc-finalize --workflow-id {wf_id} --stage 3
```

### Phase 5: 최종 안내

```text
Stage 1-2 완료: {wf_id}

생성된 파일:
  {state_dir}/stage1_analysis.json
  {state_dir}/stage2_brief.md
  {state_dir}/stage2_design_review.json

다음 단계:
  1. stage2_brief.md 검토
  2. game-design-philosopher 점수 확인: {average}/10 ({recommendation})
  3. 승인: touch "{state_dir}/gates/gate_stage2.approved"
  4. Stage 3: /design-doc-finalize --workflow-id {wf_id} --stage 3
```

---

## --self-test 모드

```bash
# 더미 1px PNG 생성
python -c "
import struct, zlib
def make_dummy_png(path):
    sig = b'\\x89PNG\\r\\n\\x1a\\n'
    ihdr_data = struct.pack('>IIBBBBB', 1, 1, 8, 2, 0, 0, 0)
    ihdr_crc = zlib.crc32(b'IHDR' + ihdr_data) & 0xffffffff
    ihdr = struct.pack('>I', 13) + b'IHDR' + ihdr_data + struct.pack('>I', ihdr_crc)
    raw = b'\\x00\\xff\\xff\\xff'
    idat_data = zlib.compress(raw)
    idat_crc = zlib.crc32(b'IDAT' + idat_data) & 0xffffffff
    idat = struct.pack('>I', len(idat_data)) + b'IDAT' + idat_data + struct.pack('>I', idat_crc)
    iend_crc = zlib.crc32(b'IEND') & 0xffffffff
    iend = struct.pack('>I', 0) + b'IEND' + struct.pack('>I', iend_crc)
    open(path, 'wb').write(sig + ihdr + idat + iend)
make_dummy_png('/tmp/dummy_ui.png')
"
```

검증 항목:
1. SubAgent 생성 (image-to-spec-extractor) 완료 여부
2. stage1_analysis.json 스키마 준수 (`$schema_version`, `images[]` 최소 1개)
3. stage2_brief.md에 `<!-- section-id: -->` 1개 이상 존재
4. Gate 체크리스트 파일 생성 여부
5. manifest.json의 stage1.status == "completed"

---

## 에러 처리

| 에러 | 대응 |
|------|------|
| 이미지 경로 없음 | `AskUserQuestion`으로 경로 재요청 |
| 포맷 불지원 (PDF, GIF 등) | 해당 파일 스킵 + 경고, 나머지 진행 |
| Stage 1 SubAgent 실패 | 에러 메시지와 함께 재시도 옵션 제시 |
| Gate 1 미승인 + --stage all | Gate 1 승인 안내 후 중단 |
| 10장 초과 | `AskUserQuestion`으로 분할 처리 확인 |
| SHA256 계산 실패 (certutil/sha256sum 없음) | uuid4 기반 fallback hash 사용 + 경고 |

---

## 관련 스킬 및 파일

- **계속**: `/design-doc-finalize --workflow-id <id> --stage 3` (Stage 3-4)
- **상태 파일**: `.claude/state/design-workflow/<wf_id>/`
- **스키마**: `.claude/skills/image-to-design-spec/schemas/stage1_analysis.schema.json`
- **어휘**: `.claude/skills/image-to-design-spec/vocab/game_design_vocab.json`
- **SubAgent**: `image-to-spec-extractor`, `game-design-philosopher`
- **참고**: 부록 A.1-A.2 (플랜 `wondrous-drifting-wigderson.md`)
