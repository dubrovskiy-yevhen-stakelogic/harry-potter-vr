# C28: proximity speech, Ron lead-in and Gregory passage

## Scope and evidence boundary

User requested ordinary NPC speech without camera capture, Ron arriving before
the twins scene, and restoration of the statue/closed secret wall. Preserved
the existing dirty C18-C27 workspace. Retail and Documents runtime state were
read-only. Standing install permission used; no game launch or headset claim.

## Implementation

- Read instance BumpLines from owned Lev_Tut1 and resolve their original sound
  objects. Ten source profiles cover Dumbledore, Filch and the twins' different
  lesson instances. Map each canonical twin to the current lesson's profile.
  No invented speech or cinematic dialogue substituted for missing profiles.
- Proximity speech leaves player/camera control unchanged, uses line-of-sight,
  1.9 m entry / 3 m exit hysteresis, 30 s per-profile and 8 s global cooldowns.
  No interruptions of active story tracks/dialogue; restore idle/attention
  after a line. NPCs without authored bump lines remain unchanged.
- Missing CutScene0 supplies Ron's BackPeddle -> AllPath2 -> RonHallLoc route.
  Only movement and final attention run; its camera/short Harry capture are
  intentionally omitted. Ron uses the original Tut1Ron GroundSpeed 230 UU/s
  (4.6 m/s), run animation, and grounded continuous movement. CutScene52 waits
  for Ron at the destination, including vertical gating. Stage 3/4 saves resume
  at their nearest route segment; old stage 5 saves with Ron downstairs recover
  via stage 4 in memory without editing save files externally.
- Restore HProps.GregorySmarmy actor 2722, class 237/mesh 1145, authored scale
  and yaw, with mesh feet supported on the pedestal. Props total 55.
- Restore Mover33/FGsec1 brush 1754, owned pivot/prepivot and KeyRot, MoveTime 2 s.
  The CutScene52 RonGone departure cue opens it when the twins leave, once, with
  stone_door_long. This is an explicit cue bridge, not a general UE1 Trigger VM.
  It remains open after this lesson. V4 derives secret-wall state from the
  quest stage; existing two hall-door save indices/order stay unchanged.
- Closed wall contact sweep covers walking and jumping without requiring a
  floor in the mover mesh. Contact is removed when opening is nearly complete.
- Deduplicate byte-identical resampled mover texture layers. Full host scene
  uses 247 layers before runtime fire (248 including fire), below 256. Updated
  full-scene probe to reserve that fire layer instead of accepting 256 before it.

## Offline verification

- Host Release and Android ARM64 build PASS; CTest 13/13 PASS.
- C28 owned-data probe PASS: 359 grounded Ron route samples, largest adjacent
  height change 0.024878 m; destination reached. Tests cover no camera capture,
  wrong-floor/early-arrival rejection, resume segment, bump stage mapping,
  closed wall walking/airborne contact and unobstructed movement away from it.
- Owned mover test verifies hall-door ordering, initially closed FGsec1 and
  solid collision triangles. Full scene probe PASS: 23 character models,
  3 movers, 10,669,722 vertices, 29 beans, 18 children spawned/despawned without
  missing support. Existing glasses/skin/cue checks passed.
- Sixteen new PCM files added only to the owned-data cache. All 68 frontend
  cache-plan files match host/device SHA256. Total runtime dialogue/effect
  slots: 68; this is not a count of newly added proximity lines.
- APK signature/alignment/ARM64/proprietary-asset exclusion and C28 markers PASS.

## Deployment

Installed with adb install -r on Quest 3 serial 2G0YC1ZF760BPC.
Package io.github.hpvr.quest; no launch, pidof empty after installation.

- APK SHA256: 59869077E8FA23FAD5D81F13A9BF894F230246E02B7F04BC86F2019731D962B2
- Native SHA256: FE509CE245C2665BC83DA69FAD79D3B0BF4FF43E7FE80DCAB3BCFE291E65F96C
- Installed base.apk SHA256 matches local APK.
- Prior C27 APK: local/quest-c27-before-c28.apk,
  A52F3197DA12FD2D413B5815A3DCBF967ADF6458756333B2ABE44A622A36716E.
- All six save-bank hashes unchanged before/after install:
  slot1.0 60da9adfd6a37bdc55b3df9bec63d6d912f38585d318f00eb4b105f86185f40e
  slot1.1 5532b68b369bd6da004d6d7955ebd72ad65f89e5bcabf67eff0a9a68da10a326
  slot2.0 a7b636c2733d11be8f2f7aeab243feff22e4558bb03f82b9fb4570f43637dea2
  slot2.1 ed0f8147e787ecd1badeaaac22098d8cf7996daec52beb55e08b218762ad78d3
  slot3.0 bbfaf63bf1c9f867e761a6c67286e219b46e8a0e98d30ec0b597373768b482bb
  slot3.1 3f2df090be6b88cc7c251ae9a143d2def8f184d3ccfe95ec043a957d3fd9e03e

## Pending headset acceptance

Approach Dumbledore after opening; speech without a camera jump, no repeated
line while standing beside him. Follow Ron upstairs: he reaches the twins
before their scene begins, including if the player sprints ahead. Gregory
statue rests on the pedestal; wall is initially visible/closed and opens when
the brothers leave. Check opening timing and collision clearance visually.
Use an earlier slot/new game for the already-completed passage sequence;
current completed saves intentionally do not replay it.
