# vcs-lock Examples

**Version**: 1.0.0
**Skill**: [SKILL.md](SKILL.md) | **Reference**: [REFERENCE.md](REFERENCE.md)

---

## Example 1: Query Lock Status (P4)

**Command**: `/vcs-lock Content/Blueprints/BP_Player.uasset`

**VCS**: Perforce (auto-detected), default `--status` mode

### Sample Output
```text
VCS Detected: p4
Lock Support: Native

=== Lock Status: Content/Blueprints/BP_Player.uasset ===
VCS:    p4
Locked: yes
Owner:  john.smith (since 2026-04-08 14:30)

⚠️ 파일이 john.smith에 의해 잠겨있습니다.
   연락하여 작업 완료 시 언락을 요청하세요.
```

---

## Example 2: Acquire Lock (SVN)

**Command**: `/vcs-lock Content/Blueprints/BP_Enemy.uasset --lock`

**VCS**: SVN (auto-detected)

### Sample Output
```text
VCS Detected: svn
Lock Support: Native

Dry-run check:
  svn info Content/Blueprints/BP_Enemy.uasset | grep "Lock"
  → No lock found. File is available.

Content/Blueprints/BP_Enemy.uasset에 락을 걸까요? [Y/n]

Executing: svn lock Content/Blueprints/BP_Enemy.uasset

✅ 락 획득: Content/Blueprints/BP_Enemy.uasset
```

---

## Example 3: Release Lock (Git LFS)

**Command**: `/vcs-lock Content/Maps/MainLevel.umap --unlock`

**VCS**: Git (auto-detected, LFS enabled)

### Sample Output
```text
VCS Detected: git
Lock Support: Git LFS

Current lock check:
  git lfs locks --path=Content/Maps/MainLevel.umap
  → Locked by: you (hyunwoo.kang) since 2026-04-09 10:00

Executing: git lfs unlock Content/Maps/MainLevel.umap

✅ 락 해제: Content/Maps/MainLevel.umap
```

---

## Example 4: Binary Asset Warning (Auto-Triggered)

**Context**: User modifies a `.uasset` file without a lock (detected by any VCS skill)

### Sample Output
```text
⚠️ 바이너리 에셋 수정 감지: Content/Blueprints/BP_Player.uasset
   바이너리 파일은 머지할 수 없으므로 작업 전 락 획득을 권장합니다.
   락 확인: /vcs-lock Content/Blueprints/BP_Player.uasset --status
   락 획득: /vcs-lock Content/Blueprints/BP_Player.uasset --lock
```
