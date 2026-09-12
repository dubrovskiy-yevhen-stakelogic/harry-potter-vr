# Casting and challenge interactions

## Casting modes

**CLASSIC** is the default. Hold the right trigger to sweep the colored aim
reticle, then release. Eligible targets display the Flipendo symbol and select
that spell automatically; other surfaces receive the basic air cast.

**VISIBLE GESTURE** uses the same aiming step. Acquiring an eligible target locks
it and starts the stroke; draw a Flipendo curl and release. The stroke is visible.

**GESTURE** behaves the same way but hides the stroke. Neither gesture mode locks
empty space on trigger-down. Gameplay matching accepts rotation, reversed drawing
direction and varied proportions while checking the overall curl structure.
The classroom retains its guided exercise and separate Original/Relaxed policies.

**VOICE CASTING** is an independent, optional setting for the challenge. Hold the
trigger on an eligible target and say “Flipendo”. Keep holding to cast again once
the projectile and Harry's incantation finish; release to acquire another target.
Microphone permission is required. **VOICE HINTS** can be enabled separately and
are off by default; hidden hints do not disable recognition.

Voice recognition runs entirely on the headset. Public builds do not save or
upload microphone audio. It is experimental and has only been tested by the
author: pronunciations can be missed and near-sounding phrases can trigger while
aiming. See [voice behavior and limitations](../tools/voice/README.md).

## Effects and interactions

Flipendo uses the owned projectile and hit textures with bounded batched particles.
The aim symbol is placed in front of target geometry. Bean collection has its own
sound and pickup effect; stars use their original star sound.

Pots play their tipping frames before rewards emerge. Vases retain a broken mesh.
Rewards leave the opening, collide with surrounding geometry and remain
collectible. Stable item IDs prevent a saved reward from being granted twice.

Save books bob at their authored positions, save on touch and disappear after
use. Gnomes retain their hit and seated animations rather than disappearing.
Candle flames/glows, animated fireplaces and the abyss effects use the world
effects clock, which continues while the floating VR menu is open.

## Movement, saves and settings

Ledge grabs transition into an automatic mantle. Moving platforms preserve
walking and turning. Depleted health and lethal pits use a timed death/fade
transition before returning to the challenge entrance or the last save book.
Ordinary landings do not restart the level.

Challenge menu saves retain checkpoint recovery instead of a rolling position
near a fall. Older saves remain readable; if their exact historical checkpoint
state is unavailable, recovery uses a recorded book or the level entrance.

**LEVEL SELECT** starts either supported map fresh in the selected save slot.
Replacing that slot requires confirmation. Failed loading leaves saves intact.

**REFRESH RATE** offers runtime default or supported 72/80/90/120 Hz modes.
Changes are requested live through OpenXR. Casting, voice hints, camera,
difficulty and graphics preferences are saved in checksummed settings banks.

Click **L3 + R3** together, then release, to recenter height at the current body
position. Recenter does not move the player to a different place in the level.
