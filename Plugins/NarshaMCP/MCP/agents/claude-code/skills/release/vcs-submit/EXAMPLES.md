# vcs-submit Examples

**Version**: 1.0.0
**Skill**: [SKILL.md](SKILL.md) | **Reference**: [REFERENCE.md](REFERENCE.md)

---

## Example 1: Git Commit (Auto-Message)

**Command**: `/vcs-submit`

**VCS**: Git (auto-detected)

### Sample Output
```text
VCS Detected: git
Branch: feat/8342-vcs-skills-series

Changed Files:
  C++ (.h/.cpp):       2 files
  Blueprint (.uasset): 0 files
  Asset (.uasset/.umap): 0 files
  Config (.ini):       1 file
  Other:               0 files
  Total:               3 files

Proposed commit message:
  feat(Characters): add AMyHero class with GAS setup

수정하시겠습니까? (Enter to accept, or type new message)

=== Submit Preview ===
VCS: git
Files: 3 files (C++ 2, Config 1)
Message: feat(Characters): add AMyHero class with GAS setup

이대로 제출할까요?

═══════════════════════════════════════════
 VCS Submit Complete (git)
═══════════════════════════════════════════
 Revision: a1b2c3d
 Message:  feat(Characters): add AMyHero class with GAS setup
 Files:    3 files submitted
   C++:       2
   Blueprint: 0
   Asset:     0
   Config:    1
   Other:     0
═══════════════════════════════════════════
```

---

## Example 2: SVN Commit (Custom Message)

**Command**: `/vcs-submit --message "fix: resolve player spawn crash"`

**VCS**: SVN (auto-detected)

### Sample Output
```text
VCS Detected: svn

Changed Files:
  C++ (.h/.cpp):       1 file
  Config (.ini):       0 files
  Total:               1 file

=== Submit Preview ===
VCS: svn
Files: 1 file (C++ 1)
Message: fix: resolve player spawn crash

이대로 제출할까요?

═══════════════════════════════════════════
 VCS Submit Complete (svn)
═══════════════════════════════════════════
 Revision: r5724
 Message:  fix: resolve player spawn crash
 Files:    1 file submitted
   C++:       1
═══════════════════════════════════════════
```

---

## Example 3: P4 Submit

**Command**: `/vcs-submit`

**VCS**: Perforce (auto-detected)

### Sample Output
```text
VCS Detected: p4
Workspace: MyGame_workspace

Changed Files (p4 opened):
  C++ (.h/.cpp):       3 files
  Blueprint (.uasset): 1 file
  Asset (.uasset/.umap): 2 files
  Config (.ini):       1 file
  Total:               7 files

Proposed commit message:
  feat(Weapons): add projectile weapon system

=== Submit Preview ===
VCS: p4
Files: 7 files (C++ 3, Blueprint 1, Asset 2, Config 1)
Message: feat(Weapons): add projectile weapon system

이대로 제출할까요?

═══════════════════════════════════════════
 VCS Submit Complete (p4)
═══════════════════════════════════════════
 Revision: @12346
 Message:  feat(Weapons): add projectile weapon system
 Files:    7 files submitted
   C++:       3
   Blueprint: 1
   Asset:     2
   Config:    1
   Other:     0
═══════════════════════════════════════════
```

---

## Example 4: Git Commit with Impact Analysis

**Command**: `/vcs-submit --check`

**VCS**: Git

### Sample Output
```text
VCS Detected: git
Branch: fix/5400-crash-on-spawn

Changed Files:
  C++ (.h/.cpp):       4 files
  Blueprint (.uasset): 2 files
  Total:               6 files

Impact Analysis:
  C++ modules affected: Characters, Abilities
  Blueprints referencing changed code: BP_Player, BP_Enemy
  Config changes: none
  Risk level: MEDIUM

Proposed commit message:
  fix(Characters): resolve null pointer in spawn logic

이대로 제출할까요?

═══════════════════════════════════════════
 VCS Submit Complete (git)
═══════════════════════════════════════════
 Revision: e5f6g7h
 Message:  fix(Characters): resolve null pointer in spawn logic
 Files:    6 files submitted
   C++:       4
   Blueprint: 2
═══════════════════════════════════════════
```
