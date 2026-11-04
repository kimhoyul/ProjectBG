---
name: _shared
description: "Shared Python utility modules for skill scripts. Not a user-invocable skill — provides research_gate.py (Step 1.5 structural pre-checks) used by example-extractor and module-analyzer. Issue #8928."
allowed-tools: []
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
---

# _shared

Internal utility package for skill Python scripts. Not user-invocable.

## Contents

- `research_gate.py` — Step 1.5 structural pre-checks (checks A/B/C) shared by example-extractor and module-analyzer skills.
- `tests/` — Unit tests for shared utilities.

## Usage

Imported by skill scripts via `sys.path.insert`:

```python
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "_shared"))
from research_gate import require_source, step15_check, compute_quality_score
```
