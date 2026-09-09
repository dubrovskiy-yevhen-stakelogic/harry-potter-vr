# Wand and gesture contract

## Goal

The VR port must make the player's physical motion authoritative while keeping
HP1's authored spell templates, lesson thresholds, scoring, progression,
effects, and spell dispatch authoritative. It must not add a second,
incompatible gesture language beside the original one.

## Input sample

The XR layer publishes one timestamped sample per predicted display frame:

```text
WandSample
  predicted_display_time
  pose_valid
  grip_pose              controller or tracked-hand grip in stage space
  aim_pose               runtime-provided aim pose when available
  trigger_value
  trigger_pressed_edge
  trigger_released_edge
  tracking_source        right_controller | left_controller | hand
```

Calibration adds a fixed grip-to-prop transform and wand length. The derived
tip pose, not the controller origin, drives the visible wand, targeting ray,
trace particles, and lesson path. Raw poses remain available in diagnostics so
filtering mistakes can be distinguished from tracking loss.

## Ordinary casting

- Head pose drives only the camera.
- Wand-tip position and forward direction drive targeting independently.
- The casting action maps to HP1's existing press/hold/release input edges.
- Target selection and `baseWand.CastSpell` remain original game behavior.
- Tracking loss freezes or safely releases casting; it never fabricates a
  high-speed stroke.

## Spell lessons

At lesson entry, capture a stable lesson plane from the authored template and
camera. During a held cast:

1. transform the tracked wand tip into that plane;
2. project to normalized lesson coordinates;
3. retain the raw 3D path and the projected 2D path separately;
4. cancel the attempt if any held-frame pose is invalid or its predicted display
   time does not increase monotonically;
5. resample onto a canonical time grid derived by the game host from the
   authored lesson `DrawTime` and point capacity;
6. preserve the exact start and end while limiting the submitted path to HP1's
   authored 500-point lesson capacity;
7. submit the normalized points to the runtime's existing
   `Gesture.CompareGesture` path on release.

The shipped lesson logic maps its pointer-driven wand position into normalized
coordinates equivalent to `(x + 0.5, 0.5 - y)`. That is evidence for coordinate
orientation, not permission to guess the VR plane scale. The first PC slice
must calibrate scale against the visible shipped template and record the
projected points and returned score.

Gate A5b now performs that external-template projection and score in Quest
Link. The first authored point is anchored to the physical press tip and the
template is visible in cyan. Its current `0.42 m` plane extent is a VR
calibration value rather than authored data; it remains explicitly provisional
even though the user accepted the initial interaction.

Gate A6 adds a narrow portable dispatch seam after that accepted score. One
accepted release produces one immutable `SpellCastEvent` containing a
monotonic serial, spell identity, predicted display time, tip and aim data,
score, and threshold. The current consumer is intentionally only a fixed
test-world target: it rejects duplicate or reordered serials, renders a short
green beam, and applies one deterministic knockback reaction. It does not claim
retail target selection, `baseWand.CastSpell`, damage, progression, or effects.

Pose and timing validation happen before projection. The jitter filter then
measures physical in-plane distance after projection, so motion normal to the
lesson plane cannot create a false 2D stroke. The raw start and end are exempt
from jitter rejection. Do not rewrite the gesture scorer to make a poor
coordinate mapping appear successful.

The canonical grid is `0, period, 2*period, ...` plus the exact endpoint
when the duration is not a multiple of the period. Only a stroke which exceeds
the authored capacity is compressed onto an evenly distributed grid. A
tracking gap is never interpolated across: it returns no gesture points and an
explicit failure status.

## Controller and hand policy

The first accepted wand is a Touch Plus controller attached to a physical wand
prop. It provides the most reliable pose, trigger edge, and haptics. Quest hand
tracking is a later provider for the free hand and optional controller-free
casting. Simultaneous hands-and-controllers support is preferred so the player
can hold a tracked wand and still use a natural off hand.

## Required telemetry

Each attempt records:

- XR predicted display time and pose-validity transitions;
- raw grip, aim, and derived tip poses;
- chosen lesson-plane transform and normalized bounds;
- raw, filtered, and submitted point counts;
- trigger edges, gesture score, authored threshold, and success/failure;
- target identity and dispatched spell when outside a lesson;
- CPU/GPU frame timing and missed-frame indicators.

Passing a unit test proves only the coordinate and sampling code. Runtime
acceptance requires a current capture showing the physical motion, rendered
trace, score, and resulting original game action together.
