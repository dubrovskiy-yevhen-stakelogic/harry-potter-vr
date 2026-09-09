# C33: world panels, live diagnostics, SSR floor selection and lesson recovery

## Scope and acceptance

Implemented the user's C33 feedback; preserved the shared C18-C32 worktree.
Retail C:\Program Files\HP and Documents runtime state were read-only.
No game assets or extracted audio were added to tracked source.
No retail or Quest launch was performed. APK installation is not headset acceptance.

## Player-visible changes

- Level objective uses white text on a dark inset; health HUD keeps the lightning
  icon and no numeric HP.
- Both grips + menu opens a small, fixed-in-world settings panel over the current
  game scene. It uses the dark/pink/cyan convention inspected in the current
  C:\Dev\miamivr-quest reference, with original HP UI implementation.
- Settings retain their existing independent two-bank save format. Render scale
  supports 50-175% with live viewport/scissor/projection extent changes. The XR
  allocation allows 175%, clamped by the runtime's advertised maximum.
  Existing selected scale is preserved, not forced to 175.
- PERFORMANCE DEBUGGER opens a matching panel. Trigger pins it and resumes
  gameplay; B returns to settings. Both grips + menu hides a pinned panel and
  opens settings. Pin/resume consumes the trigger until release, avoiding an
  accidental spell. Pinned diagnostics are hidden during cutscenes.
- Original card pickup audio restored as an independent world-effect channel;
  frog pickup retains its separately resolved index.
- Two original HogwartsUrn props are loaded and grounded with the other props.

## Root causes and evidence

### SSR

The owned BSP loader already outputs Y-up normals. C32's floor classification
used normal.z, selecting vertical wooden details instead of the floor.
Production now uses normal.y. The owned-data C33 regression confirms 3,762
horizontal wooden-floor vertices, versus 426 wrongly selected wall vertices
under the previous test. Shader strength and raymarch are otherwise unchanged.

SSR still uses one shared previous left-eye color/depth history and material
shading in each existing eye pass. There is no extra scene render or fullscreen
reflection pass. It remains a screen-space approximation: absent/off-screen
content cannot be reflected, and shared mono history has stereo limitations.
Visible reflections require a fresh headset check.

### Lesson entry

CutScene59 has an authored box trigger with CollisionWidth=140, Radius=40,
Height=80 UE units, not just a 40-unit sphere. The trigger loader and selection
now preserve the box dimensions and yaw. Stage-18 recovery also accepts the
owned lesson route marks if a save is already inside the doorway.

The read-only current-save location (39.6532555,25.0901165,-111.846046), stage 18,
selects the lesson in the CPU test. Earlier stage 16 cannot activate it.
Draco-before-optional-Filch ordering was not changed.

### Card sound

Owned HProps.WizzardCardIcon, Core.TextBuffer export 548, distinguishes its
Spawned sound pickup_wizardcard from Rising.BeginState pickup_wizardcard2.
The latter is HPSounds.u Engine.Sound export 337, MPEG payload. The new
hash-keyed cache is 48 kHz mono signed 16-bit PCM, 1,013,134 bytes
(10.5535 seconds). Only this one new audio cache was transferred.

## Diagnostics definitions

- FPS is submitted application frames per wall-clock second, refreshed about
  once per second; it is not a hardcoded headset refresh-rate claim.
- DEVICE CPU/GPU are optional runtime percentage counters. CPU is the advertised
  average device utilization. APP (META) reports the runtime's CPU/GPU frame ms.
- FRAME CPU WORK is render-thread CPU time before queue submission, excluding
  GPU/XR wait time; it is not all-thread utilization. Per-eye CPU times measure
  command recording.
- GPU eye/frame/copy scopes use Vulkan timestamps and the device timestamp
  period/valid-bit count, including wrap handling. Scope intervals can overlap
  with pipelined work and are not additive device-utilization percentages.
- The query is read after the renderer's existing submitted-frame fence; no
  additional GPU wait or second world render is introduced.
- Missing counters show N/A, not a guessed percentage. The panel text uploads
  once per second and draws in a batch: panel + text, two draws per eye.

META extension is enabled only when advertised, paths are enumerated, metrics
are enabled on the session, and result validity/unit fields are checked.
Reference: [Khronos XR_META_performance_metrics](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XR_META_performance_metrics.html).

175% per-axis render scale uses 3.0625 times the pixels of 100%, with larger
swapchain/depth/history allocations. Neither 72 FPS nor utilization at that
setting has been measured in C33. No performance conclusion is inferred from
Vice City or the age of the original game.

## Offline verification

- ARM64 native build: PASS.
- Host build and CTest: 18/18 PASS, including pinned-panel input, 175% clamp,
  timestamp wrap and unavailable-metric behavior.
- C33 owned-data probe: PASS for lesson width, current-save recovery, stage
  gating, two urns, original card MPEG and horizontal SSR material selection.
- Full intro/scene CPU probe: PASS, 32 actors, 60 props, 5 doors,
  12,069,096 vertices; 303 route-ground samples. All 18 intro children reach
  patrol end, zero ground misses. Classroom cast/blackboards/ghost pass.
- 254 texture layers before animated fire, 255 after fire; below the 256 cap.
- Full-scene test's old exact audio-count expectation was updated from 78 to 79
  for the new card clip; it also validates the card asset and required caches.
- Layout previews inspected: settings, debugger with deliberate N/A sample data,
  and white-text objective. These are host previews, not Quest screenshots.
- APK signature/alignment/ARM64/proprietary-asset/marker audit: PASS.
- git diff --check: PASS (existing line-ending warning only).

## Deployment

Quest 3 serial 2G0YC1ZF760BPC, package io.github.hpvr.quest.
adb install -r returned Success; no process was launched.

- APK android/app/build/outputs/apk/debug/app-debug.apk:
  694F59A1F6978FE0E5D679F2508B4FECAEAB6EE98CC8093F16838403F52F3892
- Native build/quest-c3-android/libhpvr_quest.so:
  6D0A3839C52EE78D742EF58D6BF2564F4ED564F6212367BC83D5DE8969BB715B
- New cache HP/Cache/Audio/pickup_wizardcard2.84078648.s16:
  853F053FB20D60B0230A56011FC9335C3CE2784C1E2AD3A78022A817397FE2CC
- Installed APK:
  /data/app/~~-MMMUD9Ar5DVcKfGCDAPtA==/io.github.hpvr.quest-KaNdGzJHs0PFU49Yq2xAtw==/base.apk
- C32 rollback local/quest-c32-before-c33.apk:
  ABA1E0DA05159131EC70EBB9FC342E9947F17FEC62C3BDE2BF903911EB8032D8

All six progress-save bank hashes and both VR-settings bank hashes are identical
before and after installation. The new cache's device SHA matches the host.

## Fresh headset checks

1. Open the world menu with both grips + menu; turn/lean and verify it stays
   anchored while the world remains visible. Open and pin the debugger.
2. Compare 100% and 175%, check effective eye dimensions and real counters.
   N/A means the runtime did not expose that counter.
3. Compare SSR 0/100 over the red wooden floors with visible scene objects.
4. Continue the existing stage-18 save into the Quirrell lesson.
5. Check objective/health HUD, original card pickup jingle and the two urns.
