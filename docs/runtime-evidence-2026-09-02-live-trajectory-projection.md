# Quest Link live trajectory-projection evidence

- Date: 2026-09-02
- Scope: PCVR Quest Link diagnostic only; no retail game launch, proprietary
  data extraction, Quest APK installation, or standalone launch
- Result: Gate A5a functional projection and user visual acceptance passed;
  a finite clean-exit aggregate run remains pending
- Starting commit: `51ec84c329eb44b80026341e2f1ed89b617b2509`

## What was tested

The opt-in `--gesture` mode extends the accepted Gate A4 Touch Plus wand.
Press, held, and release samples are captured into a timestamped raw 3D path
which is separate from the trimmed visual trail. The release endpoint is
captured before one call through a versioned C ABI into the same original C++20
trajectory core that builds for Android ARM64.

The bridge uses caller-owned POD buffers, catches every native exception, checks
the ABI version and layout, and never exposes C++ containers across FFI. The
raw path is never silently trimmed: tracking loss, non-monotonic time, a sample
gap over `100 ms`, a tip jump over `0.25 m`, profile/reference-space/session
discontinuity, or the `16,384`-sample bound cancels the attempt. Completed raw
3D and projected 2D paths remain separate in process memory. Per-attempt logs
record their counts, endpoints, and stable hashes.

Gate A5a deliberately uses a provisional `0.70 m` `LOCAL X/Y` plane frozen
at the first wand tip. It does not fit, rotate, or scale a player's stroke
afterward. Its `4 mm` jitter threshold and `13,888,889 ns` resampling period
are diagnostic values, not authored Harry Potter lesson parameters.

The visual legend is:

- orange: recording;
- green: `TRAJECTORY_PROJECTED_NOT_SPELL`;
- red: trajectory projection rejected;
- amber: capture canceled.

No authored template was loaded, no HP1 gesture score or pass threshold was
computed, and no spell was dispatched.

## Build and static verification

The normal Gate A4 build remains independent of C++20: Cargo's
`gesture-projection` feature is off by default, its `cc` build dependency is
optional, and the feature-off build script is empty.

The final tree passed:

```text
Cargo default: 17 passed; 0 failed
Cargo --features gesture-projection: 27 passed; 0 failed
C++ hpvr_wand_tests: 1/1 passed
MSVC pure-C header/layout compile target: passed
Android NDK r27c arm64-v8a static-library build: passed
git diff --check: passed
```

The tests cover raw press/release endpoint retention, FFI layout sizes,
alignment and offsets, native timing rejection, buffer-capacity queries without
partial output, null arguments, tracking gaps, jumps, overflow-safe timestamp
subtraction, neutral rearming, profile changes, and all accounting identities.
Independent read-only reviews found no remaining P0/P1/P2 issue and found no
Gate A4 regression.

## Runtime attempt 01: accepted functional capture

With Quest Link active:

```powershell
.\tools\xr-runtime-probe\target\debug\wgpu_stereo_clear.exe `
  --loader C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll `
  --frames 4500 --gesture
```

The runtime was Oculus `1.207.0` on an RTX 4090 with a
`2064x2272x2` sRGB swapchain. Three runtime-owned Vulkan images were imported
directly by matching handle, with no intermediate color copy. The session
reached `FOCUSED`, the right controller produced a live wand, and an initial
`LOCAL` reset was applied at its announced predicted-time boundary before the
scene tick. No active attempt existed at that reset.

The ignored local log contains 12 distinct attempts:

```text
10 trajectory_projection_status=ok
2 reason=tip_jump, projected_2d_points=0, bridge_call=NONE
861 raw samples entered successful projection calls
864 projected points were returned
108 raw-prefix samples belonged to the two canceled attempts
43 samples were rejected by the physical jitter filter
all 10 successful reports: invalid=0, nonmonotonic=0
```

Successful stroke durations ranged from `708.292 ms` to `2249.915 ms`.
Every successful projected first point was `(0.5, 0.5)`, consistent with the
frozen first-tip plane origin. Raw and projected endpoints and their distinct
hashes were printed before the in-memory paths were retained. A representative
attempt reported:

