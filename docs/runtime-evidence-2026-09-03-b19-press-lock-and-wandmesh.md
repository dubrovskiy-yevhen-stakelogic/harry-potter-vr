# B19 press-time target lock and original WandMesh

## Headset finding that triggered this gate

The user rejected B18 after its first headset run:

- the procedural brown wand and cyan target ray visibly diverged; and
- the target ray was sampled after the completed gesture, so drawing the spell
  could change which NPC was selected.

The supplied headset screenshot is consistent with the first finding. The
second finding is also directly explained by B18 code, which copied the
release sample into `SpellCastEvent`.

## B19 behavior

- OpenXR's right-hand aim orientation is authoritative for the rendered wand.
- The visible tip, cyan guide ray, and gameplay ray share one origin and axis.
  A runtime invariant fails the frame if they diverge.
- The initial trigger press freezes a normalized aim ray. A successful
  Flipendo release carries that immutable press-time ray to NPC selection even
  when the controller moved while drawing.
- Tracking, session, reference-space, and interaction-profile cancellation
  clear the lock. A held trigger cannot silently reacquire it.
- The large idle diagnostic tip cube is gone. A much smaller glow is present
  only while the trigger is held.

## Original Harry wand asset

Read-only package inspection established this exact chain:

```text
HPBase.baseWand (class export 26)
  Mesh -> HPBase.WandMesh (SkeletalMesh export 1092)
```

B19 adds a geometry-only C ABI and loads that mesh directly from the user's
`system\HPBase.u` at startup. Nothing is extracted or copied into the
repository. The 48-vertex/16-triangle stream is normalized along its measured
longitudinal axis to the existing 0.34-metre physical wand length while
preserving its source proportions.

`WandMesh` references a material layout that the current strict P8 character
texture decoder rejects. B19 therefore uses the genuine mesh geometry with a
procedural brown vertex colour. It does not claim the original material is
decoded.

## Verification

Feature-enabled Rust/C++ tests:

```text
58 passed; 0 failed
```

Exact owned-data offline preflight:

```text
[hp1.wand.model] class=HPBase.baseWand mesh=HPBase.WandMesh mesh_ref=1092 vertices=48 triangles=16 source_bounds_min=Vec3(-0.5699613, -19.90326, -0.48483804) source_bounds_max=Vec3(0.56996125, 0.0, 0.59929276) longitudinal_axis=1 handle_end=max rendered_length_m=0.340 geometry=EXTERNAL_READ_ONLY material=PROCEDURAL_BROWN
[hp1.collision.validate] displacement=Vec3(0.01, 0.11531478, 0.0) blocked_substeps=0 grounded_substeps=1 PASS
[hp1.npc.spell.validate] actor_ref=1506 class=Tut1.Tut1George object=Tut1George2 target_distance_m=6.539 bsp_distance_m=Some(10.3080845) PASS
[hp1.assets.validate] PASS openxr_started=0
```

The press-lock regression test deliberately gives the release sample a
different origin and direction and verifies that the emitted event retains the
press origin/direction.

## Runtime status

No OpenXR session was started while building B19. The subsequent user-run
headset check accepted the corrected alignment, press-time target selection,
and WandMesh behavior with the report that everything worked. The remaining
UX finding was that the moving cyan aim guide should disappear while drawing;
B20 addresses that separately.
