---
name: vcs-submit
description: "VCS commit/submit execution with UE asset classification and impact analysis. Auto-detects Git/SVN/P4/PlasticSCM. Triggers: '체크인해줘', '커밋해줘', 'submit changelist', 'svn commit', 'p4 submit', 'commit changes'."
allowed-tools: [Bash, Read]
argument-hint: "[--check] [--message 'msg'] [files...] [--self-test] (defaults to all changed files)"
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
- Changed files: `! git status --short 2>/dev/null | head -10 || svn status 2>/dev/null | head -10 || echo "N/A"`

# VCS Submit — Commit/Checkin with UE Asset Analysis

Auto-detect VCS type and execute commit/submit with UE-specific asset classification and optional impact analysis.

## Purpose

변경 사항을 VCS에 제출합니다:
- VCS 자동 감지 (Git/SVN/P4/PlasticSCM)
- 변경 파일 카테고리 분류 (C++/Blueprint/에셋/설정)
- 선택적 사전 검증 (`--check`)
- 커밋 메시지 자동 생성 (conventional commit 형식)
- 사용자 확인 후 실행

## Activation Triggers

### Auto-Trigger Keywords
**Korean**: "체크인해줘", "체크인", "커밋해줘", "커밋 제출", "서밋해줘", "svn 커밋", "p4 서밋", "변경사항 제출"
**English**: "check in", "checkin", "submit changelist", "svn commit", "p4 submit", "submit changes", "commit changes"

### Manual Invocation
```bash
/vcs-submit                    # Submit all changed files
/vcs-submit --check            # Run impact analysis before submit
/vcs-submit --message "fix: resolve crash"  # Specify commit message
```

---

## Execution Protocol

**Follow these steps exactly. Do not skip steps or improvise.**

---

### STEP 0: VCS DETECTION

1. Detect VCS type:
   ```bash
   if [ -d ".git" ]; then echo "VCS=git"
   elif [ -f ".p4config" ] || [ -n "$P4PORT" ]; then echo "VCS=p4"
   elif [ -d ".svn" ]; then echo "VCS=svn"
   elif [ -d ".plastic" ]; then echo "VCS=plastic"
   else echo "VCS=none"; fi
   ```

2. **IF VCS=none**: Display "VCS를 감지할 수 없습니다." → **DONE**

3. **GOTO STEP 1**

---

### STEP 1: COLLECT CHANGED FILES

1. List changed files:

   **Git**:
   ```bash
   git status --short
   ```

   **SVN**:
   ```bash
   svn status
   ```

   **P4**:
   ```bash
   p4 opened -c default
   p4 opened  # All changelists
   ```

   **Plastic**:
   ```bash
   cm status --short
   ```

2. **IF no changes**: Display "변경 사항이 없습니다." → **DONE**

3. Classify files by UE category:
   ```text
   C++ (.h/.cpp):       {N} files
   Blueprint (.uasset): {N} files
   Asset (.uasset/.umap): {N} files
   Config (.ini):       {N} files
   Other:               {N} files
   Total:               {N} files
   ```

4. Try MCP for detailed analysis (optional):
   ```python
   ue_manage_project_ops(operation="sc_get_opened")
   ```
   - IF MCP unavailable → use CLI output from above

5. **GOTO STEP 2**

---

### STEP 2: IMPACT ANALYSIS (optional, --check flag)

1. **IF `--check` flag provided OR user requests verification**:

   a. Run impact analysis:
   ```python
   ue_manage_project_ops(operation="sc_get_change_impact")
   ```
   - IF MCP unavailable → skip, display "MCP 미연결: 영향 분석 생략"

   b. Display impact summary:
   ```text
   Impact Analysis:
     C++ modules affected: {list}
     Blueprints referencing changed code: {list}
     Config changes: {list}
     Risk level: {LOW/MEDIUM/HIGH}
   ```

   c. **IF risk HIGH**: Warn user and suggest `/ue-pre-commit` for detailed analysis.

