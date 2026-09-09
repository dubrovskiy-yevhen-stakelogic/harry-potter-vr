# Gate B12: exact Level actor placement for a non-player character

Date: 2026-09-03

## Outcome

The direct UE1 skeletal-mesh preview can now be associated with one exact
actor reference from the validated `Level` actor array. The bridge retains the
actor slot, object identity, serialized `Location`, raw Unreal rotator units,
and `DrawScale`. The calibration scene removes the old diagnostic translation,
applies the actor transform, and then applies the same PlayerStart-relative
world transform as the BSP.

This gate deliberately stops using Harry as the preview character. Harry is
the first-person player and should not be rendered as an object in front of the
camera. The first non-player runtime candidate is the real tutorial Dumbledore
actor and his directly loaded mesh:

```text
actor_ref=1672 slot=854 class=Tut1.Tut1Dumbledore
object=Tut1Dumbledore1
location_unreal=-1133.6,-2020.72,-28
rotation_units=0,-40,0

class=Tut1Dumbledore mesh_path=HPModels.skdumbledoreMesh
mesh_package=HPModels.u mesh_ref=645
points=318 wedges=431 faces=568 materials=4
texture_refs=372,374,376,378
```

Dumbledore is about 1,323 Unreal units, or 26.5 m at the current explicit
`0.02` scale, from the selected PlayerStart. Hermione was not selected for the
first placement run because her real level position is about 122 m from that
start and would not provide a useful immediate visual check.

## Interface and validation

The C ABI `hpvr_hp1_load_actor_visual_utf8` selects a positive actor reference
exactly and performs no class-script execution or asset export. Rust checks the
440-byte ABI layout and exposes the placement through the new command-line
option `--hp1-npc-actor <actor-reference>`. The option fails unless both a map
and an NPC preview are supplied.

Native Release build and tests:

```text
cmake --build build\wand --config Release --parallel
ctest --test-dir build\wand -C Release --output-on-failure
100% tests passed, 0 tests failed out of 2
```

Rust tests and Release build:

```text
cargo test --manifest-path tools\xr-runtime-probe\Cargo.toml
25 passed
cargo test --manifest-path tools\xr-runtime-probe\Cargo.toml --features gesture-projection
43 passed
cargo build --release --manifest-path tools\xr-runtime-probe\Cargo.toml --features gesture-projection --bin wgpu_stereo_clear
```

The owned map and Dumbledore package then passed the asset-only path without
starting OpenXR:

```text
[hp1.map.load] selected_triangles=20003 available_triangles=20003 vertices=60009
[hp1.player_start] actor_ref=2071 actor_slot=222 object=PlayerStart0
[hp1.npc.load] points=318 wedges=431 faces=568 vertices=1704 materials=4 texture_bytes=1048576
[hp1.npc.actor] actor_ref=1672 actor_slot=854 object=Tut1Dumbledore1 position_m=Vec3(-40.41442, -0.56, 22.67201) rotation_units=[0, -40, 0] draw_scale=1
[hp1.assets.validate] PASS openxr_started=0
```

## Acceptance boundary

No OpenXR session was launched for this gate yet. Build and offline validation
do not establish that Dumbledore is visible from the current view, correctly
oriented in the headset, or occluded by the right castle surfaces. Those are
the bounded goals of the next explicitly authorized runtime run. Animation,
AI, collision, and gameplay state remain out of scope.

The golden installation remained read-only throughout.

## Headset acceptance

The subsequently authorized Quest Link run reached `FOCUSED`, submitted its
first stereo frame, and rendered 61,713 vertices / 20,571 triangles with model
hash `eba466412f74b24e`. The user observed a small visible part of Dumbledore
through the upper-floor geometry and correctly noted that the actor's real
position is upstairs and around the corner. This accepts world placement and
normal BSP depth occlusion; it does not claim a complete character view.

The run later followed `STOPPING`, `IDLE`, and `EXITING` and emitted the known
terminal-state diagnostic rather than a clean aggregate. Runtime evidence is
kept in ignored `local/gate-b12-dumbledore-live-01.log` and its stderr
companion.
