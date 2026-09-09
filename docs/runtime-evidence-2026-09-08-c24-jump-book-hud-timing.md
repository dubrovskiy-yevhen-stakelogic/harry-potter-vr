# C24: jump, in-game book/HUD, visible stairs and cutscene release

## Scope and acceptance

Implemented in the existing Quest standalone adapter, not in the retail install.
The user's report is that rendering feels smooth. No FPS drop was measured or
reproduced in this turn. A clamped render clock can diverge from audio under
slow frames; that conditional observation is not evidence of a performance
problem on this device. Resolution, refresh rate and movement speed were not
changed. Retail files and Documents/Harry Potter were not modified.

The APK was installed under the user's standing install permission. Neither
the Quest application nor the retail executable was launched. Device behavior,
timing, comfort and appearance remain pending user acceptance.

## Changes

- A on the right Touch controller starts a grounded jump. It works without a
  movement-stick input, also supports forward movement, rejects double jumps
  and sweeps against floors/walls/ceilings. Initial vertical speed is 4.9 m/s
  (245 authored Harry JumpZ units at 0.02 m/unit); adapter gravity is 19 m/s2.
  Integration uses at most 8 ms substeps. Six seconds without landing returns
  to takeoff as a bounded recovery, not a completed falling/death mechanic.
  Idle gameplay does not run the extra physics path unless a jump is pending
  or active. Menus/cinematics pause player physics.
- Inventory can be saved during jumping/mantling; the saved player position
  uses supported takeoff/start instead of an airborne point. New encounters
  wait for landing. V3 save layout and V1/V2 compatibility are unchanged.
- The original lightning icon is drawn on a flat, head-relative stereo HUD.
  The original bean-counter art appears for four seconds on pickup. The HUD
  uses the ordered, depth-disabled UI pass and disappears during cinematics
  and menus. Its transform is updated after locomotion for the current frame.
- Left Menu or right B opens the in-game book. Resume, save/main menu, scene
  skip, a seven-page card album and report page are accessible with stick and
  trigger. Horizontal stick changes album pages, with a release latch.
  Back from album/report returns to the book, not the main menu.
  Dialogue pauses in all book subpages.
- Book, album, report, lightning, missing-card silhouettes, tabs, arrows and
  bean graphics are loaded from the user's HPMenu.u / HPBase.u. No extracted
  bitmap or audio is embedded in the APK. Text uses the existing original-work
  bitmap font, not the original game's font implementation.
- The world fragment shader now discards PF_Invisible surfaces. The BSP has
  invisible collision ramps over the visible stair treads; these were formerly
  textured and hid the steps. Collision triangles remain unchanged.
- ANIMATE no longer blocks its script track for a full animation cycle.
  Dialogue waits query the PCM playback cursor rather than waiting again on
  a clamped frame-duration countdown. Explicit script sleeps remain intact.
  Harry release plus camera release returns player control once; remaining
  NPC exit tracks continue instead of holding the player.
  Story-page padding is 0.6 seconds after the voice budget, with the existing
  1.9-second voice startup accounted for.

## Deliberate remaining boundaries

This is still a bounded cutscene/gameplay adapter, not a complete UE1 VM.
Card acquisition/rewards and card descriptions are not implemented: the album
shows uncollected slots, including a locked secret page; it does not grant cards.
Damage/food are not implemented, so the lightning remains full. House-point
vessels are original report artwork, not functioning score simulation.
The playable story boundary is still the next room after the twins' climb
lesson. Jump input does not imply CutScene54 or the entire jump quest is restored.
Basic unlearned casting remains available; Flipendo remains story-locked.

## Offline evidence

- MSVC Release host and Android ARM64 native builds: PASS.
- CTest: 11/11 PASS. C24 covers stationary jump, 20/72/90/120 Hz integration,
  low roof, double-jump rejection, a forward gap crossing, supported airborne
  checkpoint, neutral-stick camera movement, book/album/report navigation,
  page debouncing and control release while an NPC exit track keeps moving.
- Owned-data stair inspection: 9 invisible ramp triangles and 11 distinct
  visible tread levels in the bounded Dumbledore-route corridor sample.
  This is a sample, not the total number of steps.
- Owned BSP navigation: grand stairs up/down, 5.12 m rise; Ron approach;
  twins approach; four mantle transitions to the two bookcase beans and
  the next-room route: PASS.
- Owned intro probe: PASS, 17 character draws, two doors, 29 beans,
  18 children spawned/removed with zero ground misses, six-line twins cue
  dependency simulation. This CPU probe is not Android interpreter playback.
- UI assets: 89 textures, 573 prebuilt UI draw ranges, 141552 UI vertices.
  Combined owned-data probe: 233 texture layers before the runtime fire layer,
  5819199 vertices. All ranges fit the existing 256-layer atlas.
- CPU-rendered book, album, secret page, report and HUD were inspected in
  local/c24-frontend. Fixed squeezed lightning aspect ratio and report-label
  overlap before packaging. No claim of headset screenshot verification.
- APK audit: ARM64, signature and alignment PASS; required C24 markers present;
  proprietary packages absent; retired red/blue loading path absent.
- git diff --check: PASS, only existing CRLF conversion warning.

## Installation evidence

Quest 3 serial: 2G0YC1ZF760BPC. Package: io.github.hpvr.quest.

- adb install -r returned Success.
- Local and installed APK SHA256:
  350411E6539A9492829685228113CD8F0591D46237ED5A8D4D39CE836FB03B5A
- Native library SHA256:
  9BD48CBF2C41A6F0871098DAD789E1FE249DA3B580658C09F02398D52117E0D0
- Installed base.apk:
  /data/app/~~9wQER97uwIZ1yR-RoJrDEg==/io.github.hpvr.quest-yq8A27Z0OIJeLV296fLAuw==/base.apk
- pidof was empty before and after installation; no launch command issued.
- All four private save banks had identical before/after hashes:
  - slot1.0: 9fdf950e5af3804cdc428ec5491bcf6831922434f2f3481bb4f88d582bf93466
  - slot1.1: e5ad9ed602675c25fe646cd82e62439f503296b77b6c9ba86022b855860012fd
  - slot2.0: 403d1483b88e0e4e086274b4b76780c29efe71e3c7fcb9d714462ed317357eae
  - slot2.1: 853bc65e017cd8c8b1601feb3afd3da19b628f83b5617a3872f19b4c5358b1e8
- Existing device HPMenu.u matches the owned PC file:
  42DA2A2F43AC6A15EA87EACE4EBD59A69BAB7685CDA854E9E7A86E7E6D9C6DBD
- Existing device HPBase.u matches the owned PC file:
  30B5EF44E9755AA9C020BE9D863E35335C26C2D6988FD0A00A347A98C44E105D
- No game-data package or PCM cache needed replacement.
- Preserved prior C23 APK: local/quest-c23-before-c24.apk, SHA256
  BAC2260E76FD411A28A7C9F17C015CED396A65779145615A25CB203E18CBDF64.

Launch normally at the user's convenience; an existing save can be loaded.
No new game or immediate per-object headset test is required.
