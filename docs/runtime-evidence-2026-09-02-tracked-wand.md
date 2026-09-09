# Quest Link tracked-wand evidence

- Date: 2026-09-02
- Scope: synthetic PCVR wand input and rendering through Quest Link; no retail
  game launch, proprietary data access, Quest APK install, or standalone launch
- Result: Gate A4 machine invariants passed and the user visually accepted the
  live wand interaction
- Starting commit: `1a023695074873f554ecb3f5887eeb3343886321`

## What was tested

The opt-in `--wand` mode extends the accepted Gate A3 metric room. It creates
one OpenXR action set for the right Touch Plus controller, suggests bindings
for the OpenXR 1.0 extension profile
`/interaction_profiles/meta/touch_controller_plus` and the Oculus Touch
fallback, attaches once, and synchronizes once per begun XR frame.

Grip pose, aim pose, analog trigger, and a trigger-derived boolean cast action
are sampled at the same predicted display time and in the same `LOCAL` space as
both eyes. The original procedural visual is a brown tapered wand, cyan raw-aim
ray, bright tip, and orange trigger stroke. Provisional grip-to-prop calibration
is identity rotation/translation, length `0.340 m`, and forward `-Z`; it is not
the final hand-authored Quest controller mount.

Only valid and tracked grip/aim positions and orientations in a focused session
produce a wand. Tracking loss hides it and cancels a live stroke. A new cast
requires an observed release. Strokes reject gaps over `100 ms` and jumps over
`0.25 m`, so they cannot bridge a tracking discontinuity.

One immutable dynamic-geometry snapshot is uploaded once per begun frame and
drawn from the same range in both eyes. Runtime checks cover the fixed
12,288-vertex capacity, finite values, snapshot hashes, duplicate uploads,
per-eye draws, and teardown.

This gate does not call the separate `hpvr_wand` gesture library and does not
recognize a Harry Potter spell yet. It proves the live input and stroke source
that a recognizer can consume next.

## Reproduction and build checks

With Quest Link active:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe build --locked `
  --bin wgpu_stereo_clear
& .\target\debug\wgpu_stereo_clear.exe `
  --loader C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll `
  --frames 9000 --wand
Pop-Location
```

The final source passed `cargo fmt --all -- --check`, `cargo check`,
`cargo test --all-targets`, and a debug build. All 17 tests passed. They cover
projection/IPD, procedural geometry and buffer capacity, stroke release and
new-press boundaries, time gaps and teleports, trigger rearming, inactive cast
cancellation, controller-profile spelling, mount calibration, and pending
`LOCAL` reference-space changes.

## Runtime evidence

The accepted run used Oculus runtime `1.207.0`, an RTX 4090, and a
`2064x2272x2` sRGB swapchain. It traversed:

```text
IDLE -> READY -> SYNCHRONIZED -> VISIBLE -> FOCUSED
FOCUSED -> VISIBLE -> SYNCHRONIZED -> STOPPING
```

The first tracked sample and rendered frame reported:

```text
[wand.first] grip=(-0.488,-0.557,0.453) aim=(-0.508,-0.574,0.549)
root=(-0.488,-0.557,0.453) tip=(-0.434,-0.404,0.752)
trigger=0.000 dynamic_vertices=144
[geo.first] tick=58 dynamic_vertices=144 trail_points=0
first Gate A4 frame submitted; room and provisional wand use LOCAL space
```

The clean-exit summaries were:

```text
session_begin=1 session_end=1
frame_begin=9057 frame_end=9057 rendered=9000 skipped=57
acquire=9000 wait=9000 submit=9000 gpu_complete=9000 release=9000
max_outstanding=1 events_lost=0 reference_space_changes=0
per_image=[3000, 3000, 3000]

[geo.scheduler] ticks=9057 frames_begun=9057 max_ticks_per_frame=1
snapshot_mismatches=0 draws_left=9000 draws_right=9000 PASS
[geo.projection] samples=9000 boundary_max_error=0.00000006
quaternion_norm_max_error=0.00000012 ipd_min/avg/max=0.06667m PASS
[geo.motion] translation_span=(0.0913,0.0935,0.0645)m
rotation_span=(53.67,40.70,20.84)deg reference_space_changes=0 PASS

[wand.render] snapshots=9057 visible_ticks=7327 hidden_ticks=1730
dynamic_rendered=7327 uploads=7327 duplicate_uploads=0
max_uploads_per_frame=1 draws_left=7327 draws_right=7327
snapshot_hash_mismatches=0 capacity_overflows=0 nonfinite_vertices=0 PASS
[wand.trace] started=17 completed=17 canceled=0 points_recorded=1434
points_trimmed=64 gap_breaks=0 teleports_rejected=0 gap_bridges=0
open_strokes=0 PASS
[wand.input] begun=9057 sync=9057 nonfocused=57
grip_active=7840 aim_active=7840 trigger_active=7840 cast_active=7840
tracked=7327 inactive=1160 invalid_flags=513 presses=17 releases=17
cancels=0 tracking_losses=6 PASS
[wand.pose] samples=7327 grip_tip_min/avg/max=0.3400/0.3400/0.3400m
quaternion_norm_max_error=0.00000012 trigger_range=0.000..1.000
nonfinite=0 invalid_values=0 nonmonotonic=0 PASS

[wand.result] runtime_invariants=PASS controller_tracking=PASS
visual_acceptance=PENDING gesture_recognition=NOT_IMPLEMENTED
external texture wrappers dropped non-owningly: 3/3
final wgpu runtime errors: 0; device losses: 0
Gate A4 tracked-wand probe exited cleanly
```

