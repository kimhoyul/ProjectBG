---
name: game-design-intelligence
model: opus
description: "Purpose: Generate game design intelligence documents. GAS balance analysis + AI behavior documentation (StateTree/BT/EQS). Orchestrates ue_manage_gameplay(generate_balance_report) + ue_manage_ai(generate_doc). Triggers: '밸런스 분석', 'balance analysis', '게임 디자인 문서', 'AI 행동 문서', 'game design report', 'GAS 밸런스'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "asset name or mode (e.g., GA_Melee balance, ST_EnemyAI doc)"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "Proprietary"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Game Design Intelligence Workflow

**Priority**: Medium
**Goal**: Automate game design document generation from binary asset data

---

## Purpose

Orchestrates game design intelligence generation:

1. **GAS Balance Analysis** - Effect/ability aggregation, attribute impact matrix, balance warnings
2. **AI Behavior Documentation** - StateTree/BehaviorTree/EQS Mermaid diagrams + structured JSON
3. **Cross-Reference** - Ability→Effect mapping, CurveTable references

**Key Difference from Individual Tools**:
- **Individual Tools**: Single query, raw JSON output
- **Game Design Intelligence**: Multi-tool aggregation with structured JSON + Mermaid diagrams

---

## Trigger Phrases

### Korean

- "밸런스 분석", "GAS 밸런스"
- "이펙트 밸런스", "게임 디자인 문서"
- "AI 행동 문서", "BT 문서", "ST 문서"
- "어트리뷰트 영향 분석"

### English

- "balance analysis", "GAS balance"
- "game design report", "design intelligence"
- "AI behavior doc", "BT documentation"
- "attribute impact analysis", "effect balance"

---

## Workflow Overview

**Target**: `$ARGUMENTS`

> If `$ARGUMENTS` is empty, run the full 4-phase workflow (system overview + balance + AI docs) for the entire project.

### Phase 0: Discovery Pre-Exploration (Issue #6289)

> Before calling `ue_manage_gameplay`, check if the project uses GAS and what AI assets exist.

```python
# Run all glob calls in parallel — they are independent.

# 0a. Check for GAS assets
ue_glob(pattern="GA_*", domain="assets", limit=50)
# Returns: GameplayAbility asset names (~50 tokens)
# If zero → skip Phase 2 (Balance Analysis)

# 0b. Check for AI assets
ue_glob(pattern="ST_*", domain="assets", limit=50)
ue_glob(pattern="BT_*", domain="assets", limit=50)
# Returns: StateTree/BehaviorTree asset names
# If zero → skip Phase 3 (AI Behavior Documentation)

# 0c. Check for EQS assets
ue_glob(pattern="EQS_*", domain="assets", limit=50)
# If EQS found → include EQS queries in Phase 3 AI behavior docs
```text

**Note**: For projects with 50+ assets of a type, the glob results represent a representative sample. The analysis tools handle full enumeration internally.

**Phase 0 Decision Logic**:
- GA_* found → Proceed to Phase 1 + Phase 2 (GAS balance)
- ST_*/BT_*/EQS_* found → Include Phase 3 (AI behavior docs)
- Neither found → Report "No GAS or AI assets found", suggest project-onboarding instead
- Both found → Full 4-phase workflow

---

### 4-Phase Analysis Process

```text
Phase 1: System Overview
│ Tool: ue_manage_gameplay(operation="analyze_gameplay_system")
│ Output: GAS/Input system counts and samples
↓
Phase 2: Balance Analysis
│ Tool: ue_manage_gameplay(operation="generate_balance_report")
│ Output: Attribute impact matrix, ability-effect map, balance warnings (structured JSON)
│ Note: For large projects (50+ abilities), pass `scope` or `tag_filter` to
│       `generate_balance_report` to focus on a specific ability group.
│       Without filtering, the report may be very large.
↓
Phase 3: AI Behavior Documentation
│ For each AI asset type discovered:
│ Tool: ue_manage_ai(operation="generate_doc", ai_type="statetree|behaviortree|eqs")
│ Output: Mermaid diagrams + structured JSON per asset
↓
Phase 4: Combine
│ Merge all outputs into unified game design document
```
---

## Tool Integration

### Primary Tools Used

| Tool | Operation | Purpose |
|------|-----------|---------|
| `ue_manage_gameplay` | `analyze_gameplay_system` | Get system overview (counts, samples) |
| `ue_manage_gameplay` | `generate_balance_report` | Attribute impact matrix + warnings |
| `ue_manage_ai` | `list_assets` | Discover ST/BT/EQS assets |
| `ue_manage_ai` | `generate_doc` | Mermaid + structured JSON per AI asset |

### Routing Logic

