---
name: mcp-analyze
model: opus
description: "Retrospective analysis of MCP tool calls in the current session: tool selection quality, output correctness, execution quality, and improvement recommendations."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
allowed-tools: [Bash, Read, Glob, Grep, Skill]
argument-hint: "tool name (e.g., ue_manage_blueprint), --last N (analyze last N calls)"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# MCP Analyze

Analyze whether recent MCP usage was correct, efficient, and aligned with intent.

## Usage

```bash
/mcp-analyze
/mcp-analyze ue_manage_blueprint
/mcp-analyze --last 5
```markdown

## Workflow

### Phase 0: Parse arguments

1. Parse arguments from `$ARGUMENTS`.
   - `$ARGUMENTS[0]` → tool name (e.g., `ue_manage_blueprint`)
   - `--last <N>` → analyze last N calls

   > If `$ARGUMENTS[0]` is empty, analyze all MCP calls in the current session.

### Phase 1: Build MCP call inventory

Collect, for each recent call:
- tool name
- operation
- key parameters
- returned output
- intended purpose

### Phase 2: Validate tool selection

Compare selected tool/operation to best-fit alternatives.

Expected output:
- correct selections
- acceptable but suboptimal selections
- incorrect selections with replacement suggestion

### Phase 3: Validate output correctness

Cross-check outputs against source of truth where possible:
- source files
- config files
- asset lists
- known runtime constraints

Also track execution quality:
- latency
- errors/warnings
- duplicate retries

### Phase 4: Score response quality

Weighted scoring:
- Correctness 30%
- Completeness 25%
- Efficiency 20%
- Clarity 15%
- Best Practices 10%

### Phase 5: Report

Return:
- overall score
- per-call findings
- top gaps
- concrete next-step improvements

## Output Format

```text
=== MCP Analyze Report ===

Session Calls Analyzed: {N}
Overall Score: {X.X}/5.0

--- Per-Call Findings ---
| # | Tool                  | Operation       | Selection | Output  | Notes              |
|---|----------------------|-----------------|-----------|---------|---------------------|
| 1 | ue_analyze_symbols   | search_symbols  | Correct   | Valid   | -                   |
| 2 | ue_manage_blueprint  | get_structure   | Correct   | Valid   | -                   |
| 3 | ue_search_assets     | search          | Suboptimal| Valid   | Use ue_manage_gameplay instead |

--- Top Gaps ---
1. {gap_description}
2. {gap_description}

--- Improvements ---
- {concrete_improvement}
```

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| No MCP calls found in session | Session has no prior MCP tool invocations to analyze | Report empty inventory; suggest running MCP tools first before analysis |
| Output cross-check fails (source file not found) | Referenced file was deleted or path changed | Skip source validation for that call; note as "unverifiable" in findings |
| Scoring produces all 5.0 (no differentiation) | Too few calls or all calls were trivially correct | Expand analysis to include efficiency metrics (latency, retries) for better discrimination |

## MCP Tool Usage

```python
# Analyze MCP tool usage patterns
ue_analyze_symbols(operation="smart", query="NarshaMCP")
```
