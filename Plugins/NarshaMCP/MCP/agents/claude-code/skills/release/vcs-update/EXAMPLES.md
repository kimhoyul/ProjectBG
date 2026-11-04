# vcs-update Examples

**Version**: 1.0.0
**Skill**: [SKILL.md](SKILL.md) | **Reference**: [REFERENCE.md](REFERENCE.md)

---

## Example 1: Git Pull (Default)

**Command**: `/vcs-update`

**VCS**: Git (auto-detected)

### Sample Output
```text
VCS Detected: git
Branch: feat/8342-vcs-skills-series

Pre-Update Check:
  No uncommitted changes.

Executing: git fetch origin && git pull origin feat/8342-vcs-skills-series

═══════════════════════════════════════════
 VCS Update Complete (git)
═══════════════════════════════════════════
 Updated to: a1b2c3d
 Files:
   Added:    3
   Modified: 7
   Deleted:  1
 Conflicts: none
═══════════════════════════════════════════
```

---

## Example 2: SVN Update (Large Repository)

**Command**: `/vcs-update`

**VCS**: SVN (auto-detected)

### Sample Output
```text
VCS Detected: svn
Working Copy: /projects/MyGame

Pre-Update Check:
  ⚠️ 로컬 변경사항이 있습니다. 업데이트 시 충돌 가능성 있음.
    M  Source/MyGame/Character.cpp
    M  Config/DefaultGame.ini
  계속 진행할까요?

Executing: svn update (run_in_background — 71K+ items)

═══════════════════════════════════════════
 VCS Update Complete (svn)
═══════════════════════════════════════════
 Updated to: r5723
 Files:
   Added:    12
   Modified: 45
   Deleted:  3
 Conflicts: 2
═══════════════════════════════════════════

⚠️ 2개 충돌 감지:
  [텍스트]  Source/MyGame/Character.cpp
  [바이너리] Content/BP_Player.uasset

충돌 해결: /vcs-resolve
```

---

## Example 3: P4 Sync

**Command**: `/vcs-update`

**VCS**: Perforce (auto-detected)

### Sample Output
```text
VCS Detected: p4
Workspace: MyGame_workspace

Pre-Update Check:
  p4 opened: 2 files
    //depot/Source/MyGame/Hero.cpp#3 - edit
    //depot/Config/DefaultEngine.ini#5 - edit

Preview (p4 sync -n):
  3 files to update, 1 file to add, 0 to delete

Executing: p4 sync

═══════════════════════════════════════════
 VCS Update Complete (p4)
═══════════════════════════════════════════
 Updated to: @12345
 Files:
   Added:    1
   Modified: 3
   Deleted:  0
 Conflicts: none
═══════════════════════════════════════════
```

---

## Example 4: Git Pull with Rebase

**Command**: `/vcs-update --rebase`

**VCS**: Git

### Sample Output
```text
VCS Detected: git
Branch: feat/5123-config-hierarchy

Pre-Update Check:
  No uncommitted changes.

Executing: git fetch origin && git pull origin feat/5123-config-hierarchy --rebase

═══════════════════════════════════════════
 VCS Update Complete (git)
═══════════════════════════════════════════
 Updated to: d4e5f6g (rebased)
 Files:
   Added:    0
   Modified: 2
   Deleted:  0
 Conflicts: none
═══════════════════════════════════════════
```
