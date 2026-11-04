# vcs-resolve Examples

**Version**: 1.0.0
**Skill**: [SKILL.md](SKILL.md) | **Reference**: [REFERENCE.md](REFERENCE.md)

---

## Example 1: Text Conflict Resolution (Git)

**Command**: `/vcs-resolve`

**VCS**: Git (auto-detected)

### Sample Output
```text
VCS Detected: git

⚠️ 3개 충돌 감지:

[텍스트 - C++]
  Source/MyGame/Character.cpp

[텍스트 - Config]
  Config/DefaultGame.ini

[바이너리 - Blueprint]
  Content/Blueprints/BP_Player.uasset

--- Analyzing: Source/MyGame/Character.cpp ---

Conflict Region 1 (lines 45-52):
  <<<<<<< HEAD
    Health = 100.0f;
  =======
    Health = MaxHealth;
  >>>>>>> origin/main

  Analysis: Both sides modify Health initialization.
  - HEAD: hardcoded value (100.0f)
  - origin/main: uses MaxHealth variable (more flexible)
  Recommendation: Accept origin/main (uses variable, better practice)

  Apply this resolution? [Y/n]

✅ Resolved: Source/MyGame/Character.cpp (AI-merged)
```

---

## Example 2: Binary Conflict Resolution (SVN)

**Command**: `/vcs-resolve`

**VCS**: SVN (auto-detected)

### Sample Output
```text
VCS Detected: svn

⚠️ 1개 충돌 감지:

[바이너리 - Blueprint]
  Content/Blueprints/BP_Player.uasset

바이너리 파일은 병합할 수 없습니다. 하나를 선택하세요:

[theirs] 원격 버전 사용 — 서버의 최신 변경 반영
[mine]   로컬 버전 유지 — 내 작업 유지

최근 수정:
  user_a: 2026-04-08 — feat: add dash ability to BP_Player
  user_b: 2026-04-07 — fix: adjust BP_Player collision

선택: theirs

Executing: svn resolve --accept=theirs-full Content/Blueprints/BP_Player.uasset

═══════════════════════════════════════════
 VCS Resolve Complete (svn)
═══════════════════════════════════════════
 Resolved:  1 files
   Text:      0 (AI-merged: 0, manual: 0)
   Binary:    1 (theirs: 1, mine: 0)
 Remaining: none
═══════════════════════════════════════════

다음 단계: /vcs-submit (변경 사항 커밋)
```

---

## Example 3: Batch Resolve (--theirs)

**Command**: `/vcs-resolve --theirs`

**VCS**: Git

### Sample Output
```text
VCS Detected: git

⚠️ 5개 충돌 감지:

[텍스트 - C++]
  Source/MyGame/Character.cpp
  Source/MyGame/Character.h

[텍스트 - Config]
  Config/DefaultGame.ini

[바이너리 - Blueprint]
  Content/Blueprints/BP_Player.uasset
  Content/Blueprints/BP_Enemy.uasset

--theirs 모드: 모든 파일을 원격 버전으로 해결합니다.

Executing:
  git checkout --theirs Source/MyGame/Character.cpp && git add Source/MyGame/Character.cpp
  git checkout --theirs Source/MyGame/Character.h && git add Source/MyGame/Character.h
  git checkout --theirs Config/DefaultGame.ini && git add Config/DefaultGame.ini
  git checkout --theirs Content/Blueprints/BP_Player.uasset && git add Content/Blueprints/BP_Player.uasset
  git checkout --theirs Content/Blueprints/BP_Enemy.uasset && git add Content/Blueprints/BP_Enemy.uasset

═══════════════════════════════════════════
 VCS Resolve Complete (git)
═══════════════════════════════════════════
 Resolved:  5 files
   Text:      3 (AI-merged: 0, manual: 3)
   Binary:    2 (theirs: 2, mine: 0)
 Remaining: none
═══════════════════════════════════════════
```

---

## Example 4: Config Conflict (P4)

**Command**: `/vcs-resolve Config/DefaultGame.ini`

**VCS**: Perforce (auto-detected)

### Sample Output
```text
VCS Detected: p4

⚠️ 1개 충돌 감지 (필터: Config/DefaultGame.ini):

[텍스트 - Config]
  Config/DefaultGame.ini

--- Analyzing: Config/DefaultGame.ini ---

Conflict Region 1 ([/Script/Engine.GameSession]):
  <<<<<<< yours
    MaxPlayers=16
  =======
    MaxPlayers=32
  >>>>>>> theirs

  Analysis: Both sides modify MaxPlayers.
  - yours: 16 players
  - theirs: 32 players
  Recommendation: Confirm with team which value is intended.

  Which value? [theirs/mine/custom]:

  User selected: theirs (MaxPlayers=32)

✅ Resolved: Config/DefaultGame.ini
Executing: p4 resolve -at Config/DefaultGame.ini

═══════════════════════════════════════════
 VCS Resolve Complete (p4)
═══════════════════════════════════════════
 Resolved:  1 files
   Text:      1 (AI-merged: 0, manual: 1)
   Binary:    0
 Remaining: none
═══════════════════════════════════════════
```
