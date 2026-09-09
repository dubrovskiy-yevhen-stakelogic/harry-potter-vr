# ADR-0001: portable HP1 runtime foundation

- Date: 2026-09-02
- Status: reference candidate; wholesale/public fork is currently no-go

## Context

The verified retail runtime is Unreal Engine build 433, PE32 x86, and uses a
Direct3D 7 render device. It contains no Android, ARM64, OpenXR, Vulkan, engine
SDK, or native source tree. A DLL hook can help observe the PC game, but it
cannot become a Quest 3 standalone binary.

The original packages do expose the right gameplay seams. `Engine.Gesture`
owns authored points and native `CompareGesture` / `CompareGesturePoint`
functions. `SpellLearnTrigger` records a normalized wand path, while
`baseWand`, `baseSpell`, targeting, and `Harry.PlayerCalcView` retain the
original casting and camera behavior.

## Decision

Build the product around a portable, clean-room HP1 runtime which reads a
user-owned installation externally. Keep the retail Windows game as a
read-only behavioral oracle. Do not make x86 injection the product foundation.

OpenHP1 is the leading candidate runtime, evaluated at local checkout commit
`d1e9107`. It is substantially closer to the goal than a generic UE1 port: the
workspace already separates package loading, UnrealScript execution, runtime,
scene, renderer, audio, and game host, and its runtime contains HP1 gesture and
spell-lesson support.

No OpenHP1 code is vendored or copied by this decision. Adoption requires both:

1. actual license texts or an unambiguous grant from the copyright holder; and
2. a provenance review that finds no proprietary game code or assets in the
   source we would distribute.

The current checkout declares `MIT OR Apache-2.0` in `Cargo.toml`, but tracks
no `LICENSE`, `COPYING`, or `NOTICE` file, and no such root license existed in
its Git history. Cargo metadata communicates intent, but it is not an acceptable
release-quality provenance package on its own.

The audit found no raw game packages, executables, audio, or disassembly dumps
in the full history and no sign of leaked native source. It did find material
that must not enter a public fork: 62 committed window masks derived from
original textures, HP-style branding, bundled agent skills without an included
license, and an Intel-derived shader without the full third-party notice which
the upstream documentation itself requires.

Until upstream supplies root license texts, a clear copyright holder and
provenance statement, complete third-party notices, and a public
trademark/non-affiliation notice, use it only as a read-only architecture and
behavior reference. Any later adoption must exclude `banner.png`, `splash.jpg`,
`window_masks/`, `window_editor.state.json`, `.agents/`, and `skills-lock.json`.
Derived masks must be generated locally from each user's own installation and
remain untracked.

## Rejected primary paths

### Retail x86 hook

Useful for diagnostics and comparison, but tied to PE32, Direct3D 7, Windows
input, and the copy-protection-era executable. It would create a disposable PC
implementation before the actual ARM64 work.

### Old UE1 engine forks

The examined mobile forks target much earlier UE1 revisions and carry either
version-compatibility or source-provenance risk. HP1 build 433 and its native
game classes would still require a large compatibility effort.

### Reusing another game's APK injection path

The working GTA SA Quest pattern depends on an existing ARM64 native game
library. HP1 has no such library, so split-APK injection cannot supply the
missing engine.

## Product gates

1. **Legal/provenance:** license grant, notices, trademark/non-affiliation text,
   and a clean tracked-file audit.
2. **Reproducible desktop:** install Rust without weakening the machine's
   existing toolchains; `cargo check` and focused tests pass; the external HP1
   folder loads without modification.
3. **Renderer seam:** prove that the existing wgpu renderer can target OpenXR
   swapchain images without a full-frame copy. Stop if this requires maintaining
   a second renderer.
4. **PC OpenXR slice:** one room, two eyes, head pose, correct world scale,
   tracked Touch controller rendered as the wand, and timing telemetry.
5. **Original spell slice:** one lesson uses its shipped template and native
   comparison semantics, then dispatches the original spell interaction.
6. **Android host:** ARM64 cold start, external/imported legal game-data flow,
   audio, save path, lifecycle, and diagnostics.
7. **Quest acceptance:** current headset evidence at 72 Hz or better before
   claiming standalone success.

## Intended module boundary

The portable game/runtime remains independent of OpenXR. A small XR host owns
session lifecycle, predicted display time, views, controller/hand actions, and
swapchains. It publishes head and wand samples through a platform-neutral input
contract. The game host converts those samples into camera, targeting, casting,
and lesson inputs. Quest hand tracking is an optional input provider, not a
dependency of gesture scoring.
