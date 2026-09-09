# Harry Potter VR

An unofficial native VR port of *Harry Potter and the Sorcerer's Stone*
(PC, 2001) for **Meta Quest 3**. Runs standalone, without a PC or Quest Link.

The demo covers the opening story and tutorials **through the first Flipendo
lesson**. A user-owned US PC copy is required. Original maps, textures, models,
music, and dialogue are not included in the APK or source code.

## Features

- **Full 6DOF VR:** stereoscopic rendering, tracked head rotation and position,
  room-scale leaning, and independent wand aiming.
- **Screen-space reflections (SSR):** adjustable reflection strength in the VR menu.
- **First-person cutscenes:** watch through Harry's eyes or use the theatrical
  camera. Both support 6DOF, with instant switching even during a cutscene.
- **Tracked wand and gesture casting:** a right-hand wand, free-aim basic casts,
  and Flipendo drawn with physical gestures. Choose Original or Relaxed difficulty.
- **In-world VR menu:** available during gameplay and cutscenes without pausing
  the game. Settings persist between sessions.
- **Render Scale from 50% to 175%** and an optional performance overlay with FPS,
  frame time, resolution, per-eye CPU/GPU render timing, and available device metrics.
- **VR locomotion:** smooth movement, toggle sprint, 30-degree snap turns,
  jumping, stairs, climbing, and world/NPC collisions.
- Original Hogwarts geometry, textures, lighting, fire, lamp glows, animated
  characters, and moving suits of armour.
- Main menu, story opening, level objectives, tutorial quests with Ron, Fred
  and George, Peeves, and the first spell lesson, with original dialogue,
  music, and sound effects.
- Lightning-bolt health HUD, beans, a chocolate frog, the tutorial wizard card,
  card collection viewer, and original pickup audio.
- Three save slots, checkpoint autosaves, and the ability to continue without
  replaying the introduction.
- Welcome and demo-completion messages with a community feedback link.

## Installation

1. Extract the release ZIP on a Windows PC.
2. Connect your Quest 3 with developer mode enabled and USB debugging authorised.
3. Run `INSTALL-HPVR.cmd` from the release package and select your original game
   folder. ADB and FFmpeg are required to prepare and import the data.
4. Once installation finishes, open **Harry Potter VR Demo** from Unknown Sources
   on your headset.

The installer imports data from your copy without modifying the original game.
See the [installation guide](tools/release/PLAYER-INSTALL.md) for requirements
and command-line options.

A release signed with a different key cannot update an existing development
build. Do not uninstall the old build to bypass this error: you could lose your
saves. Contact the author first to arrange a data migration.

## Touch / Touch Plus Controls

| Action | Control |
| --- | --- |
| Move | Left stick |
| Toggle sprint | Click left stick (L3) |
| Snap turn 30 degrees | Right stick left / right |
| Jump | A |
| Cast, draw a gesture, confirm | Right trigger |
| Game pause / back | B or left Menu button without both grips held |
| VR menu, including during cutscenes | Hold both grips and press left Menu |
| Select menu item | Left stick up / down |
| Adjust value | Left stick left / right |

Difficulty and camera mode can also be toggled with the trigger.
`CUTSCENE CAMERA` applies immediately: `HARRY 1ST PERSON` follows Harry,
while `THEATRICAL` uses the scripted camera with free head movement.

## Settings and Limitations

Defaults: Render Scale **100%**, SSR **30**, difficulty **RELAXED**, and
cutscenes **HARRY 1ST PERSON**. Changes are saved between sessions; updates
preserve existing preferences. Select **ORIGINAL** for the original lesson difficulty.

SSR is experimental. Missing reflections and a strip between a bean and its
reflection are known issues. Higher render scales and reflections increase GPU
load. Unavailable performance metrics display `N/A`; per-eye render times are
not percentages of total CPU/GPU utilisation.

Quidditch, the full original options menu, and progression beyond the first
lesson are not included. Original PC save files cannot be imported.

News and feedback: [Discord](https://discord.com/channels/747967102895390741/1547254536203407390).

<!-- player-readme-end -->

## Development

- [Source layout and build instructions](SOURCE-KIT-README.md)
- [Release APK builds](docs/RELEASE-BUILD.md)
- [Architecture](docs/architecture.md)
- [Gesture recognition](docs/wand-gesture-contract.md)
- [Third-party components and licences](docs/THIRD-PARTY-NOTICES.md)

This project is not affiliated with Warner Bros. or the Harry Potter rights holders.
