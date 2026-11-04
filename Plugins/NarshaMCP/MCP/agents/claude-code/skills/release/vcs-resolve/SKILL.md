---
name: vcs-resolve
description: "VCS conflict resolution guide with file-type-aware strategies. Auto-detects Git/SVN/P4/PlasticSCM. Triggers: '충돌 해결', 'resolve conflicts', 'svn conflict', 'p4 resolve', '머지 충돌'."
allowed-tools: [Bash, Read, Edit]
argument-hint: "[file-pattern] [--theirs | --mine] [--self-test] (defaults to all conflicted files)"
compatibility:
  requires:
    - "One of: git, svn, p4, cm CLI available"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

## Current State (auto-collected)
- Branch: `! git branch --show-current 2>/dev/null || echo "N/A"`

# VCS Resolve — Conflict Resolution Guide

Auto-detect VCS type, classify conflicts by file type, and guide resolution with UE-specific strategies.

## Purpose

VCS 충돌을 유형별로 분류하고 해결을 안내합니다:
- 텍스트 충돌 (.cpp/.h/.ini): diff3 표시 + AI 분석으로 해결 제안
- 바이너리 충돌 (.uasset/.umap): theirs/mine 선택 안내 + 에셋 참조 영향 분석
- 설정 충돌 (.ini): 키 단위 비교
- 해결 실행 (사용자 확인 후)

## Activation Triggers

### Auto-Trigger Keywords
**Korean**: "충돌 해결", "컨플릭트 해결", "svn 충돌", "머지 충돌", "충돌 파일"
**English**: "resolve conflicts", "fix conflicts", "svn conflict", "p4 resolve", "merge conflict resolution", "conflict files"

### Manual Invocation
```bash
/vcs-resolve                   # Resolve all conflicts
/vcs-resolve --theirs          # Accept all remote/theirs
/vcs-resolve --mine            # Accept all local/mine
/vcs-resolve "*.cpp"           # Resolve only C++ files
```

---

## Execution Protocol

**Follow these steps exactly. Do not skip steps or improvise.**

---

### STEP 0: VCS DETECTION + CONFLICT DISCOVERY

1. Detect VCS type:
   ```bash
   if [ -d ".git" ]; then echo "VCS=git"
   elif [ -f ".p4config" ] || [ -n "$P4PORT" ]; then echo "VCS=p4"
   elif [ -d ".svn" ]; then echo "VCS=svn"
   elif [ -d ".plastic" ]; then echo "VCS=plastic"
   else echo "VCS=none"; fi
   ```

2. List conflicted files:

   **Git**:
   ```bash
   git diff --name-only --diff-filter=U
   ```

   **SVN**:
   ```bash
   svn status | grep "^C"
   ```

   **P4**:
   ```bash
   p4 resolve -n
   ```

   **Plastic** (best-effort, untested):
   ```bash
   cm status --short | grep "^C"
   ```
   Note: `cm status --conflict` flag is unverified. Fallback: check for `.conflict` marker files.

3. **IF no conflicts**: Display "충돌이 없습니다." → **DONE**

4. Try MCP conflict analysis (optional):
   ```python
   ue_manage_project_ops(operation="sc_resolve_conflicts")
   ```
   - IF MCP unavailable → use CLI output

5. **GOTO STEP 1**

---

### STEP 1: CLASSIFY CONFLICTS

1. Classify each conflicted file:
   ```text
   ⚠️ {N}개 충돌 감지:
   
   [텍스트 - C++]
     Source/MyGame/Character.cpp
     Source/MyGame/Character.h
   
   [텍스트 - Config]
     Config/DefaultGame.ini
   
   [바이너리 - Blueprint]
     Content/Blueprints/BP_Player.uasset
   
   [바이너리 - Asset]
     Content/Maps/MainLevel.umap
   ```

2. **IF `--theirs` or `--mine` flag**: Skip to STEP 3 (batch resolve)

3. **GOTO STEP 2**

---

### STEP 2: FILE-TYPE-SPECIFIC ANALYSIS

For each conflicted file, apply type-specific analysis:

#### Text Files (.cpp/.h/.cs/.ini)

1. Read the file to show conflict markers:
   ```bash
   # Git: show conflict markers
   grep -n "<<<<<<< \|======= \|>>>>>>> " <file>
   ```

2. Display conflict regions with context using `Read` tool.

3. AI-analyze each conflict region:
   - What changed on each side?
   - Which change is likely correct?
   - Can they be merged automatically?

4. Propose resolution for each region.

#### Binary Files (.uasset/.umap)

1. **Cannot merge** — binary files must pick one side.

