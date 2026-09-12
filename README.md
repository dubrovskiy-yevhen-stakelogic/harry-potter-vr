# Harry Potter VR — 0.1.2.1 Alpha

An unofficial native VR port of *Harry Potter and the Sorcerer's Stone*
(PC, 2001) for **Meta Quest 3**. Runs standalone, without a PC or Quest Link.

This early alpha includes the opening Hogwarts tutorials, the first Flipendo
lesson, **Flipendo Challenge** and **Broomstick Training**. A user-owned US PC
copy is required.
Original maps, textures, models, music and dialogue are not included in the
download.

## Changes in 0.1.2.1

- Expanded offline Flipendo recognition for additional pronunciations, including
  different syllable timing and stress.
- Voice-only hotfix for 0.1.2: the same three maps, with no new level content.

Voice casting remains experimental; support for every accent is not guaranteed.

## Features

- **Full 6DOF VR:** stereoscopic rendering, tracked head position and rotation,
  room-scale leaning and an independently aimed, tracked wand.
- **First-person cutscenes:** watch through Harry's eyes or choose the theatrical
  camera. Both support 6DOF; switch instantly, including during a cutscene.
- **Screen-space reflections (SSR)** with adjustable strength.
- **Three casting modes:** CLASSIC, VISIBLE GESTURE and GESTURE. Gameplay gestures
  accept rotated and reversed strokes without requiring precise tracing.
- **Optional offline voice casting:** aim at a Flipendo target and say
  “Flipendo”; repeat while holding the trigger. Voice hints are optional.
- **Three playable maps:** the opening tutorials, Flipendo Challenge and
  Broomstick Training, with timed hoop routes and a hidden wizard card.
- **VR movement:** smooth locomotion, toggle sprint, snap or smooth turning, jumping,
  automatic ledge climbing and L3 + R3 height recentering.
- **Live VR menu**, accessible during gameplay and cutscenes, with saved settings.
- **Graphics controls:** render scale, reflection strength, supported headset
  refresh rates and an optional performance overlay.
- Original character animations, dialogue and music; animated fireplaces,
  candle flames and glows, dark abyss fog and pickup effects.
- Animated tipping pots, persistent broken vases, collectible beans, wizard
  cards and levitating save books.
- Three save slots, level selection and challenge checkpoints at the level
  entrance and original save books.

## Installation

1. Extract the complete release ZIP on a Windows PC.
2. Connect your Quest 3 with developer mode enabled and USB debugging authorised.
3. Run `INSTALL-HPVR.cmd` and select your original US PC game folder.
4. After installation completes, open Harry Potter VR from **Unknown Sources**
   on the headset.

The installer downloads and caches FFmpeg and Android Platform Tools when
missing; the first ADB download asks you to accept the Android SDK terms.
It prepares all three maps and their audio on the PC for faster headset loading.
It does not modify or launch the original game.

See the [installation guide](tools/release/PLAYER-INSTALL.md) for requirements,
offline options and troubleshooting. Do not uninstall an existing build to
bypass a signing-key mismatch: that can erase saves and imported data.

## Touch / Touch Plus controls

| Action | Control |
| --- | --- |
| Move | Left stick |
| Toggle sprint | Click left stick (L3) |
| Recenter height at the current body position | Click both sticks together (L3 + R3), then release |
| Turn (snap or smooth, selected in VR settings) | Right stick left / right |
| Jump | A |
| Aim, cast, draw a gesture or confirm | Right trigger |
| Game pause / back | B, or left Menu without both grips held |
| VR menu, including during cutscenes | Hold both grips and press left Menu |
| Select VR menu item | Left stick up / down |
| Adjust value | Left stick left / right |

**CLASSIC:** hold the trigger to aim, then release. An eligible target displays
the Flipendo symbol and selects that spell automatically.

**VISIBLE GESTURE / GESTURE:** hold to aim, acquire a target, draw the curl, then
release. VISIBLE GESTURE shows your stroke; GESTURE hides it. The classroom
lesson keeps its guided exercise.

**VOICE CASTING:** enable it in the VR menu, grant microphone permission, then
hold the trigger on a Flipendo target and say the spell. Keep holding to cast
again after the projectile and Harry's incantation finish. Release to change
targets. It works with voice hints hidden and is available in the challenge.

**BROOM FLIGHT:** use the left stick to fly, right stick left/right to turn and
right stick up/down to change height. The mounted view shows the broom and
Harry's body without his head. Wand casting is disabled throughout this map.
Hoops allow a small VR aiming margin, but you must still fly through them.

## Settings and limitations

Defaults: render scale **100%**, SSR **30**, refresh rate **90 Hz**, difficulty
**RELAXED**, cutscenes **HARRY 1ST PERSON**, casting **CLASSIC**. Voice casting
and voice hints are off. Turning defaults to **SNAP**; select **SMOOTH** and
adjust **TURN SPEED** from 30 to 180 degrees/second in the VR menu. Settings
persist between sessions. Refresh-rate choices
depend on headset support; ORIGINAL restores the original lesson difficulty.

This is an early alpha, not the complete game. It ends after Broomstick
Training; later levels, Quidditch and the full original options menu are not
implemented. Original PC saves cannot be imported.

Fountain water uses the original texture; its original procedural ripple
simulation is not implemented yet.

**Voice casting is experimental, with limited player testing.**
Recorded examples from a small number of speakers do not establish support for
every accent or headset microphone. Some pronunciations may be missed;
similar-sounding phrases can trigger a spell
while aiming. Recognition runs locally, without uploading or saving your voice.
No internet connection or personal voice training is needed.

SSR can miss reflections or show gaps around objects. Higher render scales,
refresh rates and reflection settings increase rendering load. Unavailable
performance metrics display `N/A`; per-eye timings are not total CPU/GPU usage.

News and feedback: [Discord](https://discord.com/channels/747967102895390741/1547254536203407390).

<!-- player-readme-end -->

## Development

- [Source layout and build instructions](SOURCE-KIT-README.md)
- [Release APK builds](docs/RELEASE-BUILD.md)
- [Runtime architecture](docs/architecture.md)
- [Casting and challenge interactions](docs/CLASSIC-CASTING-AND-CHALLENGE.md)
- [Flipendo Challenge](docs/FLIPENDO-CHALLENGE.md)
- [Scene preparation and loading](docs/LOADING-AND-PICKUPS.md)
- [Gesture processing](docs/wand-gesture-contract.md)
- [Offline voice build guide](tools/voice/README.md)
- [Third-party components and licences](docs/THIRD-PARTY-NOTICES.md)

The source kit contains no game data, speech-model binaries, bundled build
dependencies or signing keys. Build dependencies are prepared separately.

This project is not affiliated with Warner Bros. or the Harry Potter rights holders.