```text
attempt=4 raw_3d_points=146
raw_hash=52b46209317d8c29
duration_ms=2013.781
trajectory_projection_status=ok
projected_2d_points=146
projected_first=Some(Vec2(0.5, 0.5))
projected_last=Some(Vec2(0.81931734, 0.23604262))
projected_hash=08db75c66ac056c7
invalid=0 nonmonotonic=0
green_means=TRAJECTORY_PROJECTED_NOT_SPELL
scorer=UNAVAILABLE template=NOT_LOADED dispatch=NONE
```

The two sudden motions crossed the explicit `0.25 m` continuity bound. Each
was logged with its raw count, first/last sample and hash, then canceled before
the bridge; no invalid interval was interpolated or submitted.

## Visual evidence

While attempt 01 was focused, the user drew repeated trigger-held strokes and
reported "it works" in Russian. This accepts that the live wand trace changed
from recording orange to projection-success green in the headset. It does not
mean a Harry Potter spell was recognized.

## Clean-exit limitation and excluded attempt

After the visual check and all 12 logged attempts, Quest Link entered
`STOPPING -> IDLE` before the requested 4500 rendered frames were reached. The
probe correctly ended the XR session, then waited for another `READY` session
until its 120-second timeout. Consequently it did not execute its final
aggregate `verify_and_report` block and exited with status 1.

This attempt is accepted only as functional bridge and visual evidence. It is
not claimed as a finite clean-exit renderer/accounting pass. Gate A4 remains the
separate accepted clean renderer/input baseline.

A shorter follow-up (`local/gate-a5-live-02.log`) created the instance and
swapchain but the unworn headset stayed `IDLE` for 120 seconds. It rendered no
frames, captured no strokes, and is excluded from runtime acceptance.

## Runtime-tested artifact hashes

```text
tools/xr-runtime-probe/target/debug/wgpu_stereo_clear.exe (12,001,280 bytes)
  884FAF2C9858F043FDB603418BFC414C0C116574CAD955B1F170354151075E25
OpenXR 1.1.61 openxr_loader.dll (2,341,376 bytes)
  866A8A9EF162E91B2ECF321506F584F2AA51A0E7D1C452856F8AE519670231BD
src/wand/src/wand_trajectory.cpp
  471758FF62DD022D0133218304797DE0EAFF93299718E5167CAA20685C87CF31
src/wand/src/wand_trajectory_c.cpp
  A3867317795CC46680B7892CAD6FD3BE925283568C4B2D514C2210C190A4AD28
src/wand/include/hpvr/wand_trajectory_c.h
  ED8CC4D5D857107262A83288A523F9FABCF81489089A2DBBF9EFB529A32D23AF
tools/xr-runtime-probe/src/bin/support/gesture_projection.rs
  8D0CBB5C9AF8781BC7EE9FB9B5F4EC8BA90B51FBDCCFB856B0C959738D3A60AA
tools/xr-runtime-probe/src/bin/support/wand_trajectory_ffi.rs
  56F94C5B5BCE528CF7AB306851FF3070E4BA0F8BAB5AA56ED701676B120A8D22
local/gate-a5-live-01.log (28,286 bytes; ignored local evidence)
  B72EC86DA37BFE27676A9A38155E0D74B80ACDF27C88B36CB4343C9957E4741C
local/gate-a5-live-02.log (14,402 bytes; ignored excluded attempt)
  13230B07983A723F944658F46591E4F870CE03056C89E8A4B70CD6B34F0016CC
```

After the runtime attempts, `tools/verify-baseline.ps1 -StrictHashes`
reported `MATCH` for all seven recorded retail files. The read-only
`C:\Program Files\HP` installation was neither modified nor launched.

## Claim boundary and next gate

This evidence proves that live Quest Link controller strokes can cross the
Rust/C ABI boundary into the shared C++ trajectory core, preserve separately
identified raw/projected endpoints, reject discontinuities before projection,
and drive unambiguous headset feedback. The user visually accepted that path.

It does not prove a clean finite A5 aggregate run, an authored lesson-plane
calibration, template loading, HP1 scoring parity, spell dispatch, retail-game
integration, Android packaging, APK installation, or Quest standalone runtime.

The next semantic gate must load the authored template, draw time, threshold,
and spell identity from the user's legally owned game data, then feed the
projected path to a clean-room-compatible implementation of HP1's existing
gesture comparison. It must not substitute a guessed circle or a second spell
language.
