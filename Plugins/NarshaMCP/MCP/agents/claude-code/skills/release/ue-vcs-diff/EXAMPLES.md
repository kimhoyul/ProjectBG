# UE VCS Diff — Examples

## Git Examples

### Compare last commit
```bash
/ue-vcs-diff HEAD~1
```
**Parses as**: `revision_a=HEAD~1`, `revision_b=""` (HEAD), `vcs_type=git`

### Compare two commits
```bash
/ue-vcs-diff abc1234..def5678
```
**Parses as**: `revision_a=abc1234`, `revision_b=def5678`, `vcs_type=git` (hex 7+ chars)

### Last 3 commits with BP tracing
```bash
/ue-vcs-diff HEAD~3 --trace-bp
```
**Parses as**: `revision_a=HEAD~3`, `--trace-bp` enabled

### Filter to assets only
```bash
/ue-vcs-diff HEAD~5..HEAD --filter=*.uasset
```

## SVN Examples

### Compare SVN revisions
```bash
/ue-vcs-diff r5600..5700
```
**Parses as**: `revision_a=5600`, `revision_b=5700`, `vcs_type=svn` (pure numeric)

### Pure numeric range
```bash
/ue-vcs-diff 100..200
```
**Parses as**: SVN auto-detected (numeric-only)

## Perforce Examples

### Compare changelist
```bash
/ue-vcs-diff @12345
```
**Parses as**: `revision_a=12345`, `vcs_type=p4`

### Shelved changes
```bash
/ue-vcs-diff @12345 --shelved
```

### Explicit VCS override
```bash
/ue-vcs-diff 500 --vcs=p4
```

## Korean Examples

### Recent commits
```
최근 3커밋 뭐가 바뀌었어?
```
**Parses as**: `revision_a=HEAD~3`, `vcs_type=git`

### Last commit
```
마지막 커밋 변경사항 분석해줘
```
**Parses as**: `revision_a=HEAD~1`, `vcs_type=git`

## Expected Output Structure

All examples produce a report in this format:

```markdown
## VCS Diff Report

**VCS**: git | **Revision**: HEAD~1 → HEAD | **Time**: 150ms

### Summary
| Category | Count |
|----------|-------|
| Total files changed | 5 |
| Source files (.h/.cpp) | 2 |
| Asset files (.uasset) | 2 |
| Config files (.ini) | 1 |

### Source Changes
| File | Status | +Lines | -Lines |
|------|--------|--------|--------|
| Source/MyGame/Hero.cpp | modified | +12 | -3 |

### Asset Changes
| Asset | Status | Diff Method | Changes |
|-------|--------|-------------|---------|
| BP_Hero.uasset | modified | typed+generic | 2 exports changed |

### Config Changes
| Config | Status | Note |
|--------|--------|------|
| DefaultEngine.ini | modified | Use `/ue-diff` with `compare_configs` for detail |
```

## Error Cases

### No revision argument
```bash
/ue-vcs-diff
```
**Result**: Prompts user for revision range

### No VCS in project
```bash
/ue-vcs-diff HEAD~1
```
**Result** (in non-VCS project): Error "No VCS detected"

### No changes between revisions
```bash
/ue-vcs-diff HEAD~0
```
**Result**: "No changes found between HEAD~0 and HEAD"
