# vcs-resolve Reference

**Version**: 1.0.0
**Skill**: [SKILL.md](SKILL.md) | **Examples**: [EXAMPLES.md](EXAMPLES.md)

---

## Phase Summary

| Phase | Purpose | Key Output |
|-------|---------|------------|
| STEP 0 | VCS Detection + Conflict Discovery | Conflicted file list |
| STEP 1 | Classify Conflicts | Text/Binary/Config categorization |
| STEP 2 | File-Type-Specific Analysis | Resolution proposals per file |
| STEP 3 | Execute Resolution | Resolved files (user-confirmed) |
| STEP 4 | Verification + Report | Remaining conflicts count |

---

## VCS Command Matrix

| Operation | Git | SVN | P4 | PlasticSCM |
|-----------|-----|-----|----|----|
| List conflicts | `git diff --name-only --diff-filter=U` | `svn status \| grep "^C"` | `p4 resolve -n` | `cm status --short \| grep "^C"` |
| Accept theirs | `git checkout --theirs <file> && git add <file>` | `svn resolve --accept=theirs-full <file>` | `p4 resolve -at <file>` | N/A (manual) |
| Accept mine | `git checkout --ours <file> && git add <file>` | `svn resolve --accept=mine-full <file>` | `p4 resolve -ay <file>` | N/A (manual) |
| Mark resolved | `git add <file>` | `svn resolve --accept=working <file>` | `p4 resolve -am <file>` | N/A |
| Show conflict markers | `grep -n "<<<<<<< \|======= \|>>>>>>> " <file>` | Same | N/A (P4 uses resolve dialog) | N/A |

---

## File Type Classification

| Category | Extensions | Resolution Strategy |
|----------|-----------|-------------------|
| Text - C++ | `.h`, `.cpp`, `.cs` | Show conflict markers + AI merge proposal |
| Text - Config | `.ini` | Key-value level comparison |
| Binary - Blueprint | `.uasset` (BP_ prefix) | Theirs/mine selection only |
| Binary - Asset | `.uasset`, `.umap` | Theirs/mine selection only |

## Resolution Strategies by File Type

### Text Files (.cpp/.h/.ini)

1. Read file to show conflict markers (`<<<<<<<`, `=======`, `>>>>>>>`)
2. Display both sides with context
3. AI-analyze each conflict region:
   - What changed on each side?
   - Which change is likely correct?
   - Can they be merged automatically?
4. Propose resolution using `Edit` tool
5. After user approval, mark as resolved

### Binary Files (.uasset/.umap)

1. **Cannot merge** — binary files must pick one side
2. Show recent modification history for context
3. Display recommendation with author/date info
4. User selects theirs or mine
5. Apply chosen side via VCS command

### Config Files (.ini)

1. Read file and show conflicts
2. Compare at key-value level (not line-by-line)
3. Suggest merge preserving both additions where possible
4. Apply via `Edit` tool after user confirmation

## Batch Resolution Flags

| Flag | Behavior | Use Case |
|------|----------|----------|
| `--theirs` | Accept all remote versions | "Take everything from the server" |
| `--mine` | Accept all local versions | "Keep all my local work" |
| (none) | Interactive per-file | Default — review each conflict |

## Error Recovery Details

| Error | Cause | Recovery Steps |
|-------|-------|----------------|
| No conflicts found | Already resolved or none exist | Display message, exit |
| Binary merge attempted | Cannot merge binary files | Force theirs/mine selection |
| Git add fails after resolve | File still has conflict markers | Re-read and fix remaining markers |
| P4 resolve fails | Changelist issue | Check `p4 changes -s pending` |

## Safety Rules

- **NEVER auto-resolve** without user confirmation (except with explicit `--theirs`/`--mine` flags)
- **Binary files**: Always require explicit choice — no automatic merge
- **Text files**: Show proposed changes before applying via `Edit`
