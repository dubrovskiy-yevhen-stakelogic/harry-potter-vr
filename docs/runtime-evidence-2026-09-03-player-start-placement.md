# Gate B8.1: scale-invariant BSP and PlayerStart placement

Date: 2026-09-03

## Outcome

Gate B8.1 removes preview scale from all BSP triangle acceptance and winding
decisions, then adds a bounded read-only decoder for direct
`Engine.PlayerStart` actors referenced by the map's validated Level actor
array.

The PC OpenXR diagnostic can now opt into one PlayerStart by zero-based census
ordinal. It translates that serialized actor location to the OpenXR `LOCAL`
origin and applies the serialized yaw. The implementation has been built and
regression-tested on Windows and Android ARM64. A clean Quest Link run and the
user's headset observation now accept the inside-room location and facing.
The `0.01` scale is visually rejected as too small.
The follow-up `0.02` run was reported as possibly slightly too large, but the
user requested that it remain provisional until textures and NPCs provide a
credible scale reference.

## Scale-invariance correction

Gate B8 exposed 20,079 available `Lev_Tut1` triangles when the preview scale
was `0.01`, versus 20,003 at scale `1.0`. The old builder scaled `float`
positions before measuring relative degeneracy, plane alignment, and winding.
Changing scale therefore changed the selected topology.

The builder now:

1. converts Unreal axes without applying scale;
2. performs finite, degeneracy, plane-alignment, winding, and normal
   calculations in unscaled coordinates;
3. applies `meters_per_unreal_unit` only to the final emitted positions;
4. rejects any non-finite position after that final multiplication.

The opt-in real-map bridge test loads the same 64-triangle prefix at scales
`0.01` and `1.0`. It requires identical available/degenerate/reversed counts,
identical node/surface/flag provenance, scale-independent normals, and
positions related only by the requested scale. The test passed against the
owned `Lev_Tut1.unr`; the stable available count is 20,003.

## PlayerStart decoder boundary

`inspect_hp1_player_starts`:

- starts from the already validated top-level `Engine.Level` actor array;
- accepts only direct `Engine.PlayerStart` exports;
- does not construct actors, follow class inheritance, or infer defaults;
- decodes only scalar `Location`/`Vector` and `Rotation`/`Rotator` property
  tags;
- requires a finite explicitly serialized Location;
- retains the actor reference, Level slot, object name, raw Unreal position,
  raw rotator units, and serialized-field flags;
- rejects duplicate transform tags, incorrect structure types or sizes,
  unterminated properties, and trailing payload.

The C ABI selects one census ordinal, contains all C++ exceptions, and returns
the same explicit metric scale and axis mapping as the BSP slice:

- Unreal `+Y` -> OpenXR `+X`;
- Unreal `+Z` -> OpenXR `+Y`;
- Unreal `+X` -> OpenXR `-Z`.

The Rust owner validates ABI layout, status agreement, ordinal/count identity,
finite metric position, positive actor reference, serialized Location, UTF-8
object identity, and boolean ranges before exposing the result to the
renderer.

## Owned-map evidence

The Release Level probe reported for the tutorial map:

```text
level_status=ok package_version=76 level_reference=3617 world_model_reference=3593 actor_slots=2046 actor_refs=2011 null_slots=35 player_starts=1
player_start=0 actor_ref=2071 actor_slot=222 object=PlayerStart0 location_serialized=1 location_unreal=-619.707,-832.07,-299.766 rotation_serialized=1 rotation_units=0,-16032,0
```

A read-only census over all 41 installed maps passed:

- 40 maps contain at least one direct PlayerStart;
- 41 direct PlayerStart actors were decoded in total;
- `Entry.unr` has none;
- `Lev3_Intro.unr` has two;
- every other map with a start has one.

This census demonstrates package compatibility. It does not establish that
every start is the correct gameplay entry point.

## Renderer placement contract

The new opt-in argument is:

```text
--hp1-player-start <zero-based-ordinal>
```

It requires `--hp1-map-slice`. Given the axis-converted metric map position
`p`, selected start position `s`, and serialized yaw `y`, the preview
transform is:

```text
p_local = R_y(y) * (p - s)
y_radians = y_units * 2*pi / 65536
```

