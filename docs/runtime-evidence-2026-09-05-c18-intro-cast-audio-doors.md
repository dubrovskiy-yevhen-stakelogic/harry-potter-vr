# Gate C18: intro cast, grounding, dialogue and mover doors

Date: 2026-09-05

## User-reported C17 defects

The headset showed an exhibition crowd, Dumbledore translated above the floor
without facing changes, dialogue was silent, and the right-hand entrance doors
were absent. These observations supersede C17's unverified runtime claims.

## Changes

- Removed the class-based stage offsets and exhibition cast. The visible cast
  is resolved from CutScene4: Tut1Dumbledore1 (1672). Harry (603) stays hidden
  for first-person play.
- Sampled seven original HPModels skeletal sequences at approximately 30 Hz
  (bounded to 512 frames per clip), with individual clip clocks. MOVETO uses
  Walk, TALK uses Talk2; ANIMATE, FACE and TURNTO now change actor state.
- Each sampled mesh frame has a feet-relative origin. Moving actor positions
  are projected onto the loaded BSP; a failed ground query retains the last
  supported floor rather than treating a CutMark's Z as foot height.
- The original encoded Sound payload was previously copied through the end of
  its UObject export, including unrelated serialized tail fields. The loader
  now retains complete Layer II frames only. Added a synthetic regression for
  tail exclusion and rejection of an incomplete first frame.
- Removed one-sample silent success for dialogue. PREPARE-QUEST-AUDIO.ps1
  converts nine exact owned MPEG clips into external 48 kHz mono signed-16 PCM.
  Runtime selects these by object name plus encoded-source FNV checksum;
  SHA-256 verifies deployment. No audio is packaged in the APK or source tree.
  A missing cache has an explicitly logged platform-codec fallback.
- Restored GrandHallDoors Mover0/1, brush Models 2593/2600, with original P8
  materials, PrePivot, Location, Rotation, KeyRot and MoveTime. Each brush has
  12 triangles, three decoded materials, and no fallback triangles.
- Updated the scene's vertex-range validation for separate doors and multiple
  per-actor clips (the old global 16-frame count no longer describes storage).

## Deliberate compatibility limits

This is still a bounded intro interpreter, not a complete UnrealScript VM.
Trigger21 is serialized with bCollideActors=false; it is not silently enabled.
Until the original enabling/door-event logic is interpreted, the door pair opens
on the HarryIntro entrance cue through an explicit INTRO_ENTRANCE_BRIDGE.
Generic TRIGGER effects, spawned children, music, fades and camera facing are
not fully implemented. Camera translation still combines with live head
orientation; the first-person Harry mesh is intentionally absent.

The five dialogue clips actually come from Sounds/AllDialog.uax, not the
AllDialog.u path mistakenly stated in the C17 report.

## Verification completed

- Host build: PASS.
- Host CTest: 7/7 PASS, including the new MPEG-tail regression.
- Production CPU loader exercised by hpvr_quest_intro_probe: PASS.
- Visible actors: 1; doors: 2; animation clips: 7; dialogue cache files: 5.
- Dumbledore sampled animation minima equal the supported floor.
- All 303 sampled points along his three intro movement legs found BSP support.
- Final scene storage: 1,272,081 vertices; combined texture byte count matches.
- ARM64 native build: PASS.
- APK signature v3, alignment and C18 marker audit: PASS.
- APK proprietary assets: 0.

CPU probe command:

```powershell
& 'C:\Dev\harry-potter-vr\build\src\quest\Release\hpvr_quest_intro_probe.exe' 'C:\Program Files\HP' 'C:\Dev\harry-potter-vr\local\quest-owned-audio'
```

## Installation evidence

- Device: Quest 3, 2G0YC1ZF760BPC.
- Package: io.github.hpvr.quest.
- adb install -r: Success.
- Device lastUpdateTime: 2026-09-05 21:30:25.
- Local and installed APK SHA-256:
  8DCAE66C292A9BA53E7823F2907C5BFDB8257C79060D77039F9B5AE8E67B07F4
- Nine external PCM files transferred and individually SHA-256 matched.
- App process after installation: absent (pidof returned no PID).
- No retail game or Quest app was launched by the agent.
- Packaging used the existing audited APK container via
  PACKAGE-QUEST-DEBUG.ps1; only the native library and signatures changed.

## Acceptance boundary

The loader, route ground queries, frame bounds, PCM preparation, compilation,
APK audit and installation are verified. Visual facing, animation transitions,
door placement/opening and audible playback on Quest await the user's launch.
Do not treat the CPU probe or installed hashes as headset acceptance.
