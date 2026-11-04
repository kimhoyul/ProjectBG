---
name: vcs-lock
description: "File lock management for P4/SVN/Git LFS/PlasticSCM. Query lock status, acquire/release locks, binary asset lock warnings. Triggers: '파일 락', 'lock file', '누가 락', 'unlock', '락 상태', '파일 잠금'."
allowed-tools: [Bash, Read]
argument-hint: "<file-or-pattern> [--lock | --unlock | --status] [--self-test] (defaults to --status)"
compatibility:
  requires:
    - "One of: p4, svn, git lfs, cm CLI with lock support"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

## Current State (auto-collected)
- CWD: `! pwd`

# VCS Lock — File Lock Management

Query lock status, acquire/release file locks, and warn about unlocked binary asset modifications.

## Purpose

파일 락을 관리합니다:
- 락 상태 조회 (누가, 언제 락을 걸었는지)
- 작업 전 락 획득 (dry-run 먼저)
- 작업 완료 후 언락
- 바이너리 에셋(.uasset) 수정 전 자동 락 경고

## VCS Lock Support Matrix

| VCS | Lock Support | Command | Notes |
|-----|-------------|---------|-------|
| Perforce | Native | `p4 lock/unlock` | Full support |
| SVN | Native | `svn lock/unlock` | Requires server config |
| Git | Git LFS only | `git lfs lock/unlock` | Requires LFS setup |
| PlasticSCM | Native | `cm lock create/unlock` | Best-effort, untested. Branch-scoped locks. |
| Lore | **Unsupported** (#22277) | - | LORE_WORKSPACE or .lore maps to Unsupported (warn-and-proceed). No KV soft locks. Prefix narsha/agent/. See REFERENCE.md. |

## Activation Triggers

### Auto-Trigger Keywords
**Korean**: "파일 락", "락 걸어줘", "누가 락", "락 해제", "언락", "락 상태", "파일 잠금"
**English**: "lock file", "file lock", "who locked", "unlock file", "release lock", "lock status", "check lock"

### Manual Invocation
```bash
/vcs-lock Content/BP_Player.uasset         # Check lock status
/vcs-lock Content/BP_Player.uasset --lock   # Acquire lock
/vcs-lock Content/BP_Player.uasset --unlock # Release lock
/vcs-lock "*.uasset" --status              # Check all asset locks
```

---

## Execution Protocol

**Follow these steps exactly. Do not skip steps or improvise.**

---

### STEP 0: VCS DETECTION + LOCK SUPPORT CHECK

1. Detect VCS type:
   ```bash
   if [ -d ".git" ]; then echo "VCS=git"
   elif [ -f ".p4config" ] || [ -n "$P4PORT" ]; then echo "VCS=p4"
   elif [ -d ".svn" ]; then echo "VCS=svn"
   elif [ -d ".plastic" ]; then echo "VCS=plastic"
   else echo "VCS=none"; fi
   ```

2. Verify lock support:
   - **Git**: Check if Git LFS is configured: `git lfs env 2>/dev/null`
     - IF not configured: "Git LFS가 설정되지 않아 파일 락을 사용할 수 없습니다."
   - **P4**: Lock always available
   - **SVN**: Lock available (server may restrict)
   - **Plastic**: Lock available

3. **IF VCS=none OR lock not supported**: Display error → **DONE**

4. Parse argument to determine mode:
   - No flag or `--status` → **GOTO STEP 1** (query)
   - `--lock` → **GOTO STEP 2** (acquire)
   - `--unlock` → **GOTO STEP 3** (release)

---

### STEP 1: QUERY LOCK STATUS

1. Query lock information:

   **P4**:
   ```bash
   p4 opened -a <file>     # Who has it opened?
   p4 fstat -T "otherLock,otherOpen,headAction" <file>
   ```

   **SVN**:
   ```bash
   svn info <file> | grep "Lock"
   svn status -u <file>
   ```

   **Git LFS**:
   ```bash
   git lfs locks --path=<file>
   ```

   **Plastic**:
   ```bash
   cm lock list <file>
   ```

2. Try MCP for enhanced info (optional):
   ```python
   ue_manage_project_ops(operation="sc_get_status")
   ```

3. Display lock status:
   ```text
   === Lock Status: {file} ===
   VCS:    {type}
   Locked: {yes/no}
   Owner:  {username} (since {date})
   ```

4. **IF locked by another user**: Display contact suggestion.

5. **DONE**

---

### STEP 2: ACQUIRE LOCK

**⚠️ MANDATORY: Dry-run first, then confirm with user.**

1. **Dry-run** — check if lock is available:

   **P4**:
   ```bash
   p4 fstat -T "otherLock,otherOpen" <file>
   ```

   **SVN**:
   ```bash
   svn info <file> | grep "Lock"
   ```

   **Git LFS**:
   ```bash
   git lfs locks --path=<file>
   ```

2. **IF already locked by another user**:
   ```text
   ⚠️ 파일이 이미 잠겨있습니다:
   Owner: {username}
   Since: {date}

   락 강탈(steal)을 시도하시겠습니까? (위험: 다른 사용자의 작업이 유실될 수 있음)
   ```
   - IF user confirms steal: Add `--force`/`-f` flag
   - ELSE: **DONE**

3. **IF available**: Confirm and acquire:
   ```text
   {file}에 락을 걸까요?
   ```

   **P4**: `p4 lock <file>` (requires `p4 edit <file>` first)
   **SVN**: `svn lock <file>`
   **Git LFS**: `git lfs lock <file>`
   **Plastic**: `cm lock create br:/<current_branch> item:<file>` (branch-scoped; resolve current branch via `cm status --header`)

4. Display result:
   ```text
   ✅ 락 획득: {file}
   ```

5. **DONE**

---

### STEP 3: RELEASE LOCK

1. Verify current lock ownership:
   - Only owner can unlock (unless `--force`)

2. **IF not lock owner AND no --force**:
   ```text
   ⚠️ {username}의 락입니다. 본인의 락만 해제할 수 있습니다.
   강제 해제하시겠습니까? (관리자 권한 필요)
   ```

3. Execute unlock:

   **P4**: `p4 unlock <file>`
   **SVN**: `svn unlock <file>`
   **Git LFS**: `git lfs unlock <file>`
   **Plastic**: `cm lock unlock <file> --remove`

4. Display result:
   ```text
   ✅ 락 해제: {file}
   ```

5. **DONE**

---

## Binary Asset Warning

When detecting `.uasset` or `.umap` modifications without a lock (in any VCS skill):

```text
⚠️ 바이너리 에셋 수정 감지: {file}
   바이너리 파일은 머지할 수 없으므로 작업 전 락 획득을 권장합니다.
   락 확인: /vcs-lock {file} --status
   락 획득: /vcs-lock {file} --lock
```

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Lock not supported | Git without LFS, or VCS without lock feature | Display message, suggest alternative |
| Lock owned by another user | Concurrent editing | Display owner, suggest contacting them |
| Lock steal fails | Insufficient permissions | Need admin/superuser privileges |
| P4 lock fails | File not opened for edit | `p4 edit <file>` first, then lock |
| SVN lock fails | Server doesn't support locking | Check SVN server configuration |
| MCP unavailable | Server not running | Continue with CLI-only mode |

## Safety

- **Dry-run by default** — always check availability before acquiring.
- **NEVER force-steal** without explicit user confirmation.
- **Display owner info** for locked files — enable team communication.

## Self-Test Mode

`/vcs-lock --self-test` runs a focused validation:

1. **VCS Detection**: Verify VCS type can be detected in current directory
2. **CLI Availability**: Check that the detected VCS CLI is available (`git --version`, `svn --version`, `p4 -V`, `cm version`)
3. **Lock Support**: Verify lock capability (Git requires LFS: `git lfs env`, P4/SVN/Plastic: always available)
4. **Structure Check**: Verify SKILL.md, REFERENCE.md, EXAMPLES.md exist
5. **Frontmatter Check**: Parse YAML frontmatter for required fields (`name`, `description`, `allowed-tools`)
6. **Routing Check**: Verify routing.json entry exists and trigger patterns match

**Output**: PASS / FAIL with per-check results. Does not execute any VCS operations.

## Related

- [vcs-update](../vcs-update/SKILL.md) — 업데이트 전 락 확인
- [vcs-submit](../vcs-submit/SKILL.md) — 커밋 후 자동 언락 안내
- [vcs-resolve](../vcs-resolve/SKILL.md) — 충돌 방지를 위한 사전 락
