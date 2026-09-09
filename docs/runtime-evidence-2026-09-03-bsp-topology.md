# Gate B6: validated HP1 BSP topology view

Date: 2026-09-03

## Outcome

Gate B6 adds a bounded, read-only loader for the minimal immutable BSP topology
needed by a future mesh builder. It returns typed vectors, points, nodes,
surfaces, and active vertex references from the `Engine.Model` selected through
the top-level `Engine.Level`.

It does not load `Engine.Polys`, construct triangles, resolve materials, create
collision, instantiate actors, execute scripts, submit rendering work, or write
game state.

## Serialized layout and conservative boundary

The exact collection framing remains the clean-room result recorded in Gate
B5 for strict-baseline `Engine.dll` SHA-256
`7756A2A3DF7198D72F4706952196BEE8ADB3B79EDFE7C8B3A5E4D2E3593D8EBC`.
Gate B6 interprets only fields whose target collection can be identified and
validated from that framing and real-package behavior:

- vectors and points are finite three-component 32-bit float values;
- nodes retain a finite four-component plane, zone mask, surface link,
  front/back links, collision-bound link, two zone links, active vertex span,
  flags, and two leaf links;
- surfaces retain validated texture/actor package references, polygon flags,
  point/vector/light-map indices, and signed pan values;
- active vertices retain point and shared-side indices.

Two serialized node CompactIndex fields are consumed but deliberately not
exposed. In `Lev_Tut1.unr`, the field serialized from object offset `0x2C`
contains value 7,391 while the serialized node array contains 7,344 elements;
the model also records `Linked=0`. Treating that value as a ready node link
would therefore be unsafe. Gate B6 neither guesses a repair nor silently clamps
it.

The raw `Model Verts` array also contains stale entries outside node-referenced
spans. One real unused entry addresses point 11,521 in an array with valid
indices 0 through 11,520. The public view consequently copies only the spans
selected by validated nodes and remaps each node's `vertex_pool_index` to that
compact collection. Every copied vertex is then range-checked. Unused raw pool
entries are not exposed.

## Validation contract

`load_hp1_bsp_topology(path)` returns `Hp1BspTopology` only after:

- the Level, Model, Polys, and embedded collection framing passes the existing
  B4/B5 identity and boundary checks;
- all retained geometry floats are finite;
- every node surface, front, back, collision-bound, zone, and leaf index is
  either the exact allowed `-1` sentinel or within its target collection;
- every node vertex span fits entirely inside the serialized vertex pool;
- every retained surface point, normal, texture-axis, and light-map index is
  within its target collection;
- every active vertex point and shared-side index is valid;
- the compacted active-vertex result stays below the one-million-element
  safety cap;
- the model payload is consumed completely with no trailing bytes.

Failure clears the result and returns structured status/error text. Synthetic
tests include a stale unused vertex that is safely omitted and an invalid
active vertex that fails closed.

## Owned-data results

The Release probe was run read-only over every `*.unr` in
`C:\Program Files\HP\Maps`:

| Result | Count |
| --- | ---: |
| Maps inspected | 41 |
| Maps passed | 41 |
| Maps failed | 0 |

Validated topology totals:

| Field | Total | Largest map value |
| --- | ---: | ---: |
| Vectors | 37,068 | 7,328 (`Lev2_fire1.unr`) |
| Points | 278,415 | 23,177 (`Lev4_Sneak2.unr`) |
| BSP nodes | 181,331 | 14,008 (`Lev4_Sneak2.unr`) |
| Surfaces | 100,021 | 7,734 (`Lev4_Sneak2.unr`) |
| Active vertex references | 862,853 | 67,504 (`Lev4_Sneak2.unr`) |

The B5 raw serialized vertex-pool total is 2,871,243 entries. Gate B6 exposes
only the 862,853 entries selected by validated node spans; this is intentionally
not claimed to be a lossless copy of editor/transient model state.

`Lev_Tut1.unr` produced:

| Field | Value |
| --- | ---: |
| Package version | 76 |
| Model reference | 3,593 |
| Vectors | 565 |
| Points | 11,521 |
| BSP nodes | 7,344 |
| Surfaces | 3,658 |
| Active vertex references | 36,231 |
| Shared sides | 16,454 |
| Zones | 14 |
| Light maps | 3,616 |
| Bounds | 3,324 |
| Leaves | 1,679 |

## Build and regression evidence

- Windows Debug build with MSVC `/W4 /WX /permissive-`: passed.
- Windows Release build with MSVC `/W4 /WX /permissive-`: passed.
- Debug CTest: 2/2 passed.
- Release CTest: 2/2 passed.
- Default locked Rust all-target tests: 19 passed.
- Feature-enabled Rust/C++ bridge tests: 35 passed.
- Android NDK r27c ARM64 static-library build: passed.
- The standalone Release topology probe passed all 41 owned maps.
- Post-change strict golden verifier: 7/7 recorded retail files `MATCH`.
- Existing PC runtime log still reports Version 433, Startup, wand spawn, and
  clean exit.

No retail or user-state file was written. Neither the retail game nor a Quest
build was launched.

## Boundary and next gate

This proves safe topology ingestion, not correct triangle winding, scale,
materials, collision, rendering, or runtime behavior. Gate B7 should build a
CPU-only untextured triangle stream from each validated node span, reject
degenerate/non-finite output, and verify winding and coordinate conversion on
synthetic fixtures before any GPU integration.
