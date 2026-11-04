# Caller Graph Visualizer

**Version**: 1.0.0 (Issue #151 Phase 4)
**Status**: ✅ Production Ready
**Author**: NarshaMCP Development Team
**Date**: 2025-10-22

---

## Overview

Visualize complete caller graphs for C++ ↔ Blueprint ↔ Delegate call flows with refactoring impact analysis.

---

## Quick Start

**Example Query**:

```
"ApplyDamage 함수 누가 호출해?"
"Who calls the FireWeapon function?"
```

**Auto-Activation**: Keywords like `caller`, `call graph`, `who calls`, `impact`, `refactoring`, `delegate`

---

## Key Features

- **Cross-Domain Coverage**: C++ (70-80%), Blueprint (100%), Delegate (100%)
- **Recursive Analysis**: 1-5 levels of call depth
- **Visual Diagrams**: Mermaid call flow diagrams
- **Refactoring Advice**: Safe refactoring recommendations
- **Performance**: <15s for standard analysis (depth=2)

---

## Workflow

```
Stage 1: Target Identification  → Resolve function/delegate name
Stage 2: Caller Discovery       → Find all callers recursively
Stage 3: Impact Visualization   → Mermaid diagram + refactoring advice
```

---

## Use Cases

1. **Impact Analysis**: "이 함수 수정하면 어디에 영향가?"
2. **Refactoring Safety**: "이 함수 이름 바꿔도 돼?"
3. **Delegate Tracing**: "OnDamageReceived 델리게이트 누가 bind 해?"
4. **Dependency Analysis**: "FireWeapon 전체 영향 범위 분석해줘"

---

## Integration

**Works with**:

- ✅ Error Doctor Skill - Fix callers after refactoring
- ✅ Blueprint Flow Tracer - Understand Blueprint caller logic
- ✅ Performance Health Check - Identify performance-critical paths

---

## Documentation

See [REFERENCE.md](REFERENCE.md) for complete specification.

---

## Related

- **Issue #107**: Delegate Tracking (Complete C++ ↔ Blueprint ↔ Delegate flow)
- **Issue #151**: Tool Orchestration Layer + Skills Expansion
- **v4.5.0**: Rust Caller Tracking (70-80% C++ coverage)
