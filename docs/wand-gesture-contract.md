# Wand and gesture behavior

Implementation: `src/quest/src/quest_gesture.cpp`,
`src/wand/src/wand_trajectory.cpp`, `src/wand/src/hp1_gesture.cpp` and
`src/quest/include/hpvr/quest_basic_cast.h`.

## Input and casting

`GestureSample` supplies the predicted display time in nanoseconds, tip position,
aim direction, tracking validity and trigger-held state. Positions are in meters
in one stable XR reference space. The XR backend derives the tip from the wand
pose; head direction does not aim the spell.

On a Flipendo press, `QuestGesture` freezes the origin, aim direction and drawing
plane. Subsequent hand motion draws the gesture without changing that saved ray.
The targeting ray is hidden while drawing. A successful attempt produces one
`FlipendoEvent`; `ConsumeEvent` returns it once. Spell targeting rejects duplicate
or reordered event serials.

The unlearned basic cast is separate: `BasicCast` updates its aim while held and
releases a short air-puff projectile toward the last aimed point. It does not
require a gesture.

## Projection and sampling

The drawing plane faces the press-time aim direction. Its first template point
is anchored at the press-time tip; its full width and height are both **0.42 m**.
For tip `p`, plane origin `o`, normalized perpendicular axes `right` and `up`:

```text
x = 0.5 + dot(p - o, right) / width
y = 0.5 - dot(p - o, up)    / height
```

Coordinates outside 0..1 are not clamped. The projection rejects non-finite
values, invalid axes and non-increasing timestamps. Quest recording also cancels
on tracking loss, a sample gap over 100 ms, a tip jump over 0.25 m, or more than
16,384 raw samples. After cancellation, release the trigger before retrying.

Jitter filtering measures in-plane motion with a 4 mm threshold; normal-only
motion does not add a stroke. Start and end points are retained. Temporal
resampling uses the period derived from the authored `DrawTime`, preserves the
exact endpoint and caps submission at 500 points. It never interpolates across
an invalid tracking sample.

## Lesson scoring

The loader reads `FlipPattern` from `system/HPBase.u` and the `spellFlip` lesson
settings from `Maps/Lev_Tut1.unr`. The clean-room coverage scorer compares the
projected stroke with that template.

| Setting | Original | Relaxed |
| --- | --- | --- |
| Accuracy radius | Authored radius | Authored radius x 1.75 |
| Required score | Authored mark for each of four rounds | First authored mark |
| Drawing deadline | Authored `DrawTime` | None |
| Position/scale assistance | None | Fits the drawn bounds to the template bounds |

Scoring occurs on release or at the Original-mode deadline. Changing difficulty
or lesson round cancels an active attempt. The saved VR preference selects the
mode; this source version defaults to Original.

The template is hidden when idle and shown during recording and brief result
feedback. Invalid input never emits a spell event.

## Tests

`hpvr_wand_tests` covers projection and sampling; `hpvr_hp1_gesture_tests` covers
the scorer; `hpvr_quest_gesture_tests` covers recording, cancellation and lesson
difficulty; `hpvr_quest_spell_targets_tests` covers event dispatch and targets.
