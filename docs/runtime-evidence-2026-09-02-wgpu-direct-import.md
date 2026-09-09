# Quest Link direct-wgpu swapchain evidence

- Date: 2026-09-02
- Scope: PC process rendering through Quest Link; no retail game launch, no
  proprietary data access, and no Quest APK install or standalone launch
- Result: functional direct-import pass; Vulkan validation sub-gate pending
- Starting repository commit: `553373fe76e1dbe6e7c22bde816bb0a9b2fdda09`

## What was tested

`wgpu_stereo_clear` creates one OpenXR legacy-Vulkan session and one two-layer
color swapchain. OpenXR selects the physical device. The probe then adopts the
same Vulkan instance, logical device, and graphics queue into the exactly pinned
`wgpu 29.0.4` stack.

Every runtime-owned `XrSwapchainImageVulkanKHR` is wrapped once with
`wgpu-hal` as `TextureMemory::External` and a non-owning drop callback. Separate
2D views address array layer 0 and 1. Each renderable frame clears those views
green and purple, waits for that exact wgpu submission to complete, releases
the image to OpenXR, and submits a stereo projection layer. There is no
intermediate color texture and no Vulkan copy.

The probe treats uncertain cleanup as fatal: if GPU completion or deferred
view destruction cannot be proved, it aborts without running Vulkan/OpenXR
destructors and cannot print the clean-exit marker. The accepted run reached the
normal verified teardown instead.

## Reproduction command

From the repository root:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe build --locked `
  --bin wgpu_stereo_clear
& .\target\debug\wgpu_stereo_clear.exe `
  --loader C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll `
  --frames 300
Pop-Location
```

Toolchain:

```text
cargo 1.97.0 (c980f4866 2026-06-30)
rustc 1.97.0 (2d8144b78 2026-07-07)
openxr 0.21.1
ash 0.38.0
wgpu/wgpu-core/wgpu-hal/wgpu-types 29.0.4
```

The Khronos loader was used in place from another local VR workspace and was
not copied into this repository.

## Machine evidence

The accepted process reported:

```text
runtime: Oculus 1.207.0
Vulkan API range: 1.0.0..=1.2.0
Vulkan validation layer: unavailable (not enabled; this run is functional evidence only)
OpenXR-selected GPU: NVIDIA GeForce RTX 4090; Vulkan 1.4.351
wgpu-hal exposed adapter: NVIDIA GeForce RTX 4090 (DiscreteGpu)
shared Vulkan handles verified: instance/physical/device/queue; family=0 index=0
stereo swapchain: 2064x2272x2, Vulkan format 43, wgpu format Rgba8UnormSrgb, sample_count=1
direct import image 0: Xr VkImage == wgpu-hal VkImage
direct import image 1: Xr VkImage == wgpu-hal VkImage
direct import image 2: Xr VkImage == wgpu-hal VkImage
direct no-copy imports ready: 3/3 runtime images; no intermediate color texture
```

The user removed and rewore the headset while the same process was alive. This
produced a genuine recovery sequence with rendered frames on both sides:

```text
IDLE -> READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED
VISIBLE -> SYNCHRONIZED -> STOPPING -> IDLE
READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED
VISIBLE -> SYNCHRONIZED -> STOPPING
```

The final acceptance counters were:

```text
session_begin=2 session_end=2
frame_begin=366 frame_end=366 rendered=300 skipped=66
acquire=300 wait=300 submit=300 gpu_complete=300 release=300
max_outstanding=1 events_lost=0 per_image=[100, 100, 100]
external texture wrappers dropped non-owningly: 3/3
final wgpu runtime errors: 0; device losses: 0
direct-wgpu stereo-clear probe exited cleanly
```

The user supplied the visual acceptance statement `зеленый и розовый вижу`,
confirming green in the left eye and the intended purple/pink color in the
right eye.

Meta Runtime IPC also printed `VirtualLock failed` warnings with Windows error
1453 and one pipe-close warning during normal shutdown. They did not produce an
OpenXR error, wgpu error callback, device loss, nonzero probe exit, or unbalanced
resource counter. They are retained here rather than silently treated as
Vulkan-validation evidence.

## Artifact hashes

```text
wgpu_stereo_clear.rs
  7BC6F8BB6EC450601D834C2350EBA7B3D22454D55CBDF4E219DDEA9801FE4886
Cargo.toml
  903B52C65AC7638AB48C7FEBE3848BF8E68AFDEAE1AF4060F20AC5583608A6E3
Cargo.lock
  829D3FAD6360E7185E50F2A3852C973CB3FEB72DB72E43DDFE8470FBB6D4DA3D
target/debug/wgpu_stereo_clear.exe (11,521,024 bytes)
  D04D97BE690270498E008EBADD20E4020B8AEFF36E58733FFFC084D4403CFC61
openxr_loader.dll 1.1.61
  866A8A9EF162E91B2ECF321506F584F2AA51A0E7D1C452856F8AE519670231BD
```

## Claim boundary

This run proves functional, direct wgpu rendering into OpenXR-owned Vulkan
swapchain images through the current Oculus Quest Link runtime. It also proves
the tested same-process stop/restart path and verified non-owning wrapper
teardown for this run.

It does not prove a Vulkan-validation-clean run because
`VK_LAYER_KHRONOS_validation` is absent. It does not prove game-scene rendering,
head-pose application to geometry, controller or wand tracking, gesture
recognition, performance at game-scene load, Android ARM64 packaging, or Quest
3 standalone runtime behavior.
