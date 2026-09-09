# C29: loading, motion, Peeves and optional Filch encounter

## Scope and source boundary

Changes are original adapter work in this checkout. Retail `C:\Program Files\HP`
and Documents runtime state were not modified. Existing C18-C28 dirty work was
preserved. Owned package inspection/dumps/audio remain in ignored `local/`.
No retail or Quest application was launched. Installation uses the user's
standing permission to install normal builds without launching them.

## Player-visible changes implemented (headset acceptance pending)

- Warner LOCAL-space theater anchor is initialized on valid XR views **before**
  scene GPU readiness. Previously the anchor was created only after loading,
  when the loading quad was already hidden.
- Gregory statue yaw rotated by pi. Hermione's Intro1/Intro2 faces Harry's
  current cutscene position. These orientation adjustments need visual acceptance.
- Running child actors use the same 4.6 m/s translation as the accepted Ron
  route, instead of playing Run at 1.46 m/s. Adult walking remains 1.46 m/s.
  Ron's CutScene0 lead is spliced onto his finished CutScene51 track, without
  waiting for Harry to approach RonWait. Both normal and last-track completion
  are handled before the scene closes.
- NPC barks rearm after leaving 2.3 m, including during an unfinished line.
  Removed 30-second per-NPC and 8-second global cooldowns; the current voice
  must still finish and the player must leave/re-enter. No camera capture.
- TorchFire02 scale increased from 1 to 2.4. Candle emitter anchors use mesh
  top-cap centers and no longer rise away from the wick with particle age.
- Masked BSP material palette index zero now decodes transparent. Non-solid
  sibling surfaces sharing that masked material inherit its alpha test. Owned
  layers 20 and 34 now have 33,404 and 48,804 transparent texels respectively.
  Previously both had zero transparent texels (black arch panels).
- Basic-cast ray tests include props and the knights' visible skeletal rest
  poses, not their retired static pivots. Head animation is not dynamically
  ray-traced; the rest-pose approximation remains a known limitation.
- Restored CutScene6 authored twin travel with a canonical-actor handoff after
  the exit waypoint. Jump-lesson actors retain those arrival positions instead
  of being reset to the original new-instance spawn coordinates.
- Added owned tut1Peeves0 mesh/Float/Grab/voice and Trigger25 encounter location.
  The initial flight/Grab/retreat is a VR adapter: it never captures or relocates
  the camera and does not block movement. It is not the complete Peeves combat
  or repeating patrol/taunt state machine.
- After that encounter, CutScene5 uses original parallel actor/camera commands,
  cues and two twin lines. Only this subsequent departure uses theatrical
  camera control; then the twins stage offscreen for CutScene55.
- **User-corrected order:** Draco triggers at entry; Filch is a separate approach
  after Draco, not a prerequisite. Filch returns to the interrupted main quest
  stage and does not roll Hermione/Quirrell progression backward. His cutscene
  completion is separate from repeatable proximity barks. The temporary reversed
  order was corrected before packaging/installing.

## Save compatibility

V5 journals Peeves phase, twin transfer, Filch completion and main-stage resume.
Readers retain V1-V4 compatibility. V4 saves after the jump lesson do not replay
Peeves; V4 saves after Draco allow the previously skipped optional Filch scene.
No live save was rewritten during installation. Use a checkpoint before the
jump lesson to inspect the newly restored encounter/departure.

## Offline evidence

- Windows host build and Android ARM64 native build passed.
- CTest: 14/14 passed, including V5 round trips and V4 migration; explicit
  Draco-first, optional-Filch, no-replay and wrong-floor policy cases.
- Owned C29 probe: mask alpha, Peeves clips and all CutScene5/6 wait/cue
  dependencies passed; 8 and 4 authored moves respectively; all three voices found.
- Owned C28 regression: Ron route 359 ground samples, largest step 0.024878 m,
  10 bark profiles, 71 runtime dialogue/effect clips.
- Full owned scene probe: 24 character models, 3 movers, 11,005,884 vertices,
  250 texture layers before the runtime fire layer (251 total, below 256).
  76 candle anchors within 0.10 m of their meshes; all 6 knight aim rays hit.
  Glasses masks, 10 distinct child skins, 29 beans, 303 opening ground samples
  and all 18 spawned/destroyed children passed, with zero child ground misses.
- The full probe caught and fixed a rejected ground lookup for the flying
  Peeves actor before installation. A first knight aim diagnostic used an
  incorrect static pivot; the final check uses actual rendered body bounds.
- APK signature/alignment/ABI, required C29 symbols/markers, and no-proprietary-
  assets audit passed. Retired twin PRESTAGED marker was replaced by the new
  AUTHORED_TRANSITION check; red/blue loading remains rejected.

## Install evidence

Device: `2G0YC1ZF760BPC`, package `io.github.hpvr.quest`.

- C28 backup: `local/quest-c28-before-c29.apk`, SHA256
  `59869077E8FA23FAD5D81F13A9BF894F230246E02B7F04BC86F2019731D962B2`.
- C29 native `build/quest-c3-android/libhpvr_quest.so`, SHA256
  `A81066096C1D1CE36C30EE76AAE1C016A6743E9123ED4483CBBFA0C8FF162D86`.
- C29 APK `android/app/build/outputs/apk/debug/app-debug.apk`, SHA256
  `08EF4D9860F2FABEAE9E7C7900CB28721666E1BFCE975C443CE434FDA3A10555`.
- `adb install -r`: Success. Installed base APK independently matches that hash.
- Installed path: `/data/app/~~vEVqXmO_iDH0TvwgoIpMqA==/io.github.hpvr.quest-s5Dgu--4Uqdp1tGkG9y9WQ==/base.apk`.
- Uploaded only three missing PCM caches (275854, 320994, 290900 bytes).
  All 71 frontend audio-plan files match local/device SHA256 values.
- App PID absent after installation; no launch performed.
- All six save-bank hashes match before and after installation:

```
slot1.0 4e60cfee1c1db230ea688fad327fd6629d6437b2b0f2b4bc931e3f77c0dd281c
slot1.1 5218bd3b8d020785542ca5f08926d8ba1c87b9d5ef95952eb9a345a28afec4ab
slot2.0 a7b636c2733d11be8f2f7aeab243feff22e4558bb03f82b9fb4570f43637dea2
slot2.1 ed0f8147e787ecd1badeaaac22098d8cf7996daec52beb55e08b218762ad78d3
slot3.0 bbfaf63bf1c9f867e761a6c67286e219b46e8a0e98d30ec0b597373768b482bb
slot3.1 3f2df090be6b88cc7c251ae9a143d2def8f184d3ccfe95ec043a957d3fd9e03e
```

## Remaining acceptance

Headset testing has not occurred for C29. Check stationary Warner art during
loading; Ron/child running cadence; statue/Hermione facing; leave/reapproach
barks; flame size/wick alignment; knight aim and transparent arches. On an
early checkpoint, check visible twin exits/arrival positions, Peeves without
camera capture, then the theatrical departure, Draco at entry and Filch only
on a subsequent approach. No claim of full original script/AI parity is made.
