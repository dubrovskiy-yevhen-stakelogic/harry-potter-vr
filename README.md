# Harry Potter VR

An unofficial standalone VR port of *Harry Potter and the Sorcerer's Stone*
(PC, 2001) for Meta Quest 3. No PC or Quest Link is needed while playing.

The demo covers the opening story and tutorial **through the first Flipendo
lesson**. You need your own US PC copy; game assets are not included.

The features below describe the current demo release. This source checkout is
older: it does not yet include the first-person cutscene switch and uses different
defaults. See the [source guide](SOURCE-KIT-README.md).

## VR features

- **Full 6DOF and stereo rendering:** turn, lean and move your head through the
  scene, with wand aiming independent of head direction.
- **SSR reflections:** screen-space reflections with adjustable strength in
  the VR menu.
- **First-person cutscenes:** watch through Harry's eyes or use the theatrical
  camera. Both retain 6DOF, and you can switch live during a scene.
- **Tracked wand and gesture casting:** a right-hand wand, free basic casting
  and physical Flipendo tracing, with Original and Relaxed difficulty options.
- **World-space VR menu:** available during gameplay and cutscenes without
  pausing them. VR settings persist between sessions.
- **Render Scale from 50% to 175%**, plus a separate debugger window showing FPS,
  frame time, resolution, per-eye CPU/GPU render timings and available device metrics.
- Smooth movement, toggle sprint, 30-degree snap turns, jumping, stairs,
  climbing, and collision with the world and characters.
- Original Hogwarts geometry, textures, lighting, fire, lamp glows, animated
  characters and knights.
- Main menu, story opening, level objectives and tutorial quests with Ron,
  Fred, George and Peeves through the first lesson, with original dialogue,
  music and sound effects.
- Lightning-bolt health HUD, beans, Chocolate Frogs, the tutorial Wizard Card,
  card viewing and the original card pickup sound.
- Three save slots, checkpoint autosaves and continuing without replaying
  the opening every time.
- First-step welcome and end-of-demo messages with news and feedback links.

## Installation

1. Extract the release ZIP on a Windows PC.
2. Connect a Quest 3 with developer mode enabled and USB debugging authorized.
3. Run `INSTALL-HPVR.cmd` from the release kit and select your original game
   folder. ADB and FFmpeg are required for setup.
4. When installation finishes, open **Harry Potter VR Demo** from Unknown
   Sources on the headset.

The installer imports data from your copy without modifying the original game.
Detailed requirements and commands are included in the release ZIP.

A release signed with a different key cannot replace an existing development
build. Do not uninstall the old version to bypass this error: you may lose saves.
Contact the author about migration first.

## Touch / Touch Plus controls

| Action | Control |
| --- | --- |
| Move | Left stick |
| Toggle sprint | Click left stick — L3 |
| Snap turn 30 degrees | Right stick left / right |
| Jump | A |
| Cast, trace a gesture, confirm | Right trigger |
| Game pause / back | B, or left menu button without both grips |
| VR menu, including during cutscenes | Hold both grips and press left menu |
| Select a menu item | Left stick up / down |
| Change a value | Left stick left / right |

The trigger also switches difficulty and camera modes. `CUTSCENE CAMERA` applies
immediately: `HARRY 1ST PERSON` follows Harry, while `THEATRICAL` uses the staged
camera and lets you look around.

## Defaults and limitations

Release defaults: Render Scale **100%**, SSR **30**, **RELAXED** difficulty and
**HARRY 1ST PERSON** cutscenes. Settings persist, and updates preserve existing
choices. Select **ORIGINAL** for the original lesson difficulty.

SSR is experimental: missing reflection areas and a strip between a bean and its
reflection can still occur. Higher render scale and reflections increase GPU load.
Unavailable debugger readings display `N/A`; per-eye timings are not overall
CPU/GPU utilization percentages.

Quidditch, the complete original options menu and content after the first lesson
are not included. Original PC saves cannot be imported.

<!-- player-readme-end -->

## Development

- [Source layout and build instructions](SOURCE-KIT-README.md)
- [Architecture](docs/architecture.md)
- [Wand and gestures](docs/wand-gesture-contract.md)

This project is not affiliated with Warner Bros. or the Harry Potter rights holders.
News and feedback: [Discord](https://discord.com/channels/747967102895390741/1547254536203407390).
