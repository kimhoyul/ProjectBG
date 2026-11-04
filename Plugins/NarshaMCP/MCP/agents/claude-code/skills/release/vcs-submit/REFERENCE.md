# vcs-submit Reference

**Version**: 1.0.0
**Skill**: [SKILL.md](SKILL.md) | **Examples**: [EXAMPLES.md](EXAMPLES.md)

---

## Phase Summary

| Phase | Purpose | Key Output |
|-------|---------|------------|
| STEP 0 | VCS Detection | VCS type (git/svn/p4/plastic) |
| STEP 1 | Collect Changed Files | File list + UE category classification |
| STEP 2 | Impact Analysis (optional) | Risk level + affected modules |
| STEP 3 | Commit Message | Auto-generated conventional commit message |
| STEP 4 | Execute Submit | Committed revision/hash |
| STEP 5 | Result Report | Summary with file counts by category |

---

## VCS Command Matrix

| Operation | Git | SVN | P4 | PlasticSCM |
|-----------|-----|-----|----|----|
| Detect | `[ -d ".git" ]` | `[ -d ".svn" ]` | `[ -f ".p4config" ] \|\| [ -n "$P4PORT" ]` | `[ -d ".plastic" ]` |
| List changes | `git status --short` | `svn status` | `p4 opened` | `cm status --short` |
| Stage files | `git add <files>` | N/A (auto) | N/A (already opened) | N/A (auto) |
| Commit | `git commit -m "msg"` | `svn commit -m "msg"` | `p4 submit -d "msg"` | `cm checkin -c "msg"` |

---

## UE Asset Classification Rules

Files are classified by extension into UE-specific categories:

| Category | Extensions | Examples |
|----------|-----------|---------|
| C++ | `.h`, `.cpp` | `Source/MyGame/Character.h` |
| Blueprint | `.uasset` (in `Blueprints/` or `BP_` prefix) | `Content/Blueprints/BP_Player.uasset` |
| Asset | `.uasset`, `.umap` (non-Blueprint) | `Content/Maps/MainLevel.umap` |
| Config | `.ini` | `Config/DefaultGame.ini` |
| Other | Everything else | `README.md`, `.build.cs` |

## Conventional Commit Patterns

Auto-generated message format: `<type>(<scope>): <description>`

| Detection Signal | Commit Type |
|-----------------|-------------|
| New files dominant | `feat:` |
| Bug fix patterns (fix, patch, resolve) | `fix:` |
| Config/build changes only | `chore:` |
| Test files only | `test:` |
| Documentation only | `docs:` |

**Scope**: Derived from the most common module directory among changed files.

## Error Recovery Details

| Error | Cause | Recovery Steps |
|-------|-------|----------------|
| No changes to submit | Working directory clean | Verify files are saved and tracked |
| Git commit fails (hooks) | Pre-commit hook failure | Read hook output, fix issues, retry |
| SVN commit fails (out of date) | Need update first | Run `/vcs-update` before retry |
| P4 submit fails (locked files) | Another user has lock | Check lock owner with `/vcs-lock`, contact them |
| P4 submit fails (resolve needed) | Merge required | Run `/vcs-resolve` first |

## Safety Rules

- **NEVER auto-push** after Git commit — push requires explicit user request
- **ALWAYS confirm** before executing commit/submit
- **Dry-run by default** for P4 submit (show preview first)
- **No --force flags** — never force-push or override safeguards
