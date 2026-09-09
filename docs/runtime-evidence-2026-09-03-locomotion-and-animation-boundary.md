# Gate B14: VR locomotion and the HP1 animation boundary

Date: 2026-09-03

## Outcome

The populated HP1 map now has a controller locomotion layer that remains in
the same world coordinate system as the stereo cameras, tracked wand, gesture
trail, aim ray, and spell events.

- left Touch thumbstick: head-relative smooth movement at 1.8 m/s;
- radial 0.18 deadzone with normalized range outside it;
- right Touch thumbstick: 30-degree snap turn;
- turn is pivoted around the tracked head position, so the camera does not
  orbit around the LOCAL-space origin;
- the stick must return below 0.35 before another snap can fire;
- LOCAL reference-space changes reset accumulated locomotion and all dynamic
  wand/gesture continuity together;
- movement is enabled only for an HP1 map preview with tracked-wand input.

The OpenXR action set is synchronized without a hand subaction filter so both
the left movement action and right wand/turn actions are active. Bindings are
suggested for both Meta Touch Plus and the Oculus Touch fallback profile.

## Coordinate contract

The static BSP and character population remain in the PlayerStart-relative
world established by Gate B9/B13. Locomotion maintains one rigid
`world_from_local` transform. That transform is applied to both eye poses at
render time and to every live wand/aim point before gesture recognition. This
avoids the common failure where the castle moves but the wand is left behind
in tracking space.

## Animation format probe

A bounded `Engine.Animation` probe was added for direct local exports. It
parses the stock UE1 reference skeleton and conventional motion-track layout,
enforces finite values and allocation caps, and never constructs UObjects or
executes script.

The shipped version-76 HP1 animation payload is not stock UE1 at the track
level. `HPModels.skdumbledoreAnims` resolves correctly as export reference 364
and its 63 reference bones parse, but move 0 / track 5 reaches a negative
compact array count under the conventional `AnalogTrack` layout:

```text
animation_status=2 version=76 animation_ref=364
object=skdumbledoreAnims parsed_bones=63 parsed_moves=0
error=Animation move=0 track=5 flags=1845690627 offset=2688
orientation keys has an invalid count -5
```

The pinned read-only UE Viewer oracle reports the same `AnalogTrack` failure
at track 5 for the same export. Therefore the decoder remains fail-closed and
the current character population intentionally stays in bind pose. No guessed
key interpretation is sent to the renderer.

## Verification

- native MSVC Release build with warnings as errors: passed;
- Android ARM64 native library build with NDK 27.2: passed;
- CTest: 2/2 passed;
- Rust gesture-projection suite: 47/47 passed;
- Rust default suite: 29/29 passed;
- deterministic tests cover deadzone behavior, head-relative forward motion,
  snap re-arming, and preservation of virtual head position across a turn;
- complete `Lev_Tut1` asset pass still loads 20,003 BSP triangles, 28 character
  actors, 15 distinct skeletal meshes, and 39 texture layers;
- complete asset result: `PASS openxr_started=0`;
- the golden installation was read only.

## Acceptance boundary

This gate is build- and offline-validated. It has not been started in OpenXR,
so controller direction, comfort, and runtime bindings are not yet headset
accepted. Collision and floor following are also deferred; this first
locomotion pass is intentionally unconstrained movement through the validated
rendered world.
