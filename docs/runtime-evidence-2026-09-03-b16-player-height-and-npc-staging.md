# Gate B16: player eye height and visible NPC staging

Date: 2026-09-03

## Outcome

B16 turns the previously headset-tested B15 slice into a user-launchable build
with a corrected player viewpoint and a visible group of real HP1 characters
near the tutorial `PlayerStart`. The one-click entrypoint is
`RUN-LATEST-VR.cmd` at the repository root. Codex did not start OpenXR while
collecting this gate's evidence.

This remains a vertical slice, not a playable restoration of the tutorial.
NPC behavior, dialogue, story scripting, triggers, doors, collision, and the
retail save/game loop are not implemented.

## B15 headset evidence and diagnosis

The user confirmed that the B15 wand, Flipendo gesture, and controller
locomotion worked. They also reported a cat-height viewpoint, no nearby NPCs,
and movement through BSP geometry.

The class-default probe read the owned US `HarryPotter.u` package and found:

```text
class=Harry base_eye_height=40.75 base_eye_height_serialized=1
collision_height=42 collision_height_serialized=1
```

At the accepted map scale of `0.02 m/UU`, the correct eye offset from the
PlayerStart origin is 0.815 m. B16 applies this to the shared locomotion
transform, so head, wand, gesture trail, aim ray, and spell events remain in
one coordinate space. Reset restores the authored offset instead of zero.

The population probe also proved that all 28 actors and their animation data
were present. Their bind and first-animation-frame heights are consistent; for
example Dumbledore measures about 2.140 m in both. The apparent emptiness came
from the serialized pre-UnrealScript locations: the nearest principal
characters are tens of metres from the selected PlayerStart, and the map's
startup script has not yet relocated them.

## Provisional staging boundary

`--hp1-character-stage-near-player` moves exactly one Dumbledore, McGonagall,
Quirrell, Hermione, Ron, Fred, and George into a bounded group 5-8.5 metres in
front of PlayerStart. It uses the PlayerStart transform and Harry's serialized
42 UU capsule half-height to put their feet on the local floor. The other 21
actors retain their authored Level positions.

This placement is deliberately labelled staging. It is not claimed to match a
retail cutscene or the result of executing the map's UnrealScript. Animation is
separately opt-in through `--hp1-character-animation`; the B16 launcher enables
it after the per-actor bind/frame AABB audit found no displaced or collapsed
meshes.

## Build and offline evidence

The feature-enabled Rust test target passed 48/48 tests. The native Release
build passed 2/2 CTest targets. A fresh optimized
`wgpu_stereo_clear.exe` was built, and the exact launcher asset command passed
without starting OpenXR:

```text
[hp1.characters.load] actors=28 distinct_meshes=15 source_faces=7693
expanded_vertices=41817 texture_layers=39 texture_bytes=10223616
[hp1.assets.validate] PASS openxr_started=0
```

The same run emitted seven `staged=true` actor records and finite bind/frame-0
bounds for every loaded actor.

## Runtime command

The launcher runs for 540000 frames (about 100 minutes at 90 Hz) so it will not
end while the user is taking a screenshot. It validates the executable,
OpenXR loader, and owned map before starting. Stop it with Ctrl+C or by closing
its console.

Runtime appearance is still pending the user's next explicit headset run.