`visual_acceptance=PENDING` is emitted because the process cannot certify what
the wearer saw. The user's report below supplies that separate evidence.

## Oculus bound-source diagnostic quirk

An earlier active-headset diagnostic obtained a valid sample and submitted its
first A4 frame, then aborted while optional telemetry passed an enumerated
bound-source path to `xrPathToString`. Oculus `1.207.0` returned
`XR_ERROR_PATH_INVALID` for that conversion even though action states and poses
were live.

The final build makes only interaction-profile and bound-source naming
best-effort. In the accepted run the current profile was successfully reported
as `/interaction_profiles/meta/touch_controller_plus`; 24 bound-source string
conversions produced warnings while rendering continued. Profile-query and
bound-source-enumeration errors were zero. Action creation, attachment,
synchronization, state reads, and pose location remain checked and fatal.

Two other attempts never reached `READY` because the unworn headset reported
`Asleep`; they rendered no frames and are excluded. One launch without an
explicit loader path failed before creating an OpenXR instance and is excluded.

## User visual evidence

While the accepted run was focused, the user was instructed to move the right
controller, draw a circle while holding the trigger, release, and draw a
separate zigzag. The user reported `работает` (`it works`). This accepts the
visible live wand interaction. The runtime independently recorded 17 distinct
press/release pairs and 17 completed strokes with no gap bridge.

The report does not calibrate the exact physical grip offset or accept final
wand artwork. Those remain future UX work.

## Runtime-tested artifact hashes

```text
tools/xr-runtime-probe/src/bin/wgpu_stereo_clear.rs (53,623 bytes)
  CB45DB5993053262F193CBA8EB0DFB33C7B23B79BF0810F5C9A0967DB93F19E4
tools/xr-runtime-probe/src/bin/support/calibration_scene.rs (59,731 bytes)
  BDA468980D67F98070A58E12C27F5FB525822D86B92674F7B2992609A1CD80E1
tools/xr-runtime-probe/src/bin/support/wand_input.rs (30,810 bytes)
  BA4BD24BA50F9A1029FECF483541B2537C47C0EDB23CC367142B65D69C758E81
tools/xr-runtime-probe/src/bin/support/calibration_scene.wgsl (1,384 bytes)
  D46D7EF4BF7E6CC78DBFDB703D2DBF427CFBB29B8ED0BB7A9F63B7918A187AB7
tools/xr-runtime-probe/Cargo.toml (415 bytes)
  5D2A09D6C1067A7DE212CD8AED67A8FFBB6D05E5954DF312E5A122176714EBF4
tools/xr-runtime-probe/Cargo.lock (25,719 bytes)
  B3BE7CCF00AEE9294155DEA59C1578BC8BB40D587E15E8F6E35DDFACBCB5E301
tools/xr-runtime-probe/target/debug/wgpu_stereo_clear.exe (11,910,656 bytes)
  F7CE07A99466FDA7E931B988E86EE6E792FDB9503194607D9CD5F749E381251B
openxr_loader.dll 1.1.61 (2,341,376 bytes)
  866A8A9EF162E91B2ECF321506F584F2AA51A0E7D1C452856F8AE519670231BD
local/gate-a4-live-07.log (22,523 bytes; ignored local evidence)
  077185680E8500813119B9A4D5FFCC21E88D6513757AD3AF1FD22261E8D32FE3
```

After the run, `tools/verify-baseline.ps1 -StrictHashes` reported `MATCH` for
all seven recorded retail files. The golden installation was not modified or
launched.

## Claim boundary

This run proves that Quest Link/Oculus PC runtime supplied live right-controller
grip, aim, and trigger state; the probe derived a metrically stable wand tip,
rendered one shared dynamic snapshot into both eyes, retained separate trigger
strokes without discontinuity bridges, and shut down with balanced XR/GPU
ownership. The user visually accepted that the interaction worked.

It does not prove production grip calibration, hand tracking, haptics, spell
recognition, a game scene, retail-engine integration, Android ARM64 packaging,
APK installation, or Quest 3 standalone behavior.
