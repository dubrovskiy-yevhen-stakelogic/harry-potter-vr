# Gate B8: bounded HP1 BSP render bridge

Date: 2026-09-03

## Outcome

Gate B8 connects the validated Gate B7 triangle stream to the existing PC
OpenXR/wgpu stereo renderer through a bounded C ABI and a checked Rust owner.
The renderer can now replace its procedural room with one caller-selected,
untextured HP1 map slice.

The gate now also has a first Quest Link visual result: the user saw the
colored HP1 geometry in the headset and supplied a screenshot showing coherent
architectural forms. It is not a clean-duration renderer pass, and gameplay
scale, placement, comfort, frame rate, and textured appearance remain pending.

## Surface evidence and non-semantics

The Release mesh probe re-read all 41 owned maps. All 470,948 emitted
triangles had an imported texture reference and a local actor reference:

| Raw classification | Triangles |
| --- | ---: |
| `actor_reference == 0` | 0 |
| `actor_reference != 0` | 470,948 |
| imported actor reference | 0 |
| local actor reference | 470,948 |
| no texture reference | 0 |
| imported texture reference | 470,948 |
| local texture reference | 0 |

The largest number of distinct raw polygon-flag combinations in one map was
35. `Lev_Tut1.unr` had 20,003 emitted triangles, 21 raw flag combinations,
20,003 imported texture references, and 20,003 local actor references.

Consequently, Gate B8 does not call `actor_reference == 0` static geometry and
does not invent material or visibility meanings for the flags. The bounded
slice is simply the first `N` already validated triangles in native BSP order.
Raw polygon flags plus node/surface indices are used only to generate stable
diagnostic colors.

## Native ownership boundary

`hpvr_hp1_load_bsp_slice_utf8`:

- requires an explicit finite positive `meters_per_unreal_unit`;
- requires an explicit triangle limit in `1..=100000`;
- uses a query call followed by a caller-owned exact buffer;
- writes no partial vertices when the supplied buffer is short;
- returns metric positions, finite unit normals, raw polygon flags, and source
  node/surface indices;
- returns bounds and raw reference-kind counters but no texture, actor, script,
  or other proprietary payload;
- contains allocation and all other C++ exceptions before the ABI boundary.

The Rust owner checks the ABI layout, status agreement, all bounded counter
sums, exact vertex count, finite bounds, finite positions, unit normals, and
stable metadata across the query/load calls before constructing a GPU buffer.

## Preview transform and renderer boundary

The source axis conversion and caller-selected metric scale remain unchanged
from Gate B7. For this diagnostic only, the selected bounds receive a logged
translation: their lowest point is placed on the existing `Y=-1.5 m` floor and
their X/Z midpoint is placed at `X=0, Z=-4 m` in `LOCAL` space. The log labels
this `DIAGNOSTIC_CENTERED`. It is not a recovered player spawn, map origin, or
gameplay transform.

The mode is opt-in and requires the existing native bridge feature:

```text
--hp1-map-slice <map-package> <meters-per-unreal-unit> <max-triangles>
```

The path, scale, and limit are all mandatory. The option selects geometry mode
but does not implicitly enable the wand or gesture capture. It can be combined
with those explicit modes later.

Current renderer limits are deliberate:

- untextured diagnostic colors only;
- no texture or lightmap decode;
- no collision, actor spawning, scripts, portals, zones, or gameplay;
- no inferred visible-surface filtering;
- no culling for the first visual A/B;
- the existing 30 m far plane remains unchanged;
- selection is a count bound, not spatial streaming.

## Build and regression evidence

- Native synthetic C ABI test: passed, including exact query, short-buffer
  no-write, exact fill, axis/normal values, and retained metadata.
