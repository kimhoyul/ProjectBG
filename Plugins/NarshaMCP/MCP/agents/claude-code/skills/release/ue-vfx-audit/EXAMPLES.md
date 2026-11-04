# UE VFX Audit Examples

These examples exercise the project-wide Niagara rollup. Use `/ue-explain` for
one system's structure and `/ue-audit` for a whole-project, multi-domain audit.

## Project-wide health report

```text
/ue-vfx-audit
```

Discovers up to 200 Niagara systems, validates each system, runs a per-system
performance audit, enriches top offenders with `get_execution_flow` /
`get_module_details` / `find_asset_consumers` (optional `measured_cost`), and
reports the ten worst systems. Truncated inventory or skipped structural
validation is `PARTIAL`, never healthy.

## Audit a subset and compare the worst systems

```text
/ue-vfx-audit Weapons --top 5 --compare-pairs 3
```

Uses `Weapons` as the `search_systems` query, displays the five worst systems,
and runs `compare_systems` on adjacent pairs among the three worst candidates.
The per-system performance call uses `asset_name` and accepts only
`mode="single_system"` responses for scoring.

## Show actionable failures only

```text
/ue-vfx-audit --errors-only
```

Restricts the displayed ranking to systems with validation errors or hard
performance failures while still reporting scanned versus total coverage.

## Natural-language triggers

```text
VFX 감사해줘
niagara health report
NS 일괄 점검
```

These route to `/ue-vfx-audit`. Requests such as `NS_Fire 설명해` remain with
`/ue-explain`, and runtime `.utrace` profiling remains with the performance
profiling skills.
