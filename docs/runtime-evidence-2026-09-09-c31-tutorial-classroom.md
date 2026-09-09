# C31 — tutorial staging, reward approach and classroom

## Scope and source evidence

Worked only in the clean-room HP1 workspace. The retail installation and
Documents runtime state were read-only. Existing unrelated checkout changes
were preserved. Derived probes/previews remain in ignored local directories.
No retail launch and no Quest launch were performed.

- Retail CutScene6 swaps two separate pairs of twins. C30 incorrectly routed
  the canonical pair between disconnected locations. C31 stages them at the
  transfer trigger, not immediately when the climbing lesson begins. Saved
  stage-6 wall-bound actors are restored to the authored waiting marks;
  later lesson starts also normalize the canonical pair.
- CutScene5 is primed at its authored StartCam/Target before the first frame.
  The twins begin at FredLoc/GeorgeLoc. Initial approach/sleep/camera-travel
  waits are removed; both owned lines, cues and visible exit moves remain.
- Reward requires 25 beans plus a new, unobstructed close approach to Fred
  (1.9 m; rearm outside 2.3 m). The pickup itself cannot start the scene.
  Obsolete bean-request barks are suppressed once the task is satisfied.
- First Peeves damage is idempotent and saved; departure cannot bypass it.
  Health flashes red after damage without flashing the entire VR view.
  C30 already had a damage path: available evidence did not establish why
  the user did not see it. Their save also showed the healing frog consumed.
- ChocolateFrog uses owned Croak/Hop/Breath/Hop animation, with a fixed rest
  floor anchor rather than the static import pivot. Pickup healing is retained.
- Classroom actor 2279 (Ron) and six authored pupils are now loaded and kept
  enabled. HProps.TransBlackboard actors 3565/2280 use their owned mesh/texture.
  Identical character skin arrays are reused without merging actor identities.
- Ghost actor 3148 uses its owned Float animation, a depth-tested translucent
  pass, and no collision/camera capture. The classroom flyby is deliberately
  scheduled for VR; it is not an exact implementation of the original random
  patrol with its 120-second idle.
- Level objective is read from hpmenu.int's [text] section. The empty duplicate
  key in another section must not overwrite it. A fixed theatrical parchment
  panel appears before the opening scene; trigger continues.
- V7 saves read V1–V6 and preserve existing progress. No save files were edited
  or reset during deployment. No new audio/cache or game-data copies needed.

## Offline checks

- Final Windows build and Android ARM64/Vulkan shader build: PASS.
- Final CTest: 16/16 PASS (2.90 seconds).
- C31 owned-data policy probe: PASS, including cue closure, both post-Peeves
  lines, removal of cross-room navigation, reward entry gating, first-hit
  persistence, original objective and classroom roster.
- Full production-loader probe: PASS. 32 character draws, 5 doors, 57 props,
  31 pickups, 12,057,792 combined vertices; 252 texture layers before the fire
  layer (253 runtime, below the 256-layer budget).
- Frog: 129 animation samples; rest-foot ground error 0 m; maximum sampled
  hop-foot height 0.868791 m. These are geometry measurements, not headset feel.
- 303 Dumbledore route support samples; 18 intro children spawned/destroyed,
  zero ground misses; existing candle-anchor and knight-aim checks pass.
- Objective preview was rendered and visually inspected: readable owned text,
  no cropping or overlap. Private preview: local/quest-owned-audio/c31-objective.jpg.
- APK audit: signature/alignment/AArch64/native dependencies/C31 markers PASS,
  no proprietary assets and no retired red-blue loading path.

## Installation evidence

- Quest 3 serial: 2G0YC1ZF760BPC; package: io.github.hpvr.quest.
- APK: android/app/build/outputs/apk/debug/app-debug.apk.
- APK SHA256: 66344E64073F98EE7BB9DFF69385B3A410C3D6E042B8A19C0D237B732C7F9127.
- Native SHA256: FA7EEC5CABF8190A72AA9FE34C514FDC4FFE6B3895191E4AEE714E6B758B6DAF.
- Installed APK hash matches exactly. Installed path:
  /data/app/~~XKGJWZJNt7jKiHkbuZuCDA==/io.github.hpvr.quest-KJrhZUKafrw8t9PbegIHrg==/base.apk.
- adb install -r returned Success. The old app process was running before
  replacement (PID 1969); no app process remained after install. No launch.
- C30 APK backup: local/quest-c30-before-c31.apk, SHA256
  20094F89F310D06981C70221D01ADA94DF68B51585DD4AD298CBAED52B74E3D8.
  This is not permission to downgrade future V7 saves into C30.
- Device and PC hpmenu.int hashes match:
  96C192DB44BF168052A9A2243A11D0EF1AEE3430EF38A2479E8A6994178E15EA.

All six save hashes were identical before/after installation:

| File | SHA256 |
| --- | --- |
| slot1.0.hpvr | 0c877da593d4fb710176b67ca13ce81bfbbbdaebdaa84ecab6ad31983a20d85f |
| slot1.1.hpvr | acd8eb15d3ca29bae736092eafd855a519cbce9e2a5e80996697e8b02f862142 |
| slot2.0.hpvr | a7b636c2733d11be8f2f7aeab243feff22e4558bb03f82b9fb4570f43637dea2 |
| slot2.1.hpvr | ed0f8147e787ecd1badeaaac22098d8cf7996daec52beb55e08b218762ad78d3 |
| slot3.0.hpvr | bbfaf63bf1c9f867e761a6c67286e219b46e8a0e98d30ec0b597373768b482bb |
| slot3.1.hpvr | 3f2df090be6b88cc7c251ae9a143d2def8f184d3ccfe95ec043a957d3fd9e03e |

## Headset acceptance still pending

One continuous tutorial pass should check the twins waiting during climbing,
post-Peeves framing/pacing, first damage and frog hopping, no reward on bean 25
until approaching Fred, no stale task barks, then the classroom roster, boards
and ghost. Objective appears on starting the level, not on every free-play
save reload. No performance or compositor acceptance is inferred from builds.
The next-map transition after the four-round lesson remains outside this block.
