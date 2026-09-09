# Gate B4: Engine.Level actor and model handles

Date: 2026-09-03

## Outcome

Gate B4 adds a bounded, read-only decoder for the first selected native map
payload. It finds the unique top-level `Engine.Level` export and retains:

- the Level export reference;
- every ordered actor slot as a package-local signed reference;
- null holes in the actor array;
- the following world-model reference, verified as a local `Engine.Model`
  export.

No UObject, actor, model, script, BSP, texture, sound, or game state is
constructed. The API does not expose copied proprietary payload bytes.

## Clean-room format evidence

The format was recovered from read-only inspection of the legally owned US PC
installation. No code was copied from the unlicensed local OpenHP1 reference.
The strict baseline identifies the inspected `Engine.dll` as SHA-256
`7756A2A3DF7198D72F4706952196BEE8ADB3B79EDFE7C8B3A5E4D2E3593D8EBC`.

Relevant x86 bodies in that exact image:

- `ULevelBase::Serialize` at `0x103ADCE0` calls `UObject::Serialize`,
  handles the actor container at object offset `0x2C`, then serializes
  `FURL` at offset `0x44`;
- its normal package-load branch reads two 32-bit transaction-array counts and
  serializes that many object references in order;
- the `FURL` serializer at `0x1042DC90` serializes protocol, host, map,
  portal, the options string array, port, and validity;
- `ULevel::Serialize` at `0x103AEC60` calls the base serializer and then
  serializes the world-model object reference at object offset `0x98`.

The two actor counts are required to agree. The initial CompactIndex-only
hypothesis failed closed on the real map, and was replaced only after the
transaction-array branch and real data agreed.

## Public surface and validation

`inspect_hp1_level_handles(path)` returns `Hp1LevelHandles`. The decoder:

- requires exactly one top-level export whose class identity is
  `Engine.Level`;
- consumes and bounds-checks the inherited tagged-property stream;
- caps actor slots and FURL collections;
- validates every non-null actor reference against the package tables and
  rejects imported actor ownership;
- accepts ANSI and Unicode FURL framing and requires string terminators;
- requires the world model to be a local export with exact class identity
  `Engine.Model`;
- returns structured status and error text instead of partial success.

Synthetic tests cover ordered actor slots, preserved null holes, the world
model identity, rejection of an imported actor reference, and rejection of a
non-Model world reference.

## Owned-data results

The standalone probe was run read-only over every `*.unr` in
`C:\Program Files\HP\Maps`:

| Result | Count |
| --- | ---: |
| Maps inspected | 41 |
| Maps passed | 41 |
| Maps failed | 0 |
| Actor slots | 49,574 |
| Non-null local actor references | 46,253 |
| Null actor slots | 3,321 |

`Lev_Tut1.unr` produced:

| Field | Value |
| --- | ---: |
| Package version | 76 |
| Level reference | 3,617 |
| World model reference | 3,593 |
| Actor slots | 2,046 |
| Non-null actor references | 2,011 |
| Null actor slots | 35 |

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

This is structural compatibility evidence, not proof that actors or the map
can run. Gate B5 should decode only the referenced `Engine.Model` structural
arrays needed for an initial BSP geometry census, retaining package references
and bounds checks before producing any renderable mesh.
