# Harry Potter VR

An original, data-driven VR port project for the 2001 PC release of *Harry
Potter and the Sorcerer's Stone* (the US edition of *Philosopher's Stone*).
The goal is to preserve the original game
while replacing its presentation and interaction model with native VR:

- independent head and wand aiming;
- a physically held wand;
- spell gestures rather than button-only casting;
- two-hand interactions, spatial UI, and VR comfort adaptations;
- Meta Quest 3 standalone as the final platform.

No proprietary game data belongs in this repository. A user must provide a
legally owned PC installation.

## Confirmed starting point

- Retail data root: `C:\Program Files\HP` (read-only)
- User state: `C:\Users\user\Documents\Harry Potter` (read-only)
- Game: *Harry Potter and the Sorcerer's Stone* for Windows (US release)
- Engine: Unreal Engine build `433`, compiled 2001-10-28
- Executable: `system\HP.exe`, PE32 x86
- Original renderer in the current baseline: `D3DDrv.D3DRenderDevice`
- Current PC launch evidence: the game reached `Startup.unr`, opened a viewport,
  initialized audio, and exited cleanly on 2026-09-02.

The exact hashes and evidence are recorded in
[`docs/baseline-inventory.md`](docs/baseline-inventory.md).

## Current phase

The workspace began empty. The working direction is now a native,
data-compatible reimplementation rather than trying to carry the retail x86
executable onto Quest. OpenHP1 is the leading foundation candidate, but it is
being held outside the tracked tree until its missing license texts and source
provenance are resolved. No OpenHP1 code or third-party source tree has been
vendored; the probe's version-pinned `openxr` dependency is resolved by
Cargo.

The first product slice is one original spell lesson in VR: headset-driven
stereo, a separately tracked wand, the original lesson template and scoring,
and an original spell interaction. See
[`docs/runtime-foundation-decision.md`](docs/runtime-foundation-decision.md) and
[`docs/wand-gesture-contract.md`](docs/wand-gesture-contract.md).

Run the read-only baseline verifier from PowerShell:

```powershell
.\tools\verify-baseline.ps1 -StrictHashes
```

## Wand trajectory core

The first original code is a platform-neutral C++20 module under `src/wand`.
It accepts timestamped, validity-qualified wand-tip poses, validates the lesson
plane, preserves HP's normalized axis orientation, rejects invalid timing and
tracking loss by cancelling the entire stroke, and resamples onto a canonical
time grid before limiting submission to the original 500-point lesson capacity.
The first and last point are always preserved.

Build and test it on Windows with:

```powershell
cmake -S . -B build\wand -G "Visual Studio 17 2022" -A x64
cmake --build build\wand --config Debug --parallel
ctest --test-dir build\wand -C Debug --output-on-failure
```

Compile the same library for Android ARM64 with the already installed NDK:

```powershell
cmake -S . -B build\wand-android -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=C:\Dev\android-toolchain\sdk\ndk\27.2.12479018\build\cmake\android.toolchain.cmake `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DBUILD_TESTING=OFF
cmake --build build\wand-android --parallel
```

The module also exposes a versioned, exception-contained C ABI with caller-owned
POD buffers so the same implementation can be called by the Rust PC probe and a
future Android host. The 2026-09-02 Debug build passed all tests, and NDK r27c
produced the ARM64 static library. Those results prove only the pure
coordinate/trajectory layer; it is not runtime evidence of stereo rendering,
controller tracking, gesture acceptance, or Quest behavior.

Gate A5b extends that module with a narrow UE1 package `76/0` reader for an
external `FlipPattern`/lesson profile and a clean-room implementation of the
shipped coverage score. The parser stores no game data in the repository and
checks qualified object identities rather than terminal names.

## OpenXR runtime capability probe

The default `hpvr-xr-runtime-probe` binary in `tools/xr-runtime-probe` is an
independent capability probe. It enumerates the active runtime and relevant
extensions, creates an OpenXR instance, and—when a headset is available—queries
the Vulkan requirements and stereo view configuration. That default binary does
not create a session or launch the game. The separate `stereo_clear` and
`wgpu_stereo_clear` diagnostics below create finite OpenXR sessions, but still
do not launch the game.

Build it with:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe check --locked
Pop-Location
```

On Windows, either place the official Khronos `openxr_loader.dll` beside
the probe executable, expose it through `PATH`, or pass it explicitly:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe run --quiet --locked -- `
  --loader C:\path\to\openxr_loader.dll
Pop-Location
```

