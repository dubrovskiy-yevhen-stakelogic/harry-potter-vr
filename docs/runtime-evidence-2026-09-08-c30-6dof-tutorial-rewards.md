# C30 — 6DoF cutscenes, tutorial rewards and Flipendo practice

Date: 2026-09-08, installation verified at approximately 23:42 +03:00.

## Scope and acceptance boundary

User requested positional head movement in cutscenes, twins staging/facing,
Peeves' scare and subsequent jump interference, the 25-bean/card door gate,
the chocolate frog, tracking-loss recovery, candle alignment and playable
Flipendo instruction. Keep Draco at the room entrance BEFORE optional Filch.

The owned retail installation and Documents saves were not modified. Changes
were made in the existing dirty clean-room checkout; earlier C18–C29 work was
preserved. No assets or derived dumps were staged or committed. Quest deployment
used the user's standing install permission. The old process was stopped for
installation; this agent did NOT launch C30.

Build, host probes and installation passed. **Headset behavior is not yet
accepted.** This document does not claim the user-visible problems are all fixed.

## Implemented

- Cinematic eye mapping uses a full reference pose and the eye's positional
  delta from that pose. Leaning/crouching no longer cancels out through subtraction
  of the current head position. Stereo eye separation remains present.
- Loss of focus/tracked pose pauses game advancement and presentation audio,
  cancels gesture recording and prevents locomotion physics ticks. On return,
  the last valid world head position/yaw is restored instead of resetting the
  locomotion origin. LOCAL-space changes are applied at their announced display
  time rather than immediately on the pending event.
- Cutscene camera targets that refer to actors use their live positions,
  not the map's original spawn point. This addresses the lesson's initial
  camera looking toward Harry's unrelated original spawn.
- Waiting twins face the encounter approach. During their dialogue the speaker
  faces the actual Harry track. The transfer toward the jump lesson starts earlier
  (after preceding actor tracks finish); canonical twins follow the route rather
  than teleporting to its final marker. Starting the lesson preserves their
  current positions.
- Finished MOVETO routes settle actors back to idle. This applies both at waits
  and after a route finishes, including Hermione; a moving actor keeps its run
  animation. A new test covers this separately from route translation.
- Peeves starts hidden and uses the initial base station, not the later obstacle
  path. The first attack moves toward the player, applies the scripted 5 health
  loss and retreats without taking the VR camera. After the twins departure he
  transitions into a repeatable obstacle patrol; nearby contact deals 5 health
  damage with a one-second cooldown. Tutorial damage is currently nonlethal
  (minimum health 1; no death/restart implementation in this change).
- Added the owned chocolate frog pickup (10 health) and its actual PCM pickup
  sound. It is a static pickup presentation, not the full original frog hop AI.
- Loaded both FGsec2 sliding wall movers. They remain closed/collidable until
  the 25-bean reward scene awards the card and broadcasts the opening event.
  CutScene3 uses the original speech/cues with the existing twins, including
  card-cast/reaction animation clips. The Dumbledore card appears in the book
  and as the world reward pickup.
- Flipendo instruction now has four accepted gesture rounds, original Quirrell
  responses, a ready/listening HUD and a durable round count. Casting is gated
  while the teacher speaks. The existing forgiving VR gesture threshold is kept
  across all rounds, not the original increasing 2D mouse-score thresholds.
  After four rounds, CutScene60 plays.
- Candle flame anchors use the center of the upper wax shaft rather than one
  extreme top vertex. Candle particles start at the wick without lateral jitter.
- Save V6 persists health, frog/card state and lesson passes and reads V1–V5.
  Older saves already beyond stage 12 retain access beyond the newly restored
  reward gate; this migration does not reset story progress.

## Important unfinished boundary

The lesson's authored `ChangeLevel Lev_tut1b.unr` is recognized and ends its
camera/cue waits, but loading and implementing that next challenge map is NOT
implemented. C30 delivers four-round instruction and the follow-up scene, not
the complete Flipendo challenge level. It logs `travel=NOT_IMPLEMENTED` and saves
stage 23. The original full script VM is also not claimed: this remains a scoped
clean-room interpreter/VR adaptation. Peeves uses a bounded two-point obstacle
patrol, not the complete retail AI path state machine.

## Owned-data evidence

Read-only references in `Maps/Lev_Tut1.unr` and owned packages:

- CutScene52 actor 2258 / twins 1329 and 1326: original initial George track
  waits for RonGone without an initial facing command; Talk2 is cross-cast speech.
- Peeves actor 2968: baseStation1 861, baseStation2 1101, trigger 1858,
  later path nodes 866 and 886 are distinct locations.
- CutScene3 actor 1702: SpawnWizardCard, swap cues and FGsec2 event.
  Card spawner 1617; sliding movers 1830/1906 (Mover35/Mover37).
