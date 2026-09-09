# Gate C6: Quest Flipendo guide and trail build

## Scope and claim boundary

Gate C6 addressed the user's direct C5 finding: the trigger changed the wand
color, but neither the path nor a drawing reference was visible. C6 was built,
audited, installed, launched by the user, and observed on Quest 3. The user
confirmed that the authored reference was visible, then identified two UX
faults: it remained visible before pressing, and the shifted/projected live
trail diverged from the physical wand tip.

## Implemented behavior

- The genuine external `FlipPattern` points are reconstructed in the frozen
  aim-facing gesture plane and drawn as a cyan world-space ribbon.
- The guide follows the tracked wand before capture and freezes on trigger
  press, matching the plane used by recognition.
- While held, up to 384 decimated wand-tip samples form a thicker orange
  projected trail. This is distinct from an aim ray and does not retarget the
  press-time spell selection.
- On release, the fitted scored trajectory remains visible for 0.8 seconds:
  green for accepted and red for rejected. Cancellation uses amber.
- Rendering uses one static six-vertex quad and one push-constant model matrix
  per segment. It does not rewrite a shared dynamic GPU buffer while frames are
  in flight.
- The guide is offset 5.5 cm along the aim normal; the trail is biased 4 mm
  toward the player to avoid equal-depth rejection against the template.

## Verification

The portable regression loads the real owned Flipendo profile and proves:

~~~text
idle tracked frame -> full authored template visible
held trigger       -> live trail contains at least two points
90-degree aim      -> accepted at quarter physical size
accepted release   -> retained accepted trail visible
spell event        -> press-time rotated aim preserved
~~~

All five host tests passed. Android NDK r27c compiled the Vulkan ribbon path
for ARM64 with warnings treated as errors. Packaging and offline audit passed:

~~~text
path   = android/app/build/outputs/apk/debug/app-debug.apk
sha256 = 36FC49FE82CB355E0DD17474208BBD72D0FE81D86B286A62C527E15624FCF15E
ABI    = arm64-v8a
signature = APK Signature Scheme v3
alignment = valid
resources.arsc = stored
proprietary assets = 0
~~~

`adb install -r` returned `Success` on Quest 3 serial `2G0YC1ZF760BPC`.
The C6 runtime logged 20 authored template points and a live trail reaching
300 points while held. Recognition itself succeeded twice:

~~~text
attempt=2 outcome=ACCEPTED score=1.000000 threshold=0.500000
gesture_attempts=3 accepted=2 rejected=1
guide=VISIBLE template_points=20 trail_points=300
~~~

This proves that C6 rendering and the corrected scorer ran, but the user's
alignment and idle-visibility findings reject C6 as the visual baseline. C7
supersedes it.
