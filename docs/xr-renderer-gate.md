# OpenXR, Vulkan, and renderer gate

- Date: 2026-09-02
- Scope: PC capability and Quest Link stereo-session evidence; no game launch
  and no Quest standalone run

## Verified on the current PC

The active 64-bit runtime registry entry resolves to:

```text
C:\Program Files\Meta Horizon\Support\oculus-runtime\oculus_openxr_64.json
```

That manifest names Oculus runtime library `LibOVRRTImpl64_1.dll`.
Windows did not expose `openxr_loader.dll` through the probe process's
default DLL search. For the controlled run, the probe loaded an already present
Khronos loader from another local VR workspace without copying it:

```text
C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll
version: 1.1.61
SHA-256: 866A8A9EF162E91B2ECF321506F584F2AA51A0E7D1C452856F8AE519670231BD
```

The independent `tools/xr-runtime-probe` build passed with Rust/Cargo
1.97.0 and `openxr = 0.21.1`. Its runtime output established:

```text
runtime: Oculus 1.207.0
XR_KHR_vulkan_enable: yes
XR_KHR_vulkan_enable2: yes
XR_META_touch_controller_plus: yes
XR_FB_display_refresh_rate: yes
```

The current PC runtime did not advertise `XR_EXT_hand_tracking`,
`XR_META_simultaneous_hands_and_controllers`, `XR_FB_foveation`,
or `XR_FB_foveation_vulkan`. That is a fact about this Meta PC runtime
instance, not a claim about the native Quest runtime.

The first run had no active Quest Link session and stopped at
`XR_ERROR_FORM_FACTOR_UNAVAILABLE`. After Quest Link was launched, the
same capability probe reported Vulkan `1.0.0..=1.2.0`, two recommended
`2064x2272` views, sample count 1, and `OPAQUE` blend mode.

The first stereo session then remained in `IDLE` because the headset had
been removed and its proximity sensor put it to sleep. After the headset was
worn and awake, the legacy-Vulkan stereo-clear probe completed 450 frames and
cleanly traversed:

```text
IDLE -> READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED
FOCUSED -> VISIBLE -> SYNCHRONIZED -> STOPPING
```

The user confirmed red in the left eye and blue in the right eye. Exact
evidence is recorded in
[`runtime-evidence-2026-09-02-stereo-clear.md`](runtime-evidence-2026-09-02-stereo-clear.md).
After lifecycle hardening, the final source also completed a 60-frame smoke run
through the same focused-to-stopping path with exit code 0; visual acceptance
remains attached to the 450-frame run.

## Intended renderer bridge

The preferred path is a thin Rust XR frontend around `openxr 0.21.1`,
Vulkan, and the pinned wgpu stack:

1. OpenXR owns frame timing, views, actions, session state, and swapchains.
2. OpenXR chooses the Vulkan physical device.
3. One Vulkan device and graphics queue are shared with wgpu.
4. Runtime-owned `XrSwapchainImageVulkanKHR` images are wrapped as
   non-owning `wgpu-hal` textures.
5. Each eye receives an explicit asymmetric projection matrix from its
   `XrView::fov`; simulation advances once per XR frame, not once per eye.
6. The first pass clears the acquired image and the final image layout is
   suitable for a color attachment when released to OpenXR.

A C++ OpenXR shell would not remove the unsafe Vulkan/wgpu image bridge for the
preferred direct-render path. A raw Vulkan copy could target the XR image
without wrapping it in wgpu, but it would add a second render target plus
explicit synchronization, layout transitions, and copy bandwidth. Treat that
as a diagnostic fallback, not the first-line architecture.

## Pass sequence

### Gate A1: raw Vulkan stereo clear — passed

- Vulkan requirements and required instance/device extensions were queried;
- OpenXR selected the RTX 4090; the application chose its graphics-capable
  queue family 0;
- one two-layer stereo swapchain was created;
- 450 red-left/blue-right frames were submitted and visually accepted;
- the post-hardening source completed a second 60-frame smoke run;
- the session stopped cleanly after `request_exit`.

This pass intentionally uses two Vulkan clear render passes and no shader.

### Gate A2: direct wgpu swapchain import — functional pass; validation pending

The Quest Link functional sub-gate passed:

- `wgpu`, `wgpu-core`, `wgpu-hal`, and `wgpu-types` are locked to `29.0.4`;
- OpenXR's selected physical device and the wgpu Vulkan adapter are the same
  RTX 4090, and the raw logical-device/queue handles were verified identical;
- all three runtime-owned `VkImage` handles were wrapped as non-owning
  `wgpu-hal` textures and addressed as two array layers;
- 300 frames were cleared directly through wgpu with no intermediate color
  texture or copy;
