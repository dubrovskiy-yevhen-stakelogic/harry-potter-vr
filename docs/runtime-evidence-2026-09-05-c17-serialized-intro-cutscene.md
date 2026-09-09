# Gate C17: serialized intro cutscene

Date: 2026-09-05

## Scope

This gate executes the first level-load cutscene from the user's owned game
data. It does not embed a reconstructed sequence or any proprietary asset in
the source tree or APK.

## Serialized source evidence

The read-only actor census of `Lev_Tut1.unr` reports 2011 actors, including 16
`HPBase.CutScene`, 110 `HPBase.CutMark`, and 49
`HPBase.CutCameraPos` instances. `CutScene4` is the level-load scene nearest the
player start and has `bLevelLoadStarts=true`.

The generic UE1 actor-property retention layer now preserves compact object
references, names, strings, vectors, rotators, `CutCast`, and `CutLoc` values.
For `CutScene4` it resolves:

- five populated cast tracks from the seven serialized slots;
- 21 named actor/camera locations;
- the original command strings, including synchronization cues and waits;
- the original dialogue names `111DumbledoreInfo1` through
  `111DumbledoreInfo4` and `Dumbledore_01`.

The five dialogue exports are decoded at runtime from the user's `AllDialog.u`.
They are MPEG Layer II, 22050 Hz, mono in the original package and are
resampled into the existing 48000 Hz AAudio mixer.

## Runtime implementation

The first interpreter pass supports the commands required to preserve the
scene's main timing and blocking structure:

- parallel cast tracks;
- `CUE` and `WAITFOR` synchronization;
- `SLEEP`;
- `GOTO`, `TELEPORT`, and smooth `MOVETO` for actors and camera;
- `CAPTURE`, `RELEASE`, `CAMSPEED`, and `CAMPROX` state;
- `SAY` and `TALK` dialogue playback.

While the scene owns the camera, player locomotion, wand display, and casting
are suppressed. The authored camera contributes translation only: the Quest
continues to use live stereo eye separation and live head orientation. This
avoids forcibly rotating the user. NPC collision and Flipendo target bounds
move together with a cutscene-driven character.

`FACE`, `TURNTO`, `ANIMATE`, `SETIDLE`, `SETWALK`, `PREFACE`, `FADEIN`,
`FADEOUT`, `EMOTE`, and arbitrary `TRIGGER` side effects are decoded and logged
but are currently state-only/no-op commands. Consequently C17 proves the
serialized scene, dialogue, timing, synchronization, camera translation, and
actor-path layer; it does not yet claim pixel-identical animation, facing,
music, fades, doors, or a complete UnrealScript VM.

## Build and package evidence

- Windows host build: PASS
- Host tests: 7/7 PASS
- Moving spell-target bounds test: PASS
- Android ABI: `arm64-v8a`
- Native target: `hpvr_quest` PASS
- APK audit: PASS
- Signature: APK Signature Scheme v3 PASS
- APK proprietary asset count: 0
- APK SHA-256:
  `66ABE9DA88894652A54FBD4B05CE76EA80BFD843C2E43EF9033440A4803433C2`

The Gradle Java/D8 packaging stage encountered a Windows `AccessDeniedException`
while creating generated dex archives. The already compiled C17 ARM64 library
was therefore packaged through the repository's established native replacement
path, using the installed, previously audited C16 APK as a container. The
script replaced only `lib/arm64-v8a/libhpvr_quest.so`, removed old signature
records, realigned the APK, and signed it again. The final C17 APK then passed
the complete current offline audit.

## Device evidence

- Device: Meta Quest 3 (`2G0YC1ZF760BPC`)
- `adb install -r`: Success
- Installed base APK SHA-256:
  `66abe9da88894652a54fbd4b05ce76ea80bfd843c2e43ef9033440a4803433c2`
- Source/installed hash: MATCH
- App process after install: NOT RUNNING

## Acceptance boundary

Source extraction, host tests, ARM64 compilation, packaging, audit,
installation, and installed-byte identity are proven. The agent did not launch
the application. Headset execution of `CutScene4`, audible dialogue, cinematic
camera comfort, actor paths, and clean return of player control require the
user's current runtime review.
