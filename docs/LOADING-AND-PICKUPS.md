# Scene preparation and loading

The Windows installer prepares both supported maps before transferring data to
Quest. This moves expensive geometry, texture and animation preparation off the
headset. First-time installation still takes time; headset loading still includes
file reads, validation, GPU uploads and pipeline creation.

## Prepared scenes

`hpvr_quest_prepare_assets.exe` uses the same CPU preparation code as the runtime.
It reads a user-owned PC installation and writes only to a separate output
directory. Prepared data includes BSP geometry, lightmaps, animation frames,
props, movers, pickups, collision and target bounds.

The installer independently verifies the generated files, includes their SHA-256
hashes in its transfer manifest and copies them to `HP/Cache/Scenes/`.
Audio is prepared separately in `HP/Cache/Audio/`. Scripts, saves, playback state
and player settings remain runtime state.

The scene envelope checks schema, cook revision, map identity, source-content
fingerprint, payload length and checksum. Decode happens into an isolated
candidate; draw ranges, dimensions and references are checked before adoption.
Source fingerprints track package contents rather than installation paths or
timestamps. Compatible older caches receive supported corrections during loading.

Missing or rejected scene caches fall back to runtime preparation.
`-SkipScenePreparation` explicitly selects this slower fallback for troubleshooting.
Use the tools supplied with the matching release; do not mix caches or helper
executables from unrelated builds.

## Runtime behavior

Loading keeps the Warner artwork and an animated hourglass visible during
asynchronous preparation. Synchronous GPU setup may temporarily hold the last
submitted loading frame. Failed preparation returns to the frontend without
adopting partial scene or audio state.

Package reads and shared import/light indexes are reused during preparation.
A bounded frontend reserve avoids copying the large world buffers when menu
geometry is appended. Cache validation and repair run during loading, not every
rendered frame.

Scene journals separate preparation, audio and GPU stages for troubleshooting.
Their timings describe the machine and run that produced them; they are not
universal loading-time guarantees.

## Pickups

Wizard cards use a growing, spinning pickup effect. Beans have their own pickup
sound and HUD-bound effect; challenge stars use the star sound. Pot rewards are
emitted from the animated opening, collide with nearby geometry and settle.
Save books animate at their authored locations, save on touch and disappear
after use.

Generated scene caches, extracted audio and asset dumps contain game data.
They must not be committed to source or included in public APKs or release ZIPs.
