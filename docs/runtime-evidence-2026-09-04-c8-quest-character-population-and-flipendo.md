# Gate C8: standalone character population and Flipendo target reaction

## Scope and claim boundary

Gate C8 replaces the empty standalone tutorial castle with character geometry
loaded directly and read-only from the user's external HP1 packages. It also
connects the already accepted press-locked Flipendo event to nearest-character
selection and a short visible push/lift reaction. The user confirmed that the
asynchronous C8 candidate reaches Hogwarts and renders the NPC population. C8B
removed the red/blue loader and added the old shared floor correction; its
headset run showed that the correction pushed NPC feet slightly into the floor.
The C8C headset run proved that aligning those model bounds with
PlayerStart-local zero made the actors float because zero is not the BSP floor.
The follow-up C8D correction was built, audited, installed, and then accepted
by the user in the headset: the staged NPC placement now works.

This is still a clean-room vertical slice. It does not yet execute retail
UnrealScript AI, dialogue, health, navigation, or animation state machines.

## Implemented behavior

- The Level character manifest excludes actor reference `603` (Harry), because
  the standalone view is first person.
- All other loadable `Lev_Tut1` characters are resolved from the external data
  root. Meshes and 256x256 material layers are deduplicated and appended to the
  existing Vulkan texture array; no game package is embedded in the APK.
- One package-native idle frame is sampled per unique skeletal mesh, avoiding
  the raw bind-pose/T-pose presentation.
- Dumbledore, McGonagall, Quirrell, Hermione, Ron, Fred, and George are staged
  once near PlayerStart. Each actor's current visible idle-frame minimum is
  measured after DrawScale. The highest walkable solid BSP triangle in the
  authored capsule-foot search band is measured beneath that actor's XZ
  position, and the visible minimum is placed on that height. Remaining actors
  retain their authored Level placements.
- Until the asynchronously decoded scene is GPU-ready, both eyes clear to
  opaque black. No red/blue loading fallback remains in the active renderer.
- Target AABBs are built from the rendered idle-frame vertices with a 10 cm
  aiming margin. The nearest target up to 18 m is selected using the origin and
  direction frozen on the initial trigger press, not the release pose.
- An accepted hit applies a reversible 1.2 second, 0.65 m push plus 0.24 m lift.
  Duplicate or reordered spell serials are rejected.

## Verification

The current read-only manifest probe against the golden installation reported:

~~~text
character_manifest_status=ok
inspected=2011
selected=28
excluded=1
missing_mesh=0
hidden=0
~~~

All 6/6 host tests passed, including the new nearest-target, miss,
exactly-once, and finite-reaction regressions. Android NDK r27c compiled and
linked `libhpvr_quest.so` for arm64-v8a with warnings treated as errors.

The normal Gradle Java packaging path hit the already documented Windows D8
cache/output access race. The accepted native-only fallback rebuilt the ARM64
library directly, pulled the still-installed and hash-matching C7 APK as the
valid container, replaced only `libhpvr_quest.so`, stored native/resources
entries uncompressed, zip-aligned, and debug-signed the result.

~~~text
path   = android/app/build/outputs/apk/debug/app-debug.apk
sha256 = 4C029607E6BF2FBDE235FBF365FA2580F16F0632A29D9247681C08F901CCD6C7
host tests = 6/6 PASS
ARM64 NDK build = PASS
APK audit = PASS
install result = Success
installed lastUpdateTime = 2026-09-04 18:00:57
launch by agent = not performed
~~~

The strengthened APK audit requires the linked character-manifest, textured
skeletal-mesh, skeletal-animation, and spell-target paths, in addition to the
existing OpenXR/Vulkan/wand/gesture symbols. The user's C8 run confirmed NPC
rendering. The user subsequently accepted the corrected C8D staged height.

## Startup watchdog correction

The first installed C8 candidate entered the native host and completed the
PlayerStart probe, but did not create an XR session before the platform's
roughly ten-second activity timeout. Device evidence showed no native crash:

~~~text
16:15:12.890 gate=C8
16:15:12.896 player_start=READY
16:15:22.611 Force finishing activity io.github.hpvr.quest/android.app.NativeActivity
~~~

The cause was synchronous map plus character decoding before the Android event
loop. The corrected candidate queues CPU-only owned-data decoding on a worker,
immediately enters lifecycle/OpenXR processing, renders a stereo fallback while
loading, and adopts/uploads Hogwarts on the main render thread once the future
is complete. Shutdown keeps the future owned until completion, so the scene
state cannot outlive the runtime. The user confirmed that this version reaches
Hogwarts and displays NPCs.

## C8B headset-feedback correction

That accepted startup run exposed two presentation errors: the asynchronous
fallback was still red/blue, and the provisional seven-character group floated
because the standalone staging path omitted the PCVR floor correction. C8B
replaces the per-eye diagnostic colors with opaque black and restores exactly
`42 UU * 0.02 m/UU = 0.84 m` of downward stage displacement. The gate marker is
`C8B`, making fresh device logs distinguishable from the earlier package. The
user's headset run then showed that this shared correction placed the feet
slightly below the floor.

## C8C per-model grounding correction

C8C removes the shared `0.84 m` displacement. Before placing each staged actor,
the loader measures the minimum vertical coordinate referenced by the rendered
idle-frame triangles after applying that actor's DrawScale. It subtracts that
individual value from the stage origin, making the rendered lower bound land at
local height zero. Yaw does not alter the vertical component. The headset run
showed that the actors floated, proving that the actual BSP floor is below local
zero.

## C8D BSP-floor correction

C8D keeps the per-model visible lower bound and replaces the assumed floor with
a direct query over the loaded map triangles. It rejects `NotSolid` and
degenerate faces, accepts only walkable normals, evaluates triangle height at
each staged XZ position, and selects the highest surface inside the same
`-0.45/+0.32 m` capsule-foot search band used by the accepted PCVR collision
path. If a local staged position has no candidate, it falls back to the measured
BSP floor below PlayerStart rather than an arbitrary constant. The gate marker
and APK audit now require `C8D` while retaining the opaque-black loading marker
and rejecting `RED_BLUE_CLEAR`. The hash and install time above refer to C8D;
the agent did not launch it, and the user later accepted its grounding.