The connected Quest Link evidence now includes a real legacy-Vulkan OpenXR
session. `stereo_clear` created one two-layer `2064x2272` swapchain,
submitted 450 frames, and exited through `STOPPING` without an XR/Vulkan
error. The user confirmed that the left eye was red and the right eye blue.
After lifecycle hardening, the final source was rebuilt and completed a second
60-frame smoke run with the same clean state sequence and exit code 0. Visual
acceptance remains attached to the 450-frame run.

Run the same finite diagnostic session with:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe run --quiet --locked `
  --bin stereo_clear -- `
  --loader C:\path\to\openxr_loader.dll --frames 450
Pop-Location
```

The next diagnostic uses the OpenHP1-aligned, exactly pinned `wgpu 29.0.4`
stack. It shares OpenXR's Vulkan instance, physical device, logical device, and
queue; wraps every runtime-owned swapchain image non-owningly; and clears the
two array layers directly without an intermediate color texture or copy:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe run --quiet --locked `
  --bin wgpu_stereo_clear -- `
  --loader C:\path\to\openxr_loader.dll --frames 300
Pop-Location
```

On Quest Link, that probe completed 300 direct-wgpu frames across two session
starts, including a real `STOPPING -> IDLE -> READY` recovery. All three
runtime images were imported by identical `VkImage` handle, all
acquire/wait/submit/GPU-complete/release counts balanced, and the user confirmed
green in the left eye and pink-purple in the right eye. Non-owning wrapper drop
callbacks completed `3/3`; wgpu reported zero uncaptured errors and zero device
losses. The PC has no `VK_LAYER_KHRONOS_validation`, so this is a functional
direct-import pass, not yet a Vulkan-validation-clean pass.

Gate A3 adds an opt-in procedural 3D calibration room while preserving that
clear probe as the default. It applies each runtime eye pose and asymmetric FOV,
uses a two-layer depth target, samples the live head `VIEW` pose against the
same `LOCAL` world, and advances animation once per begun XR frame:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe run --quiet --locked `
  --bin wgpu_stereo_clear -- `
  --loader C:\path\to\openxr_loader.dll --frames 1800 --geometry
Pop-Location
```

The first Quest Link run rendered 1800 depth-tested stereo frames with balanced
resource counters, distinct eye matrices, plausible `66.79 mm` IPD, one shared
simulation snapshot per frame, zero wgpu errors, and zero device loss. Runtime
and projection invariants passed. The user confirmed that the geometry fused
into one image. World-lock/orientation/parallax acceptance remains open, and
motion coverage is formally inconclusive because pending `LOCAL` and `STAGE`
reference-space-change events occurred during the capture.

Gate A4 adds the opt-in right Touch Plus wand, aim ray, analog trigger, and a
raw visual stroke without changing the default/A3 path:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe run --quiet --locked `
  --bin wgpu_stereo_clear -- `
  --loader C:\path\to\openxr_loader.dll --frames 9000 --wand
Pop-Location
```

The accepted Quest Link run completed 9000 rendered frames, 17 balanced
press/release strokes, per-eye dynamic draws, and all renderer/lifecycle
invariants. The user confirmed that the live wand interaction worked. Exact
scope and evidence are in
[`docs/runtime-evidence-2026-09-02-tracked-wand.md`](docs/runtime-evidence-2026-09-02-tracked-wand.md).

Gate A5a is a separately compiled diagnostic that feeds the timestamped raw 3D
stroke through the platform-neutral C++ trajectory projection:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe run --quiet --locked `
  --features gesture-projection --bin wgpu_stereo_clear -- `
  --loader C:\path\to\openxr_loader.dll --frames 4500 --gesture
Pop-Location
```

Orange means recording. Green means only
`TRAJECTORY_PROJECTED_NOT_SPELL`; red means trajectory rejection, and amber
means a canceled capture. This gate deliberately does not load an authored
spell template, compute HP1's score, or dispatch a spell. The default build
remains independent of the C++ bridge; test both configurations with:

```powershell
& C:\Users\user\.cargo\bin\cargo.exe test --locked --all-targets `
  --manifest-path .\tools\xr-runtime-probe\Cargo.toml
& C:\Users\user\.cargo\bin\cargo.exe test --locked --all-targets `
  --manifest-path .\tools\xr-runtime-probe\Cargo.toml `
  --features gesture-projection
```

Gate A5b uses the same feature build but adds the legally owned installation as
a read-only data source:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe run --quiet --locked `
  --features gesture-projection --bin wgpu_stereo_clear -- `
  --loader C:\path\to\openxr_loader.dll --frames 9000 `
  --flipendo-data-root 'C:\Program Files\HP'
Pop-Location
```

