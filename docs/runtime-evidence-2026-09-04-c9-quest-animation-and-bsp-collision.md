# Gate C9: animated NPC population and BSP capsule locomotion

## Outcome and claim boundary

C9 advances the native Quest 3 standalone slice from a static populated
Hogwarts viewer to an animated, spatially constrained gameplay sandbox. It
loads no proprietary content from the APK: map, character meshes, textures, and
animation tracks are decoded read-only from the user's imported owned data.

The ARM64 library and signed APK were built, audited, and installed without an
agent launch. The user subsequently confirmed in the headset that NPC
animations and BSP collision work. NPC-vs-player collision was still absent and
is addressed by C10.

This is not yet the retail game loop. C9 does not execute UnrealScript,
dialogue, quests, navigation AI, level transitions, save data, damage, audio,
or original cutscene state.

## Package-native NPC animation

- The same 28-character manifest and 15 deduplicated skeletal meshes from C8
  remain active; Harry actor reference `603` stays excluded for first-person
  play.
- Every distinct mesh now requests 16 bounded samples from its automatically
  selected package-native idle sequence rather than one pose.
- Sampling is normalized over the source sequence's reported duration. Runtime
  playback follows the accepted PCVR policy: one calm 16-frame loop at 10
  frames per second.
- Character vertices are laid out frame-major in one pre-uploaded host-visible,
  coherent Vulkan vertex buffer. Runtime changes only the first-vertex offset;
  it performs no per-eye upload and no per-frame CPU skinning.
- Each staged actor is independently grounded for every animation frame using
  that frame's visible lower bound and the C8D BSP floor query. This prevents
  root motion or varying model origins from reintroducing foot drift.
- Flipendo target AABBs now contain the union of every rendered animation frame,
  retaining the 10 cm aim margin while avoiding frame-dependent misses.
- Existing reactions remain model-matrix translations, so animation and the
  finite push/lift response compose without rewriting the shared vertex data.

## Player BSP collision

C9 ports the previously accepted PCVR capsule policy into the native Quest
locomotion path:

~~~text
eye height              = 0.815 m  (40.75 UU)
capsule radius          = 0.300 m  (15 UU ratio)
capsule half-height     = 0.840 m  (42 UU ratio)
maximum movement step   = 0.080 m
maximum step up         = 0.320 m
maximum step down       = 0.450 m
walkable normal |Y|     >= 0.64
contact epsilon         = 0.006 m
~~~

The scene precomputes normalized normals and AABBs for every solid,
non-degenerate BSP triangle. Each active stick move:

1. derives Harry's capsule center from tracked horizontal head displacement,
   locomotion translation, yaw, and authored eye height;
2. divides the requested head-relative displacement into bounded substeps;
3. follows the highest walkable floor inside the step-up/down band;
4. rejects a candidate whose three capsule-axis samples overlap wall
   triangles; and
5. retries blocked diagonal motion along X and Z independently for wall
   sliding.

Physical head movement and snap-turn pivot preservation are not passed through
the stick collision solver. The resolver reports blocked and grounded substeps
plus accumulated vertical correction in the 300-frame runtime telemetry.

## Verification

The existing six-host-test suite passes. The locomotion regression now also
proves that a resolver receives the capsule center at authored body height, can
shorten horizontal motion, can apply a stair-height correction, and contributes
blocked/grounded/vertical telemetry.

~~~text
host tests = 6/6 PASS
ARM64 NDK build = PASS
APK audit = PASS
install result = Success
installed lastUpdateTime = 2026-09-04 18:22:33
launch by agent = not performed
path = android/app/build/outputs/apk/debug/app-debug.apk
sha256 = 09BBB0280C1006E66809A1B15AA7423C2C9CAD7F685C12CE8236C53B145011D4
~~~

The strengthened APK audit requires the `C9` marker, opaque-black loading,
source animation-duration and frame-count diagnostics, collision telemetry,
capsule diagnostics, the package-native skeletal animation symbol, and all
previous OpenXR/Vulkan/wand/gesture/target paths. It continues to reject
`RED_BLUE_CLEAR` and proprietary Unreal packages.
