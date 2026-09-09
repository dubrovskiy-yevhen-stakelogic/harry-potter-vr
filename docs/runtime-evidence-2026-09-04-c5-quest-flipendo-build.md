# Gate C5: standalone Quest Flipendo gesture build

## Scope and claim boundary

Gate C5 keeps the runtime-accepted C4 textured Hogwarts, wand, locomotion, and
snap-turn paths. It adds a native standalone Flipendo capture/scoring loop that
uses the legally owned external game data at runtime. The first C5 APK was
installed and launched, but the user reported that Flipendo did not work. Logs
showed 10 recorded attempts, 8 score rejections, no acceptances, and two
attempts without a terminal score before tracking/session loss. The corrected
candidate described below is built and audited but has not yet been
reinstalled.

The APK contains no proprietary game assets. `FlipPattern` and `spellFlip` are
loaded from the user's external `system/HPBase.u` and `Maps/Lev_Tut1.unr`.

## Implemented slice

- Right trigger press starts one gesture and immediately locks the wand-tip
  origin and normalized aim direction for eventual dispatch.
- The moving wand tip is captured while the trigger remains held. Tracking
  loss, timestamp gaps over 100 ms, jumps over 0.25 m, and sample overflow
  cancel safely and require a neutral trigger before another attempt.
- Trigger release projects the captured 3D trajectory onto a frozen aim-facing
  lesson plane anchored at the authored first template point. The plane follows
  the direction selected on press instead of the initial world X/Y axes.
- The free-space path is translated and independently scaled to the authored
  template bounds before scoring. This keeps the real shape and scorer while
  allowing the player to turn and draw at a natural physical size without a
  fixed 0.42 m screen guide.
- The existing original-semantics scorer compares the projected path with the
  real `FlipPattern` and applies the first authored pass mark (`0.5`).
- During testing, the authored accuracy radius is multiplied by `1.75`; the
  threshold itself is unchanged.
- Wand color communicates state: orange while recording, green after accepted,
  red after rejected, and amber after cancellation. Feedback lasts 0.8 s.
- No aim beam is drawn once capture begins. The selected cast direction remains
  the one saved at trigger press, so later drawing motion cannot retarget it.
- An accepted gesture emits one serialised `FLIPENDO_ACCEPTED` event and logs
  its locked origin/direction. Applying force to a selected actor, spell VFX,
  audio, NPC AI, animation, and script progression are not part of C5.

## Verification

Windows Release tests ran against the legally owned external data root:

~~~text
hpvr_wand_tests             PASS
hpvr_hp1_gesture_tests      PASS
hpvr_quest_lifecycle_tests  PASS
hpvr_quest_view_tests       PASS
hpvr_quest_gesture_tests    PASS
100% tests passed, 0 tests failed out of 5
~~~

The new state-machine regression checks fail-closed startup, real profile
loading, authored threshold preservation, widened test accuracy, neutral
arming, one capture per press/release, and exactly one terminal result.

Android NDK r27c compilation completed for `arm64-v8a`. The packaged library
contains the OpenXR boolean-action call plus the profile loader, trajectory
projection, and gesture scorer symbols. The verifier also checks signature,
alignment, stored `resources.arsc`, external-data-only packaging, and the prior
stereo/Hogwarts/Touch/wand paths.

Final audited package:

~~~text
path   = android/app/build/outputs/apk/debug/app-debug.apk
sha256 = 963B7F02069FBAF7564A57CE241DA529155A85793656D7D1E21889EC43771468
ABI    = arm64-v8a
signature = APK Signature Scheme v3
alignment = valid
resources.arsc = stored
proprietary assets = 0
Flipendo profile loader = linked
gesture projection = linked
gesture scoring = linked
~~~

## First C5 runtime finding

The earlier audited APK with SHA-256
`103A21DDC36A4110A2EFFADFFFC19896A9E23031F45C2C450FD35CDFC50CF6A9`
was installed on Quest 3 serial `2G0YC1ZF760BPC`. It loaded the real profile,
created a RUNNING OpenXR session, and submitted more than 600 HOGWARTS frames.
After the user attempted the gesture, logs showed:

~~~text
wand=TRACKED
gesture_attempts=10 accepted=0 rejected=8
~~~

The user confirmed that the feature did not work. Snap-turn telemetry had
already reached 20. Code inspection then found that capture was still projected
onto fixed world X/Y, which is incompatible with player-relative drawing after
turning. The two unterminated attempts are conservatively recorded only as
non-terminal; later frames did report tracked-wand loss, but the old telemetry
cannot prove one exact cancellation reason for each attempt.

## Corrected candidate verification

The regression test now feeds the real external Flipendo template through a
90-degree rotated aim plane at one quarter of its former physical extent. The
gesture is accepted exactly once and the emitted event preserves the rotated
press-time aim. All 5/5 tests, Android ARM64 compilation, packaging, signature,
alignment, proprietary-asset audit, and linked-symbol audit pass.

The corrected `963B...1468` APK was installed, but Meta's controller-required
dialog prevented the native host from entering before it was superseded by C6.
No corrected C5 headset behavior claim is made.
