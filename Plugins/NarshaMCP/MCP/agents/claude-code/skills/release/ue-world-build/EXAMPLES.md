# ue-world-build -- Examples

Concrete invocations of `/ue-world-build` (Issue #20039). Workflow: [SKILL.md](SKILL.md).
Op wiring / error recovery: [REFERENCE.md](REFERENCE.md).

## 1. Full pipeline (default)

```text
/ue-world-build
```
Runs Phase 0 preflight, then all 4 stages in order: nav -> HLOD -> grass -> lighting.
Landed stages (HLOD, lighting) execute; PENDING stages (nav, grass) report their
manual-editor fallback + read-only baseline. Ends with the unified World Build Report.

## 2. Stage subset

```text
/ue-world-build --hlod --lighting
```
Runs only HLOD then lighting (both landed today), skipping nav and grass. Stages
always execute in canonical order regardless of flag order, so
`/ue-world-build --lighting --hlod` behaves identically (HLOD before lighting).

## 3. Named world + dry-run

```text
/ue-world-build --map L_Expanse --dry-run
```
Targets the `L_Expanse` world and PREVIEWS the plan + per-stage availability
(LANDED vs PENDING) without issuing any write command. Use before a real bake to
confirm which stages will run automatically.

## 4. PENDING-stage degradation (nav, today)

```text
/ue-world-build --nav
```
Because the nav build write op is PENDING (#20033), this does NOT hard-fail. It
captures a read-only baseline via `ue_manage_navmesh(get_structure)` and reports:

```text
  1. Navigation ...... PENDING  (#20033 -- manual: Build > Build Paths)
```
Once #20033 lands, the same invocation runs the real nav build automatically
(the skill probes the owning tool's op enum at run time).

## 5. World Partition disabled

```text
/ue-world-build
```
On a non-World-Partition (standard streaming) level, Phase 0 `wp_get_status`
reports WP disabled, so the HLOD stage is marked **N/A** (HLOD proxies are a WP
feature) and the pipeline continues with nav / grass / lighting -- no failure.

## 6. Editor offline (preflight only)

```text
/ue-world-build
```
With no live Editor, write stages cannot run. The skill emits
`=== World Build Report (PREFLIGHT ONLY) ===`, runs only the offline read-only
inspectors (nav / HLOD status / foliage), and suggests `/launch-editor` before a
real bake.

## Related

- Read-only WP/DDC/SC status dashboard: `/ue-project-ops`
- C++ compile / cook / package: `/ue-build-automate`
