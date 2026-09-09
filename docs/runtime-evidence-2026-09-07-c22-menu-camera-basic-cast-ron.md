# C22 - menu compositing, cinematic direction, basic wand and first quest

## Scope and acceptance

Implemented the user's menu seams/flicker, faster movement, unlearned wand cast,
authored cinematic facing, and the beginning of the first quest.
Quest 3 installation succeeded. **The application was not launched. Visual,
audio, timing, comfort and performance acceptance on the headset are pending.**
The retail installation and retail saves were read-only. Existing Quest saves
were not deleted or modified by deployment.

## Changes

- Menu/book/logo use an ordered, depth-test/write-disabled Vulkan pass instead
  of almost-coplanar depth offsets. UI texture coordinates clamp to texel
  centers at tile edges; world texture repeating is unchanged.
- Walking speed is 2.8 m/s (previously 1.8); capsule stair support is retained.
- Cinematic Preface commands resolve the original named targets. Camera
  orientation uses those targets and preserves relative physical head turns
  and eye separation. Non-camera FACE can resolve a cast alias such as Harry.
- Basic casting remains separate from the locked Flipendo gesture. Hold the
  trigger to aim with red/cyan orbiting glow discs; release casts toward the
  displayed point with owned Smoke5 sprites and the original spell_dud sound.
  There is no damage or Flipendo incantation. BSP picking is two-sided and
  nearest-hit; enabled character bounds cannot override a nearer BSP hit.
  Tracking loss or entering presentation mode cancels the basic cast. The
  trigger must be released after a menu/tracking transition before charging.
- Load CutScene51 from the owned map: five cast tracks, 22 non-null locations,
  original 2.6 m trigger radius, RON_001 dialogue, Ron's run/Talk animation,
  Harry's theatrical participation and the authored passing child.
  The disabled male-child movement track stays invisible.
- Quest stages: opening, meet Ron upstairs, Ron encounter, follow Ron into
  corridor, reached Ron. Pause shows the current objective.
- Save journal version 2 adds quest stage and Ron pose. Version 1 continues
  after Dumbledore at the meet-Ron stage without replaying the opening.
  A saved in-progress Ron scene restarts that scene, not Dumbledore's opening.
  Save paths remain restricted to the app's Android private SaveGames directory.

This is the first quest leg, **not the full tutorial**. Fred/George's following
scene, jumping passage and the Flipendo lesson remain unimplemented. The air
effect is a bounded port rendering owned smoke artwork, not an assertion of
pixel-identical retail particle simulation. Cinematic interpolation/fades are
still the bounded interpreter, not a complete UE1 script VM.

## Offline checks

- ARM64 native build: PASS.
- Host build and CTest: 10/10 PASS.
- New tests cover basic input edges, reticle destination retention, cancellation,
  empty-space casting, closest/two-sided BSP picking, coplanar ordered UI
  geometry, and every quest objective's baked menu geometry.
- Camera tests: target-facing negative-Z, vertical targets, initial headset
  heading cancellation, retained head turn, 32 mm eye offset, invalid input.
- Save tests: v2 Ron pose/stage round trip, v1 migration, two-bank recovery,
  incomplete writes, slot isolation and invalid numeric input.
- Owned frontend probe: 14 story pages, 4 music tracks, 49 textures; 20 audio
  cache entries prepared locally (14 story + 4 stereo music + 2 gameplay).
- Owned intro probe: PASS, 15 actor meshes including ten distinct child skins;
  18 opening child spawns/despawns, zero ground misses, 303 Dumbledore ground
  samples. CutScene51 targets and cross-track cues resolve; eight Preface
  references validated across the opening and Ron scenes.
- Owned navigation probe: grand stair up/down 5.12 m; bounded collision-aware
  paths from upper stair area to Ron trigger and from Harry's end marker to
  Ron's corridor wait marker PASS. Search expansions: 763, 842, 76.
  The navigation search is an offline test, not a new runtime navigation system.
- APK audit: signature, ARM64, alignment, required C22 markers and no bundled
  proprietary assets PASS. Retired red/blue loading marker absent.

## Deployment evidence

Device: Quest 3, serial 2G0YC1ZF760BPC.
Package: io.github.hpvr.quest.
Read-only device checks confirmed the installed C21 hash and matching owned
AllDialog.uax and Magic_sfx.uax before transfer.

Only two new source-keyed PCM cache files were added under the port's existing
external HP/Cache/Audio folder. Local and device SHA256 match:

- RON_001.243597c9.s16: 606878 bytes, about 6.32 s,
  58F1FCF351B0DED6F5AD295BD96BC8506784D0937880FC4C59D0D6EA9213A46B.
- spell_dud.674dbb6c.s16: 95296 bytes, about 0.99 s,
  3E0C837F0CB009CDFC38C80DDFA8EEF88D92DDF5476BD06F7746AEEC24292E89.

C22 APK: android/app/build/outputs/apk/debug/app-debug.apk

- SHA256 BBF96048B68FA61BB24347538790A04B782E0512593F871DBDEE65F5CF0531EE.
- Installed base.apk SHA256 matches exactly; adb install -r returned Success.
- Native libhpvr_quest.so SHA256:
  FD71C35E99D76C5E3D49990054130DAEEE73C211E509A77F33D349BE27F457A2.
- pidof returned no application PID after installation. No launch command sent.
- Previous C21 APK preserved at local/quest-c21-before-c22.apk, SHA256
  9B62F36F2EA198DDB4FF241467F468F28722E581AE4342174356432A46EBAB3A.

C21 cannot read new version-2 saves; keeping an APK backup is not a promise of
backward save compatibility. No downgrade or save conversion was performed.

## User-run acceptance

Launch normally when convenient. Menu/book edges and logo should be stable.
In free movement, hold trigger and aim, then release for basic cast; no tracing
gesture is required. After Dumbledore, climb the stairs to meet Ron. Continue
should retain quest progression; B opens pause with the objective.
Confirm headset behavior before calling these changes runtime-accepted.
