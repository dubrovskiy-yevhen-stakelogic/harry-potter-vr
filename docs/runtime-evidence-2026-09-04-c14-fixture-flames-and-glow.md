# Gate C14: fixture flames and local glow

> Superseded by C15. Headset review found visible rectangular glow silhouettes,
> excessive generated candle flames, and missing `HProps.Knight` static props.
> C14 remains documented only as a rejected runtime experiment.

## Scope

C14 keeps the headset-accepted C13 BSP lightmaps and restores two missing
source details: visible candle flames and the soft warm haze around lanterns.
Owned meshes and map actors are read at runtime; no game asset is packaged.

## Mesh-derived flame anchors

The earlier C12 pass assigned one guessed vertical offset per fixture actor.
That was wrong for the centered mesh origins and could put effects inside or
above the visible model. C14 derives the wick position from the highest source
vertices of each loaded fixture mesh, then applies the actor draw scale and
yaw. `ThreeArmFloorCandleStickMesh` is split into left, centre, and right bands
and receives three independent emitters.

The accepted owned map now has the following fail-closed totals:

```text
10 TorchFire02 flames
16 SingleCandleStick flames
60 ThreeArmFloorCandleStick flames (20 actors x 3)
86 total flames
90 glow emitters (86 flame glows + 4 LampPost glows)
```

Candle flames use a smaller scale than wall torches and have independent
flicker phases.

## Additive effect pass

The previous effect drawing reused the opaque wand pipeline and discarded the
shader alpha channel. C14 carries push-constant alpha through the wand/effect
shader and creates a separate pipeline with depth testing, depth writes off,
and source-alpha additive color blending. Four nested crossed layers around
each emitter approximate the original soft lantern haze without adding a
full-screen post effect or changing the C13 BSP lighting.

## Evidence

- ARM64 C++ and all four GLSL shaders built with the pinned NDK/CMake/Ninja.
- The signed APK audit requires `gate=C14`, the effect pipeline marker, and the
  combined `flames/glows` diagnostic.
- Final APK SHA-256:
  `689744028B5E109C4729D0CAB3D97AD0960A087A3F2D08475653F4F7F37E9B9A`.
- Installation succeeded and a pulled copy of the installed `base.apk` has the
  exact same SHA-256.
- The agent did not launch the application. Headset appearance remains a user
  runtime acceptance step.