```python
DESIGN_INTELLIGENCE_ROUTING = {
    "balance": ("ue_manage_gameplay", "generate_balance_report"),
    "overview": ("ue_manage_gameplay", "analyze_gameplay_system"),
    "statetree_doc": ("ue_manage_ai", "generate_doc", "statetree"),
    "behaviortree_doc": ("ue_manage_ai", "generate_doc", "behaviortree"),
    "eqs_doc": ("ue_manage_ai", "generate_doc", "eqs"),
}
```
---

## Quick Example

**User**: "프로젝트 밸런스 분석하고 AI 행동 문서 생성해줘"

**Game Design Intelligence Workflow**:

```text
Phase 1: System Overview
├─ Tool: ue_manage_gameplay(operation="analyze_gameplay_system")
└─ Result:
   ├─ 42 GameplayEffects, 18 GameplayAbilities
   ├─ 156 GameplayTags, 12 InputActions
   └─ 8 Input Flows

Phase 2: Balance Analysis
├─ Tool: ue_manage_gameplay(operation="generate_balance_report")
└─ Result:
   ├─ Attribute Impact: Health (12 effects), Damage (8 effects)
   ├─ Warnings: GE_Regen has Infinite+NoStacking
   └─ 5 abilities with SetByCaller dynamic values

Phase 3: AI Behavior Documentation
├─ Tool: ue_manage_ai(ai_type="statetree", operation="generate_doc", asset_name="ST_EnemyAI")
│  └─ Mermaid stateDiagram-v2 with 8 states, 12 transitions
├─ Tool: ue_manage_ai(ai_type="behaviortree", operation="generate_doc", asset_name="BT_Combat")
│  └─ Mermaid graph TD with 15 nodes (3 composites, 8 tasks, 4 decorators)
└─ Tool: ue_manage_ai(ai_type="eqs", operation="generate_doc", asset_name="EQS_FindTarget")
   └─ Mermaid flowchart LR with 2 options, 5 tests

Phase 4: Unified Document
└─ Combined structured JSON with all sections + embedded Mermaid diagrams
```

---

## Output Format

### Balance Report
- Summary table (effects by duration type)
- Attribute impact matrix (which effects modify which attributes)
- Ability → Effect mapping table
- Balance warnings (infinite stacking, missing cooldowns, dynamic values)
- CurveTable cross-references

### AI Behavior Docs (per asset)
- Mermaid diagram (stateDiagram/graph TD/flowchart)
- Node/state detail tables
- Scoring/weight details (EQS)

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| No GAS abilities found | Project doesn't use GAS | Report "No GameplayAbilities detected" and skip balance analysis |
| AI asset not found | Asset name typo or wrong type | Use `ue_search_assets` to find matching AI assets |
| generate_doc returns empty | StateTree/BT has no analyzable nodes | Report minimal structure summary from `get_structure` instead |
| generate_balance_report timeout | Large ability set | Narrow scope with tag filter parameter |

---

## Evaluation Criteria

### Quantitative Metrics

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| Full report generation | **<60s** | From query to complete document |
| Balance data accuracy | **>95%** | Correct attribute/effect mapping |
| Mermaid diagram validity | **100%** | Parseable by Mermaid renderer |
| AI asset coverage | **>90%** | Generates docs for discovered assets |

### Activation Test Cases

**Positive (5)** - Should activate:
1. "프로젝트 밸런스 분석해줘" → Activate (balance analysis)
2. "GAS 밸런스 리포트 생성" → Activate (balance report)
3. "AI 행동 문서 만들어줘" → Activate (AI docs)
4. "game design intelligence report" → Activate (full report)
5. "어트리뷰트 영향 분석해줘" → Activate (attribute impact)

**Negative (3)** - Should NOT activate:
1. "GA_Attack 분석해줘" → DO NOT activate (gas-development skill)
2. "BT_Enemy 구조 보여줘" → DO NOT activate (ue_manage_ai directly)
3. "GE_Damage 상세 정보" → DO NOT activate (ue_manage_gameplay directly)

---

## Success Criteria

- [x] GAS balance report with attribute impact matrix
- [x] AI behavior Mermaid diagrams (ST/BT/EQS)
- [x] Balance warnings (infinite+no stacking, missing cooldowns)
- [x] CurveTable cross-references
- [x] Combined structured JSON output (Issue #6267: markdown removed)
- [ ] DataTable integration (post-MVP)

---

## Related Files

- **Balance Report**: `MCP/native/mcp_server/src/tools/gameplay_gas.rs` (generate_balance_report)
- **ST Doc Gen**: `MCP/native/mcp_server/src/tools/ai_statetree/doc_ops.rs` (generate_doc)
- **BT Doc Gen**: `MCP/native/mcp_server/src/tools/ai_behaviortree/` (generate_doc)
- **EQS Doc Gen**: `MCP/native/mcp_server/src/tools/gameplay.rs` (generate_doc)
- **Parent Issue**: [#5506](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/5506)

---

**Status**: v1.0.0 MVP
**Last Updated**: 2026-02-28
