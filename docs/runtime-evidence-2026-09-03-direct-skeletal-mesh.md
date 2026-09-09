# Gate B11: direct UE1 SkeletalMesh and material bridge

Date: 2026-09-03

## Outcome

The accepted B10 Harry visual no longer requires a developer-exported PSK or
TGA directory. The clean-room loader now reads the bind-pose mesh, material
mapping, skeleton census, and four P8 textures directly from the user's owned
`HarryPotter.u`. The source package remains read-only and all decoded bytes
remain in caller-owned memory.

This gate proves offline decoding, triangle construction, texture-array
packing, and the Rust/C ownership boundary. It does **not** claim a new headset
render: OpenXR was deliberately not started. Visual parity with B10 remains the
next explicit Quest Link acceptance run.

## Format boundary

`inspect_hp1_skeletal_mesh_census(path, reference)` accepts only a positive
local export whose direct class is `Engine.SkeletalMesh`. For UE1 package
version 76 it bounds and validates the complete serialized chain:

- `UPrimitive`: box and sphere;
- `UMesh`: fixed-size lazy arrays, animation-sequence framing, texture
  references, bounds, counts, and mesh transform metadata;
- `ULodMesh`: collapse arrays, byte-UV wedges, faces, materials, special-face
  framing, and LOD metadata;
- `USkeletalMesh`: points, reference bones, bone-weight spans, local points,
  animation identity, and weapon adjustment.

Every `TLazyArray` absolute end offset must equal the checked package position
plus `count * element_size`. References, face/wedge/material indices, point
indices, bone-weight spans, parent indices, finite floats, and the zero-byte
payload tail are all checked before success.

The serialization layout was cross-checked against the official MIT-licensed
UE Viewer repository at local commit
`a0bfb468d42be831b126632fd8a0ae6b3614f981`. No third-party engine code was
copied into this project.

## Owned-package evidence

Read-only probe:

```powershell
.\build\wand\src\wand\Release\hpvr_hp1_skeletal_mesh_probe.exe `
  'C:\Program Files\HP\system\HarryPotter.u' 1341
```

Observed `skharryMesh` results:

- package version: 76;
- points / LOD wedges / faces: 509 / 738 / 988;
- triangle-stream vertices: 2,964;
- materials: 4, using texture slots 0, 1, 2, and 3;
- bones / bone-index records / weights: 92 / 92 / 732;
- skeletal depth: 13;
- animation reference retained, not followed: 907;
- direct mesh bounds at 0.02 m per Unreal unit:
  `(-0.803534, 0.00735962, -0.213864)` to
  `(0.803376, 1.65771, 0.192583)` metres.

Direct P8 material decode from the same package:

| Material | Reference | Object | Source size | RGBA bytes |
| --- | ---: | --- | ---: | ---: |
| 0 | 1364 | `skharryTex0` | 256x256 | 262,144 |
| 1 | 1365 | `skharryTex1` | 256x256 | 262,144 |
| 2 | 164 | `skharryTex2` | 128x128 | 65,536 |
| 3 | 918 | `skharryTex3` | 256x128 | 131,072 |

`build_hp1_skeletal_triangle_mesh` reproduces the accepted legacy handedness
conversion in memory: the UE1 X mirror and first-two-corner swap are composed
with the project's Unreal-to-OpenXR axis map. No derived mesh is written.

## Runtime bridge

`hpvr_hp1_load_skeletal_mesh_utf8` is a versioned, two-call C ABI. A null-buffer
query reports exact vertex and RGBA capacities; the second call writes only
after both buffers are large enough. The four material images are nearest
resampled into a 256x256 RGBA8 array, matching the B10 diagnostic renderer.

New opt-in runtime argument:

```text
--hp1-npc-package-preview <package> <export-reference> <meters-per-unit>
```

The old `--hp1-npc-preview <psk> <texture-directory> ...` path remains only as
an A/B oracle for the next visual comparison.

Offline Release validation used the full textured hall plus direct Harry:

```powershell
.\tools\xr-runtime-probe\target\release\wgpu_stereo_clear.exe `
  --hp1-map-slice 'C:\Program Files\HP\maps\Lev_Tut1.unr' 0.02 100000 `
  --hp1-player-start 0 `
  --hp1-npc-package-preview `
    'C:\Program Files\HP\system\HarryPotter.u' 1341 0.02 `
  --validate-assets-only
```

Result:

```text
[hp1.npc.load] points=509 wedges=738 faces=988 vertices=2964 materials=4 texture_bytes=1048576
[hp1.assets.validate] PASS openxr_started=0
```

## Verification

- CMake/MSVC Release build with `/W4 /WX`: passed;
- CTest: 2/2 passed;
- synthetic UE1 SkeletalMesh success case: passed;
- corrupt lazy-array offset fail-closed case: passed;
- invalid import-as-mesh reference case: passed;
- direct triangle axis/winding/UV/material checks: passed;
- Rust default suite: 25/25 passed;
- Rust `gesture-projection` suite: 43/43 passed, including the new ABI layout
  test;
- Release offline owned-package validation: passed with `openxr_started=0`;
- strict golden verifier: all 7/7 recorded retail files `MATCH`.

## Remaining acceptance

## Quest Link acceptance

The user explicitly launched the Release candidate with
`--hp1-npc-package-preview` and confirmed in-headset that Harry was present and
looked the same as the accepted B10 PSK/TGA rendering: "разницы нет". This
closes visual parity for the direct package path.

The run reached Oculus runtime 1.207.0, `FOCUSED`, and submitted the first
stereo frame. Logged render input was 62,973 vertices / 20,991 triangles with
FNV-1a64 `e07fa829daf80eaf`; the NPC contribution remained 2,964 vertices / 988
triangles and four 256x256 texture-array layers. The session later reported
`STOPPING`, `IDLE`, and `EXITING`, but the probe still emitted its known
terminal-state diagnostic instead of the aggregate clean-run summary. Visual
acceptance is therefore confirmed; a clean terminal-counter claim is not made.

Next work is to replace `DIAGNOSTIC_FRONT` placement with the real level actor
transform and then decode the referenced skeletal animation rather than leave
Harry in bind pose.
