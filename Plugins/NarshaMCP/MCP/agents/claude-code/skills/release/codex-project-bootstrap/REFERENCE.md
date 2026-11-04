# Codex Project Bootstrap Reference

## Result model

The checker emits stable rows with `id`, `status`, `summary`, optional
`remediation`, and redacted evidence. Overall status is the strongest row:

| Status | Meaning | Exit behavior |
|---|---|---|
| `PASS` | The observed state satisfies the contract. | Check exits 0. |
| `WARN` | Setup is usable, but an optional tool or shell reload remains. | Check exits 0 when there are no blockers. |
| `BLOCKED` | A required prerequisite, safety proof, or manual trust decision is missing. | Check/apply exits 2. |

`--session-start` always exits 0 because startup diagnostics must not block the
client. Its only write is an opaque, bounded deduplication marker in the OS temp
directory; no repository, user configuration, registry, or trust state is
changed.

## State matrix

| Row | `PASS` proof | Non-PASS recovery |
|---|---|---|
| `python` | A validated `py -3`, `python3`, or `python` reports the repository minimum (currently 3.13+). | Install a supported Python and reopen the shell. The bootstrap never installs Python. |
| `uv` | A PATH-resolved `uv --version` and an unambiguous single-path `uv tool dir --bin` response both succeed; only its line terminator is removed, never path bytes. | Install/repair uv using its supported platform installer. |
| `pre_commit` | One validated argv works: Python module, PATH console script, or direct uv tool-bin executable. | Apply may run `uv tool install pre-commit`; offline failure stays blocked. |
| `uv_tool_bin_persisted` | Windows HKCU user PATH or every applicable startup chain for the effective POSIX shell ends in a statically proven normalized bin entry. | Apply may run `uv tool update-shell`. Unreadable/unknown commands, inactive files, prose mentions, and prefix/suffix path collisions are not success. |
| `uv_tool_bin_active` | The current process PATH contains the exact normalized bin entry. POSIX environment bytes are already shell-expanded, so literal `$HOME`, `~`, quotes, spaces, and backslashes are never reinterpreted. | Persisted-but-inactive is `WARN reload_pending`; open a new shell. A child PATH never proves persistence. |
| `core_hooks_path` | Only Git exit 1 (unset) or the exact default common-dir hooks location. Git path output is captured as bytes: `core.hooksPath` uses NUL framing, other path probes remove one final LF only, and undecodable bytes use the filesystem's surrogate-escape mapping. Trailing CR, spaces, and other valid path bytes remain identity-significant. | A redundant default is repairable. Custom, ambiguous, unreadable, and explicitly empty values are manual and block every apply mutation before hook inspection. |
| `git_hook_*` | All three files carry the pre-commit generated signature and their exact `--hook-type`: `pre-commit`, `pre-push`, `post-checkout`. | Apply invokes the repository installer; a foreign file is never accepted from substring text alone. |
| `git_lfs` | Git-LFS is available when `.gitattributes` declares LFS filters. | Install Git-LFS manually. Apply does not change LFS configuration. |
| `git_lfs_legacy` | Existing legacy bytes remain chained and their shebang resolves. | Restore disabled/missing bytes or make a shell available. A Git-bundled but inactive shell is remediation evidence, not a false PASS. |
| `gitleaks` | The optional executable validates with `--version`. | Missing is one non-blocking warning; install manually. |
| `codex_project_trust` | The normalized current worktree entry in user Codex TOML is explicitly `trusted`. | Review and trust the project manually. Apply never writes this file. |
| `codex_hook_source` | The root checkout's generated `.codex/hooks.json` is valid and the generator `--check` reports zero drift. | Regenerate only as a reviewed source change. Linked worktrees use the root checkout source; divergence blocks. |
| `codex_hook_trust` | Codex app-server `hooks/list` returns exactly one result for the requested worktree and its project rows exactly match the generated root-checkout inventory; every row is unmanaged, enabled, `trusted`, and has a valid `currentHash`. | Review `/hooks`. Missing, extra, duplicate, divergent, managed, plugin-owned, disabled, warning/error, or wrong-source project rows remain blocked. |

