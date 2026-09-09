# Gate B5: Engine.Model structural census

Date: 2026-09-03

## Outcome

Gate B5 adds a bounded, read-only census of the `Engine.Model` referenced by
the unique top-level `Engine.Level` in a map. It retains only object references,
collection sizes, null-light counts, and the two terminal flags. It does not
retain vectors, points, nodes, surfaces, vertices, light maps, bounds, leaves,
or other proprietary payload bytes.

No UObject, BSP tree, polygon, material, texture, script, render mesh, or game
state is constructed. `Engine.Polys` is identity-checked but its payload is not
opened.

## Clean-room format evidence

The format was recovered by read-only inspection of the legally owned US PC
installation. No code or declarations were copied from the unlicensed local
OpenHP1 reference. The inspected `Engine.dll` is the strict-baseline image with
SHA-256
`7756A2A3DF7198D72F4706952196BEE8ADB3B79EDFE7C8B3A5E4D2E3593D8EBC`.

Relevant x86 bodies in that exact image:

- `UModel::Serialize` at `0x103BB750` selects embedded collections for package
  versions greater than 61 and names the sequence `Vectors`, `Points`,
  `Nodes`, `Surfs`, `Verts`, `Zones`, `Polys`, `LightMap`, `LightBits`,
  `Bounds`, `LeafHulls`, `Leaves`, and `Lights`;
- `UPrimitive::Serialize` at `0x103FA0D0` serializes an `FBox` as six 32-bit
  values and one byte, then a post-v61 `FSphere` as four 32-bit values;
- the vector and point collection body at `0x103BE070` stores a CompactIndex
  count followed by 12 bytes per element;
- `FBspNode` serialization at `0x103BB210`, `FBspSurf` at `0x103BAE30`, and
  `FVert` at `0x10314FB0` establish the exact raw-block and CompactIndex
  framing used to advance through those collections;
- the remaining collection bodies at `0x103BDB10`, `0x103163C0`,
  `0x103B9860`, `0x10362190`, and `0x103BDA30` establish the bounded framing
  for light maps, light bits, bounds, leaf hulls, and leaves.

The older package-version branch stores several collections through object
references instead of embedded arrays. Gate B5 rejects version 61 explicitly
rather than guessing that layout. The owned Maps set contains only version 72
and 76 maps.

## Public surface and validation

`inspect_hp1_model_census(path)` returns `Hp1ModelCensus`. The decoder:

- resolves the model only through the already-validated top-level Level;
- requires a local `Engine.Model` export and validates its property terminator;
- caps every collection at one million elements and checks all byte ranges;
- validates texture, actor, zone-actor, polygon, and light object references;
- requires a non-null polygon reference to identify a local `Engine.Polys`;
- requires complete consumption of the model export, rejecting both truncation
  and trailing bytes;
- exposes counts and references only, with structured failure status and no
  partial success.

Synthetic tests cover all collection families, null light slots, terminal
flags, truncation, and explicit rejection of the legacy indirect layout.

## Owned-data results

The Release probe was run read-only over every `*.unr` in
`C:\Program Files\HP\Maps`:

| Result | Count |
| --- | ---: |
| Maps inspected | 41 |
| Maps passed | 41 |
| Maps failed | 0 |
| Version 76 maps | 40 |
| Version 72 maps | 1 (`Entry.unr`) |

Aggregate collection framing across all 41 world models:

| Field | Total |
| --- | ---: |
| Vectors | 37,068 |
| Points | 278,415 |
| BSP nodes | 181,331 |
| Surfaces | 100,021 |
| Vertex references | 2,871,243 |
| Shared sides | 387,565 |
| Zones | 497 |
| Light maps | 97,502 |
| Light-bit bytes | 7,595,958 |
| Bounds | 86,064 |
| Leaf-hull words | 788,736 |
| Leaves | 53,106 |
| Light slots | 1,619,960 |
| Null light slots | 147,745 |

`Lev_Tut1.unr` produced:

| Field | Value |
| --- | ---: |
| Package version | 76 |
| Model reference | 3,593 |
| Polys reference | 2,285 |
| Vectors | 565 |
| Points | 11,521 |
| BSP nodes | 7,344 |
| Surfaces | 3,658 |
| Vertex references | 122,272 |
| Shared sides | 16,454 |
| Zones | 14 |
| Light maps | 3,616 |
| Light-bit bytes | 567,924 |
| Bounds | 3,324 |
| Leaf-hull words | 34,168 |
| Leaves | 1,679 |
| Light slots | 110,081 |
| Null light slots | 4,991 |

All 41 serialized models report both `RootOutside=0` and `Linked=0`. This is
recorded framing evidence only; no runtime meaning is inferred from it yet.

## Build and regression evidence

- Windows Debug build with MSVC `/W4 /WX /permissive-`: passed.
- Windows Release build with MSVC `/W4 /WX /permissive-`: passed.
- Debug CTest: 2/2 passed.
- Release CTest: 2/2 passed.
- Default locked Rust all-target tests: 19 passed.
- Feature-enabled Rust/C++ bridge tests: 35 passed.
- Android NDK r27c ARM64 static-library build: passed.
- Post-change strict golden verifier: 7/7 recorded retail files `MATCH`.
- Existing PC runtime log still reports Version 433, Startup, wand spawn, and
  clean exit.

No retail or user-state file was written. Neither the retail game nor a Quest
build was launched.

## Boundary and next gate

This is package-framing compatibility evidence, not a renderable map and not
Quest runtime acceptance. Gate B6 should decode a minimal immutable BSP
topology view (points, nodes, surfaces, and vertex references), validate every
cross-array index before exposing it, and still defer materials, collision,
actors, scripts, and rendering.
