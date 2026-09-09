# Quest Link external Flipendo scoring evidence

- Date: 2026-09-02
- Scope: PCVR Quest Link diagnostic only; no retail game launch, package
  extraction, Quest APK installation, or standalone launch
- Result: Gate A5b external-template loading, live scoring, and user visual
  acceptance passed; a finite clean-exit aggregate run remains pending
- Starting commit: `081ab6f`

## Claim

Gate A5b proves that a live Touch Plus wand stroke can be projected into the
coordinate system of the shipped `FlipPattern`, scored with the behavior
observed in the shipped `Engine.dll`, compared with the compiled first-lesson
threshold, and presented as an unambiguous accepted/rejected result in Quest
Link.

The template and lesson policy are read directly from the user's external,
legally owned packages at startup. No authored points, packages, executables,
or derived asset dumps are tracked in this repository. Spell dispatch remains
disabled in this gate.

## Direct clean-room evidence

The implementation was derived from read-only inspection of the golden US PC
installation, not from imported engine or game source. The relevant inputs are:

```text
system/HPBase.u
  SHA-256 30B5EF44E9755AA9C020BE9D863E35335C26C2D6988FD0A00A347A98C44E105D
system/Engine.u
  SHA-256 B3661A1D2AFB1F730BA5BB3CBD2A6716EFAF55FD0D8CEFA753B69026A8BC5A85
system/Engine.dll
  SHA-256 7756A2A3DF7198D72F4706952196BEE8ADB3B79EDFE7C8B3A5E4D2E3593D8EBC
maps/Lev_Tut1.unr
  SHA-256 C3A23B396E67FB43ED6E018944A60496B19EE8A15D5253EBF26029ABACA150F7
```

The package reader independently validated the UE1 package magic, version
`76/0`, name/import/export table boundaries, compact indices, object stacks,
property tags, dynamic vector/int arrays, class defaults, and actor overrides.
Object identity is checked as qualified `Core.Class` imports with the expected
outer path, including `Engine.Gesture`, `HPBase.SpellLearnTrigger`, and the
requested `HPBase` spell class. The parser fails closed on malformed ranges,
unsupported versions, ambiguous objects, incomplete policy, and non-finite
values.

Read-only package inspection established:

- `FlipPattern` is an `Engine.Gesture` containing 20 authored points;
- `SpellLearnTrigger0` in `Lev_Tut1.unr` selects `spellFlip`;
- the class default accuracy radius is `0.03`;
- the actor overrides `DrawTime` from `6` to `12` seconds;
- compiled `PassMark[0]` is `0.50`;
- 500 temporal lesson slots therefore produce a `24,000,000 ns` period.

The compiled defaults are authoritative. A differing value visible in embedded
ScriptText is treated as source text, not proof of active serialized defaults.

`Engine.u` assigns native indices 426 and 427 to `CompareGesture` and
`CompareGesturePoint`. In the golden `Engine.dll`, their export thunks lead to
the implementation around `0x103A3F60` and `0x103A3CC0`; the shared scoring
helper is around `0x103A39C0`. Disassembly established the following behavior:

- consume at most 1024 input slots;
- discard the exact legacy `Z == -1` unused-slot marker;
- remove global XY duplicates when both component deltas are strictly below
  `0.001`;
- densify every neighboring template pair with seven points at eighths;
- compute symmetric nearest-point coverage with a maximum distance of `1.0`;
- apply the two shipped rounded penalty formulas, capped at 2 and 3;
- add `500` to penalty and total when fewer than ten input points survive;
- clamp `1 - penalty / total` to `[0, 1]`, with an empty total returning zero.

`Segments` is parsed and reported as package metadata but is not consumed by
the observed native scorer. The exact scorer entry point retains the legacy
Z sentinel. The VR convenience entry point submits compacted XY projection
points with `Z=0`.

OpenHP1 remains an untracked, reference-only candidate because its root license
texts and full provenance package are unresolved. No OpenHP1 code was copied or
vendored for Gate A5b.

## Implementation

The platform-neutral C++20 layer now contains:

- a narrow, bounds-checked package `76/0` profile loader;
- the clean-room gesture scorer and 500-slot period calculation;
- a versioned C ABI with caller-owned point/segment buffers, capacity queries,
  fixed layouts, and exception containment;
- a metadata-only profile probe which never prints authored coordinates.

The Rust Link host adds:

- `--flipendo-data-root <installation-root>`;
- checked Rust/C layouts and UTF-8 path transfer;
- external profile loading from `system/HPBase.u` and `maps/Lev_Tut1.unr`;
- a `0.42 m` calibration plane whose first authored point is anchored to the
  physical press tip;
- a cyan template overlay while recording, orange live trace, green accepted
  trace, red rejected trace, and amber cancellation marker;
- per-attempt score, threshold, outcome, counts, and hashes.

The `0.42 m` extent is an explicit VR calibration value, not authored game
data. The user reported that the resulting interaction worked as expected, but
the value remains labelled `CALIBRATION_REQUIRED` until broader ergonomic
testing.

## Static and cross-platform verification

The final tree passed:

```text
MSVC C++20 /W4 /WX build: passed
CMake/CTest: 2/2 passed
Cargo default feature-off tests: 17 passed; 0 failed
Cargo gesture-projection tests: 32 passed; 0 failed
Opt-in Rust -> C ABI -> golden package test: passed
Release wgpu_stereo_clear build: passed
Android NDK r27c arm64-v8a static-library build: passed
Pure-C header/layout compile target: passed
git diff --check: passed
```

Synthetic tests construct public in-memory UE1 `76/0` fixtures with qualified
imports, class defaults, actor overrides, arrays, and object stacks. No retail
binary fixture or authored point array is checked in. Tests cover strict object
identity, profile merge, missing objects/files, C ABI ownership, short buffers
without partial writes, sentinel ordering, duplicate removal, template
densification, score penalties, period calculation, template anchoring, and
dynamic overlay capacity.

## Live Quest Link run

The runtime-tested command was equivalent to:

```powershell
.\tools\xr-runtime-probe\target\release\wgpu_stereo_clear.exe `
  --loader C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll `
  --frames 9000 --flipendo-data-root 'C:\Program Files\HP'
```

The external profile loaded before the session rendered:

```text
profile=ok, package versions=76/0
template points=20, segment metadata values=32
accuracy=0.03, draw time=12 s, period=24,000,000 ns
lesson level=0, pass mark=0.50
```

Seven release attempts crossed the complete live path. All seven projection
and scorer calls returned `ok`; no score bridge rejection occurred:

```text
attempts scored: 7
accepted: 2
rejected: 5
accepted scores: 0.592798, 0.802432
closest rejected score: 0.474860
accepted raw samples: 368 and 425
accepted projected points: 214 and 247
invalid/non-monotonic projected samples: 0
```

The higher accepted attempt retained 242 unique points and compared them with
153 densified template points. The user then reported in Russian that the full
interaction worked as expected. This accepts the headset-visible template,
trace, and accepted/rejected color behavior in addition to the logged scores.

The ignored local evidence is:

```text
local/gate-a5b-live-01.log (18,861 bytes)
  SHA-256 4B412A8851D446B9471BDAB89468E14B31873AB50B9B1BC606EC72E574E1E19A
local/gate-a5b-live-01.err.log (804 bytes)
  SHA-256 67796BEB016919959D451A7A29CF19E3E6AE88FFD311BF24782172465535EF2C
```

The stderr file contains eight non-fatal Oculus bound-source path warnings and
no wgpu, Vulkan, device-loss, profile-loader, projection, or scorer error.

## Clean-exit limitation

After the visual/scoring work, Quest Link transitioned through
`VISIBLE -> SYNCHRONIZED -> STOPPING -> IDLE`. The run did not reach the
requested 9000 rendered frames and contains no final aggregate or
`probe exited cleanly` marker. It is accepted as current functional and visual
Flipendo-scoring evidence, not as a finite clean-exit renderer/accounting pass.
Gate A4 remains the separate 9000-frame clean renderer/input baseline.

The strict read-only baseline verifier reported `MATCH` for all seven golden
retail files before and after the work. The retail executable was not launched
and the installation was not modified.

## Boundary and next gate

Gate A5b proves authored-template loading and live original-semantic scoring.
It does not prove a spell event, target selection, gameplay effect, retail-game
integration, APK packaging, or Quest standalone runtime.

The next gate is an original `FlipendoAccepted` event consumed by a portable
test-world target. It must retain the score and aim evidence, emit exactly once
per accepted release, never fire on rejection/cancellation, and remain separate
from any eventual HP1 gameplay-runtime adapter.