The renderer logs this as `placement=PLAYER_START`. Until full rotator
semantics are verified, the path rejects a selected start with nonzero
serialized pitch or roll. If the option is omitted, the previous
`DIAGNOSTIC_CENTERED` transform remains available.

This is not yet a recovered gameplay camera. In particular, it supplies no
eye-height offset, collision capsule, crouch/standing policy, actor or script
initialization, portal/zone state, or authoritative gameplay scale.

## Build and regression evidence

- Native synthetic PlayerStart decoder and C ABI tests: passed.
- Native synthetic scale-invariance regression: passed.
- Windows Debug build with warnings-as-errors: passed.
- Windows Release build with warnings-as-errors: passed.
- Debug CTest: 2/2 passed.
- Release CTest: 2/2 passed.
- Android NDK r27c ARM64 static-library build: passed.
- Default locked Rust all-target tests: 21 passed.
- Feature-enabled locked Rust/C++ tests: 38 passed.
- The feature run included the real `Lev_Tut1.unr` scale-invariance and
  PlayerStart bridge checks at scale `0.01`.
- Feature-enabled Windows Release `wgpu_stereo_clear`: built successfully.
- Post-change strict golden verifier: 7/7 recorded retail files `MATCH`.

No retail or user-state file was written.

## Live Quest Link evidence

The explicitly authorized Quest Link A/B loaded all 20,003 available tutorial
triangles rather than the first 4,000-triangle prefix:

```powershell
& .\tools\xr-runtime-probe\target\release\wgpu_stereo_clear.exe `
  --loader C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll `
  --frames 900 `
  --hp1-map-slice 'C:\Program Files\HP\Maps\Lev_Tut1.unr' 0.01 100000 `
  --hp1-player-start 0
```

Machine evidence:

- Oculus runtime `1.207.0` and the RTX 4090 were selected;
- stereo swapchain: `2064 x 2272 x 2`;
- direct wgpu imports: 3/3 OpenXR images;
- 20,003 selected / 20,003 available triangles and 60,009 GPU vertices;
- `placement=PLAYER_START` with yaw `-1.5370488` radians;
- `IDLE -> READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED`;
- 900/900 requested frames rendered and submitted;
- 900 GPU completions, zero events lost, and zero reference-space changes;
- balanced session/frame/image counters;
- stereo projection and motion coverage: `PASS`;
- final wgpu runtime errors: 0; device losses: 0;
- process exit: clean;
- post-run strict golden verifier: 7/7 recorded retail files `MATCH`.

User headset acceptance:

- the geometry was recognizable as Hogwarts rather than procedural geometry;
- the viewpoint was inside the level;
- camera position and facing were correct;
- scale `0.01` looked too small, described as being inside a dollhouse rather
  than a large castle.

Therefore:

- full bounded BSP runtime invariants: **PASS**;
- direct PlayerStart placement and facing: **PASS**;
- visual identity of the Hogwarts geometry: **PASS**;
- human/gameplay world scale at `0.01`: **FAIL**;
- texture, material, collision, gameplay, and standalone Quest acceptance:
  **NOT IMPLEMENTED**.

The ignored local log is `local/gate-b8-playerstart-live-01.log`.

## Next scale calibration

The completed follow-up Quest Link A/B kept the same PlayerStart and full
triangle set at `0.02` metres per Unreal unit:

```powershell
& .\tools\xr-runtime-probe\target\release\wgpu_stereo_clear.exe `
  --loader C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll `
  --frames 900 `
  --hp1-map-slice 'C:\Program Files\HP\Maps\Lev_Tut1.unr' 0.02 100000 `
  --hp1-player-start 0
```

This remains an A/B candidate, not an accepted conversion. The run completed
900/900 frames with clean projection/motion and no wgpu errors or device loss.
The user described it as possibly slightly too large and requested no scale
change until real textures and NPCs are visible. Runtime acceptance must still
establish whether the architecture has believable human scale and whether a
separate eye-height offset is needed. A later data-derived calibration should
compare the game's player collision/eye-height or character dimensions rather
than treating an engine-wide Unreal-unit convention as authoritative for HP1.

Textures, materials, collision, scripts, gameplay, native Quest packaging,
and Quest standalone behavior all remain outside this gate.