2. Provide context for decision:
   - Check recent modification history:
     - Git: `git log --oneline -5 -- <file>`
     - SVN: `svn log -l 5 <file>`
   - Try MCP asset analysis (if available):
     ```python
     ue_search_assets(query="<asset_name>")
     ```

3. Display recommendation:
   ```text
   바이너리 파일은 병합할 수 없습니다. 하나를 선택하세요:
   
   [theirs] 원격 버전 사용 — 서버의 최신 변경 반영
   [mine]   로컬 버전 유지 — 내 작업 유지
   
   최근 수정:
     {user_a}: {date} — {commit_message}
     {user_b}: {date} — {commit_message}
   ```

#### Config Files (.ini)

1. Read file and show conflicts.
2. Try MCP config analysis (if available):
   ```python
   ue_analyze_config(operation="search_config", params={"query": "<conflicted_key>"})
   ```
3. Compare at key-value level and suggest merge.

**After analyzing all files → GOTO STEP 3**

---

### STEP 3: EXECUTE RESOLUTION

**⚠️ MANDATORY: Always confirm with user before resolving.**

1. **IF `--theirs` flag (batch)**:

   **Git**: `git checkout --theirs <file> && git add <file>` for each file
   **SVN**: `svn resolve --accept=theirs-full <file>` for each file
   **P4**: `p4 resolve -at <file>` for each file

2. **IF `--mine` flag (batch)**:

   **Git**: `git checkout --ours <file> && git add <file>` for each file
   **SVN**: `svn resolve --accept=mine-full <file>` for each file
   **P4**: `p4 resolve -ay <file>` for each file

3. **ELSE (interactive, per-file)**:

   For each file, ask user which resolution to apply:
   - **Text files**: Apply AI-proposed merge using `Edit` tool, then mark resolved
   - **Binary files**: Apply chosen side (theirs/mine)
   - Mark as resolved:
     - Git: `git add <file>`
     - SVN: `svn resolve --accept=working <file>`
     - P4: `p4 resolve -am <file>` (auto-merge) or `-at`/`-ay`

4. **GOTO STEP 4**

---

### STEP 4: VERIFICATION + REPORT

1. Verify no remaining conflicts:
   - Git: `git diff --name-only --diff-filter=U`
   - SVN: `svn status | grep "^C"`
   - P4: `p4 resolve -n`

2. Display result:
   ```text
   ═══════════════════════════════════════════
    VCS Resolve Complete ({VCS_TYPE})
   ═══════════════════════════════════════════
    Resolved:  {N} files
      Text:      {N} (AI-merged: {N}, manual: {N})
      Binary:    {N} (theirs: {N}, mine: {N})
    Remaining: {N or "none"}
   ═══════════════════════════════════════════
   ```

3. **IF remaining conflicts > 0**: Display list and suggest re-running `/vcs-resolve`

4. Suggest next step:
   ```text
   다음 단계: /vcs-submit (변경 사항 커밋)
   ```

5. **DONE**

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| No conflicts found | Already resolved or none exist | Display message, exit |
| Binary merge attempted | Cannot merge binary files | Force theirs/mine selection |
| Git add fails after resolve | File still has conflict markers | Re-read and fix remaining markers |
| P4 resolve fails | Changelist issue | Check `p4 changes -s pending` |
| MCP unavailable | Server not running | Continue with CLI-only mode |

## Safety

- **NEVER auto-resolve without user confirmation** (except with explicit `--theirs`/`--mine` flags).
- **Binary files**: Always require explicit choice — no automatic merge.
- **Text files**: Show proposed changes before applying via `Edit`.

## Self-Test Mode

`/vcs-resolve --self-test` runs a focused validation:

1. **VCS Detection**: Verify VCS type can be detected in current directory
2. **CLI Availability**: Check that the detected VCS CLI is available (`git --version`, `svn --version`, `p4 -V`, `cm version`)
3. **Structure Check**: Verify SKILL.md, REFERENCE.md, EXAMPLES.md exist
4. **Frontmatter Check**: Parse YAML frontmatter for required fields (`name`, `description`, `allowed-tools`)
5. **Routing Check**: Verify routing.json entry exists and trigger patterns match

**Output**: PASS / FAIL with per-check results. Does not execute any VCS operations.

## Related

- [vcs-update](../vcs-update/SKILL.md) — 업데이트 중 충돌 → 여기로 라우팅
- [vcs-submit](../vcs-submit/SKILL.md) — 충돌 해결 후 커밋
- [vcs-lock](../vcs-lock/SKILL.md) — 충돌 방지를 위한 파일 락
- [ue-pre-commit](../ue-pre-commit/SKILL.md) — 커밋 전 안전 검사
