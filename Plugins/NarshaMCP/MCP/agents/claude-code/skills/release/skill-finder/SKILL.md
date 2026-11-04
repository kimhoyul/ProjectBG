---
name: skill-finder
description: "Purpose: routing.json 기반 런타임 스킬 추천. 자연어 쿼리를 받아 trigger_patterns/negative_patterns/disambiguation 매칭으로 최적 스킬 1-3개 추천. Triggers: '어떤 스킬', '스킬 추천', '스킬 찾아', 'find skill', 'which skill', 'recommend skill', 'skill finder'."
argument-hint: "Natural language query (e.g., '어떤 BP가 내 BP를 참조해?', 'How to trace execution flow?') | --self-test"
allowed-tools:
  - Read
  - Glob
  - Grep
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  issue: "#7707"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Skill Finder — routing.json 기반 런타임 스킬 추천

**Issue**: #7707
**Purpose**: 자연어 쿼리 → routing.json 매칭 → 최적 스킬 1-3개 추천

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the matching algorithm automatically**.
> Do NOT just display documentation — actually run the matching workflow!

### Input

- **Query**: `$ARGUMENTS`
  > If `$ARGUMENTS` is empty, ask the user: "어떤 작업을 하고 싶으신가요? / What task are you trying to accomplish?"

---

## Matching Algorithm

### Step 1: Load routing.json

```
Read(".claude/skills/routing.json")
```

Parse the JSON and extract `version` (top-level string) and the `rules` array. Each rule has:
- `skill`: skill name (kebab-case)
- `trigger_patterns`: `{ ko: string[], en: string[] }`
- `negative_patterns`: `{ ko: string[], en: string[] }`
- `disambiguation`: human-readable intent description
- `priority`: numeric (lower = higher priority)

### Step 2: Normalize Query

```
query = $ARGUMENTS (lowercase, trim whitespace)
```

### Step 3: Match Each Rule

For each rule in `rules`:

**3a. Negative Pattern Check (exclude first)**
- For each pattern in `negative_patterns.ko` + `negative_patterns.en`:
  - If `query` contains the pattern (case-insensitive substring match) → **skip this rule entirely**

**3b. Positive Pattern Match (score)**
- `match_count = 0`
- For each pattern in `trigger_patterns.ko` + `trigger_patterns.en`:
  - If `query` contains the pattern (case-insensitive substring match) → `match_count += 1`
- If `match_count == 0` → skip this rule

**3c. Calculate Confidence Score**
```
total_patterns = len(trigger_patterns.ko) + len(trigger_patterns.en)
if total_patterns == 0: skip this rule
match_ratio = match_count / total_patterns
confidence = match_ratio * 100

# Priority bonus: lower priority number = higher bonus
max_priority = max(r.priority for r in rules)  # dynamic, currently 106
priority_bonus = max(0, (max_priority - rule.priority) / max_priority * 10)
confidence = min(99, confidence + priority_bonus)
```

### Step 4: Rank and Filter

1. Sort matched rules by `confidence` descending
2. Take top 3 results
3. If no results found, report "No matching skill found" and suggest the user rephrase or try `/project-onboarding`

### Step 5: Output Format

Present results in this exact format:

```markdown
## Skill Recommendations

| # | Skill | Confidence | Matched Patterns | Why |
|---|-------|-----------|-----------------|-----|
| 1 | `/skill-name` | 92% | "pattern1", "pattern2" | disambiguation text |
| 2 | `/skill-name-2` | 65% | "pattern3" | disambiguation text |

**Recommended**: `/top-skill-name` — Run this skill with: `/top-skill-name <original query>`

---

*Source: routing.json v{version} ({total_rules} rules)*
```

**Output Rules**:
- Show 1-3 results only (never more than 3)
- Confidence below 10% → do not show
- If top result confidence > 80% → bold the recommendation
- Include the matched pattern(s) so user understands WHY it matched
- Use disambiguation text as the "Why" column (truncate to ~60 chars if long)
- End with the routing.json version and rule count for transparency

---

## Edge Cases

### No Match
```
No matching skill found for: "{query}"

Suggestions:
- Rephrase your query with more specific keywords
- Try `/project-onboarding` for a guided tour of available tools
- Browse skills: check `.claude/skills/README.md` for the full list
```

### Single Strong Match (confidence > 85%)
```
**Strong match**: `/skill-name` (93%)

{disambiguation}

Run: `/skill-name <original query>`
```

### Multiple Close Matches (top 2 within 15% of each other)
Show the disambiguation text for both so the user can choose:
```
Two skills are close matches. Key difference:

- `/skill-a`: {disambiguation-a}
- `/skill-b`: {disambiguation-b}

Choose based on your intent.
```

---

## Examples

### Example 1: BP Reference Query
```
Input: "어떤 BP가 내가 만든 BP를 참조해?"

Matching:
- ue-impact: "어떤 BP가" ✓, "참조" ✓ → confidence ~92%
- ue-asset-discovery: "참조" ✓ → confidence ~65%

Output:
| # | Skill | Confidence | Matched Patterns | Why |
|---|-------|-----------|-----------------|-----|
| 1 | `/ue-impact` | 92% | "어떤 BP가", "참조" | Change impact analysis. "What breaks if I change X?" |
| 2 | `/ue-asset-discovery` | 65% | "참조" | Asset reference discovery and dependency analysis |

Recommended: `/ue-impact`
```

### Example 2: Error Query
```
Input: "빌드 에러가 나는데 해결해줘"

Matching:
- ue-debug: "에러" ✓, "해결해줘" ✓ → confidence ~90%
- unreal-error-doctor: "빌드 에러" ✓ → confidence ~75%

Output:
| # | Skill | Confidence | Matched Patterns | Why |
|---|-------|-----------|-----------------|-----|
| 1 | `/ue-debug` | 90% | "에러", "해결해줘" | Systematic UE debugging with symptom-to-cause diagnosis |
| 2 | `/unreal-error-doctor` | 75% | "빌드 에러" | UE compilation error resolution |

Recommended: `/ue-debug`
```

### Example 3: English Query
```
Input: "How to trace execution flow of a Blueprint?"

Matching:
- ue-execution-trace: "trace execution" ✓, "execution flow" ✓ → confidence ~88%
- blueprint-flow: "Blueprint" partial → confidence ~40%

Output:
| # | Skill | Confidence | Matched Patterns | Why |
|---|-------|-----------|-----------------|-----|
| 1 | `/ue-execution-trace` | 88% | "trace execution", "execution flow" | Trace Blueprint/GAS execution flows |

Recommended: `/ue-execution-trace`
```

---

## Self-Test Mode

When `$ARGUMENTS` is `--self-test`, run these 5 checks instead of the normal matching workflow:

1. **routing.json readable**: `Read(".claude/skills/routing.json")` succeeds and parses as valid JSON
2. **Rules non-empty**: `rules` array has > 0 entries
3. **Schema fields present**: Every rule has `skill`, `trigger_patterns`, `priority`
4. **Known query match**: "어떤 스킬 쓸까?" matches skill-finder with > 0% confidence
5. **Negative exclusion**: "스킬 만들어줘" does NOT match skill-finder (blocked by negative pattern)

Output: `Self-test: 5/5 PASS` or `Self-test: N/5 FAIL` with details per failed check.

See [REFERENCE.md](REFERENCE.md) for full self-test specification.

---

## Limitations

- **Substring matching only** (v1): May miss semantic synonyms. If no match found, user should try different keywords.
- **Does not auto-invoke**: This skill recommends — the user must run the suggested skill manually.
- **Stale data**: If routing.json is outdated, recommendations may not reflect newly added skills.
