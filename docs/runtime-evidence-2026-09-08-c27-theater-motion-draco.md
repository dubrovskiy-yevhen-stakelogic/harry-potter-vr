# C27: theatrical loading, animation continuity, L3 and Draco recovery

## Evidence boundary

User accepted C26 loading art/knight animation but reported head-locked art,
knight pose snaps, George facing away, late bean-lesson twins, blocked Draco
scene and continued run bobbing; requested L3 sprint. Preserved the existing
dirty workspace. Retail installation and Documents saves were read-only.
Installation authorized by standing user request; no automatic launch.

## Diagnosis and changes

- Warner quad used XR VIEW space. It now uses LOCAL space and a once-captured,
  upright theater anchor. Loading and front-end share BuildTheaterPose: 2.5 m
  distance, 2.8 x 2.1 m screen; subsequent head rotation does not move the screen.
- Knight transition clips were sampled as loops, including interpolation from
  the last key back to the first. Added an opt-in non-looping sampler that
  clamps each track at its last key; only one-shot knight transitions use it.
  Existing default looping semantics remain unchanged.
- Run/walk sampling can hold root-bone Z at bind height while retaining limb
  articulation. This is explicit VR stabilization, not a claim of byte-exact
  original root motion. Removed idle/reset between consecutive MoveTo segments;
  only an actual locomotion-clip change resets its clock.
- Waiting/lesson twins now inherit the authored yaw of the corresponding source
  cast instance, not the previous room's direction. George's bean-lesson arrival
  restores attention toward HPLOC while waiting for the dialogue to finish.
- Bean-lesson twins are positioned during stage 10 once the player is more than
  8 m from the prior lesson trigger, well before CutScene55 begins. This avoids
  relocating them in front of the player immediately on camera release.
  Existing stage-10 saves stage them by the same rule; save format unchanged.
- L3 is bound to left thumbstick/click. Press while moving toggles 6 m/s sprint
  (normal speed remains 4 m/s); stopping, menus, cutscenes or focus loss cancels.
- Device save inspection found stage 12 at head (58.66809,25.17321,-107.21909),
  beyond Filch and the narrow Draco trigger. C26's mandatory sequential stage
  gate prevented Draco from starting. Draco now accepts stage 12 or 14 and a
  bounded 7 m arrival/recovery area with the existing vertical gate. Completed
  Draco stages do not replay. No device save was edited to advance the story.

## Verification

- Host Release and Android ARM64 native builds: PASS.
- CTest: 12/12 PASS. New synthetic sampler test verifies last-key hold, unchanged
  default wrapping, root stabilization. View tests check theater pose and sprint.
- Owned C27 probe: measured sampled top-height span in base model scale:
  Harry 0.132927 -> 0.0397244 m; Ron 0.0926617 -> 0.0120369 m.
  Source timings/limb clips are retained. This measures data, not headset motion.
- Owned scene selection: actual C26 saved coordinates select Draco at stage 12
  and 14; rejected from another floor and after completion.
- Owned knight regression: max adjacent-frame vertex displacement 0.02915 m
  across all six knights, including transitions/cycle boundary; feet stable.
- Owned full scene probe: PASS, 23 models, 10,668,618 vertices, 250 atlas layers
  before the runtime fire layer; ranges/skins/intro child lifecycle valid.
- Existing grounding/jump/cutscene cue-graph tests: PASS.
- APK signature/alignment/ARM64/C27 input-marker/asset exclusion audit: PASS.
- git diff --check: PASS.

## Deployment

- Device Quest 3 serial 2G0YC1ZF760BPC, package io.github.hpvr.quest.
- APK SHA256: A52F3197DA12FD2D413B5815A3DCBF967ADF6458756333B2ABE44A622A36716E
- Native SHA256: 304CEA4D913DFA83287778B4AAB93FD86197952894AE18A2395F4704D55F5D57
- Installed base.apk matches local APK hash:
  `/data/app/~~RwPLjm5_wFVS1mVPSUBRYg==/io.github.hpvr.quest-2qgrzSU69ZozOpj8X_TuKA==/base.apk`.
- Six save-bank hashes identical before/after install. No data/audio replacement.
- Package not running after install; no launch, C27 headset acceptance pending.
- C26 backup: ignored `local/quest-c26-before-c27.apk`, SHA256
  0E1565C715DCAEECADC7E8CAEFF2963DD1F057088DDB8807AE4210F42081401C.

Full SpellLearnTrigger, later level transition and generic script/mover VM
remain outside this bug-fix block, as recorded in C26 evidence.