While the trigger is held, cyan is the external authored template and orange
is the physical wand trace. After release, green means the score met compiled
`PassMark[0]`; red means it did not, and amber means capture cancellation. The
accepted Link run scored seven attempts and produced two real acceptances
(`0.592798` and `0.802432` against `0.50`), which the user confirmed visually.
No spell is dispatched yet. Exact scope and evidence are in
[`docs/runtime-evidence-2026-09-02-flipendo-scoring.md`](docs/runtime-evidence-2026-09-02-flipendo-scoring.md).

Gate A6 keeps that read-only command line and adds the first deliberately small
gameplay adapter. A successful Flipendo release emits one portable
`SpellCastEvent`; an isolated fixed lesson target consumes it exactly once,
shows a short green beam, and moves a non-cubic crystal target through a
deterministic knockback arc. The live Link run accepted `0.847352` against
`0.50`, logged matching event/impulse serial `1`, and the user confirmed the
crystal, beam, and reaction in the headset. This is test-world dispatch only,
not integration with a retail actor. Exact scope and evidence are in
[`docs/runtime-evidence-2026-09-02-flipendo-test-target.md`](docs/runtime-evidence-2026-09-02-flipendo-test-target.md).

Gate B1 begins the independent game-data/runtime path. The clean-room package
census validates UE1 header and name/import/export tables without decoding or
extracting payloads. It supports only the package versions actually observed
in the owned installation, including the legacy v61 name encoding. All 242
installed UE1 package files passed; `Lev_Tut1.unr` exposes 3,617 exports across
75 classes and 30 direct package names. Exact evidence and remaining resolver
work are in
[`docs/runtime-evidence-2026-09-02-package-census.md`](docs/runtime-evidence-2026-09-02-package-census.md).

Gates B2-B4 extend that read-only path with deterministic package discovery,
cross-package import linking, and the first selected native payload decode.
The Level probe retains the package-local identities of the top-level
`Engine.Level`, its ordered actor slots (including null holes), and its
`Engine.Model` world reference without constructing objects or decoding
scripts or geometry:

```powershell
& .\build\windows\src\wand\Release\hpvr_hp1_level_probe.exe `
  'C:\Program Files\HP\Maps\Lev_Tut1.unr'
