# C20: opening menu, story book, private saves, theatrical Harry and music

## Scope and boundaries

- User accepted C19 children and requested a substantial start-game block:
  menu, narrated opening story, saves/continue, Harry in the theatrical intro,
  distinct original children, no pre-lesson Flipendo, and original music.
- Existing user authorization: install normal builds automatically, never launch.
- Golden `C:\Program Files\HP` and retail Documents saves were read-only.
- No proprietary assets added to the source tree/APK. Derived audio and previews
  remain ignored under `local/`; audio is private owned-data cache on this user's Quest.
- No retail game or Android activity was launched by the agent.

## Implementation

- Native world-space 640x480 menu with original MenuArt moon/title assets.
  Start Game opens three independent game slots. Existing slots offer Load Game
  and New Game; replacing a slot requires confirmation, defaulting to No.
  Options/Quidditch explicitly show unavailable placeholders. Exit directs the
  user to the Quest system menu; it does not force-kill the process.
- Original FEStoryBookPage BookPages3 class defaults specify fourteen entries.
  Their four-tile illustrations, narrator recordings and subtitle strings load
  from user-owned HPMenu.u, StoryBookTest.utx, AllDialog.uax and hpdialog.int.
  No copied retail script implementation or dialogue text is embedded in code.
  Original-work small glyph atlas renders menu labels and captions.
- Right trigger confirms or advances one book page per press; left stick selects;
  right B opens pause/back. Book narration waits 1.9 seconds and pages advance
  after clip duration plus 3 seconds. Pause freezes narration, music and scene.
  Skip Story enters CutScene4; Skip Opening Scene fast-forwards its bounded
  interpreter, suppressing dialogue playback, to the post-intro checkpoint.
- Save format is **HPVR_PROGRESS version 1**, not retail `.usa` compatibility.
  Three slots, two alternating checksum-protected banks, increasing generations,
  temporary writes, file flush/fsync and atomic rename; Android directory fsync.
  Readback verifies every successful commit. A damaged last bank falls back to
  the previous complete bank. Failed saves show a dedicated error screen.
- Saves use ANativeActivity.internalDataPath/SaveGames, explicitly restricted to
  this package's private user-0 path. Never use the owned-data root as save storage.
- Checkpoints: every book-page boundary, start/end of opening, every 15 seconds
  in free movement, and Save and Main Menu. A book save resumes its current page;
  a mid-CutScene4 save restarts that scene (or B can skip it); a completed-intro
  save restores player position/yaw, Dumbledore/Harry offsets/yaw and door phases
  without replaying the book or intro. These are bounded opening checkpoints,
  not a full Unreal VM/world serialization or later-level progression system.
- Save restoration offsets the current OpenXR head origin so a fresh headset
  tracking origin does not shift the stored world position.
- Harry is included in the authored cast only while the theatrical intro plays;
  hidden during first-person exploration, without an invisible collision capsule
  or selectable spell target. His original idle is `breath`, and forward motion
  is `run` (no `walk` sequence); loader/interpreter use these actual animations.
  Original look2, scratch and adjustglasses clips are sampled too.
- Child appearance comes from per-slot MultiSkins overrides resolved leaf-first
  through actor/class defaults. Ten distinct rendered texture sets are verified;
  no arbitrary skin-color edit or substitute assets.
- CanCast is story-locked false in this opening slice: no aim/gesture/cast before
  the as-yet-unimplemented Flipendo lesson. Existing spell code is retained.
- Four original stereo music tracks: title, narrated book, castle fly-through
  (one-shot), and happy_hogwarts_mxlp1 after entry_stairs. Music ducks under speech.
  Fourteen narrator PCM clips join five Dumbledore clips; missing mandatory speech
  or music fails loading instead of being silently accepted.

## Verification

- Windows Release build PASS; Android ARM64 native build PASS.
- CTest: 8/8 PASS, including menu press edges, overwrite cancellation, slot
  isolation, corrupt-bank recovery, incomplete-temp rejection, story resume and
  completion, visible save failure, stale pause-state reset, head-origin restore.
- Frontend owned-data probe: 14 pages, 4 music streams, 48 texture tiles PASS.
  Menu and first story page inspected as offline previews; fixed palette-index-0
  masking (green background), title/button overlap, and caption tint before deploy.
  These previews are not Quest runtime acceptance.
- Production-load CPU probe (`local/c20-intro-probe.log`) PASS:
  Harry 6 clips, Dumbledore 7, ten child templates each with 3 clips;
  ten distinct child skin hashes; all sampled feet at ground baseline;
  303 Dumbledore route/ground samples; 18 children spawned/destroyed,
  zero ground misses, no pending dispatcher events. Both authored doors included.
  79 UI state ranges / 45,006 UI vertices, 176 atlas layers before fire texture;
  total probe vertices 3,963,483. Production fire adds one texture layer.
- APK audit PASS: ARM64, native linkage, C20 markers, original lighting retained,
  black loading path, signature/alignment, no proprietary payload in APK.
- Previous accepted C19 backed up as `local/quest-c19-before-frontend.apk`:
  E4EFE6BD425761F798C0E13FD3D19A14D670E11F34FEA55DE99F692FDD91B7A6.
- Transfer safety review initially rejected an unverified destination. Read-only
  checks confirmed Quest 3 serial 2G0YC1ZF760BPC, the exact accepted C19 installed
  APK hash, and identical title UMX on headset/owned PC installation. Transfer was
  then approved with that evidence and the user's existing deployment permission.
- Eighteen new source-keyed PCM cache files copied to this same Quest package's
  external HP/Cache/Audio directory; every remote SHA-256 matched its local file.
  Existing game packages were already present. No data-root/saves deletion.
- `adb -s 2G0YC1ZF760BPC install -r .../app-debug.apk`: Success.
- Local and installed C20 APK SHA-256:
  ED59B3618CCA9663B13020B57038196AEB5D65852A1B61630F24C305D278D768.
- `pidof io.github.hpvr.quest` empty after install. User launches when convenient.

## Remaining acceptance

Actual headset display, controls, narrative/music mix, theatrical Harry, new child
appearances and on-device save/relaunch behavior require the user's next run.
Build/probe/install evidence is not a claim of that runtime acceptance.