POSIX persistence is intentionally fail-closed. The checker accepts only bounded,
quote-aware top-level `PATH=`/`export`, `fish_add_path`, and global-export fish
`set` mutations, evaluates them in order, and invalidates a proven value after
any later reachable command whose PATH effect is unknown. Quoted PATH entries
retain literal leading and trailing spaces. A reachable top-level control-list
segment that may execute `return`, `exit`, or replacing `exec` prevents later
lines from proving persistence. More generally, any reachable non-PATH command
prevents later PATH lines from restarting proof. The sole relocation exception
is an expansion-free absolute `ZDOTDIR` assignment in `.zshenv`, already used
to select the inspected startup chain. Comment markers are recognized only at shell-word
boundaries, parameter expansion in those assignments is limited to plain
variable references, and top-level multiline commands remain opaque barriers.
An expanded tool-bin path must be absolute and cannot contain the platform PATH
delimiter; otherwise it is not representable as one entry and remains
`BLOCKED`. Functions, compound
control flow, groups/subshells, heredocs, multiline strings, and continuations
cannot create proof. Bash login/non-login chains, zsh `.zshenv`/`ZDOTDIR` plus
login/non-login ordering, and fish `XDG_CONFIG_HOME` are resolved separately;
quote semantics are retained, so a quoted tilde is not expanded and unresolved
variables fail closed. A set-but-empty `ZDOTDIR` points outside `HOME` and is
therefore treated as unreadable instead of certifying the wrong startup files.
An adjacent zsh word-code file (`.zshenv.zwc`,
`.zprofile.zwc`, `.zshrc.zwc`, or `.zlogin.zwc`) is opaque and `BLOCKED` when
the text file is absent or its second-granularity timestamp is equal/newer; a
strictly older companion cannot supersede the checked text. When `SHELL` is
unset, every detected bash, zsh, and fish-specific chain must prove the target;
the checker never lets a generic `.profile` hide an opaque shell-specific
startup file. All applicable chains must prove the target. A complex or compiled startup file may remain
`BLOCKED` by design—move the generated mutation to a final direct top-level
text statement or inspect it manually instead of weakening proof.

## Hook trust authority

The read-only app-server request is authoritative:

1. start `codex app-server` over stdio;
2. send JSONL `initialize`, then `initialized`;
3. send `hooks/list` with exactly the current worktree in `cwds`;
4. require exactly one normalized `data[].cwd` result and no discovery warnings or errors;
5. reconcile the project rows as an exact multiset against the validated generated
   source: event, handler type, matcher, platform command, timeout, status message,
   and root-checkout `sourcePath` must match;
6. require unique app-server keys plus `source=project`, `isManaged=false`, no
   plugin owner, `enabled=true`, `trustStatus=trusted`, and a valid `currentHash`.

The app-server protocol has emitted both snake-case and camel-case event values.
Only the closed aliases for the six generated events are accepted; arbitrary or
Pascal-case spellings do not become trusted inventory. Non-project rows from
user, system, or plugin sources are outside this project inventory and cannot
substitute for it.

If app-server is unavailable or times out, the checker may inspect recorded TOML
state only to distinguish missing from present records. A recorded `trusted_hash`
is private Codex state and never becomes `PASS` in fallback mode; it produces at
most `WARN` plus `/hooks` guidance. Hash values are not printed.

For a linked Git worktree, project trust applies to the current worktree path,
while hook declarations come from the primary/root checkout's `.codex/hooks.json`
resolved through `git worktree list` and `--git-common-dir`.
All Git probes that return paths are captured as raw bytes before parsing. A
single final LF is protocol framing for line-oriented probes, while
`core.hooksPath` is requested with `git config -z` and requires one final NUL.
The parser never applies universal-newline conversion, whitespace trimming, or
replacement-character decoding that could alias a distinct repository path.
The separate Windows `uv tool dir --bin` parser accepts LF or native CRLF
framing and then requires a normal ASCII-letter drive or UNC absolute path. It rejects
residual ASCII controls, Win32-reserved punctuation and device names, trailing
space/dot aliases, non-drive colons, and device/extended namespaces. Those
unusual namespaces require operator review instead of an automatic PATH proof.
This Windows-only rule is never applied to Git path identity or POSIX paths.

## Apply mutation boundary

`--apply` without `--yes` changes nothing and returns `BLOCKED`. With consent, the
driver may:

- install or refresh the uv-managed `pre-commit` tool;
- ask uv to persist its tool bin with `uv tool update-shell`;
- prepend that bin only to installer/post-check child environments;
- invoke `scripts/install_precommit_hooks.py` for the three configured hook types.

The installer child receives `NARSHA_GITLEAKS_PROVISION_DISABLED=1`, has
`NARSHA_GITLEAKS_AUTOINSTALL` removed, and has legacy rewriting disabled. Apply
captures Codex trust and Git-LFS bytes before mutation and fails closed if they
change. It never installs gitleaks, changes a custom hooks path, configures LFS,
approves hashes, writes project trust, or enables a trust bypass.

The legacy installer intentionally has an always-zero compatibility contract, so
its exit code is not proof. The bootstrap always runs the complete checker again;
unresolved required or manual rows remain `BLOCKED`.

## Recovery

- Python/uv missing: install manually, reopen the shell, run `--check`.
- PATH persisted but inactive: open a new shell. Do not rerun installers merely to
  refresh the current process.
- Offline pre-commit install: restore network/proxy access and rerun apply; no
  partial output is accepted.
- Custom hooks path: integrate the generated hooks through the existing manager,
  then rerun check. Bootstrap does not unset it.
- LFS shell blocker: add a usable shell to the hook process PATH or repair the
  interpreter manually without deleting/changing the legacy body.
- Project/hook trust: review the checkout and `/hooks` in Codex. There is no
  automated recovery because trust is an operator decision.

## Repository-local versus machine-local

Repository-local inputs are tracked configuration, generated hook declarations,
the three common-dir hook files, and any preserved LFS legacy files. Machine-local
inputs are installed executables, active/persisted PATH, user Codex TOML, trust
decisions, and SessionStart temp markers. A fresh clone therefore converges the
automatable subset first and remains explicitly blocked until the operator reviews
project and hook trust.
