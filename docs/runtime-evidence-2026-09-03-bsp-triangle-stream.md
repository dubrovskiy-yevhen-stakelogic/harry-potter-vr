# Gate B7: CPU-only HP1 BSP triangle stream

Date: 2026-09-03

## Outcome

Gate B7 converts the validated Gate B6 node spans into a CPU-only,
untextured triangle stream. Every emitted triangle retains its source node and
surface indices, three converted positions, and a finite unit geometric normal.

No texture payload is decoded, no material is resolved, no GPU buffer or
pipeline is created, no collision is constructed, and no actor or script is
executed.

## Coordinate and scale contract

The mesh builder uses the OpenXR axis convention already exercised by the
project's tracked calibration scene:

- Unreal `+Y` becomes OpenXR `+X` (right);
- Unreal `+Z` becomes OpenXR `+Y` (up);
- Unreal `+X` becomes OpenXR `-Z` (forward).

This mapping changes handedness. The builder therefore measures every fan
triangle against the converted BSP node-plane normal and swaps its final two
vertices when necessary. Emitted geometry is consistently counter-clockwise
relative to its returned normal rather than relying on one blind global swap.

World scale remains an explicit positive finite
`meters_per_unreal_unit` argument. Gate B7 does not claim that HP1's correct
human scale is known. The owned-data probe uses `1.0` only as a dimensionless
geometry-validation scale; it is not a proposed runtime value.

## Safety and geometry rules

`build_hp1_bsp_triangle_mesh(topology, scale)`:

- accepts only a topology result whose status is `ok`;
- revalidates node surface links, node vertex spans, and active point links so
  a caller-mutated topology cannot produce partial output;
- caps source collections and maximum fan-triangle count at one million;
- rejects non-finite or non-positive scale and non-finite converted positions;
- fans each node polygon from its first validated vertex;
- emits no candidate with zero-length edges, a relative squared sine at or
  below `1e-12`, or zero alignment with the BSP plane normal;
- counts short polygons and rejected degenerate candidates without emitting
  them;
- clears the complete result on structural failure or allocation failure.

Synthetic tests prove the explicit axis mapping, handedness correction, CCW
normal, caller-provided scale, node/surface provenance, invalid-scale failure,
mutated-index failure without partial output, and omission of a degenerate
fan triangle.

## Owned-data results

The standalone Release probe loaded and triangulated every `*.unr` in
`C:\Program Files\HP\Maps` read-only at validation scale `1.0`:

| Result | Count |
| --- | ---: |
| Maps inspected | 41 |
| Maps passed | 41 |
| Maps failed | 0 |
| Source polygons with at least three vertices | 181,308 |
| Short source polygons | 23 |
| Fan candidates | 500,237 |
| Emitted non-degenerate triangles | 470,948 |
| Rejected degenerate candidates | 29,289 |
| Winding corrections | 463,820 |

Largest single-map values were all in `Lev4_Sneak2.unr` except the short
polygon count:

| Field | Maximum |
| --- | ---: |
| Source polygons | 14,006 |
| Emitted triangles | 36,872 |
| Rejected degenerate candidates | 2,620 |
| Winding corrections | 36,301 |
| Short polygons | 6 (`Lev2_Inc_B.unr`) |

`Lev_Tut1.unr` produced:

| Field | Value |
| --- | ---: |
| Source polygons | 7,344 |
| Short polygons | 0 |
| Emitted triangles | 20,003 |
| Rejected degenerate candidates | 1,540 |
| Winding corrections | 19,758 |

These counts establish deterministic CPU geometry output. They do not prove
that fan triangulation, visible-surface policy, player scale, origin placement,
or culling matches the shipped renderer on screen.

## Build and regression evidence

- Windows Debug build with MSVC `/W4 /WX /permissive-`: passed.
- Windows Release build with MSVC `/W4 /WX /permissive-`: passed.
- Debug CTest: 2/2 passed.
- Release CTest: 2/2 passed.
- Default locked Rust all-target tests: 19 passed.
- Feature-enabled Rust/C++ bridge tests: 35 passed.
- Android NDK r27c ARM64 static-library build: passed.
- The standalone Release CPU mesh probe passed all 41 owned maps.
- Post-change strict golden verifier: 7/7 recorded retail files `MATCH`.
- Existing PC runtime log still reports Version 433, Startup, wand spawn, and
  clean exit.

No retail or user-state file was written. Neither the retail game nor a Quest
build was launched.

## Boundary and next gate

This is CPU geometry evidence, not visual acceptance. Gate B8 should classify
surfaces using retained polygon flags and package references, define an
explicit visible/untextured subset, and feed one bounded static map slice into
the existing PC OpenXR renderer with culling disabled for the first visual A/B.
That runtime launch still requires explicit user authorization.
