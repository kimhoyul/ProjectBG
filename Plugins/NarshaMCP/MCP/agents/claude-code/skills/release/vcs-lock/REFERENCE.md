# vcs-lock Reference

**Version**: 1.0.0
**Skill**: [SKILL.md](SKILL.md) | **Examples**: [EXAMPLES.md](EXAMPLES.md)

---

## Phase Summary

| Phase | Purpose | Key Output |
|-------|---------|------------|
| STEP 0 | VCS Detection + Lock Support Check | VCS type + lock capability |
| STEP 1 | Query Lock Status | Lock owner, date, file status |
| STEP 2 | Acquire Lock | Lock confirmation or conflict info |
| STEP 3 | Release Lock | Unlock confirmation |

---

## VCS Lock Support Matrix

| VCS | Lock Support | Lock Command | Unlock Command | Notes |
|-----|-------------|--------------|----------------|-------|
| Perforce | Native | `p4 lock <file>` | `p4 unlock <file>` | Requires `p4 edit` first |
| SVN | Native | `svn lock <file>` | `svn unlock <file>` | Requires server config |
| Git | Git LFS only | `git lfs lock <file>` | `git lfs unlock <file>` | Requires LFS setup |
| PlasticSCM | Native | `cm lock create br:/<current_branch> item:<file>` | `cm lock unlock <file> --remove` | Branch-scoped locks (best-effort, untested) |
| Lore | Planned (Epic 2026) | *native forced lock — TBD* | *native unlock — TBD* | Issue #22277: write pipeline returns `Unsupported { vcs: "lore" }` (warn-and-proceed) until Lore forced-locking API lands. **Do not** invent KV-based forced locks. Agent sandbox leases live in `rust_indexer::agent_workflow`. |

---

## VCS Command Matrix

| Operation | Git (LFS) | SVN | P4 | PlasticSCM |
|-----------|-----------|-----|----|----|
| Check lock | `git lfs locks --path=<file>` | `svn info <file> \| grep "Lock"` | `p4 fstat -T "otherLock,otherOpen" <file>` | `cm lock list <file>` |
| List all locks | `git lfs locks` | `svn status -u` | `p4 opened -a` | `cm lock list` |
| Acquire lock | `git lfs lock <file>` | `svn lock <file>` | `p4 edit <file> && p4 lock <file>` | `cm lock create br:/branch item:<file>` |
| Release lock | `git lfs unlock <file>` | `svn unlock <file>` | `p4 unlock <file>` | `cm lock unlock <file> --remove` |
| Force steal | `git lfs unlock --force <file>` | `svn lock --force <file>` | `p4 lock -f <file>` | `cm lock create --force br:/branch item:<file>` |

## Lock Acquisition Flow

```text
1. Check if file is already locked
   ├── Not locked → Confirm with user → Acquire lock
   └── Locked by another user
       ├── Display owner + date
       └── Ask: Force-steal? (DANGEROUS)
           ├── Yes → Acquire with --force
           └── No → Exit (suggest contacting owner)
```

## Binary Asset Warning

When `.uasset` or `.umap` modifications are detected without a lock (triggered in any VCS skill):

```text
⚠️ 바이너리 에셋 수정 감지: {file}
   바이너리 파일은 머지할 수 없으므로 작업 전 락 획득을 권장합니다.
   락 확인: /vcs-lock {file} --status
   락 획득: /vcs-lock {file} --lock
```

## Error Recovery Details

| Error | Cause | Recovery Steps |
|-------|-------|----------------|
| Lock not supported | Git without LFS | Display message, suggest Git LFS setup |
| Lock owned by another user | Concurrent editing | Display owner info, suggest contacting them |
| Lock steal fails | Insufficient permissions | Need admin/superuser privileges |
| P4 lock fails | File not opened for edit | Run `p4 edit <file>` first, then lock |
| SVN lock fails | Server doesn't support locking | Check SVN server configuration |

## Safety Rules

- **Dry-run by default** — always check availability before acquiring
- **NEVER force-steal** without explicit user confirmation
- **Display owner info** for locked files — enable team communication
