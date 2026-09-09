# C19 - opening cutscene children and authored door events

## Request and accepted baseline

The user confirmed C18 Dumbledore animation and speech on Quest, but reported
the missing children coming through the right-hand entrance. Retain that
accepted cast/audio baseline and remove no owned files. Install updates without
launching the app, as previously requested.

## Cause

CutScene4 broadcasts MOMDis1..4, but C18 only logged these triggers. Their
Engine.Dispatcher OutEvents arrays drive TriggerSpwnBsChrOnPPnt actors. Also,
the property reader retained text only for scalar Name values, so indexed
OutEvents names were unavailable. The real GrandHallDoors event is in an
indexed slot; the C18 HarryIntro door bridge was compensating for missing data.

## Implemented

- Decode Name array values with the same bounded decoder as scalar names.
- Load four MOMDis dispatchers, their populated event slots and cumulative
  OutDelays from the owned map. Deliver events to eight routed child spawners.
- Resolve ten child classes through the existing package/class/default linker.
  Add them as model templates, not a permanent visible debug cast.
- Emit one child per matching spawner per trigger, using serialized class
  choices and corresponding GroundSpeed. KidPath2 has two matching spawners.
- Follow original PatrolPoint chains, pause times and bDestroyPawn endpoints.
  Select original Run/Breathe clips; rotate toward each path leg; support feet
  against BSP throughout traversal. Keep templates out of targeting/collision.
- Original GrandHallDoors triggers now toggle the two mover brushes both ways.
  Removed INTRO_ENTRANCE_BRIDGE; existing KeyRot and MoveTime remain.
- Keep Dumbledore's seven clips and five source-keyed dialogue recordings.
  No retail write, asset redistribution, game launch or external audio change.

## Offline evidence

- Host build: PASS. CTest: 7/7 PASS, including disabled-template target regression.
- ARM64 build: PASS with warnings treated as errors.
- Production loader/path helpers exercised by hpvr_quest_intro_probe: PASS.
- Loaded: one main actor, ten templates, three clips per child template, two doors.
- Main actor: seven clips; 303 grounded route samples; five cached voice files.
- Representative opening-event schedule plus the later direct KidPath2:
  18 spawned, 18 destroyed at route ends, zero missing floor queries,
  zero pending events. Both door toggles delivered; final open request false.
- All sampled clip feet normalized to their template floor. Every spawner
  choice resolves to a loaded model. Vertex and texture storage ranges match.
- Final scene: 2,617,281 stored vertices (prebaked animation frames).
- Full output: local/c19-intro-probe.log (owned-data diagnostics, ignored).
- APK C19 marker, signature v3, alignment and proprietary-assets audit: PASS.
- git diff --check: PASS (line-ending warning only).

The offline schedule exercises production dispatcher and movement helpers;
it is not a headset run or a full end-to-end interpreter/camera timing test.

## Installation

- Quest 3 serial: 2G0YC1ZF760BPC.
- Package: io.github.hpvr.quest.
- adb install -r: Success.
- lastUpdateTime: 2026-09-06 11:56:42.
- Local and installed APK SHA-256:
  E4EFE6BD425761F798C0E13FD3D19A14D670E11F34FEA55DE99F692FDD91B7A6
- Previous C18 APK preserved as local/quest-c18-before-children.apk, SHA-256:
  8DCAE66C292A9BA53E7823F2907C5BFDB8257C79060D77039F9B5AE8E67B07F4
- No app PID after install. Agent did not launch the app or retail game.

## Remaining fidelity limits

This remains a bounded intro interpreter, not an UnrealScript VM. Class
selection cycles through each spawner's authored pool; original random-choice
semantics have not been reconstructed. Path legs are linear with smoothed
heading and BSP floor support, not a recreation of the original pawn AI.
Children are cinematic visuals, not spell targets or colliding gameplay NPCs.
Giggling broadcasts are retained but not yet connected to sound playback.
Full head-tracked visual timing, child appearance, facing and performance
still require the user's launch; installation is not runtime acceptance.
