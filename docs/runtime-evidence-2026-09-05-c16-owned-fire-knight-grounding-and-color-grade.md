# Gate C16: owned fire, grounded knights, and display grading

Date: 2026-09-05

## Scope

This gate addresses three headset findings from C15 without modifying the
retail installation:

- the column fire looked like a small stack of solid yellow blocks;
- all six restored knights floated and the two upper knights faced the wall;
- Quest midtones and highlights were visibly darker than a PC capture.

## Source evidence

The clean-room class-default probe was extended to expose serialized property
records without embedding their payloads. `HPParticle.TorchFire02` resolves its
texture reference to `HPParticle.PotFire08`. The owned texture probe reports a
64x64 indexed texture with an opaque palette, matching the additive-blend
particle path used by the original effect.

The renderer now decodes that texture directly from the user's
`System/HPParticle.u`, resamples it into the runtime texture array, and draws
five staggered crossed sprites per each of the 10 authored `TorchFire02`
locations. No proprietary image is present in the source tree or APK.

Knight placement now searches walkable BSP triangles from each serialized
actor location, selects the highest valid surface below it, and offsets the
mesh by its measured lower bound. A 180-degree correction is limited to the two
landing actors whose serialized yaw is approximately zero. The four side-wall
actors retain their authored opposing quarter-turns.

## Screenshot measurement

The three C15 Quest captures had mean luminance 59.18-65.73 and 90th-percentile
luminance 93.88-103.72. The PC reference measured 82.94 mean and 154.45 at the
90th percentile. Quest mean saturation was 191.57-194.96 versus 163.06 on PC.
This rules out a missing saturation boost and supports a missing display
brightness/gamma stage. C16 applies exposure 1.35 and exponent 0.92 after
texture/lightmap multiplication; the existing sRGB swapchain conversion is
retained.

## Build and package evidence

- Windows host build: PASS
- Host tests: 7/7 PASS
- Android ABI: `arm64-v8a`
- Native target: `hpvr_quest` PASS
- APK audit: PASS
- Signature: APK Signature Scheme v3 PASS
- APK proprietary asset count: 0
- APK SHA-256:
  `9B8BE8459D76A2789F0A12E5663ED068A0A7CABADB45543E67AAC81D2C43C5E5`

The audit requires `gate=C16`, the owned `PotFire08` runtime loader, the
dedicated particle Vulkan pipeline, six grounded knights, two corrected landing
knights, and the color-grade marker.

## Device evidence

- Device: Meta Quest 3 (`2G0YC1ZF760BPC`)
- `adb install -r`: Success
- Installed base APK SHA-256:
  `9b8be8459d76a2789f0a12e5663ed068a0a7cabadb45543e67aac81d2c43c5e5`
- Source/installed hash: MATCH
- App process after install: NOT RUNNING

## Acceptance boundary

Build, package, audit, installation, and installed-byte identity are proven.
The agent did not launch the application. Correct flame appearance, knight
contact/orientation, and PC-like brightness still require the user's current
headset review.