```

All 41 installed maps passed the Level framing and reference checks. Exact
scope and evidence are in
[`docs/runtime-evidence-2026-09-02-package-graph.md`](docs/runtime-evidence-2026-09-02-package-graph.md),
[`docs/runtime-evidence-2026-09-03-package-linker.md`](docs/runtime-evidence-2026-09-03-package-linker.md),
and
[`docs/runtime-evidence-2026-09-03-level-handles.md`](docs/runtime-evidence-2026-09-03-level-handles.md).

Gates B5-B7 validate the selected `Engine.Model`, its BSP topology, and a
finite CCW triangle stream while keeping world scale explicit. Gate B8 feeds a
bounded slice of that stream through a checked C ABI into the existing PC
OpenXR/wgpu stereo renderer. Gate B8.1 makes the triangle
selection independent of the preview scale and can opt into a direct,
serialized `Engine.PlayerStart` transform. The map mode requires all three
inputs and is unavailable from the default Rust build:

```text
--hp1-map-slice <map-package> <meters-per-unreal-unit> <max-triangles>
[--hp1-player-start <zero-based-ordinal>]
```

Without the second option, the first visual mode retains its logged
whole-AABB diagnostic centering. With it, the selected PlayerStart is moved to
the OpenXR `LOCAL` origin and its serialized yaw is applied. This remains a
preview placement: it does not infer eye height, collision, actor semantics,
gameplay scale, or a gameplay camera. The first Quest
Link run displayed coherent colored `Lev_Tut1` geometry, confirmed by the
user, while also proving that whole-AABB centering is outside/below useful
playable space. A subsequent clean Quest Link run placed the user inside the
recognizable Hogwarts geometry with the expected facing, validating the direct
PlayerStart placement. The user also reported that the `0.01` preview looked
like a dollhouse. A second clean run at `0.02` looked possibly slightly too
large, so `0.02` remains provisional until textured architecture and NPCs can
be judged together. Gate B9 now resolves all 88 tutorial-map material
references, decodes all 87 direct P8 textures, derives scale-invariant BSP UVs,
and builds an 88-layer in-memory GPU texture array; the sole dynamic
`FireTexture` uses an explicit diagnostic fallback. The textured renderer is
built but has not yet been launched or visually accepted. Exact scope and
evidence are in
[`docs/runtime-evidence-2026-09-03-model-census.md`](docs/runtime-evidence-2026-09-03-model-census.md),
[`docs/runtime-evidence-2026-09-03-bsp-topology.md`](docs/runtime-evidence-2026-09-03-bsp-topology.md),
[`docs/runtime-evidence-2026-09-03-bsp-triangle-stream.md`](docs/runtime-evidence-2026-09-03-bsp-triangle-stream.md),
[`docs/runtime-evidence-2026-09-03-bsp-render-bridge.md`](docs/runtime-evidence-2026-09-03-bsp-render-bridge.md),
[`docs/runtime-evidence-2026-09-03-player-start-placement.md`](docs/runtime-evidence-2026-09-03-player-start-placement.md),
and
[`docs/runtime-evidence-2026-09-03-bsp-material-textures.md`](docs/runtime-evidence-2026-09-03-bsp-material-textures.md).

Gates B10-B20 extend that vertical slice from architecture to characters,
navigation, and the first character-facing spell interaction. Direct
package-native `Engine.SkeletalMesh` decoding replaced the
PSK-only oracle, actor transforms place previews at their serialized Level
locations, and an automatic manifest now populates `Lev_Tut1` with 28
non-player characters across 15 deduplicated meshes and 39 texture layers.
The user accepted the textured `0.02` Hogwarts scale, the removal of the
head-relative black diagnostic surface, and the level-placed Dumbledore
preview. The B15 headset run accepted the wand, Flipendo, and locomotion but
found the authored pre-script population too remote to make the castle feel
occupied. A controller locomotion layer now adds head-relative left-stick
movement and right-stick 30-degree snap turning while transforming the eyes,
wand, gesture trail, aim ray, and spell events together. HP1's shipped
version-76 animation format is now independently decoded from package-global
compressed key arrays. A bounded sampler, verified hierarchy/weight skinning
path, versioned C ABI, and a 16-frame population atlas animate all 28 selected
actors through one shared wgpu vertex-buffer range. B16 applies Harry's exact
serialized `BaseEyeHeight` (40.75 UU, or 0.815 m at the accepted scale), adds
bind/frame AABB diagnostics, and offers an explicitly provisional seven-NPC
staging around `PlayerStart` until UnrealScript startup state is implemented.
The user accepted the visible seven-character B16 staging in Quest Link and
then accepted B17's Harry-sized capsule collision against the original solid
BSP triangles, including bounded stair ascent, floor following, wall blocking,
and sliding. B18 connects an accepted Flipendo release to the loaded character
population: the wand ray selects the nearest character bounds, solid BSP can
occlude the cast, and only the selected actor receives a short reversible
push/lift reaction. Green feedback reports a hit; orange reports a miss or
occlusion. The first B18 headset test rejected the interaction because the
procedural prop followed grip orientation while the ray followed aim
orientation, and because the target ray was sampled only after the gesture.
B19 makes the runtime aim pose authoritative for both the visible prop and ray,
starts the ray exactly at the visible tip, and freezes the target ray on the
initial trigger press. It also replaces the procedural tapered rod with the
real `HPBase.WandMesh` geometry loaded directly and read-only from the owned
`HPBase.u`; its currently unsupported non-P8/procedural material is represented
by a bounded brown vertex colour rather than falsely reported as decoded.
This remains a clean-room vertical-slice reaction rather than retail AI,
health, script, or animation-state integration. The ready Release build is
self-launched by `RUN-LATEST-VR.cmd`; B19 has passed offline validation but
still needs headset acceptance. Exact scope and evidence are in
[`docs/runtime-evidence-2026-09-03-direct-skeletal-mesh.md`](docs/runtime-evidence-2026-09-03-direct-skeletal-mesh.md),
[`docs/runtime-evidence-2026-09-03-level-actor-placement.md`](docs/runtime-evidence-2026-09-03-level-actor-placement.md),
[`docs/runtime-evidence-2026-09-03-character-population.md`](docs/runtime-evidence-2026-09-03-character-population.md),
[`docs/runtime-evidence-2026-09-03-locomotion-and-animation-boundary.md`](docs/runtime-evidence-2026-09-03-locomotion-and-animation-boundary.md),
[`docs/runtime-evidence-2026-09-03-hp1-skeletal-animation.md`](docs/runtime-evidence-2026-09-03-hp1-skeletal-animation.md),
[`docs/runtime-evidence-2026-09-03-b16-player-height-and-npc-staging.md`](docs/runtime-evidence-2026-09-03-b16-player-height-and-npc-staging.md),
and
[`docs/runtime-evidence-2026-09-03-b17-bsp-capsule-collision.md`](docs/runtime-evidence-2026-09-03-b17-bsp-capsule-collision.md),
and
[`docs/runtime-evidence-2026-09-03-b18-npc-flipendo-interaction.md`](docs/runtime-evidence-2026-09-03-b18-npc-flipendo-interaction.md),
and
[`docs/runtime-evidence-2026-09-03-b19-press-lock-and-wandmesh.md`](docs/runtime-evidence-2026-09-03-b19-press-lock-and-wandmesh.md).

The user then accepted B19's corrected alignment, press-time target lock, and
owned-package WandMesh in the headset. B20 removes the cyan aim guide from the
initial press through the release frame because the target is already frozen
and the moving guide interfered with drawing. The launcher also enables an
explicit test-only assist which expands the shipped normalized spatial
accuracy radius from `0.03` to `0.0525` (1.75x) while leaving the authored
pass threshold and profile data unchanged. Removing
`--flipendo-test-assist` restores canonical scoring. Evidence and exact
boundaries are in
[`docs/runtime-evidence-2026-09-03-b20-gesture-focus-and-test-assist.md`](docs/runtime-evidence-2026-09-03-b20-gesture-focus-and-test-assist.md).

Gates C0-C1 now start the native Quest path without disturbing the accepted
PCVR slice. The `android` project produces an ARM64-only NativeActivity APK,
packages the official Khronos OpenXR Android loader, and links the same
portable wand and HP1 gesture C ABIs used by the PC probe. Its native Vulkan
path creates an OpenXR session and two-layer swapchain, follows the complete
frame acquire/submit/release sequence, and clears the left eye red and right
eye blue. A platform-neutral lifecycle state machine drives pause/resume and
window-loss session recreation. Game packages remain external: the host only
reports the app-specific `HP` data root and never bundles proprietary files.

Build and audit the package without installing it:

```powershell
.\BUILD-QUEST-DEBUG.ps1
.\VERIFY-QUEST-APK.ps1
```

On 2026-09-04, a later C2-compatible package retaining this C1 renderer was
installed and launched on Quest 3. The user confirmed red in the left eye and
blue in the right eye. That is visual stereo/order acceptance; pause/resume
recreation still needs its own current runtime capture. Exact evidence and
claim boundaries are in
[`docs/runtime-evidence-2026-09-03-c0-quest-native-host.md`](docs/runtime-evidence-2026-09-03-c0-quest-native-host.md)
and
[`docs/runtime-evidence-2026-09-03-c1-quest-stereo-smoke-build.md`](docs/runtime-evidence-2026-09-03-c1-quest-stereo-smoke-build.md).

Gate C2 adds a dry-run-by-default `IMPORT-QUEST-DATA.ps1` for the legally owned
installation and links a real ARM64 `Lev_Tut1.unr` PlayerStart probe into the
host. The script excludes Windows binaries, saves, help/support, and logs;
actual adb transfer requires `-Copy`, never installs/launches the APK, and
verifies three copied sentinels by SHA-256. With external data present, the
host checks the map through the same portable parser used by PCVR while keeping
the stereo diagnostic available even when data is absent or rejected. See
[`docs/runtime-evidence-2026-09-03-c2-quest-owned-data-boundary.md`](docs/runtime-evidence-2026-09-03-c2-quest-owned-data-boundary.md).

Gate C3 is the first standalone Hogwarts renderer. It uses the same external,
read-only package path and accepted PlayerStart/`0.02` transform as PCVR,
uploads the roughly 60k tutorial-map BSP vertices plus an 88-layer 256x256 SRGB
texture array, and renders both OpenXR eyes through a depth-tested Vulkan
pipeline.
The first C3 ARM64 APK passed host/static checks, was installed on Quest 3, and
all 286 selected owned-data files were copied with three required sentinel
hashes matching. Its first launch proved external data and PlayerStart parsing,
but the full scene linker rejected the absent Windows `Core.dll` and displayed
the red/blue fallback. The corrected linker now carries a bounded registry of
the shipped UE1 native-module identities without copying any DLL, passes the
full `Lev_Tut1` texture build against an exact 286-file/no-DLL mirror, and makes
C3 fail closed instead of masking scene failure as a stereo success. The
corrected APK was then installed and logged scene/GPU readiness plus 600
submitted HOGWARTS frames; the user confirmed that Hogwarts was visible.
The device decoded 60,012 vertices while the exact host no-DLL mirror decoded
60,009, so that one-triangle determinism discrepancy remains open. See
[`docs/runtime-evidence-2026-09-04-c3-quest-textured-hogwarts-build.md`](docs/runtime-evidence-2026-09-04-c3-quest-textured-hogwarts-build.md).

Gate C4 adds the first standalone gameplay-control layer: right Touch grip
position plus runtime aim orientation drive the genuine external
HPBase.WandMesh, left thumbstick provides 1.8 m/s head-relative locomotion,
and right thumbstick provides latched 30-degree snap turns around the current
head position. The audited build was installed on Quest 3 and the user accepted
the visible wand, left-stick movement, and right-stick turning. See
[`docs/runtime-evidence-2026-09-04-c4-quest-wand-locomotion-build.md`](docs/runtime-evidence-2026-09-04-c4-quest-wand-locomotion-build.md).

Gate C5 adds the first standalone Flipendo gesture loop. It loads the genuine
`FlipPattern`, pass mark, accuracy, and draw timing from the user's external
`HPBase.u` and `Lev_Tut1.unr`; locks aim when the right trigger is pressed;
records the wand-tip stroke while held; and projects/scores it on release.
Temporary test assist widens the authored accuracy radius by 1.75x. Recording
and score feedback tint the wand instead of drawing a distracting aim ray. The
first C5 headset run recorded 10 attempts but accepted none after the player
had turned. The corrected candidate freezes an aim-facing plane and normalizes
free-space position/scale; its real-template regression and APK audit pass, but
it is not yet reinstalled or runtime-accepted. See
[`docs/runtime-evidence-2026-09-04-c5-quest-flipendo-build.md`](docs/runtime-evidence-2026-09-04-c5-quest-flipendo-build.md).

Gate C6 renders the previously missing gesture UX directly in the standalone
scene: a cyan ribbon made from the real external `FlipPattern`, an orange live
wand-tip trail, and a retained green/red result for 0.8 seconds. The aim ray
remains absent during drawing. The verified C6 APK is installed on Quest 3 but
is still behind Meta's controller-required dialog, so runtime visibility is not
yet claimed. See
[`docs/runtime-evidence-2026-09-04-c6-quest-gesture-guide-build.md`](docs/runtime-evidence-2026-09-04-c6-quest-gesture-guide-build.md).

The C6 headset run proved the real guide and scorer, including two score-1.0
acceptances, but exposed that the guide was always visible and the projected
trail was displaced from the wand. Gate C7 hides all gesture geometry while
idle, reveals the frozen template only on press, and builds the live ribbon
directly from actual wand-tip positions. The verified C7 APK is installed; per
the current workflow the user launches it manually. See
[`docs/runtime-evidence-2026-09-04-c7-quest-aligned-trail-build.md`](docs/runtime-evidence-2026-09-04-c7-quest-aligned-trail-build.md).

The user manually accepted C7. Gate C8 now fills the standalone tutorial scene
from the external `Lev_Tut1` character manifest: 28 non-player actors are
resolved from owned packages, Harry is excluded for first-person play, real
textured skeletal meshes use a sampled idle frame, and seven principal
characters are staged around PlayerStart. The accepted press-time Flipendo ray
selects the nearest character bounds and applies a finite push/lift reaction.
The user confirmed that the asynchronous C8 build reaches Hogwarts and renders
the NPC population; that run exposed a red/blue loading fallback and missing
grounding for the staged group. C8B made both loading eyes black and applied
the earlier PCVR `0.84 m` floor drop, but the headset run showed slight foot
penetration. C8C derived each actor's visible idle-frame lower bound, but its
headset run proved that PlayerStart-local zero is above the actual floor. C8D
combines the per-model lower bound with the real walkable BSP height beneath
each staged position. The user accepted C8D grounding in the headset. See
[`docs/runtime-evidence-2026-09-04-c8-quest-character-population-and-flipendo.md`](docs/runtime-evidence-2026-09-04-c8-quest-character-population-and-flipendo.md).

Gate C9 turns that accepted static population into a larger gameplay slice.
Each of the 15 unique owned skeletal meshes now supplies 16 normalized frames
of its package-native idle sequence. All 28 actors animate at 10 Hz from one
pre-uploaded frame-major Vulkan buffer, with per-frame BSP grounding and target
bounds spanning the complete loop. Left-stick motion is now resolved through
Harry's `15 UU` radius / `42 UU` half-height capsule against solid BSP:
8 cm substeps follow floors and stairs, reject walls, and slide along a free
axis. The black asynchronous loader, wand, press-locked Flipendo, and finite NPC
reaction remain intact. The agent did not launch C9; the user then confirmed
in the headset that NPC animation and BSP collision both work. See
[`docs/runtime-evidence-2026-09-04-c9-quest-animation-and-bsp-collision.md`](docs/runtime-evidence-2026-09-04-c9-quest-animation-and-bsp-collision.md).

Gate C10 is the first combined world-runtime layer rather than another isolated
render tweak. Animated NPCs now physically block Harry using model-derived
capsules that follow Flipendo reactions. The read-only Level property pass
extracts all 2011 actors, 71 classes, 124 tags, 34 event-bearing actors, 38
resolved tag/event edges, 28 usable trigger volumes, 431 active authored lights,
and 15 ambient-sound properties. Trigger enter/exit is live in the movement
path. Authored light hue/saturation/brightness/radius is baked into a new GPU
vertex channel for the BSP and characters. A native AAudio mixer decodes real
PCM from owned UAX packages in memory and plays `s_fire_loop`,
`s_spell_throw1`, and `s_spell_hit1`; unresolved Hub1 streaming wrappers remain
explicitly deferred. The audited C10 APK is installed with an exact source vs
installed hash match and was not launched by the agent. See
[`docs/runtime-evidence-2026-09-04-c10-world-runtime.md`](docs/runtime-evidence-2026-09-04-c10-world-runtime.md).

Gate C11 turns the accepted gesture into a visible spell instead of an
instantaneous hidden target event. A successful press-locked Flipendo now
launches a bright crossed-ribbon projectile at 13 m/s, leaves a 1.1 m trail,
shows a short impact burst, and delays the original hit sound plus NPC reaction
until physical arrival. A miss remains visible for 18 m. The casting gesture
also plays the original external `s_wand_wave` onset and loops
`s_wand_wavehum` only while the trigger-held stroke is being recorded.

C11 also replaces C10's many-light sum, which flattened the castle by
saturating broad areas, with a bounded local two-light model. The exact runtime
function is shared with host tests. A read-only pass over all 60,009 rendered
`Lev_Tut1` vertices measures luminance `0.172..0.956`, including 46,105 shadow
vertices and 196 highlights, before the ARM64 package is accepted. See
[`docs/runtime-evidence-2026-09-04-c11-flipendo-light-and-wand-audio.md`](docs/runtime-evidence-2026-09-04-c11-flipendo-light-and-wand-audio.md).

Gate C12 restores the missing authored light fixtures instead of only shading
the BSP: 4 `LampPost`, 16 `SingleCandleStick`, and 20
`ThreeArmFloorCandleStick` instances are loaded from the user's `HProps.u`,
while all 10 `TorchFire02` placements receive animated visible flames. The 50
visible emitters also contribute bounded warm local light. Gesture audio now
uses the game's MPEG Layer II `spell_tracing_loop`, `wand_ready_loop`, and
`spell_cast` exports, and an accepted cast plays Harry's `flipendo_no` clip.
See
[`docs/runtime-evidence-2026-09-04-c12-light-fixtures-and-original-casting-audio.md`](docs/runtime-evidence-2026-09-04-c12-light-fixtures-and-original-casting-audio.md).

Gate C13 replaces the remaining flat per-vertex approximation on the castle
with the level's actual UE1 BSP lightmaps. The clean-room loader now decodes
3616 `LightMap` records, 567,924 bytes of `LightBits`, and their authored light
references, reconstructs independent lightmap UVs, and packs 110,550 native
texels into one 1024x1024 atlas with filtered shadow edges. The Vulkan scene
uses a dedicated second sampler, while NPCs and fixtures retain their dynamic
fallback. The ARM64 APK passed the offline C13 audit but was not installed or
launched because the Quest was not connected to ADB. See
[`docs/runtime-evidence-2026-09-04-c13-ue1-lightmaps.md`](docs/runtime-evidence-2026-09-04-c13-ue1-lightmaps.md).

After the user accepted C13's global lighting, Gate C14 restores the remaining
source-local effects. Wick anchors are derived from the owned fixture mesh
tops rather than guessed actor offsets; each three-arm floor candlestick gets
three flames. The scene now carries 86 independently flickering flames and 90
local glow emitters. A separate depth-tested additive pipeline renders nested
warm layers around lamps and candles while leaving the accepted BSP lightmaps
unchanged. The audited C14 APK was installed with an exact source/installed
hash match and was not launched by the agent. See
[`docs/runtime-evidence-2026-09-04-c14-fixture-flames-and-glow.md`](docs/runtime-evidence-2026-09-04-c14-fixture-flames-and-glow.md).

The headset review rejected C14's local-effect approximation: uniform-alpha
crossed quads exposed rectangular silhouettes, and generating flames for every
candlestick did not match the map's authored particle actors. Gate C15 removes
that approximation. Fire now comes only from the 10 serialized
`HPParticle.TorchFire02` actors, while the four `HProps.LampPost` actors use a
24-segment, two-ring radial alpha falloff with zero-alpha edges. The static-prop
loader also restores all six serialized `HProps.Knight` instances using the
owned `skknightMesh` and texture at runtime. C13's accepted UE1 BSP lightmaps
remain unchanged. See
[`docs/runtime-evidence-2026-09-04-c15-authored-fire-radial-glow-and-knights.md`](docs/runtime-evidence-2026-09-04-c15-authored-fire-radial-glow-and-knights.md).

Gate C16 replaces C15's solid three-strip flame placeholder with the retail
`HPParticle.PotFire08` sprite, decoded from the user's owned `HPParticle.u` at
runtime and rendered as staggered additive particles. The APK still contains
no game assets. All six knights are snapped to the highest walkable BSP surface
below their serialized locations; only the two landing actors receive the yaw
correction that prevents them from facing the wall, while the four side-wall
actors retain their authored quarter-turns. Screenshot histograms also showed
that Quest had darker mid/highlights than PC while already having greater color
saturation, so the scene now applies a restrained UE1-style brightness/gamma
pass without a saturation boost. The audited C16 APK was installed with an
exact source/installed SHA-256 match and was not launched by the agent; headset
visual acceptance remains pending. See
[`docs/runtime-evidence-2026-09-05-c16-owned-fire-knight-grounding-and-color-grade.md`](docs/runtime-evidence-2026-09-05-c16-owned-fire-knight-grounding-and-color-grade.md).

Gate C17 begins executing the level's serialized opening cutscene instead of
staging another handcrafted scene. The read-only UE1 property pass retains the
`CutCast`, `CutLoc`, and script strings stored on `Lev_Tut1.CutScene4`. At
runtime the Quest interpreter advances its five populated parallel tracks,
resolves 21 authored camera/actor marks, synchronizes `CUE`/`WAITFOR`, applies
timed `SLEEP`, `GOTO`/`TELEPORT`/`MOVETO`, capture/release, and plays five
original Dumbledore MPEG Layer II dialogue exports from the user's
`AllDialog.u`. The cinematic camera changes position while keeping live stereo
head orientation, and moving NPC hitboxes follow their visual offsets. This is
an explicit UE1 command subset: authored sequence animation, facing, fades,
music, and arbitrary trigger side effects are parsed and logged but are not yet
fully executed. The audited C17 APK was installed with an exact
source/installed SHA-256 match and was not launched by the agent. See
[`docs/runtime-evidence-2026-09-05-c17-serialized-intro-cutscene.md`](docs/runtime-evidence-2026-09-05-c17-serialized-intro-cutscene.md).

Gate A1 proves the raw-Vulkan diagnostic path; the functional part of Gate A2
proves direct wgpu rendering into OpenXR-owned images on PC Quest Link; Gate A3
now has a machine/runtime pass for synthetic stereo geometry and live pose
processing; and Gate A4 proves the live controller pose, wand rendering, and
trigger-stroke capture. Gate A5b proves external authored-template loading and
live original-semantic Flipendo scoring. Gate A6 proves exactly-once test-world
dispatch and a visible target reaction. These runs do not yet prove game
rendering, retail actor selection or spell effects, or Quest standalone
behavior.
See [`docs/xr-renderer-gate.md`](docs/xr-renderer-gate.md) and the
[raw-Vulkan evidence](docs/runtime-evidence-2026-09-02-stereo-clear.md), plus
the [direct-wgpu evidence](docs/runtime-evidence-2026-09-02-wgpu-direct-import.md),
the [tracked-geometry evidence](docs/runtime-evidence-2026-09-02-tracked-geometry.md),
the [tracked-wand evidence](docs/runtime-evidence-2026-09-02-tracked-wand.md),
the [Flipendo-scoring evidence](docs/runtime-evidence-2026-09-02-flipendo-scoring.md),
the [Flipendo test-target evidence](docs/runtime-evidence-2026-09-02-flipendo-test-target.md),
and the [package-census evidence](docs/runtime-evidence-2026-09-02-package-census.md).
