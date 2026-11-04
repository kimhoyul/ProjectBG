# Skill Finder — Examples

## Example 1: Blueprint Reference Query (Korean)

```
/skill-finder 어떤 BP가 내가 만든 BP를 참조해?
```

**Result**:

| # | Skill | Confidence | Matched Patterns | Why |
|---|-------|-----------|-----------------|-----|
| 1 | `/ue-impact` | 92% | "어떤 BP가", "참조" | Change impact analysis for UE entities |
| 2 | `/ue-asset-discovery` | 65% | "참조" | Asset reference discovery and dependency analysis |

**Recommended**: `/ue-impact`

## Example 2: Debugging Query (English)

```
/skill-finder How to fix build errors in my project?
```

**Result**:

| # | Skill | Confidence | Matched Patterns | Why |
|---|-------|-----------|-----------------|-----|
| 1 | `/ue-debug` | 88% | "fix", "error" | Systematic UE debugging with symptom-to-cause diagnosis |
| 2 | `/unreal-error-doctor` | 72% | "build error" | UE compilation error resolution |

**Recommended**: `/ue-debug`

## Example 3: Negative Pattern Exclusion

```
/skill-finder 스킬 만들어줘
```

**Result**: "스킬 만들어" matches negative pattern → skill-finder excluded.
Routes to `/mcp-skill-builder` instead.

## Example 4: Self-Test

```
/skill-finder --self-test
```

**Result**:
```
Self-test: 5/5 PASS
  1. routing.json readable: PASS (N rules)
  2. Rules non-empty: PASS
  3. Schema fields present: PASS
  4. Known query "어떤 스킬 쓸까?": PASS (matched skill-finder)
  5. Negative exclusion "스킬 만들어줘": PASS (correctly excluded)
```
