---
name: vcs-update
description: "VCS update/sync execution with progress monitoring. Auto-detects Git/SVN/P4/PlasticSCM. Triggers: 'svn 업데이트', 'p4 sync', 'git pull', 'update project', '소스 동기화', '업데이트해줘', 'pull latest'."
allowed-tools: [Bash, Read]
argument-hint: "[--rebase] [path] [--self-test] (defaults to project root)"
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
- CWD: `! pwd`

# VCS Update — Source Sync with Progress Monitoring

Auto-detect VCS type and execute update/sync with progress monitoring and conflict detection.

## Purpose

프로젝트 소스를 최신 상태로 동기화합니다:
- VCS 자동 감지 (Git/SVN/P4/PlasticSCM)
- 업데이트 실행 + 대규모 작업 모니터링
- 완료 후 변경 요약 (추가/수정/삭제 파일 수)
- 충돌 감지 시 `/vcs-resolve` 안내

## Activation Triggers

### Auto-Trigger Keywords
**Korean**: "svn 업데이트", "p4 sync", "소스 동기화", "업데이트해줘", "최신화", "소스 받아줘"
**English**: "svn update", "p4 sync", "git pull", "sync source", "update project", "pull latest", "fetch latest"

### Manual Invocation
```bash
/vcs-update              # Auto-detect VCS and update
/vcs-update --rebase     # Git: use rebase instead of merge
```

---

## Execution Protocol

**Follow these steps exactly. Do not skip steps or improvise.**

---

### STEP 0: VCS DETECTION

1. Detect VCS type from project root:
   ```bash
   if [ -d ".git" ]; then echo "VCS=git"
   elif [ -f ".p4config" ] || [ -n "$P4PORT" ]; then echo "VCS=p4"
   elif [ -d ".svn" ]; then echo "VCS=svn"
   elif [ -d ".plastic" ]; then echo "VCS=plastic"
   else echo "VCS=none"; fi
   ```

2. **IF VCS=none**: Display "VCS를 감지할 수 없습니다. 프로젝트 루트에서 실행하세요." → **DONE**

3. Try MCP status check (optional, fallback to CLI-only if unavailable):
   ```python
   ue_manage_project_ops(operation="sc_get_status")
   ```
   - IF MCP unavailable → continue with CLI-only mode

4. Display detected VCS type and current status.

5. **GOTO STEP 1**

---

### STEP 1: PRE-UPDATE CHECK

1. Check for uncommitted changes:
   - Git: `git status --short`
   - SVN: `svn status`
   - P4: `p4 opened`
   - Plastic: `cm status`

2. **IF uncommitted changes exist**:
   - Display warning: "로컬 변경사항이 있습니다. 업데이트 시 충돌 가능성 있음."
   - List changed files
   - Ask user: "계속 진행할까요? (Git은 stash 권장)"

3. **GOTO STEP 2**

---

### STEP 2: EXECUTE UPDATE

1. Execute VCS-specific update command:

   **Git**:
   ```bash
   # Check if --rebase flag was passed
   git fetch origin
   git pull origin $(git branch --show-current) --rebase  # or --no-rebase
   ```

   **SVN**:
   ```bash
   svn update
   ```
   - For large repos (71K+ items): Run with `run_in_background: true` to avoid timeout
   - Monitor with periodic `svn info` checks

   **Perforce**:
   ```bash
   # Preview first
   p4 sync -n 2>&1 | tail -5
   # Then execute
   p4 sync
   ```

   **PlasticSCM** (best-effort, untested):
   ```bash
   cm update
   ```

2. Capture exit code and output.

3. **IF exit code != 0**: Parse error output → **GOTO STEP 4** (conflict handling)

4. **GOTO STEP 3**

---

### STEP 3: SUMMARY REPORT

1. Collect update statistics:

   **Git**:
   ```bash
   # Show what changed
   git diff --stat HEAD@{1}..HEAD 2>/dev/null || echo "No previous HEAD to compare"
   ```

   **SVN**: Parse `svn update` output for A/U/D/C counts

   **P4**: Parse `p4 sync` output for added/updated/deleted counts

2. Display summary:
   ```text
   ═══════════════════════════════════════════
    VCS Update Complete ({VCS_TYPE})
   ═══════════════════════════════════════════
    Updated to: {revision/hash}
    Files:
      Added:    {N}
      Modified: {N}
      Deleted:  {N}
    Conflicts: {N or "none"}
   ═══════════════════════════════════════════
   ```

3. **IF conflicts detected**: **GOTO STEP 4**
4. **ELSE**: **DONE**

---

### STEP 4: CONFLICT HANDLING

1. List conflict files:
   - Git: `git diff --name-only --diff-filter=U`
   - SVN: `svn status | grep "^C"`
   - P4: `p4 resolve -n`

2. Display conflict list with file types:
   ```text
   ⚠️ {N}개 충돌 감지:
     [텍스트]  Source/MyGame/Character.cpp
     [바이너리] Content/BP_Player.uasset
   ```

3. Suggest next step:
   ```text
   충돌 해결: /vcs-resolve
   ```

4. **DONE**

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| VCS not detected | Not in project root | `cd` to project root and retry |
| Git pull fails (diverged) | Local and remote diverged | Suggest `git pull --rebase` or manual merge |
| SVN update timeout | Large repo (71K+ items) | Use `run_in_background` + periodic status check |
| P4 sync fails | Workspace not configured | Check `p4 info` and `p4 client` |
| MCP unavailable | Server not running | Continue with CLI-only mode |

## Self-Test Mode

`/vcs-update --self-test` runs a focused validation:

1. **VCS Detection**: Verify VCS type can be detected in current directory
2. **CLI Availability**: Check that the detected VCS CLI is available (`git --version`, `svn --version`, `p4 -V`, `cm version`)
3. **Structure Check**: Verify SKILL.md, REFERENCE.md, EXAMPLES.md exist
4. **Frontmatter Check**: Parse YAML frontmatter for required fields (`name`, `description`, `allowed-tools`)
5. **Routing Check**: Verify routing.json entry exists and trigger patterns match

**Output**: PASS / FAIL with per-check results. Does not execute any VCS operations.

## Related

- [vcs-resolve](../vcs-resolve/SKILL.md) — 충돌 해결 가이드
- [vcs-submit](../vcs-submit/SKILL.md) — 체크인/커밋 자동화
- [ue-vcs-diff](../ue-vcs-diff/SKILL.md) — 리비전 간 diff 분석
- [post-merge-cleanup](../post-merge-cleanup/SKILL.md) — 머지 후 정리
