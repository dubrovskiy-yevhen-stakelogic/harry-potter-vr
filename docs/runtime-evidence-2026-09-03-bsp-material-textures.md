# Gate B9: BSP materials, P8 textures, and GPU texture array

Date: 2026-09-03

## Outcome

Gate B9 replaces the colored BSP diagnostic key with the first clean-room,
read-only path for the map's real static materials. The implementation now:

1. resolves every BSP surface texture reference through the validated package
   graph;
2. decodes direct legacy `Engine.Texture` P8 mip data and its exact
   `Engine.Palette` export from user-owned packages;
3. derives per-corner texel UVs from the BSP surface base point, U/V vectors,
   and pan values before world scaling;
4. transfers vertices and an RGBA8 texture array across a bounded two-call C
   ABI;
5. uploads that array to wgpu and samples the proper repeating layer in the
   stereo shader.

No proprietary pixels or derived asset dump is written into the repository.
Pixels exist only in caller-owned memory and the GPU resource for the process
lifetime.

The first authorized Quest Link run displayed the textured Hogwarts geometry.
The user reported that the castle felt convincing in scale and that the
textures were present. This accepts the `0.02` value for the current static
architecture preview, while final gameplay scale still awaits NPC comparison.

## Material census

The read-only `Lev_Tut1.unr` census found:

```text
surfaces=3658
unique_texture_refs=88
exact_textures=87
unresolved=0
non_base_texture_classes=1
invalid_targets=0
```

The dominant material is
`HP_Basic.Walls.HWStone_B` (export 145): 751 surfaces and 3,755 fan
triangles. The only non-base material is
`HP_C.fx.owlstand1`, class `Fire.FireTexture`: two surfaces and 12 fan
triangles. One of those fan triangles is geometrically degenerate, so the
validated emitted mesh contains 11 fallback triangles.

## P8 decode evidence

`HWStone_B` decoded as:

```text
package_version=76 texture_ref=145 palette_ref=111 format=0
compressed_mips=0 mips=8 top=128x128 rgba_bytes=65536
rgba_fnv1a64=b48491afae077a62 alpha_min=255 alpha_max=255 alpha_zero=0
```

The mip chain is exact: 128x128, 64x64, 32x32, 16x16, 8x8, 4x4, 2x2,
and 1x1. The decoder validates lazy-array skip offsets, power-of-two dimension
bits, indexed byte counts, a direct local palette reference, exactly 256 RGBA
palette entries, payload termination, and bounded allocation.

All 87 direct static textures used by the selected map decoded successfully:

```text
texture_census total=87 ok=87 failed=0
version=69 textures=1
version=73 textures=13
version=76 textures=73
```

The accepted v62+ lazy-array implementation is covered by synthetic v69, v73,
and v76 fixtures. The already observed v61 package layout remains explicitly
outside this texture decoder.

## UV and texture-array contract

For an unscaled Unreal point `P`, BSP surface base `B`, texture vectors `U/V`,
and integer pans `PanU/PanV`, the loader emits texel coordinates:

```text
u = dot(P - B, U) + PanU
v = dot(P - B, V) + PanV
```

UVs remain attached to their corners if handedness correction swaps triangle
winding, and they are invariant under the caller-selected world scale. For the
tutorial map, the complete in-memory scene is:

```text
available_triangles=20003 selected_triangles=20003 omitted_triangles=0
vertices=60009 layers=88 layer_size=256x256 rgba_bytes=23068672
rgba_fnv1a64=49a07fdd85b40ab6 decoded_textures=87
fallback_materials=1 fallback_triangles=11
```

Layer zero is a conspicuous magenta diagnostic checker used only for the one
unsupported dynamic `FireTexture`. The 87 decoded images occupy the remaining
layers. Smaller textures are repeated into the uniform 256x256 layer extent;
the shader repeats normalized BSP UVs and never treats layer zero as a decoded
material.

## Source provenance audit

The legacy serialization shape was checked against the public
`EliotVU/Unreal-Library` repository at commit
`3207a17e9b294be3d1bf26b18e07ccff7e1d4b0c` (2026-08-15):

- repository license: MIT, copyright Eliot van Uytfanghe;
- README explicitly lists the supported Harry Potter PC package/build family;
- no contribution, code-of-conduct, security, or AI/contribution-policy file
  was present in the audited snapshot;
- no source file, binary, or dependency was copied or linked into this project.

The implementation here is an independently written minimal reader constrained
by owned-package evidence and synthetic regression fixtures.

## Validation

- synthetic native P8/palette/UV tests: passed;
- all 87 direct real-map textures: passed;
- complete 20,003-triangle textured scene build: passed;
- C and Rust ABI layout assertions: passed;
- feature-enabled Rust/C++ all-target tests: 39 passed, including the opt-in
  owned-map two-call bridge;
- the texture-array WGSL parses and validates through Naga in the test suite;
- Windows Debug and Release native builds: passed;
- Debug CTest: 2/2 passed;
- Release CTest: 2/2 passed;
- Android NDK r27c ARM64 static-library build: passed;
- default locked Rust all-target tests: 22 passed;
- feature-enabled Windows Release renderer: built without launching it;
- post-change strict golden verifier: 7/7 recorded retail files `MATCH`.

## Scale A/B and observed artifact

The preceding explicitly authorized `0.02` Quest Link run completed all 900
frames with the same 20,003 triangles, PlayerStart placement, clean stereo
projection/motion counters, zero wgpu errors, and zero device loss. The user
reported that `0.02` may be slightly too large but requested that it remain the
provisional value until real textures and NPCs make scale judgement possible.

The textured run and user screenshot retained a dark polygonal hole above the
central Hogwarts crest. Because real textures were otherwise present and the
hole followed head motion, the remaining renderer bounds were audited. The
camera used a fixed 30 m far plane, while the provisional `0.02` map bounds
reach a conservative 153.27 m from PlayerStart to the farthest AABB corner.
The head-relative far plane therefore clipped otherwise valid distant BSP and
exposed the dark clear color.

The renderer now uses a 200 m far plane with the existing `Depth32Float`
target and 0.05 m near plane. Default and feature-enabled test suites pass,
including the asymmetric projection-boundary test, and the updated Windows
Release binary builds cleanly.

The explicitly authorized follow-up reached `FOCUSED`, submitted the first
stereo frame, and logged `far=200.0` with the same 20,003 triangles, 87 decoded
textures, and PlayerStart transform. The user visually confirmed that the
black head-relative polygon was gone. This accepts the far-plane correction.
The Link session then transitioned through `STOPPING`, `IDLE`, and `EXITING`,
but the probe reported `session terminated before clean STOPPING: EXITING`
instead of producing its full requested-frame summary. Therefore visual defect
acceptance is recorded, while a clean 10,000-frame machine completion is not
claimed. The ignored logs are
`local/gate-b9-textured-far200-live-03.log` and its `.stderr.log` companion.

NPC meshes, actor transforms/animation, dynamic `FireTexture` evaluation,
collision, scripts, gameplay, and Quest standalone packaging remain outside
this gate.
