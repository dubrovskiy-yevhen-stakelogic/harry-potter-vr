# Gate B1: clean-room HP1 package census

Date: 2026-09-02

## Claim

The independent runtime can now read and strictly validate the structural
tables of every UE1 package in the legally owned HP1 installation. It can
aggregate qualified export classes and direct root package dependencies without
decoding, extracting, or writing proprietary payloads.

This is structural package compatibility only. It does not yet prove
transitive dependency resolution, object construction, property decoding
outside the existing Flipendo path, UnrealScript VM execution, BSP rendering,
audio decoding, gameplay, or Quest runtime file access.

## Clean-room implementation

`inspect_hp1_package` reuses the repository's independently written,
bounds-checked reader. The new summary reports:

- package and licensee versions;
- file, name, import, export, and serialized-payload counts;
- direct root package names;
- aggregate export count and serialized byte count per qualified class.

The reader validates table and payload bounds, every name and object reference,
outer-chain cycles, CompactIndex canonical form, aggregate overflow, and the
presence of the property terminator name `None`. A failed inspection clears
all partial aggregates.

The accepted version set is deliberately closed to versions observed in the
retail corpus: `61/0`, `68/0`, `69/0`, `72/0`, `73/0`, `75/0`, and
`76/0`. Version 61 uses the observed legacy null-terminated name encoding;
versions 64 and later use CompactIndex-prefixed name lengths. Synthetic tests
cover both v61 and v76, and reject an unobserved v62 package before table
parsing.

The CLI `hpvr_hp1_package_census` takes explicit external package paths. It
does not copy package bytes into the repository and does not emit object names,
textures, meshes, sounds, scripts, or other derived asset dumps.

## Lev_Tut1 structural result

Read-only input:

`C:\Program Files\HP\maps\Lev_Tut1.unr`

Summary:

| Field | Value |
| --- | ---: |
| Package version | 76/0 |
| File bytes | 3,465,537 |
| Names | 4,200 |
| Imports | 285 |
| Exports | 3,617 |
| Serialized export bytes | 3,335,917 |
| Distinct export classes | 75 |
| Direct package names | 30 |

The largest structural groups are 802 `Engine.Model`, 802 `Engine.Polys`,
786 `Engine.Brush`, and 715 `Engine.Light` exports. The map also contains
one `Engine.Level`, one `HPBase.SpellLearnTrigger`, one
`HarryPotter.Harry`, and the expected tutorial NPC classes. These are class
and count observations, not decoded actor or geometry data.

All 30 direct dependency names resolve to installed UE1 package candidates.
There are 32 candidate files because `Editor` names both
`system\Editor.u` and `Textures\Editor.utx`. That collision is retained as
evidence that the future resolver must implement explicit UE1 search-path and
package-kind policy rather than selecting the first basename match.

Every one of those 32 candidates passed structural validation:

| Version | Candidate files |
| --- | ---: |
| 61 | 1 |
| 69 | 2 |
| 73 | 2 |
| 75 | 1 |
| 76 | 26 |

## Whole-installation result

The same reader inspected all 242 `.u`, `.utx`, `.uax`, `.umx`, and
`.unr` files under the owned installation:

| Version | Files |
| --- | ---: |
| 61 | 6 |
| 68 | 3 |
| 69 | 16 |
| 72 | 1 |
| 73 | 2 |
| 75 | 13 |
| 76 | 201 |
| **Total** | **242** |

Result: 242 passed, 0 failed.

## Build and regression evidence

- Windows Release build with MSVC `/W4 /WX /permissive-`: passed.
- CTest: 2/2 passed.
- Default locked Rust all-target tests: 19 passed.
- Feature-enabled Rust/C++ bridge tests: 35 passed.
- Android NDK r27c ARM64 static-library build: passed.
- Post-census strict golden verifier: 7/7 recorded retail files `MATCH`.

The Android result proves only that this reader compiles for ARM64. No package
was copied to a headset and no Quest application was installed or launched.

## Next gate

Gate B2 should implement a deterministic, case-insensitive package resolver
over explicit game-data roots. It must preserve the `Editor` collision,
resolve the transitive package graph with cycle detection, and report missing
or ambiguous edges before any object payload is constructed.
