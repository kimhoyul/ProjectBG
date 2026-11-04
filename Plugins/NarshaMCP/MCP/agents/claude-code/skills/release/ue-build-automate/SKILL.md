---
name: ue-build-automate
model: opus
description: "Purpose: Automate UE build/cook/package pipelines using ue_build_pipeline. Answers 'Build my project', 'Cook for Windows', 'Package for shipping', 'Run commandlet'. 12 operations: build, cook, package, BuildCookRun, commandlet, Gauntlet, list_targets, get_build_history, get_build_stats. Target auto-discovery from .Target.cs + build history tracking. Triggers: '빌드 자동화', '쿡', '패키징', '커맨드릿 실행', 'build project', 'cook', 'package', 'run commandlet', 'BuildCookRun', '빌드 이력', 'build history', '빌드 타겟'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "--build (editor build), --cook (cook content), --package (full package), --bcr (BuildCookRun), --commandlet <name>"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Build Automate

**Issue**: #6099
**Purpose**: Automate UE build, cook, and package pipelines via MCP

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!
> **Non-negotiable execution gate (mirrors ue-scaffold #17830).** A build / cook / package / commandlet request — **however terse or specific** ("커맨드릿 CompileAllBlueprints 실행해줘") — is a **pipeline-execution** request, NOT a documentation request: EVERY invocation MUST reach **Step 2** and actually call `ue_build_pipeline(...)` **for real**, using the operation the intent maps to (`run_commandlet` for a commandlet, `build_editor` for build, `cook` for cook, `package` for package, `buildcookrun` for BCR), then report the returned result. A prose / general-knowledge answer that only explains *how* to build or run a commandlet — with **no `ue_build_pipeline` tool call** — is a **FAILED build automation**. In a non-interactive / single-shot / eval run, do NOT pause at the Step 1.6 confirmation gate; proceed with sensible defaults (Development / Win64) and CALL the tool.

### Execution Requirements
1. **Intent**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user what build operation to run (build, cook, package, commandlet, or full pipeline).
2. **Select pipeline operation** based on intent
3. **Execute MCP build calls** with appropriate parameters
4. **Monitor result** and report success/failure with error details

---

## Workflow

### Step 0: Editor Status Check (Issue #8164)

Before a build, rely on the server preflight to detect the Editor, save dirty assets, shut it down,
and wait for exit. With `auto_restart=true`, let that path own shutdown. Details: [REFERENCE.md §
Editor Preflight](REFERENCE.md#editor-preflight).

### Step 1: Intent Detection

Classify user request into pipeline stage:

```python
PIPELINE_ROUTES = {
    "build":      ["빌드", "컴파일", "build", "compile"],
    "cook":       ["쿡", "쿠킹", "cook", "cooking"],
    "package":    ["패키징", "배포", "package", "shipping", "deploy"],
    "bcr":        ["BuildCookRun", "BCR", "풀 빌드", "full build"],
    "commandlet": ["커맨드릿", "commandlet", "resave", "validate"],
    "gauntlet":   ["가운틀렛", "gauntlet", "자동화 테스트"]
}
```markdown

### Step 1.5: Target Discovery (Issue #6881)

Before building, discover available targets from `.Target.cs` files:

```python
ue_build_pipeline(operation="list_targets")
```

This returns all available build targets with their types (Game, Editor, Client, Server).
Use `recommended_editor_target` for `build_editor` operations.

> **Why**: Auto-derived target names (`.uproject` + "Editor") often mismatch actual targets.
> Example: `ElectricDreamsEnv.uproject` → wrong `ElectricDreamsEnvEditor`, correct `ElectricDreamsSampleEditor`.

### Step 1.6: Confirmation Gate

> **All 8 operations spawn real UE processes (UBT/UAT).** There is no dry-run or info-only mode.
> Always confirm intent before execution to avoid accidental full builds.
>
> **Interactive only, and only for short / low-risk ops.** This confirmation gate is for interactive sessions. In a **non-interactive / single-shot / eval run**, skip the `Proceed? [y/N]` stop **only for the short / low-risk operations** (`run_commandlet`, `build_editor`, `cook`, `list_targets`) — emitting the block as your final answer there produces a text-only response with no tool call (the exact QUAL-03 failure); instead proceed with detected/default parameters and CALL `ue_build_pipeline`. **For the long-running / high-cost operations (`buildcookrun`, `package`, `gauntlet_run`), keep `Proceed? [y/N]` MANDATORY regardless of interactivity** — an unattended 30-60 min build must never start unconfirmed.

Before executing, present the detected parameters to the user:

```text
Detected: operation={op}, platform={platform}, configuration={config}
Estimated time: {estimate}
Proceed? [y/N]
```markdown

**Time estimates by operation** → see [REFERENCE.md § Time Estimates](REFERENCE.md#time-estimates).

**Rules**:
- Ambiguous requests ("빌드해줘") → in an interactive session, ask the user to specify operation + platform + configuration; in a non-interactive / single-shot run, do NOT stop to ask — default to `operation=build_editor` (Smart Mode's own no-param fallback — lowest-risk, shortest), fill defaults (Development / Win64), and CALL `ue_build_pipeline`.
- `buildcookrun` → always warn: "Full BuildCookRun may take 30+ minutes on large projects"
- Never use `operation="smart"` without explicit params — it defaults to `build_editor` (full compile)

### Step 2: Execute Pipeline

> **Smart Mode Routing Reference** → see [REFERENCE.md § Smart Mode Routing Reference](REFERENCE.md#smart-mode-routing-reference).
> **Always specify `operation` explicitly** to avoid unintended smart routing.

#### 2A: Editor Build (compile only)

```python
ue_build_pipeline(operation="build_editor", configuration="Development", platform="Win64", timeout=300)
```markdown

#### 2B: Cook Content

```python
ue_build_pipeline(operation="cook", platform="Win64", configuration="Development", timeout=900)
```markdown

#### 2C: Package Project

```python
ue_build_pipeline(
    operation="package",
    platform="Win64",
    configuration="Shipping",
    output_dir="<output_path>",
    timeout=600,
)
```markdown

#### 2D: BuildCookRun (full pipeline)

```python
ue_build_pipeline(
    operation="buildcookrun",
    platform="Win64",
    configuration="Shipping",
    cook=true,
    package=true,
    timeout=1800,
)
```markdown

#### 2E: Run Commandlet

> **This is a real tool call, not a copy-paste snippet.** For a commandlet request (e.g. "커맨드릿 CompileAllBlueprints 실행해줘") you MUST actually invoke `ue_build_pipeline(operation="run_commandlet", commandlet_name="CompileAllBlueprints", ...)` and report its result — never merely display the code block below as guidance.

```python
ue_build_pipeline(
    operation="run_commandlet",
    commandlet_name="<CommandletName>",
    extra_args="<additional_args>",
    timeout=300,
)
```markdown

#### 2F: Gauntlet Test Run

```python
ue_build_pipeline(operation="gauntlet_run", test_name="<test_suite>", platform="Win64", timeout=900)
```markdown

### Step 3: Error Handling

If build fails, auto-diagnose:

```python
# Parse build errors
ue_fix_errors(operation="smart", project_root="<project_root>", auto_detect_git=true)
```markdown

### Step 4: Report Results

Generate build report with timing, warnings, and errors.

## Final Response Contract (MUST)

The final response is a receipt for the **actual** `ue_build_pipeline` response, not a generic build
summary. Always include these labels, using `N/A` when a field does not apply:

```text
Operation: <build_editor|cook|package|buildcookrun|run_commandlet|gauntlet_run>
Target / Platform / Configuration: <target or N/A> / <platform> / <configuration>
Commandlet: <exact commandlet_name or N/A>
Status: <SUCCESS|FAILED|PARTIAL|UNKNOWN>
Evidence: <authoritative output path, duration/counts, progress state, or exact error>
```

- Copy operation, parameters, status, paths, counts, warnings, and errors from the tool request and
  response. For commandlets, literally report `Operation: run_commandlet` and the exact
  `Commandlet:` value (for example, `CompileAllBlueprints`).
- `SUCCESS` requires an authoritative completed/success result. A timeout, missing completion field,
  or still-running operation is `UNKNOWN` or `PARTIAL`, with the last observed progress as evidence.
- On failure, name the failed stage and quote the concrete error summary; include any diagnostic
  `ue_fix_errors` result separately. Never turn a submitted request into a success claim.
- If no `ue_build_pipeline` call occurred, say `Status: FAILED` and `Evidence: pipeline was not
  executed`; do not emit a plausible-looking build report.

---

## Output Format · Error Recovery · Activation Test Cases

> Moved to [REFERENCE.md](REFERENCE.md) (progressive disclosure, Issue #19403 / #15438): the build-report output format, the error-recovery fallback table, and the activation test cases.

Build history details: [REFERENCE.md § Build History](REFERENCE.md#build-history).
