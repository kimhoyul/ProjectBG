# UE Config Audit — Reference (Cold Content)

Progressive-disclosure companion to [SKILL.md](SKILL.md) (Issue #20050). The hot workflow —
the 2-tier rollup, the two op calls, and dimension scoring — lives in SKILL.md; this file
holds the full trigger lists, output template, error-recovery table, and evaluation criteria.

---

## Triggers & Disambiguation

### Auto-Trigger Phrases

**Korean**: "config 감사해줘", "설정 감사", "config 헬스", "설정 건강 진단", "config 린트",
"설정 파일 검사", "이 config 건강한가", "설정 건강도".

**English**: "config audit", "config health", "config health check", "config lint",
"audit config files", "lint config", "is my config healthy".

### Should NOT Activate — Disambiguation

| Skill | Purpose | When to Use |
|-------|---------|-------------|
| `/ue-config-audit` | **Whole-config health rollup** (lint 5 dimensions + deprecated, scorecard + fixes) | "Is my config healthy?", pre-release config sweep, CI config gate |
| `/ue-cvar-explorer` | **Single-CVar deep dive** (ECVF flags, scalability mapping, change impact) | "What does `r.Shadow.MaxResolution` do?" — one CVar, not a health sweep |
| `/ue-audit` | **Whole-project 6-point health** (BP complexity, C++ arch, build time, assets) | Project-wide health, not config-specific |

Complementary: config-audit is the config-scoped health lane; cvar-explorer inspects one
setting; ue-audit is the project-wide diagnostic. A broad "find all X settings" sweep is
`ue_grep`, not this skill.

- "r.Shadow.MaxResolution 뭐야?" → ❌ Use `/ue-cvar-explorer` (single CVar)
- "프로젝트 건강 진단" → ❌ Use `/ue-audit` (whole project)
- "모든 그림자 설정 찾아줘" → ❌ Use `ue_grep` (broad sweep)

---

## Output Format

```text
=== Config Health Audit ===
Scope: <whole-config | DefaultEngine.ini>   Files scanned: N   Engine: <version | offline>

--- Health Scorecard ---
| Dimension            | Findings | Grade |
|----------------------|----------|-------|
| Unknown CVar         | 2        | WARN  |
| Deprecated           | 1        | WARN  |
| Type error           | 0        | PASS  |
| Conflicting override | 3        | WARN  |
| Platform sanity      | 1        | WARN  |
| Hygiene (redundant)  | 5        | info  |
| ---                  |          |       |
| OVERALL              |          | WARN  |

Skipped checks (reported N/A, not PASS): <none | unknown_cvar: engine path unavailable>

--- Top Fixes (ranked) ---
1. [WARN] Config/DefaultEngine.ini:42  r.Shaddow.MaxResolution
   Unknown CVar — likely typo of r.Shadow.MaxResolution. Fix the key or remove the line.
2. [WARN] Config/DefaultGame.ini:12  OldNetDriver
   Deprecated in 5.8 -> replace with <suggested replacement from scan_deprecated>.
3. [WARN] Config/Windows/WindowsEngine.ini:7  bUseVSync
   Type error — 'yes' is not a valid boolean; use True/False.
4. [WARN] Config/DefaultEngine.ini:88  MaxFPS
   Re-assigned at line 120 — this line never takes effect. Delete the dead override.
5. [info] Config/Android/AndroidEngine.ini:5  bMobileHDR
   Restates the base value — dead line, safe to remove.

--- Verdict ---
Config health: WARN — 7 actionable findings across 4 dimensions, 5 hygiene lines.
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `Config directory not found` | Wrong `project_root` or non-UE project | Report the resolved path; ask the user to confirm the project root |
| `unknown_cvar` in `skipped_checks` | Engine path unavailable | Grade that dimension **N/A (engine offline)** — do NOT report it as PASS |
| `scan_deprecated` returns nothing + no engine | Migration DB needs an engine version | Report deprecated dimension as N/A; still emit the four offline dimensions |
| `scan_deprecated` `success:false` "source_version (X) must be less than target_version (Y)" | `--target` resolved to <= the source default (5.2) | `--target` must exceed 5.2; re-run with a higher target (e.g. `--target 5.8`). Grade the deprecated dimension N/A meanwhile |
| 0 findings across every category | Config genuinely clean OR only skipped checks | If any check was skipped, say "clean for the checks that ran"; else "healthy" |
| `truncated: true` | More findings than `limit` per category | Note the truncation; re-run with a higher `limit` for the full list |

> **MCP Empty-Result Fallback**: a `skipped_checks` entry is not a passing dimension. Never
> assert "config is healthy" when engine-backed checks were skipped — say which dimensions
> could not be verified.

---

## Evaluation Criteria

### Activation Test Cases

**Positive (5)** — Should activate:
1. "config 감사해줘" -> Activate (whole-config audit)
2. "Is my config healthy?" -> Activate
3. "config lint DefaultEngine.ini" -> Activate (scoped)
4. "설정 건강 진단" -> Activate
5. "audit config files" -> Activate

**Negative (3)** — Should NOT activate:
1. "r.Shadow.MaxResolution 뭐야?" -> Use `/ue-cvar-explorer` (single-CVar deep dive)
2. "프로젝트 건강 진단" -> Use `/ue-audit` (whole-project health)
3. "모든 그림자 설정 찾아줘" -> Use `ue_grep` (broad sweep)

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Dimensions scored | 5 (+ hygiene advisory) |
| Tool call count | 2-4 (Bash excluded) |
| Skipped-check honesty | 100% (never grade a skipped check PASS) |
| Response time | <10s for whole-config (offline) |