- ChocolateFrog actor 1778; owned frog class pickup is +10 health and sound 316
  in HPSounds is PCM. It must not be fed to the MPEG scan/decoder.
- SpellLearnTrigger actor 1610 supplies the lesson voice properties;
  CutScene60 actor 2140 supplies the follow-up and next-map command.

Private extracted diagnostics/cache remain under ignored `local/` only.

## Offline verification

- Windows host build: PASS.
- Android ARM64 native build: PASS.
- CTest: **15/15 PASS**, final run after actor-settling change.
- Owned C30 probe: PASS — live camera targets, moving/waiting actor policy,
  lesson ready/listening UI, CutScene3/CutScene60 cue closure, 78 gameplay MPEG
  clips and the separately decoded PCM frog.
- Full owned intro/geometry probe: PASS. Last full geometry run preceded only
  the actor-settling helper (no subsequent geometry changes): 24 actors, 5 doors,
  31 pickups, 854 frontend layouts, 267444 frontend vertices, 253 texture layers
  before the fire layer, 11331786 combined vertices. It exercised 303 route
  ground samples, 18 children spawned/destroyed, 0 ground misses, and 76 candle
  anchor checks (maximum mesh gap 0.10 m).
- Existing Draco-before-optional-Filch policy tests remain passing.
- APK audit: PASS — AArch64, expected native dependencies/symbols/C30 markers,
  valid signature/alignment, no proprietary package assets, no retired red/blue
  loading path.
- No C30 runtime launch, performance capture or compositor capture was performed.

## Installed artifacts

- Device: `2G0YC1ZF760BPC` (Quest 3).
- Package: `io.github.hpvr.quest`.
- APK: `android/app/build/outputs/apk/debug/app-debug.apk`.
- APK SHA256: `20094F89F310D06981C70221D01ADA94DF68B51585DD4AD298CBAED52B74E3D8`.
- Native SHA256: `F46C08FF602D4AD4B59EF53DB99A02D4EF14AD36D69E3E890FA341C3D4035CB3`.
- Installed base APK SHA256 matched the local APK exactly.
- Installed path: `/data/app/~~25X3AquGM_7E61B1XFnZNw==/io.github.hpvr.quest-gz4Q-sQPO56iPy4PV6416w==/base.apk`.
- 25 missing speech-cache files added, 10066150 bytes. All **96 planned files**
  then matched local SHA256; 105 total remote cache entries including existing
  clips outside this frontend plan. Existing cache files were not replaced.
- Previous C29 APK preserved as `local/quest-c29-before-c30.apk`, SHA256
  `08EF4D9860F2FABEAE9E7C7900CB28721666E1BFCE975C443CE434FDA3A10555`.
  This is an APK backup, not permission to downgrade saves after they become V6.
- Install returned `Success`; `pidof` was empty afterward. No launch command.

All six private save journal files had identical hashes before/after install:

| File under files/SaveGames | SHA256 |
| --- | --- |
| slot1.0.hpvr | 7ce8cfb5727b1816b586385c6b62be90b56a036ee572adae46bf1b4a23c21074 |
| slot1.1.hpvr | 235a759bf2696c61ab18d497afef8db3599e87e8a0a49ac20a25d9b57f962668 |
| slot2.0.hpvr | a7b636c2733d11be8f2f7aeab243feff22e4558bb03f82b9fb4570f43637dea2 |
| slot2.1.hpvr | ed0f8147e787ecd1badeaaac22098d8cf7996daec52beb55e08b218762ad78d3 |
| slot3.0.hpvr | bbfaf63bf1c9f867e761a6c67286e219b46e8a0e98d30ec0b597373768b482bb |
| slot3.1.hpvr | 3f2df090be6b88cc7c251ae9a143d2def8f184d3ccfe95ec043a957d3fd9e03e |

## Next headset acceptance checks

1. In a cutscene, lean left/right and crouch: perspective/parallax changes while
   the authored shot remains the base. Remove/re-wear the headset in gameplay
   and in a scene: no return to spawn or scenario advancement while unfocused.
2. Approach the twins, hear George's first line, follow into the jump room:
   check waiting directions and no visible arrival teleport.
3. Peeves is initially hidden, attacks the player without a camera takeover,
   damages the health bar, retreats and remains a jump obstacle. Frog heals.
4. With fewer than 25 beans the exit stays blocked; with 25 return to the twins,
   receive the card, see the stone passage open and retain the card after reload.
5. Draco precedes optional Filch. Hermione stops at her destination. The lesson
   opens looking at the participants and accepts four rounds with speech/HUD.
6. Check flame bases on the table candles from both sides.

These are future player checks, not completed runtime evidence.
