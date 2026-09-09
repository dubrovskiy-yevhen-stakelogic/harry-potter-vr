# Quest Link tracked-geometry evidence

- Date: 2026-09-02
- Scope: synthetic PCVR geometry through Quest Link; no retail game launch, no
  proprietary data access, and no Quest APK install or standalone launch
- Result: renderer and pose-processing invariants passed; stereo fusion visually
  accepted; world-lock/parallax and reference-space-stable motion pending
- Starting repository commit: `50d4358b92fd958c99f919e2af55a6380f8b37d2`

## What was tested

The accepted Gate A2 clear path remains the default `wgpu_stereo_clear` mode.
The new opt-in `--geometry` mode keeps the same OpenXR/Vulkan/wgpu ownership and
submission path, then replaces only the two color-clear passes with an original
procedural calibration scene and one depth-tested draw per eye.

The scene contains 900 vertices (300 triangles): a metric checker floor and
back wall, near/mid/far color landmarks, RGB axes, a green arch, and one animated
gold cube. It contains no retail or third-party game assets. Geometry is fixed
in `LOCAL` reference space. Each eye uses its own runtime `XrView.pose` and
asymmetric `XrView.fov`; a separate `VIEW` reference space is located against
`LOCAL` at the same predicted display time only for head-pose telemetry.

One immutable animation snapshot is produced per begun OpenXR frame and shared
by both eye draws. The two eyes use separate uniform buffers, so the second
queue write cannot overwrite the first eye's matrix before submission. The
critical sequence remains acquire, wait, encode, submit, exact GPU-completion
wait, release, and `xrEndFrame`.

## Reproduction command

From the repository root:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe build --locked `
  --bin wgpu_stereo_clear
& .\target\debug\wgpu_stereo_clear.exe `
  --loader C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll `
  --frames 1800 --geometry
Pop-Location
```

Toolchain and pinned renderer dependencies:

```text
cargo 1.97.0 (c980f4866 2026-06-30)
rustc 1.97.0 (2d8144b78 2026-07-07)
openxr 0.21.1
ash 0.38.0
glam 0.30.10
wgpu/wgpu-core/wgpu-hal/wgpu-types 29.0.4
```

The final post-review source passed five tests under
`cargo test --all-targets --locked`: asymmetric FOV boundaries, wgpu-positive
Y, rigid pose inversion, finite procedural geometry, and head-local IPD
measurement with canted eye orientations.

## Runtime evidence

The completed process reported:

```text
runtime: Oculus 1.207.0
Vulkan API range: 1.0.0..=1.2.0
Vulkan validation layer: unavailable (not enabled; this run is functional evidence only)
OpenXR-selected GPU: NVIDIA GeForce RTX 4090; Vulkan 1.4.351
wgpu-hal exposed adapter: NVIDIA GeForce RTX 4090 (DiscreteGpu)
stereo swapchain: 2064x2272x2, Vulkan format 43, wgpu format Rgba8UnormSrgb, sample_count=1
direct import image 0: Xr VkImage == wgpu-hal VkImage
direct import image 1: Xr VkImage == wgpu-hal VkImage
direct import image 2: Xr VkImage == wgpu-hal VkImage
direct no-copy imports ready: 3/3 runtime images; no intermediate color texture
[geo.model] vertices=900 triangles=300 fnv1a64=de7bd05b46c89fe5 world=LOCAL meters near=0.050 far=30.0
```

The headset initially lacked positional tracking. The probe ended those frames
without acquiring an image and waited until both view and head position and
orientation were valid and tracked. The first rendered frame then reported
different per-eye FOVs and different view-projection hashes:

```text
[geo.first] tick=1237 predicted_ns=225374700385854
left_fov=(-0.94247776, 0.6981317, 0.7679449, -0.9599311)
right_fov=(-0.6981317, 0.94247776, 0.7679449, -0.9599311)
left_vp_hash=6ba03f7053e07088 right_vp_hash=b198314653be513d
```

The clean-exit counters and invariant summaries were:

```text
session_begin=1 session_end=1
frame_begin=3036 frame_end=3036 rendered=1800 skipped=1236
acquire=1800 wait=1800 submit=1800 gpu_complete=1800 release=1800
max_outstanding=1 events_lost=0 per_image=[600, 600, 600]
[geo.scheduler] ticks=3036 frames_begun=3036 max_ticks_per_frame=1 snapshot_mismatches=0 draws_left=1800 draws_right=1800 PASS
[geo.projection] samples=1800 boundary_max_error=0.00000006 quaternion_norm_max_error=0.00000012 ipd_min=0.06679m ipd_avg=0.06679m ipd_max=0.06679m eye_local_min_x=0.06679m eye_local_max_yz=0.00000m PASS
[geo.motion] translation_span=(0.1142,0.2236,0.3652)m rotation_span=(41.08,131.61,66.64)deg reference_space_changes=2 INCONCLUSIVE
[geo.timing] encode_cpu_avg_ms=0.2050 exact_gpu_completion_wait_avg_ms=0.1409 gpu_timestamps_collected=false
external texture wrappers dropped non-owningly: 3/3
final wgpu runtime errors: 0; device losses: 0
Gate A3 tracked-geometry probe exited cleanly
```