- each exact wgpu submission completed before `xrReleaseSwapchainImage`;
- one process rendered before and after `STOPPING -> IDLE -> READY`, with two
  balanced session begin/end pairs;
- acquire/wait/submit/GPU-complete/release were all `300`, maximum outstanding
  images was one, and per-image acquisitions were `[100, 100, 100]`;
- non-owning external-wrapper callbacks completed `3/3`, with zero uncaptured
  wgpu errors, zero device losses, and zero lost OpenXR events;
- the user visually accepted green-left and pink-purple-right output.

Exact output and hashes are recorded in
[`runtime-evidence-2026-09-02-wgpu-direct-import.md`](runtime-evidence-2026-09-02-wgpu-direct-import.md).

The validation sub-gate remains open. No `VK_LAYER_KHRONOS_validation` is
installed on this PC, and the probe deliberately reports the run as functional
evidence only. Before calling all of Gate A2 complete, enable the Khronos layer
with a counted debug messenger and repeat the direct-import/restart test with
zero validation errors. This limitation does not invalidate the observed
functional result.

### Gate A3: synthetic stereo geometry and live head pose — machine and stereo-fusion pass

An opt-in `--geometry` path now exercises the first real camera pipeline while
leaving the accepted A2 clear path as the default:

- an original 900-vertex metric room is fixed in `LOCAL` space and rendered
  with `Depth32Float` depth testing;
- the two eyes consume their actual `XrView.pose` and asymmetric `XrView.fov`
  values, with separate per-eye uniform buffers;
- the head `VIEW` space is located against the same `LOCAL` base at the same
  predicted display time, only for 6DoF telemetry;
- asymmetric projection boundary error was at most `0.00000006`; quaternion
  norm error was at most `0.00000012`;
- measured IPD stayed at `0.06679 m`, with the right eye consistently on local
  +X and no measurable Y/Z separation;
- one immutable simulation tick was produced for every one of 3036 begun
  frames, and both eyes drew the same 1800 renderable snapshots;
- acquire/wait/submit/GPU-complete/release were all 1800, per-image use was
  `[600, 600, 600]`, wrapper callbacks completed `3/3`, and wgpu errors, device
  losses, and lost XR events were zero.

The user reported a single/fused view of the intentionally plain cube geometry,
so binocular fusion is visually accepted. The recorded physical movement
exceeded the planned threshold on all three
translation and all three rotation axes. It remains formally inconclusive
because the runtime emitted pending `LOCAL` and `STAGE` reference-space-change
events after
the first rendered frame. Upright orientation, a world that remains fixed
during head motion, and stronger near-object parallax were not explicitly
reported and remain open. This is therefore not yet a full A3 acceptance.

Exact output, hashes, the failed headset-off repeat, and claim boundaries are
recorded in
[`runtime-evidence-2026-09-02-tracked-geometry.md`](runtime-evidence-2026-09-02-tracked-geometry.md).

### Gate A4: Touch Plus wand pose and trigger strokes — passed

The opt-in `--wand` path adds one right-hand OpenXR action set while preserving
the accepted stereo renderer and metric room:

- the Meta Touch Plus OpenXR 1.0 extension profile and Oculus Touch fallback
  are suggested before one action-set attachment;
- grip, aim, analog trigger, and boolean cast state are synchronized once per
  begun frame and located in the same `LOCAL` space and predicted display time
  as the eyes;
- only valid, tracked poses in a focused session produce a wand; tracking loss
  hides it and prevents a stroke from bridging the invalid interval;
- one provisional 0.340 m procedural wand, cyan aim ray, and orange trigger
  trace are uploaded once and drawn identically in both eye passes;
- 9000 frames completed with 7327 tracked/dynamic frames, 17 press/release
  pairs, 17 completed strokes, zero gap bridges, zero duplicate uploads, zero
  snapshot/capacity/finite-value failures, balanced eye draws, and no wgpu
  error or device loss;
- there was no `LOCAL` reference-space change, and the tracked-room motion
  protocol also passed its machine thresholds;
- the user reported `работает` while exercising the live scene, accepting the
  visible wand interaction.

Oculus `1.207.0` rejected `xrPathToString` for paths returned only by optional
bound-source-name diagnostics. The final code records those conversions as
nonfatal warnings; action creation, attachment, synchronization, state reads,
and pose location remain checked. The accepted run exited cleanly.

This pass provides the live stroke stream for the next spell-recognition slice.
It does not recognize a spell, calibrate final artwork or grip offset, integrate
the retail game, or prove native Quest behavior.

Exact output, hashes, excluded attempts, and claim boundaries are recorded in
[`runtime-evidence-2026-09-02-tracked-wand.md`](runtime-evidence-2026-09-02-tracked-wand.md).

### Gate A5a: live trajectory projection - functional and visual pass; clean aggregate pending

