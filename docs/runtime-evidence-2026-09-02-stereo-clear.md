# Runtime evidence: Quest Link Vulkan stereo clear

- Date/time: 2026-09-02 08:18 Europe/Kiev
- Device: Meta Quest 3 through wired Quest Link
- Scope: PC OpenXR/Vulkan diagnostic; no retail game and no Quest APK
- Result: runtime pass and user-accepted per-eye output

## Reproduction command

```powershell
Push-Location C:\Dev\harry-potter-vr\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe run --quiet --locked `
  --bin stereo_clear -- `
  --loader C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll `
  --frames 450
Pop-Location
```

The Khronos loader was used in place and was not copied into this repository:

```text
version: 1.1.61
SHA-256: 866A8A9EF162E91B2ECF321506F584F2AA51A0E7D1C452856F8AE519670231BD
```

## Capability evidence

```text
runtime: Oculus 1.207.0
Vulkan API range: 1.0.0..=1.2.0
PRIMARY_STEREO: 2 views
recommended per eye: 2064x2272
maximum per eye: 4128x4544
recommended sample count: 1
maximum sample count: 4
environment blend mode: OPAQUE
```

Both `XR_KHR_vulkan_enable` and `XR_KHR_vulkan_enable2` were
available. The accepted session deliberately used only the legacy extension so
the application—not the runtime—created the ordinary Vulkan instance/device
needed by the future wgpu path.

## Graphics evidence

```text
OpenXR-selected GPU: NVIDIA GeForce RTX 4090
reported Vulkan device API: 1.4.351
graphics queue family: 0
swapchain: 2064x2272, array_size=2, sample_count=1
Vulkan format: 43 (VK_FORMAT_R8G8B8A8_SRGB)
submitted stereo frames: 450
```

Each runtime-owned swapchain image received two non-owning
`VK_IMAGE_VIEW_TYPE_2D` views. Array layer 0 was cleared dark red and
array layer 1 dark blue through separate color-attachment render passes. The
render pass returned each layer to `COLOR_ATTACHMENT_OPTIMAL` before
`xrReleaseSwapchainImage`.

## Lifecycle evidence

```text
IDLE
READY
SYNCHRONIZED
VISIBLE
FOCUSED
first stereo frame submitted; view_state=15
450 frames submitted; xrRequestExitSession
VISIBLE
SYNCHRONIZED
STOPPING
clean process exit (code 0)
```

The first stereo attempt had stayed in `IDLE` because the headset was asleep
with proximity off. This was confirmed through ADB and Meta service logs. After
the headset was worn, no service restart or runtime switch was needed.

Meta's IPC layer emitted `VirtualLock failed ... 1453` warnings during
the diagnostic runs. They did not prevent device creation, session
focus, rendering, or clean shutdown; retain them as a diagnostic observation
rather than treating them as an accepted performance baseline.

## Post-review implementation smoke

After adding RAII teardown, invalid-view skipping, continuous-wait timeouts,
and balanced frame/image cleanup on error paths, the final source was formatted,
checked, rebuilt, and run again at 2026-09-02 08:32 Europe/Kiev:

```text
stereo_clear.rs SHA-256: 5AAE8DA1B617FAD4D052AED2F24C73D1C1794187A410D9F44DCC8CFD893595F1
debug executable SHA-256: 451259FF833D2B90C5D0B6CAA36087CD2FA2674FE82F7FE6E6451C7D7887745E
IDLE -> READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED
first stereo frame submitted; view_state=7
60 stereo frames submitted; xrRequestExitSession
VISIBLE -> SYNCHRONIZED -> STOPPING
clean process exit (code 0)
```

This final smoke used the same command and loader as above with `--frames 60`.
It revalidates the committed happy path after the lifecycle hardening; the
450-frame run remains the visual acceptance run.

## Visual acceptance

The user explicitly confirmed:

- left eye: red;
- right eye: blue.

This accepts the eye order and array-layer addressing. It does not prove wgpu
interop, a game scene, head-pose application, Touch controller tracking,
gesture scoring, or Quest standalone runtime behavior.
