---
description: "Purpose: safe bulk asset property modification. 5-stage safety workflow (Discovery → Impact → Strategy → Dry-Run → Execute) for Blueprint and PCG assets via the Editor-less ue_modify_asset binary patch tool (dry_run → apply, atomic .bak backup). Not for single-asset analysis (use blueprint-flow or material-analysis). Triggers: 'bulk modify', 'change all Blueprints', 'PCG parameter tuning', 'add property to Blueprints', '일괄 수정', 'BP 대량 변경'."
source: MCP/agents/claude-code/skills/release/asset-modification-wizard/SKILL.md
type: skill
---

# asset-modification-wizard

Purpose: safe bulk asset property modification. 5-stage safety workflow (Discovery → Impact → Strategy → Dry-Run → Execute) for Blueprint and PCG assets via the Editor-less ue_modify_asset binary patch tool (dry_run → apply, atomic .bak backup). Not for single-asset analysis (use blueprint-flow or material-analysis). Triggers: 'bulk modify', 'change all Blueprints', 'PCG parameter tuning', 'add property to Blueprints', '일괄 수정', 'BP 대량 변경'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/asset-modification-wizard/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- bulk modify
- change all Blueprints
- batch update
- add property to all
- change MaxHealth in every
- batch change
- bulk change
- change in every
- modify all

## MCP Tools

- ue_modify_asset
- ue_manage_blueprint
- ue_search_assets