2. **GOTO STEP 3**

---

### STEP 3: COMMIT MESSAGE

1. **IF `--message` provided**: Use provided message.

2. **ELSE**: Auto-generate conventional commit message:
   - Analyze changed files and detect type:
     - New files dominant → `feat:`
     - Bug fix patterns → `fix:`
     - Config/build changes → `chore:`
     - Test files → `test:`
   - Generate scope from module name
   - Generate description from file changes

3. Display proposed message and ask for confirmation:
   ```text
   Proposed commit message:
     feat(Characters): add AMyHero class with GAS setup
   
   수정하시겠습니까? (Enter to accept, or type new message)
   ```

4. **GOTO STEP 4**

---

### STEP 4: EXECUTE SUBMIT

**⚠️ MANDATORY: Always confirm with user before executing.**

1. Display final summary:
   ```text
   === Submit Preview ===
   VCS: {type}
   Files: {N} files ({categories})
   Message: {commit_message}
   
   이대로 제출할까요?
   ```

2. **AFTER user confirmation**, execute:

   **Git**:
   ```bash
   git add <files>
   git commit -m "<message>"
   ```
   - Note: Does NOT auto-push. User must explicitly request push.

   **SVN**:
   ```bash
   svn commit -m "<message>"
   ```

   **P4**:
   ```bash
   p4 submit -d "<message>"
   ```
   - Or if specific changelist: `p4 submit -c <CL>`

   **PlasticSCM** (best-effort):
   ```bash
   cm checkin -c "<message>"
   ```

3. **GOTO STEP 5**

---

### STEP 5: RESULT REPORT

1. Display result:
   ```text
   ═══════════════════════════════════════════
    VCS Submit Complete ({VCS_TYPE})
   ═══════════════════════════════════════════
    Revision: {hash/revision_number}
    Message:  {commit_message}
    Files:    {N} files submitted
      C++:       {N}
      Blueprint: {N}
      Asset:     {N}
      Config:    {N}
      Other:     {N}
   ═══════════════════════════════════════════
   ```

2. **DONE**

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| No changes to submit | Working directory clean | Display message, exit |
| Git commit fails (hooks) | Pre-commit hook failure | Display hook output, suggest fixing |
| SVN commit fails (out of date) | Need update first | Suggest `/vcs-update` before retry |
| P4 submit fails (locked files) | Another user has lock | Display lock owner, suggest `/vcs-lock` |
| P4 submit fails (resolve needed) | Merge required | Suggest `/vcs-resolve` |
| MCP unavailable | Server not running | Continue with CLI-only mode |

## Safety

- **NEVER auto-push** after Git commit. Push requires explicit user request.
- **ALWAYS confirm** before executing commit/submit.
- **Dry-run by default** for P4 submit (show preview first).

## Self-Test Mode

`/vcs-submit --self-test` runs a focused validation:

1. **VCS Detection**: Verify VCS type can be detected in current directory
2. **CLI Availability**: Check that the detected VCS CLI is available (`git --version`, `svn --version`, `p4 -V`, `cm version`)
3. **Structure Check**: Verify SKILL.md, REFERENCE.md, EXAMPLES.md exist
4. **Frontmatter Check**: Parse YAML frontmatter for required fields (`name`, `description`, `allowed-tools`)
5. **Routing Check**: Verify routing.json entry exists and trigger patterns match

**Output**: PASS / FAIL with per-check results. Does not execute any VCS operations.

## Related

- [vcs-update](../vcs-update/SKILL.md) — VCS 업데이트/동기화
- [vcs-resolve](../vcs-resolve/SKILL.md) — 충돌 해결 가이드
- [vcs-lock](../vcs-lock/SKILL.md) — 파일 락 관리
- [ue-pre-commit](../ue-pre-commit/SKILL.md) — 커밋 전 안전 검사 (분석 전용)
- [pr-create](../pr-create/SKILL.md) — PR 생성 (Git push + PR)
