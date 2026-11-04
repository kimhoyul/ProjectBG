# vcs-update Reference

**Version**: 1.0.0
**Skill**: [SKILL.md](SKILL.md) | **Examples**: [EXAMPLES.md](EXAMPLES.md)

---

## Phase Summary

| Phase | Purpose | Key Output |
|-------|---------|------------|
| STEP 0 | VCS Detection | VCS type (git/svn/p4/plastic) |
| STEP 1 | Pre-Update Check | Uncommitted changes warning |
| STEP 2 | Execute Update | Updated working copy |
| STEP 3 | Summary Report | Added/Modified/Deleted file counts |
| STEP 4 | Conflict Handling | Conflict list + `/vcs-resolve` routing |

---

## VCS Command Matrix

| Operation | Git | SVN | P4 | PlasticSCM |
|-----------|-----|-----|----|----|
| Detect | `[ -d ".git" ]` | `[ -d ".svn" ]` | `[ -f ".p4config" ] \|\| [ -n "$P4PORT" ]` | `[ -d ".plastic" ]` |
| Check status | `git status --short` | `svn status` | `p4 opened` | `cm status` |
| Fetch/Preview | `git fetch origin` | N/A | `p4 sync -n` | N/A |
| Update | `git pull origin <branch>` | `svn update` | `p4 sync` | `cm update` |
| Diff after update | `git diff --stat HEAD@{1}..HEAD` | Parse `svn update` output | Parse `p4 sync` output | Parse `cm update` output |
| List conflicts | `git diff --name-only --diff-filter=U` | `svn status \| grep "^C"` | `p4 resolve -n` | `cm status --short \| grep "^C"` (untested) |

---

## Pre-Update Check Rules

- **Uncommitted changes detected**: Warn user about potential merge conflicts
- **Git recommendation**: Suggest `git stash` before pull
- **SVN/P4**: Changes can coexist with update, but conflicts are possible
- **User confirmation required** before proceeding when changes exist

## Large Repository Handling

- **SVN 71K+ items**: Use `run_in_background: true` to avoid Bash timeout
- **P4 large sync**: Preview with `p4 sync -n` first, then execute
- **Monitoring**: Periodic status checks during long operations

## Error Recovery Details

| Error | Cause | Recovery Steps |
|-------|-------|----------------|
| VCS not detected | Not in project root | Navigate to project root with VCS markers (.git, .svn, etc.) |
| Git pull fails (diverged) | Local and remote diverged | Try `git pull --rebase`, or manually merge with `git fetch && git merge` |
| SVN update timeout | Large repo (71K+ items) | Use `run_in_background` + periodic `svn info` status check |
| P4 sync fails | Workspace not configured | Run `p4 info` and `p4 client` to diagnose workspace issues |
| MCP unavailable | Server not running | Continue with CLI-only mode (all commands work without MCP) |

## Safety Rules

- **Read-only by default**: Update only downloads changes, does not modify user files (except merges)
- **Conflict routing**: Conflicts are never auto-resolved; always routed to `/vcs-resolve`
- **No force operations**: Never uses `--force`, `--hard`, or destructive flags
