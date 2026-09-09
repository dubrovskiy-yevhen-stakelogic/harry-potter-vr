# Gate B17: Harry-sized BSP capsule collision

Date: 2026-09-03

## Outcome

B17 prevents left-stick locomotion from walking freely through the rendered
`Lev_Tut1` BSP. The existing `RUN-LATEST-VR.cmd` now enables collision while
retaining the accepted B16 map, player height, wand, Flipendo, animated NPC
staging, smooth movement, and snap turning.

The user confirmed before this change that the B16 NPC group was visible. B17
has not been started in OpenXR; runtime feel and map coverage remain pending
the user's next explicit headset run.

## Controller model

The collision body comes from Harry's serialized class defaults at the
accepted `0.02 m/UU` scale:

- radius: `15 UU = 0.30 m`;
- half-height: `42 UU = 0.84 m`;
- eye offset: `40.75 UU = 0.815 m`.

Requested movement is split into at most 0.08 m substeps to prevent tunnelling.
A blocked diagonal attempts independent X/Z motion for wall sliding. Walkable
triangles support up to 0.32 m ascent and 0.45 m descent per substep. The
initial PlayerStart overlap is resolved before the first frame; on this map it
raises the capsule by 0.115315 m, avoiding a delayed first-stick camera jump.

## Collision input and boundaries

The collider consumes the same transformed triangle stream as the renderer.
UE1 `PF_NotSolid` (`0x00000008`) surfaces are excluded, degenerate triangles
fail closed, and finite geometric normals determine walkable versus wall
surfaces.

The exact owned-map preflight reported:

```text
[hp1.collision.load] solid_triangles=19936 not_solid_triangles=66
degenerate_triangles=1 radius_m=0.300 half_height_m=0.840
[hp1.collision.validate] displacement=Vec3(0.01, 0.11531478, 0.0)
blocked_substeps=0 grounded_substeps=1 PASS
[hp1.assets.validate] PASS openxr_started=0
```

This first collision layer constrains controller locomotion only. Physical
room-scale leaning can still put the headset through a wall, moving actors and
doors do not yet participate, and there is no jump, gravity/falling, crouch
body resizing, or retail UnrealScript collision event dispatch.

## Verification

- feature-enabled Rust tests: 52/52 pass;
- synthetic capsule coverage: flat floor, bounded step, solid wall stop;
- default-feature `cargo check`: pass;
- exact B17 owned-asset/collision preflight: pass with `openxr_started=0`;
- no retail installation or user runtime state was modified.