The feature-gated `--gesture` path records an untrimmed timestamped 3D stroke
separately from the A4 visual trail and calls the platform-neutral C++20
trajectory core through a versioned C ABI on release. It preserves raw and
projected paths separately, logs endpoints and hashes, and explicitly cancels
tracking, timing, jump, capacity, profile, `LOCAL`, and session discontinuities.

In the accepted Quest Link capture, 10 physical strokes projected successfully
and two discontinuous tip jumps were canceled before any bridge call. The user
reported that it worked after observing the feedback. Green is explicitly
`TRAJECTORY_PROJECTED_NOT_SPELL`: this gate loads no authored template,
computes no HP1 score, and dispatches no spell.

Link stopped before the requested finite frame target, and the probe later
timed out waiting for another `READY` session. Therefore A5a has functional
runtime and visual evidence but not a final clean-exit aggregate report. Exact
attempt data, artifact hashes, build evidence, exclusions, and scope are in
[`runtime-evidence-2026-09-02-live-trajectory-projection.md`](runtime-evidence-2026-09-02-live-trajectory-projection.md).

If the legacy Vulkan extension or wgpu's chosen queue family is incompatible,
stop and make a narrow pinned `wgpu-hal` integration for
`XR_KHR_vulkan_enable2`. Do not replace the architecture with an
unmeasured second renderer.

### Gate B: one game scene on PC

- Classic renderer first, with modern AO/volumetrics disabled;
- one original room loaded from external user-owned data;
- explicit asymmetric eye cameras and live head pose;
- one simulation tick per predicted display frame;
- per-eye and CPU/GPU timing telemetry.

### Gate C: Quest 3 native smoke

- Android ARM64 `cdylib` and NativeActivity lifecycle;
- Android OpenXR loader initialization and Vulkan binding;
- the same stereo clear/triangle probe before any game systems;
- pause/resume and session recreation;
- then the Classic one-room scene.

APK creation, installation, headset launch, and runtime acceptance are separate
claims. No Quest install or launch occurs without the user's explicit request.

Gate C0 passes the package/toolchain boundary. Gate C1 now adds the native
Vulkan device, OpenXR session, two-layer swapchain, projection views, complete
frame loop, red-left/blue-right clears, and lifecycle-driven session teardown
and recreation. A C2-compatible build retaining that path was later installed
on Quest 3, where the user confirmed red in the left eye and blue in the right
eye. Lifecycle recreation remains unaccepted; see
[`runtime-evidence-2026-09-03-c0-quest-native-host.md`](runtime-evidence-2026-09-03-c0-quest-native-host.md)
and
[`runtime-evidence-2026-09-03-c1-quest-stereo-smoke-build.md`](runtime-evidence-2026-09-03-c1-quest-stereo-smoke-build.md).

Gate C2 prepares the external user-owned data boundary. Its importer defaults
to a read-only plan and requires explicit `-Copy` for adb transfer; no device
action has been performed. The ARM64 host now links and invokes the portable
PlayerStart decoder against external `Lev_Tut1.unr` when the required files
exist, without blocking the stereo diagnostic if import or parsing is absent.
See
[`runtime-evidence-2026-09-03-c2-quest-owned-data-boundary.md`](runtime-evidence-2026-09-03-c2-quest-owned-data-boundary.md).

Gate C3 adds the first native textured Hogwarts frame. The host loads the
bounded `Lev_Tut1` BSP and its texture array from app-specific external data,
applies the accepted PlayerStart transform, and records a depth-tested Vulkan
draw for each OpenXR eye. Camera projection math is platform-neutral and host
tested. The first installed build found the copied data and PlayerStart but
rejected `Core.Function`, because the intentionally omitted Windows DLL had
also been the native-package identity signal. Its red/blue fallback was not C3
success. The corrected linker passes a complete 286-file/no-DLL `Lev_Tut1`
texture build, and C3 now exits on scene failure instead of masking it with the
C1 diagnostic. The corrected APK was installed and submitted 600 logged
HOGWARTS frames; the user confirmed Hogwarts was visible. Device parsing
reported 60,012 vertices versus 60,009 in the host no-DLL mirror, which remains
an explicit determinism follow-up.
See
[`runtime-evidence-2026-09-04-c3-quest-textured-hogwarts-build.md`](runtime-evidence-2026-09-04-c3-quest-textured-hogwarts-build.md).

