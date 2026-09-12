# Wand and gesture behavior

Implementation: `src/quest/src/quest_gesture.cpp`,
`src/wand/src/wand_trajectory.cpp`, `src/wand/src/hp1_gesture.cpp` and
`src/quest/include/hpvr/quest_basic_cast.h`.

## Input and casting

`GestureSample` supplies the predicted display time in nanoseconds, tip position,
aim direction, tracking validity and trigger-held state. Positions are in meters
in one stable XR reference space. The XR backend derives the tip from the wand
pose; head direction does not aim the spell.

Gameplay gesture input begins after acquiring an eligible target; the lesson
begins on trigger press. At stroke start, `QuestGesture` freezes the origin, aim
direction and drawing plane. Subsequent hand motion does not change that ray.
The targeting ray is hidden while drawing. A successful attempt produces one
`FlipendoEvent`; `ConsumeEvent` returns it once. Spell targeting rejects duplicate
or reordered event serials.

The unlearned basic cast is separate: `BasicCast` updates its aim while held and
releases a short air-puff projectile toward the last aimed point. It does not
require a gesture.

## Projection and sampling

The drawing plane faces the stroke-start aim direction. Its first template point
is anchored at the starting tip; its reference width and height are **0.42 m**.
For tip `p`, plane origin `o`, normalized perpendicular axes `right` and `up`:

```text
x = 0.5 + dot(p - o, right) / width
y = 0.5 - dot(p - o, up)    / height
```

Coordinates outside 0..1 are not clamped. The projection rejects non-finite
values, invalid axes and non-increasing timestamps. Stroke capture also cancels
on tracking loss, a sample gap over 100 ms, a tip jump over 0.25 m, or more than
16,384 raw samples. After cancellation, release the trigger before retrying.

Jitter filtering measures in-plane motion with a 4 mm threshold; normal-only
motion does not add a stroke. Shape comparison resamples by path distance to
64 points, independently of drawing speed. The visual trail remains on the real
wand path rather than being rotated or snapped to the template. Invalid tracking
samples are never bridged by interpolation.

## Lesson scoring

The loader reads `FlipPattern` from `system/HPBase.u` and the `spellFlip` lesson
settings from `Maps/Lev_Tut1.unr`. The clean-room coverage scorer compares the
projected stroke with that template.

| Setting | Original | Relaxed |
| --- | --- | --- |
| Accuracy radius | Authored radius | Authored radius x 1.75 |
| Required score | Authored mark for each of four rounds | First authored mark |
| Drawing deadline | Authored `DrawTime` | None |
| Scale assistance | Preserves stroke scale | Fits stroke scale to the template |

Both policies center the stroke and compare either drawing direction at arbitrary
rotation. Bounded coverage checks tolerate imperfect corners and proportions.

Scoring occurs on release or at the Original-mode deadline. Changing difficulty
or lesson round cancels an active attempt. The saved VR preference selects the
mode; fresh installations default to Relaxed.

The template is hidden when idle and shown during drawing and brief result
feedback. Invalid input never emits a spell event.

## Gameplay gestures

Outside the lesson, gesture casting uses a separate permissive shape policy:
the player makes a quick curl broadly resembling Flipendo, rather than tracing
the lesson guide accurately. Rotation and drawing direction do not affect
acceptance. Gameplay assistance does not alter either Original or Relaxed
lesson scoring. Tracking validity, cancellation and one-event-per-stroke rules
remain in force.

## Tests

`hpvr_wand_tests` covers projection and sampling; `hpvr_hp1_gesture_tests` covers
the scorer; `hpvr_quest_gesture_tests` covers recording, cancellation and lesson
difficulty; `hpvr_quest_spell_targets_tests` covers event dispatch and targets.
