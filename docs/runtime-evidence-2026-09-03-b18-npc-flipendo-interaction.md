# B18 NPC Flipendo interaction

## Outcome

B18 connects the already accepted wand/gesture path to the HP1 character
population. On an accepted Flipendo release, the runtime now:

1. casts a ray from the tracked wand through the loaded actor bounds;
2. chooses the nearest intersected character within 18 metres;
3. rejects the hit when original solid BSP is closer than the character;
4. applies a 1.2-second, 0.65-metre horizontal push with a 0.24-metre lift to
   only that actor; and
5. shows a green beam for a hit or an orange beam for a miss/BSP obstruction.

The reaction is deliberately reversible and returns to the decoded idle frame.
It is not yet retail UnrealScript AI, damage, health, physics, or a shipped hit
animation.

## Accepted prerequisite

The user headset-confirmed B17 capsule collision before this gate. That
acceptance covers visible collision behavior only; it does not retroactively
prove B18.

## Implementation boundary

- Character draw ranges and world-space union bounds are retained per actor.
- Ray/AABB selection considers all 28 loaded non-player actors, including the
  seven provisionally staged near `PlayerStart`.
- The existing solid BSP triangle stream supplies the occlusion ray test.
- Reaction offsets are applied to the selected actor's vertex range during the
  normal animation-buffer upload. The base frame is restored immediately after
  the reaction.
- Event, hit/miss, occlusion, and completed-reaction counts are verified at
  shutdown.
- The old procedural pink spell target is disabled when this adapter is active.

## Offline evidence

The exact owned-data preflight used the B18 launch configuration without
starting OpenXR:

```text
wgpu_stereo_clear.exe
  --frames 540000
  --flipendo-data-root "C:\Program Files\HP"
  --hp1-map-slice "C:\Program Files\HP\Maps\Lev_Tut1.unr" 0.02 100000
  --hp1-player-start 0
  --hp1-eye-height 0.815
  --hp1-bsp-collision
  --hp1-character-population "C:\Program Files\HP" 603
  --hp1-character-stage-near-player
  --hp1-character-animation
  --hp1-npc-spell-interaction
  --validate-assets-only
```

Relevant output:

```text
[hp1.collision.load] solid_triangles=19936 not_solid_triangles=66 degenerate_triangles=1 radius_m=0.300 half_height_m=0.840 max_step_up_m=0.32 max_step_down_m=0.45
[hp1.collision.validate] displacement=Vec3(0.01, 0.11531478, 0.0) blocked_substeps=0 grounded_substeps=1 PASS
[hp1.npc.spell] targets=28 staged_targets=7 max_distance_m=18.0 reaction_seconds=1.20 status=READY
[hp1.npc.spell.validate] actor_ref=1506 class=Tut1.Tut1George object=Tut1George2 target_distance_m=6.539 bsp_distance_m=Some(10.3080845) PASS
[hp1.assets.validate] PASS openxr_started=0
```

The validation ray hit staged George before the nearest BSP surface, proving
that the actual map/player/staging transform has at least one unobstructed
target. Rust tests passed 56/56 and the feature-enabled Release binary built
successfully.

## Runtime status

The first B18 OpenXR headset check was rejected by the user. The visible
procedural wand and cyan ray diverged, and target selection used the release
pose after drawing rather than the intended pose at the initial trigger press.
These are runtime findings, not offline hypotheses. B19 supersedes both
behaviours; see `runtime-evidence-2026-09-03-b19-press-lock-and-wandmesh.md`.