Gate C4 adds a native Touch action set and the first standalone player-control
slice. The right grip position and runtime aim orientation drive a 0.34 m
render of external HPBase.WandMesh; the left stick moves head-relative at
1.8 m/s with a radial deadzone, and the right stick performs latched 30-degree
snap turns around the current head pivot. LOCAL reference-space changes reset
accumulated movement. Host tests, ARM64 compilation, APK signature/alignment,
and linked-symbol audits pass. The audited C4 APK was installed on Quest 3;
device logs showed tracked-wand HOGWARTS frames and nonzero movement/turn
counters, and the user confirmed the wand, left-stick movement, and right-stick
turning.
See
[`runtime-evidence-2026-09-04-c4-quest-wand-locomotion-build.md`](runtime-evidence-2026-09-04-c4-quest-wand-locomotion-build.md).

Gate C5 adds standalone Flipendo capture using the genuine external
`FlipPattern` lesson profile and authored first pass mark. Right-trigger press
locks the cast origin/direction, trigger hold records the wand tip, and release
projects and scores the trajectory. Test tolerance is temporarily widened by
1.75x. The wand itself changes color for recording/accepted/rejected feedback;
no moving aim ray is drawn. The first C5 headset run recorded 10 attempts but
accepted none because the plane remained on world X/Y after the player turned.
The corrected candidate freezes an aim-facing plane and normalizes free-space
position/scale before calling the real scorer. Its 90-degree/quarter-size
regression and all build/audit checks pass; it is not yet reinstalled. See
[`runtime-evidence-2026-09-04-c5-quest-flipendo-build.md`](runtime-evidence-2026-09-04-c5-quest-flipendo-build.md).

Gate C6 adds the missing drawing UX. The external authored `FlipPattern` is
converted into a cyan world-space ribbon in the frozen aim-facing plane. While
the trigger is held, the projected wand tip forms a separate orange ribbon;
the completed fitted path remains green or red for 0.8 seconds. The guide uses
six static vertices and push-constant transforms per segment, so no per-frame
GPU buffer rewrite is needed. Host tests, ARM64 compilation, and APK audit pass.
The package is installed, but Meta's controller-required dialog has prevented
a C6 session and headset acceptance so far. See
[`runtime-evidence-2026-09-04-c6-quest-gesture-guide-build.md`](runtime-evidence-2026-09-04-c6-quest-gesture-guide-build.md).

The user subsequently launched C6 and confirmed the guide was visible. Runtime
logs also recorded two accepted Flipendo attempts at score 1.0. C6 was rejected
as a visual baseline because the template appeared before trigger press and the
5.5 cm projected trail diverged from the real wand tip. Gate C7 hides the guide
while idle, places its first point on the press-time tip, and renders the live
trail from actual world-space tip positions without an offset. The completed
normalized result remains briefly visible. C7 passes all build/audit checks and
is installed but deliberately not launched by the agent. See
[`runtime-evidence-2026-09-04-c7-quest-aligned-trail-build.md`](runtime-evidence-2026-09-04-c7-quest-aligned-trail-build.md).

The user then accepted C7's idle visibility and wand-tip alignment in the
headset. Gate C8 ports the full external `Lev_Tut1` character manifest into the
standalone Vulkan scene: Harry is excluded for first person, 28 NPC instances
use deduplicated package-native textured meshes and an idle animation frame,
and seven story characters are staged near PlayerStart. Accepted Flipendo now
selects the nearest character from the press-time locked ray and applies a
short reversible push/lift reaction. The user confirmed Hogwarts and NPC
rendering after the asynchronous-startup correction, while reporting that the
staged group floated and the loading view still used the old red/blue
diagnostic. C8B replaced the diagnostic with opaque black and restored the
earlier PCVR `0.84 m` floor drop; its headset run then showed slight foot
penetration. C8C used each actor's visible idle-frame lower bound, but the
headset proved that local zero is not the floor. C8D now finds the highest
walkable solid BSP triangle beneath each staged XZ position in the authored
capsule-foot search band, then places that actor's visible lower bound on the
measured height. The user accepted C8D grounding in the headset. See
[`runtime-evidence-2026-09-04-c8-quest-character-population-and-flipendo.md`](runtime-evidence-2026-09-04-c8-quest-character-population-and-flipendo.md).

Gate C9 makes the standalone slice dynamic and spatially constrained. It loads
16 normalized package-native idle samples for every distinct skeletal mesh,
stores all population frames once in a frame-major Vulkan vertex buffer, and
selects one shared 10 Hz animation frame for both eyes. Staged actors are
grounded separately in every frame, and spell target bounds cover the full
animation loop. Controller locomotion now passes through the accepted Harry
capsule model against solid BSP, including 8 cm substeps, floor/stair following,
wall rejection, and axis sliding. The ARM64 build, six host regressions, and an
APK audit that requires both gameplay paths pass. C9 is installed but was not
launched by the agent; headset acceptance remains pending. See
[`runtime-evidence-2026-09-04-c9-quest-animation-and-bsp-collision.md`](runtime-evidence-2026-09-04-c9-quest-animation-and-bsp-collision.md).