The translation and rotation spans exceed the planned motion protocol on all
six axes. They are deliberately not accepted as clean motion evidence because
the runtime also emitted one pending `LOCAL` and one pending `STAGE` reference
space change, both with `pose_valid=false`, after the first rendered frame. A
reference-space origin change can contribute apparent motion. Renderer,
projection, stereo draw,
scheduler, ownership, and teardown checks are unaffected and passed.

A second process was started to seek a clean motion epoch, but the removed
headset stayed in `IDLE` and the process exited at the 120-second READY timeout.
It rendered no frame and is not counted as runtime evidence.

## User visual evidence

After the completed run, the user reported `цельной только это кубы обычные`
(`it was a single/fused image; they were only ordinary cubes`). This accepts
binocular fusion of the synthetic geometry. The calibration objects were
intentionally plain procedural solids. The statement does not explicitly
accept upright orientation, world locking during translation/rotation, or the
near-versus-far parallax check, so those remain open rather than inferred.

## Runtime-tested artifact hashes

These are the exact source and executable hashes used by the completed
1800-frame headset run:

```text
tools/xr-runtime-probe/src/bin/wgpu_stereo_clear.rs (49,861 bytes)
  BA582BD959A50B4B912AF1170A2FE93605F420E3E79F4C07628E5730ADBF201F
tools/xr-runtime-probe/src/bin/support/calibration_scene.rs (34,765 bytes)
  9937FFE429581E966DBCE1B942C52EE93C055FF320435CF98107CE907B156CCF
tools/xr-runtime-probe/src/bin/support/calibration_scene.wgsl (1,384 bytes)
  D46D7EF4BF7E6CC78DBFDB703D2DBF427CFBB29B8ED0BB7A9F63B7918A187AB7
tools/xr-runtime-probe/Cargo.toml (415 bytes)
  5D2A09D6C1067A7DE212CD8AED67A8FFBB6D05E5954DF312E5A122176714EBF4
tools/xr-runtime-probe/Cargo.lock (25,719 bytes)
  B3BE7CCF00AEE9294155DEA59C1578BC8BB40D587E15E8F6E35DDFACBCB5E301
tools/xr-runtime-probe/target/debug/wgpu_stereo_clear.exe (11,803,648 bytes)
  4B645B911425B37CC871896FAFD88CAA2E5C7273F6E079A4C41E3B4845928ABF
```

## Post-review artifact hashes

Review after the headset run found one error-path issue and one portability
issue that did not invalidate the observed Quest result. A fallible simulation
tick was moved inside the existing begun-frame cleanup scope, so any tick error
now still attempts `xrEndFrame`. The IPD-axis check was changed from
left-eye-local to tracked-head-local coordinates, preventing a false failure on
canted displays; the fifth unit test covers that case. The
default non-geometry OpenXR application name was also restored exactly to the
accepted A2 value.

The final source passed formatting, locked all-target check, all five tests,
and a locked debug build. The headset was removed, so this final executable has
not been attributed the earlier live run:

```text
tools/xr-runtime-probe/src/bin/wgpu_stereo_clear.rs (49,866 bytes)
  469F6075023DFCCD91986F83693CB6426AB7C5BCD99E8B26C6C8B979A0751DF9
tools/xr-runtime-probe/src/bin/support/calibration_scene.rs (35,857 bytes)
  3297BCB0143D5AF076ADB0E41B0AF93344AD87F78914555737DCF8E15F5E0F3A
tools/xr-runtime-probe/src/bin/support/calibration_scene.wgsl (1,384 bytes)
  D46D7EF4BF7E6CC78DBFDB703D2DBF427CFBB29B8ED0BB7A9F63B7918A187AB7
tools/xr-runtime-probe/Cargo.toml (415 bytes)
  5D2A09D6C1067A7DE212CD8AED67A8FFBB6D05E5954DF312E5A122176714EBF4
tools/xr-runtime-probe/Cargo.lock (25,719 bytes)
  B3BE7CCF00AEE9294155DEA59C1578BC8BB40D587E15E8F6E35DDFACBCB5E301
tools/xr-runtime-probe/target/debug/wgpu_stereo_clear.exe (11,803,136 bytes)
  94678198E0AFB84FC46F2FB3A6313310A59646D43834091A333D44E07668ECF4
```

## Claim boundary

This run proves that the current Oculus Quest Link runtime accepted depth-tested
procedural geometry rendered directly by wgpu into both OpenXR swapchain layers,
while the process consumed live, metrically plausible per-eye and head poses.
The user visually accepted binocular fusion. The run also proves balanced
scheduling, exact GPU completion before image release, and verified non-owning
teardown for this run.

It does not yet prove upright orientation, world locking, or near-versus-far
parallax. It also does not prove a reference-space-stable motion epoch,
Vulkan-validation cleanliness, a game scene, controller/wand tracking,
gestures, Android ARM64 packaging, or Quest 3 standalone behavior. The live
claims apply to the explicitly listed runtime-tested hashes; the final
post-review hashes currently have build/test evidence only.
