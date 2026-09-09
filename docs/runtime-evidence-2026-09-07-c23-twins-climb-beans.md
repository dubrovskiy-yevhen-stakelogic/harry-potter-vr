# C23: twins encounter, bookcase lesson and persistent beans

## Acceptance and scope

The user accepted C22 in this turn ("проверил все хорошо все работает") and
requested continued restoration of story and quests. Existing authorization
permits installing normal Quest builds without another confirmation, but not
launching them. C23 was installed, not launched. Headset acceptance is pending.

Golden retail installation and Documents/Harry Potter were not modified.
No leaked engine code or third-party implementation was imported. Original
assets remain owned-data runtime inputs; neither the APK nor source contains
asset dumps or copied game scripts.

## Implemented segment

- Continue from Ron to the authored CutScene52 trigger. Load its six non-null
  cast slots, 23 location aliases and parallel commands from Lev_Tut1.unr.
- Fred and George use their own skeletal meshes, skins, idle/trot/talk clips.
  Null cast-array slots are ignored. Camera identification uses actor class,
  including the generic CastName3 alias. TALK2 selects cast slot 2 (George),
  rather than using the current track's actor.
- Five original twins lines and Ron's departure line are wired to this scene.
  The scripted camera's temporary release follows visible Harry; it does not
  cut back to the old stationary VR player position. This is a bounded port
  camera adapter, not a claim of frame-identical PC camera behavior.
- After the lesson, hold locomotion toward the bookcase to mantle a nearby
  supported ledge. Mantles rise then advance in bounded increments; both legs
  are swept against the existing collision capsule. Ledge height is bounded
  at 2.8 m and reach at 1.05 m. Ordinary stair limits remain unchanged.
  Mantling is restricted to the authored lesson region and stage >= 6.
- Load 29 placed bean actors, using five owned HProps mesh/texture variants.
  They rotate/bob and use the original AmbientGlow=200 brightness. Collection
  uses body proximity, vertical limits and BSP occlusion, not a wand hit.
- Collecting the first lesson bean advances the objective and relocates the
  twins to the authored next-room actor positions. Entering that room completes
  this segment (stage 8). The pickup plays original Magic_sfx.pickup11.
- Save format V3 records five actor poses, quest stage and sorted unique bean
  actor IDs. V1 and V2 still load. Collection saves immediately; no unsupported
  mid-mantle position is journaled. Pause displays the objective and bean count.

Boundary: CutScene54 supplies the next-room destination only. Its jump lesson,
jumping physics, subsequent quest chains and Flipendo lesson remain unimplemented.
The sixth twins reminder clip is cached but no reminder trigger is enabled yet.
The twins' relocation is a stage transition, not an implementation of their
complete subsequent patrol/AI scripts. Beans elsewhere in this map are loaded,
but this does not certify all later areas or their progression as playable.

## Offline verification

- MSVC Release host build and Android ARM64 native build succeeded.
- CTest: 10/10 passed, including save corruption recovery, V1/V2 migration,
  five actor poses, unique bean IDs, all objective/counter UI ranges, smooth
  mantle and low-ceiling rejection, and existing locomotion/basic casting.
- Owned-data intro probe passed: 17 character draws, 10 distinct child skins,
  29 beans / five colors, 193 texture layers before runtime-only fire addition,
  5,757,957 vertices including UI, valid consecutive draw ranges.
- CutScene52 cue dependency simulation completed all tracks with six dialogue
  commands and cross-cast TALK2 mapped to George. This is a CPU dependency
  check, not execution of the Android scene interpreter on the device.
- Owned BSP navigation passed: grand stairs in both directions (5.12 m rise),
  Ron approach, Ron-to-twins trigger, authored Harry lesson start, bookcase
  beans 2913 and 2145 through four small mantle transitions, and next room.
  Actual pickup predicate succeeds at both beans and rejects collecting from
  underneath. Every mantle start also passes the runtime lesson-region gate.
- APK verifier passed ARM64 markers, signature, alignment and no proprietary
  assets. git diff --check passed (only existing CRLF conversion warning).

## Installation evidence

Quest 3 serial: `2G0YC1ZF760BPC`; package: `io.github.hpvr.quest`.

- The prior process (PID 30316) was stopped for the update.
- Eight new PCM caches were derived from the user's packages, pushed and each
  verified by SHA256. Existing package/data imports were not replaced.
- Device and PC AllDialog.uax both hash
  `7EEFD451BB010A7A7559C030355389BAC3A77F21A0858391C19D8FC27DA94618`.
- Device and PC Magic_sfx.uax both hash
  `722EA1D04F3EAD430FBAF9E0BD8D054C185915F56E2618897EA5C6F1233A3B94`.
- `adb install -r` returned Success. Installed base.apk SHA256 matches local:
  `BAC2260E76FD411A28A7C9F17C015CED396A65779145615A25CB203E18CBDF64`.
- Native library SHA256:
  `1D06742E1AA4CA694E5597E0EB68E0C64FDBC3E1EBD52080F67F4BB9D313403F`.
- Post-install pidof returned no running process; no launch command was issued.
- All four private save bank hashes were identical before and after install:
  - slot1.0: `9fdf950e5af3804cdc428ec5491bcf6831922434f2f3481bb4f88d582bf93466`
  - slot1.1: `e5ad9ed602675c25fe646cd82e62439f503296b77b6c9ba86022b855860012fd`
  - slot2.0: `403d1483b88e0e4e086274b4b76780c29efe71e3c7fcb9d714462ed317357eae`
  - slot2.1: `853bc65e017cd8c8b1601feb3afd3da19b628f83b5617a3872f19b4c5358b1e8`

APK: `android/app/build/outputs/apk/debug/app-debug.apk`.
Preserved C22 APK: `local/quest-c22-before-c23.apk`, SHA256
`BBF96048B68FA61BB24347538790A04B782E0512593F871DBDEE65F5CF0531EE`.
Do not blindly downgrade after C23 creates V3 saves: C22 cannot read V3 banks.

## User handoff

Launch normally and load the existing slot; no New Game/reset is necessary.
Continue the objective from Ron toward the twins. Hold the left stick toward
the bookcase to climb; touch beans to collect automatically. The pause menu
shows the objective and count. Reaching the twins in the next room is the
current playable boundary. Timing, comfort and presentation need headset
acceptance; no immediate per-object user test was requested.
