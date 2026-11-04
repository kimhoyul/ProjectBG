# UE Physics Audit — Reference

Companion to [SKILL.md](SKILL.md). Detailed rule semantics, scoring, and the project-context fields the aggregate report draws on. Issue #18109.

---

## The 6 structural rules (`validate_physics_asset`, #18108)

Each fanned-out `validate_physics_asset` call runs 6 deterministic, offline rules against the parsed `bodies[]` / `constraints[]` of one PhysicsAsset. `valid = (error_count == 0)`.

| rule | severity | condition |
|---|---|---|
| `shapeless_body` | error | body `shapes[]` empty/absent — no collision or mass geometry (empty `AggGeom`) |
| `zero_mass_body` | error | override-first: if `b_override_mass=true` flag `mass_in_kg_override <= 0`; else flag `mass_scale <= 0`. Absent/null numeric fields are never defects |
| `dangling_bone_constraint` | error | constraint `bone1_name`/`bone2_name` has no matching body (case-insensitive); one finding per dangling end |
| `all_default_bodies` | warning | non-empty bodies, ALL `physics_type == "Default"` (once per asset) |
| `effectively_unlimited_limit` | warning | axis motion `Limited` with limit `>= 180°` (UE clamps swing/twist to [0,180] → unconstrained) |
| `sim_kinematic_mismatch` | warning | inverted anchor only: child (`bone1`) Kinematic while parent (`bone2`) Simulated (standard ragdoll = kinematic parent → simulated child is NOT flagged) |

Each finding: `{rule, severity, bone_name?, joint_name?, bone1_name?, bone2_name?, message}`.
Per-asset return: `{valid, error_count, warning_count, findings[], summary:{bodies, constraints, rules_checked:6, constraints_source}}`.

---

## Scoring & prioritization

- **Project health score** = `valid_PA / total_PA` (percentage of PhysicsAssets with zero errors).
- **Secondary metrics**: total `error_count` sum, total `warning_count` sum, count of PAs with `error_count > 0`.
- **Worst-offender ranking**: sort PAs by `error_count` desc, then `warning_count` desc; present the top `--top N` (default 10).
- **`--errors-only`**: restrict the ranking + histogram to PAs with `error_count > 0` (warnings still counted in totals).
- **Per-rule histogram**: for each of the 6 rules, `건수` (total findings) and `영향 PA 수` (distinct PAs firing it) — surfaces systemic issues (e.g. every ragdoll `all_default_bodies`) vs one-off defects.

An empty PA (no bodies, no constraints) is vacuously `valid` — no rule fires. Report it as valid but note 0 bodies if it appears in a scan.

---

## Collection: `search_physics_assets` vs `ue_search_assets`

Issue #18109 body names `ue_search_assets`; this skill uses `ue_manage_physics(search_physics_assets)` instead (user-confirmed). Rationale:
- physics-native, single-tool (keeps `mcp_tools_used = ["ue_manage_physics"]`);
- returns `results[].name` that feed `validate_physics_asset`'s `resolve_physics_asset` directly;
- offline metadata cache, same source the validator reads.

Handler accepts `query`/`pattern` + `limit` only — **no offset paging**. When `total_found > count`, re-call with a larger `limit`.

---

## Project context ops (one call each, appended to the report)

- **`get_chaos_settings`** — `p.Chaos.*` CVars + `[/Script/ChaosSolverEngine.ChaosSolverSettings]` cascade. Report `MaxPhysicsDeltaTime`, solver iteration counts, substepping. Frames whether ragdoll defects are amplified by an aggressive solver budget.
- **`analyze_collision_profiles`** — walks the 12-layer `[/Script/Engine.CollisionProfile]` INI cascade (#18102). Report profile/channel counts. A PA whose bodies use undefined/misconfigured profiles is a cross-cutting risk the per-asset rules do not catch.

Both degrade gracefully (identical `UECODEGEN_ENGINE_DIR` failure mode): if `analyze_collision_profiles` or `get_chaos_settings` errors on engine-path config, omit that block and keep the PA aggregate.

---

## Composition

Audit → drilldown handoff: for each worst-offender PA, `/ue-physics-debug PA_Name --constraints` (or `--bone <name>`) inspects the exact bodies/constraints behind the findings. See [ue-physics-debug/REFERENCE.md](../ue-physics-debug/REFERENCE.md).