- Windows Debug build with MSVC warnings-as-errors: passed.
- Windows Release build with MSVC warnings-as-errors: passed.
- Debug CTest: 2/2 passed.
- Release CTest: 2/2 passed.
- Android NDK r27c ARM64 static-library build: passed.
- Default locked Rust all-target tests: 21 passed.
- Feature-enabled locked Rust/C++ tests: 38 passed.
- The feature run included an opt-in Rust -> C ABI -> `Lev_Tut1.unr` test with
  64 triangles at validation scale `1.0`; that scale was dimensionless test
  input, not a proposed player scale.
- Default and feature-enabled Windows Release `wgpu_stereo_clear` builds:
  passed.
- Standalone Release classification probe: 41/41 maps passed.
- Pre-launch strict golden verifier: 7/7 recorded retail files `MATCH`.

No retail or user-state file was written. At this point in the gate, the retail
game and Quest build had not been launched.

## Live Quest Link evidence

The explicitly authorized diagnostic used the already-built feature-enabled
Release executable:

```powershell
& .\tools\xr-runtime-probe\target\release\wgpu_stereo_clear.exe `
  --loader C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll `
  --frames 9000 `
  --hp1-map-slice 'C:\Program Files\HP\Maps\Lev_Tut1.unr' 0.01 4000
```

This `0.01` value was a diagnostic preview input, not an accepted gameplay
scale. The loader reported:

- Oculus runtime `1.207.0`;
- RTX 4090 selected by OpenXR and wgpu;
- a `2064 x 2272 x 2` stereo swapchain;
- 3/3 OpenXR images imported directly into wgpu;
- 4,000 selected triangles / 12,000 GPU vertices;
- source bounds at the selected scale from `(-68.32007, 0.48, -35.52)` to
  `(-33.52, 12.639999, -1.28)`;
- diagnostic translation `(50.920036, -1.98, 14.4)` metres;
- distinct left/right view-projection hashes on the first rendered frame;
- `IDLE -> READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED` and a submitted first
  Gate B8 frame.

The user confirmed seeing colored geometry. Their supplied screenshot shows a
connected untextured structure with stair-like runs, arches, and large room
volumes rather than the earlier procedural cubes. It also shows that centering
the entire selected AABB puts the viewpoint outside/below useful playable
space. This is a placement failure, not evidence that the original player
start has been recovered.

The Link session later transitioned through `VISIBLE -> SYNCHRONIZED ->
STOPPING -> IDLE` before 9,000 rendered frames. After waiting 120 seconds for a
new `READY`, the process exited with `session did not reach READY within 120
seconds`. Therefore:

- bounded HP1 geometry visual acceptance: **PASS**;
- coherent architectural shape acceptance: **PASS**;
- useful player placement: **FAIL / NOT IMPLEMENTED**;
- clean 9,000-frame renderer aggregate: **NOT ESTABLISHED BY THIS RUN**.

The local ignored evidence files are `local/gate-b8-live-01.log` (19,225
bytes) and `local/gate-b8-live-01.err.log` (74 bytes). The user screenshot is
not copied into the repository because it is derived from proprietary map
geometry.

The live bridge also exposed a numerical issue: `Lev_Tut1` produced 20,079
available triangles at scale `0.01`, while the unit-scale Release census
produced 20,003. At this Gate B8 checkpoint the builder scaled `float`
positions before its degeneracy and plane-alignment calculations, so those
decisions were not scale-invariant. Gate B8.1 subsequently corrected and
regression-tested this; see
[`runtime-evidence-2026-09-03-player-start-placement.md`](runtime-evidence-2026-09-03-player-start-placement.md).

The post-run strict golden verifier again reported all 7/7 recorded retail
files as `MATCH`.

## Next acceptance gate

Gate B8.1 makes degeneracy decisions scale-invariant and adds an auditable
direct `Engine.PlayerStart` selection derived from map data. A short clean
Quest Link A/B is still required to verify whether that raw actor origin and
yaw produce a useful inside-room viewpoint before texture resolution begins.
Gate B8 proves only bounded untextured map geometry, not game rendering or
Quest standalone behavior.
