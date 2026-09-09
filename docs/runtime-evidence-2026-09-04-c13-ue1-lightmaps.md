# Gate C13: UE1 BSP lightmaps

## Scope

C13 replaces the provisional per-vertex BSP illumination with the original
Unreal Engine 1 lightmap data from the user-owned `Lev_Tut1.unr`. The retail
installation remains read-only and no Unreal package is embedded in the APK.

## Decode and reconstruction

The bounded Model decoder now retains and validates:

- every `FLightMapIndex` data offset, pan, clamp, scale, and light-list start;
- the complete `Model.LightBits` byte array;
- the zero-terminated `Model.Lights` actor-reference spans;
- every surface-to-lightmap reference.

For every selected BSP surface, the portable scene builder reconstructs
lightmap UVs independently of the base material UVs. It resolves the texel
position on the source plane, evaluates the referenced authored light color,
brightness, radius, angular response, distance response, and the original
shadow bit. A normalized 3x3 filter softens the one-bit visibility edges.

The 3616 used lightmaps are packed with replicated one-pixel gutters into one
1024x1024 RGBA8 sRGB atlas. Base textures stay in their existing 256x256 array,
so the new lighting costs 4 MiB instead of inflating all 88 material layers.

## Renderer

`GpuVertex` now carries an independent lightmap UV and validity flag. The
Hogwarts pipeline binds the base `sampler2DArray` at binding 0 and the clamp-to-
edge lightmap `sampler2D` at binding 1. BSP fragments multiply the decoded base
color by the sampled texel light; dynamic meshes without a BSP lightmap keep
the bounded actor-light fallback.

## Evidence

Host tests passed. The owned-data probes reported:

```text
light_maps=3616 light_bits=567924 light_refs=110081
lightmap_max=84x84 lightmap_texels=110550 lit_maps=3330
max_lights_per_map=119
lightmap_atlas=1024x1024 lightmap_bytes=4194304
decoded_lightmaps=3616 lightmap_lights=11892
lightmap_luminance=80:113:255 lightmap_samples=201814
available_triangles=20003 selected_triangles=20003 omitted_triangles=0
```

The Android Gradle Java/Dex wrapper hit a host ACL/snapshot failure on its own
generated `R.jar`. The ARM64 native target was therefore configured and built
directly with the same pinned NDK 27.2.12479018, CMake 3.22.1, Ninja, API 32,
and cached OpenXR 1.1.43 loader. It compiled all C++ sources and all four GLSL
shaders successfully. The existing verified clean APK container was then
repacked with only the new `libhpvr_quest.so`, zip-aligned, and debug-signed.

Final offline audit:

```text
status=PASS
sha256=6BA76FA6423FB6423EDC1F32A4E63A8404DA53B885C30B5AE677DCF6BD49BF30
abi=arm64-v8a
proprietary_assets=0
ue1_lightmaps=3616 atlas=1024x1024 shadow_bits=DECODED
signature=VALID alignment=VALID
```

The Quest was not visible to ADB at packaging time, so C13 was not installed or
launched in this evidence pass. Headset appearance remains runtime acceptance,
not a build claim.
