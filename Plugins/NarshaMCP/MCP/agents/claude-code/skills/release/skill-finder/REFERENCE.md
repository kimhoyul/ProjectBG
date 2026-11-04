# Skill Finder — Reference

**Version**: 1.0.0

## Scoring Rubric

### Confidence Calculation

```
match_count = number of trigger_patterns (ko + en) found in query (case-insensitive substring)
total_patterns = len(trigger_patterns.ko) + len(trigger_patterns.en)
match_ratio = match_count / total_patterns
confidence = match_ratio * 100

priority_bonus = max(0, (max_priority - rule.priority) / max_priority * 10)
confidence = min(99, confidence + priority_bonus)
```

### Filtering

1. **Negative pattern exclusion**: If ANY negative_pattern matches → rule is skipped entirely
2. **Zero-match skip**: If no positive patterns match → rule is skipped
3. **Top-3 cutoff**: Only top 3 results by confidence shown
4. **Minimum threshold**: Results below 10% confidence are hidden

## Data Source

- **File**: `.claude/skills/routing.json`
- **Schema version**: 1.8.0+
- **Rule count**: Dynamic (106 as of v1.0.0, grows with new skills)
- **Fields used**: `skill`, `trigger_patterns`, `negative_patterns`, `disambiguation`, `priority`

## Self-Test

Run `/skill-finder --self-test` to verify:

1. **routing.json readable**: File exists and parses as valid JSON
2. **Rules non-empty**: `rules` array has > 0 entries
3. **Schema fields present**: Each rule has `skill`, `trigger_patterns`, `priority`
4. **Known query test**: "어떤 스킬 쓸까?" matches skill-finder with > 0% confidence
5. **Negative exclusion test**: "스킬 만들어줘" does NOT match skill-finder

Expected output: `Self-test: 5/5 PASS`
