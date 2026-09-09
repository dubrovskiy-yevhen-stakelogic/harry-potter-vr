# Gate C10: authored world runtime foundation

## Outcome and claim boundary

C10 is one integrated Quest 3 standalone block spanning NPC collision,
level actor/event metadata, authored lighting, and original-game PCM audio.
The APK still contains no proprietary Unreal packages. All map, actor, light,
mesh, texture, animation, and sound data are decoded read-only at runtime from
the user's imported owned copy.

The ARM64 library, signed APK, and installed package are verified. The agent did
not launch the application. Visual lighting balance, audible output, trigger
coverage, and NPC collision feel therefore remain pending the user's headset
run. C10 does not claim a complete UnrealScript VM, dialogue/cutscene playback,
AI navigation, movers, objectives, save state, or level transitions.

The user confirmed the preceding C9 build had animated NPCs and working BSP
collision. That runtime acceptance supersedes C9's earlier pending note; the
reported missing NPC collision is implemented in C10.

The subsequent C10 headset run confirmed that NPC collision works and that the
fire ambience plus accepted-spell sound are audible. The user also reported
three rejected outcomes: authored lighting was not visually distinguishable,
the wand had no sound while tracing, and accepted Flipendo had no visible
projectile/impact. Those observations are the input boundary for C11 and are
not retroactively claimed as working here.

## NPC collision

- Every one of the 28 rendered non-player characters receives a horizontal
  collision capsule derived from the union of its complete 16-frame animated
  bounds and clamped to a human-sized `0.20..0.42 m` radius.
- The existing player capsule is tested after BSP floor/wall resolution.
- Diagonal movement retries one horizontal axis for sliding. If the player is
  already overlapping a staged actor, outward movement is allowed so the
  player cannot be trapped at load.
- The collider follows the same finite Flipendo reaction offset as the visual
  model, preventing invisible stale blockers.

## Actor and event graph

The bounds-checked UE1 property decoder now retains these instance fields:

- `Tag`, `Event`, and `InitialState`;
- `AmbientSound`, `SoundVolume`, `SoundRadius`, and `SoundPitch`;
- `CollisionRadius`, `CollisionHeight`, `bCollideActors`, `bBlockActors`, and
  `bBlockPlayers`;
- `LightBrightness`, `LightHue`, `LightSaturation`, `LightRadius`, `LightType`,
  and `LightEffect`.

The read-only world probe against `Lev_Tut1.unr` reports:

~~~text
actors=2011
classes=71
unique_tags=124
event_actors=34
resolved_event_edges=38
runtime_trigger_volumes=28
active_authored_lights=431
ambient_sound_properties=15
~~~

The runtime transforms the 28 usable trigger volumes into PlayerStart-local
OpenXR coordinates and emits deterministic `ENTER`/`EXIT` events with actor,
tag, and event identity. This is the first executable bridge from original
level scripting metadata; it deliberately does not pretend to execute the
original UnrealScript bytecode yet.

## Authored lighting

The 431 enabled `Engine.Light` actors with valid position, brightness, and
radius are converted from original UE1 hue/saturation/brightness semantics.
A bounded inverse-distance contribution is baked into a packed RGB vertex
attribute for BSP geometry. Characters receive an actor-local sample. The
Vulkan vertex/fragment interface multiplies the sRGB texture sample by this
authored light value; the old uniformly full-bright scene path is gone.

This is an authored-light approximation, not decoding of the original UE1
surface lightmap bit arrays. It restores spatial color and brightness while a
future lightmap pass can improve per-surface fidelity.

## Original audio

The clean-room sound loader enumerates direct `Engine.Sound` exports, validates
bounded RIFF/WAVE chunks, and decodes shipped PCM8/PCM16 mono/stereo into
caller-owned `int16` memory. It never exports sounds to disk.

Quest audio uses an AAudio 48 kHz stereo callback mixer. C10 loads:

- `Ambient.uax:s_fire_loop` as a looping hall/faceted-torch ambience;
- `Magic_sfx.uax:s_spell_throw1` on accepted Flipendo cast;
- `Magic_sfx.uax:s_spell_hit1` on a target hit.

The map's exact AmbientSound references are resolved through the package graph.
They currently lead to `HPSounds.u`/`Hub1_amb` streaming wrapper objects whose
payload is not a standalone RIFF, so C10 logs `STREAM_WRAPPERS_DEFERRED` and
uses the real `Ambient.uax` fallback above. Full Hub1 streaming decode remains
outside this gate and is not falsely counted as supported.

## Verification and installation

~~~text
CTest = 5/5 PASS
world probe = PASS
actors/classes/tags/events/edges = 2011/71/124/34/38
triggers/lights = 28/431
Ambient.uax fallback PCM = 16000 Hz, 49321 samples
ARM64 NDK r27c compile/link = PASS
ELF NEEDED libaaudio.so = YES
APK signature = v3 VALID
APK audit = PASS
proprietary assets in APK = 0
install result = Success
installed lastUpdateTime = 2026-09-04 19:14:49
launch by agent = not performed
path = android/app/build/outputs/apk/debug/app-debug.apk
sha256 = 0AF6B6D1C51BAF8D3518E355739E42E34E11F521A53DB037C3C3AF43FFB77072
installed APK SHA256 match = YES
~~~

The regular Gradle pipeline again encountered the known Windows child-process
access failure while handling generated `R.jar`/CMake diagnostic `.bat` files.
This was before native compilation and was not a C++ or shader failure. The
accepted bounded fallback directly built `libhpvr_quest.so` with the pinned
NDK and cached OpenXR Prefab inputs, pulled the installed valid C9 container,
replaced only `lib/arm64-v8a/libhpvr_quest.so`, stored native/resources entries,
zip-aligned, v3-signed, audited, installed, and hash-compared the result.
