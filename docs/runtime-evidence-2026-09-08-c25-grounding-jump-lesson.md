# C25: grounded cutscene cast, masked glasses, candle supports, jump lesson

## Scope and evidence boundary

User reported floating twins, Ron passing underneath stairs, opaque glasses,
floating unlit candles; requested continued story restoration. Existing C18-C24
dirty work was retained. Retail HP and Documents runtime state were not modified.
Standing user instruction permits installation, not automatic launch.

## Changes

- Actor script coordinates are resolved to feet at cutscene entry, teleport,
  movement and save restore. Movement probes follow the previous supported
  height rather than linearly interpolated UE actor-center Z. Camera targets
  referencing actors receive a body-height offset; authored camera locations
  retain their coordinates.
- The skeletal C texture loader propagates PF_Masked from material vertices.
  Child skin overrides also preserve that mask. Harry's glasses no longer
  lose palette transparency during texture loading.
- Load eight owned RectangleWoodTable instances before candle fixtures.
  Ground table/stand bases; support single candles on actual table geometry.
  Add 76 wick-aligned candle emitters (16 single, 20 three-arm fixtures), alongside
  the existing ten torch emitters. Candle particles use the existing fire
  renderer with smaller scale/two particles and off-screen/distance rejection.
  These are reconstructed animated effects, not execution of the full UE
  particle system. Tables provide placement support, not new player collision.
- Restore CutScene54 jump instructions and CutScene55 treat/25-bean instructions
  from owned actor script properties, with five plus six original speech clips.
  Remap their Fred/George instances to the existing animated cast. Extend
  progress stages through 12, checkpoint/resume both scenes, and allow climbing
  back up in the jump lesson region. Spell progression remains story-locked.
- Restore Harry's authored GroundSpeed=200 UU/s, 4.0 m/s at the current scale
  (previously 2.8). Jump launch velocity remains 4.9 m/s. Add a bounded 0.16 m
  landing-lip resolution while descending; this prevents snagging on the first
  platform edge without increasing jump height.

## Offline checks

- Release host build and Android ARM64 build succeeded.
- CTest 11/11 passed, including save round-trips for stages 8-12, old save
  migration, jump ceiling/double-jump/landing checks and movement-speed checks.
- Owned C25 probe: all four encounter cue schedules terminate; Ron's sequential
  C51/C52 route, including authored teleport and carried position, has 3,000
  samples and zero support misses. Initial cast feet resolve to geometry.
- Owned first-platform test: old 2.8 m/s lands on the lower floor; restored
  4.0 m/s plus landing-lip resolution reaches x=52.2779, capsule Y=12.9153.
  This proves that gap, not a full automated playthrough of the remaining map.
- Owned intro probe: Harry's masked material has both transparent background
  and opaque frame pixels; 17 character draws, 10 distinct child skins,
  18 spawned/destroyed children, no child floor misses, two doors.
- Props: 54 instances, eight tables, 76 new candle wicks; total 86 flame emitters.
  Combined scene: 234 texture layers before the fire layer, 613 UI variants,
  170,382 UI vertices. Atlas remains within 256-layer limit.
- Frontend extraction/PCM decoding succeeded for all 11 additional dialogue clips.
- APK signature/alignment/ARM64 markers and proprietary-asset exclusion passed.
  git diff --check passed (only an LF/CRLF advisory).

## Installation

Quest 3 serial: 2G0YC1ZF760BPC. Package: io.github.hpvr.quest.
Stopped the existing process immediately before deployment. Pushed only the
11 additional derived PCM files into the user's existing HP/Cache/Audio, with
individual SHA256 matches. No retail/data package replacement.

APK: android/app/build/outputs/apk/debug/app-debug.apk

SHA256:
2F16A954FAE794F57E15F7862803210A2E0863DF8A14368523D3817D79296311

Native library SHA256:
7396E9F3CD0922D2515B960FA283600DB1970BBFB13D104EDA9A9B5B727D63A7

adb install -r returned Success. Installed base.apk SHA256 matches the APK.
All six save banks (three slots) have identical hashes before/after install.
No application process after installation; no automatic launch performed.

C24 APK preserved at local/quest-c24-before-c25.apk, SHA256:
350411E6539A9492829685228113CD8F0591D46237ED5A8D4D39CE836FB03B5A

Do not blindly downgrade after progressing: C25 retains the V3 record layout,
but C24 rejects stages above 8. No saves were rewritten during deployment.

## Remaining boundary

Device visual/comfort/performance acceptance is pending user launch. No claim
of measured FPS improvement. C55 issues the 25-bean objective; card exchange,
the following cutscenes and the transition to the first spell class are not
implemented in this block. Existing card album remains a placeholder collection.
