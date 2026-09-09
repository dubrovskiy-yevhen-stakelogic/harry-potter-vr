# Gate B10: actor identity, class visual defaults, and first NPC preview

Date: 2026-09-03

## Outcome

Gate B10 now reaches a real HP1 character asset without constructing or
executing any shipped UObject or UnrealScript code. The owned `Lev_Tut1.unr`
actor array resolves through serialized class defaults to exact
`Engine.SkeletalMesh` identities. A temporary, developer-only ActorX/TGA path
can add the real textured Harry mesh to the existing textured BSP preview for
the next explicit headset run.

This is an offline/build result. No OpenXR session was started for this gate
while collecting the evidence below, and no NPC visual result is claimed yet.

## Owned actor census

The bounded Level actor census reported:

```text
actor_slots=2046 nonnull_actors=2011 null_slots=35 classes=71 serialized_bytes=249043 missing_exports=0
```

Observed tutorial character classes include `Tut1.Tut1Fred`,
`Tut1.Tut1George`, `Tut1.Tut1Ron`, `Tut1.Tut1Hermione`,
`Tut1.Tut1Dumbledore`, `Tut1.Tut1McGonagall`, `Tut1.Tut1Quirrell`,
`Tut1.tut1Peeves`, `HarryPotter.Harry`, `HarryPotter.FILCH`, and the generic
student classes.

The instance-only visual pass found all 15 matched `Tut1.*` character actors
located and no actor-level `Mesh` overrides. It also found 16 matched
`HarryPotter.*` actors located and no actor-level `Mesh` overrides. That is
positive evidence that these identities obtain their meshes from UClass
defaults rather than a reason to synthesize or guess a model.

One exact example is:

```text
actor_ref=1296 slot=580 class=Tut1.Tut1Hermione object=Tut1Hermione0
location_unreal=1932.79,-6259.43,903.6 rotation_units=0,6264,0
mesh_serialized=0
```

## UClass default-property decoder

`inspect_hp1_class_visual_defaults` decodes only one validated local UClass
export header and its bounded property-tag stream. It currently fails closed
when the class contains non-empty bytecode; no script bytecode is interpreted.
It retains the superclass reference/path and only visual defaults needed by
the next stage: `DrawScale`, `Mesh`, `Skin`/`MultiSkins`, `AnimSequence`,
`DrawType`, and `bHidden`.

Exact owned-package results:

```text
class_visual_status=ok version=76 class_ref=183 class=Tut1Hermione
super_ref=-4 super_path=HPBase.baseChar properties=3
mesh_ref=-160 mesh_path=HarryPotter.skhermioneMesh mesh_serialized=1
draw_type=2 draw_type_serialized=1

class_visual_status=ok version=76 class_ref=44 class=Harry
super_ref=-7 super_path=HPBase.baseHarry properties=32
mesh_ref=1341 mesh_path=skharryMesh mesh_serialized=1
draw_type=2 draw_type_serialized=1
```

The package linker independently resolves Hermione's imported mesh identity:

```text
detail_source=Tut1 source_ref=-160
source_path=HarryPotter.skhermioneMesh class=Engine.SkeletalMesh
target_package=HarryPotter target_ref=561 target_path=skhermioneMesh
target_kind=2
```

Harry is the closest confirmed character in the collected class subsets to
the selected PlayerStart: about 805.49 Unreal units, or 16.11 m at the accepted
provisional `0.02` scale. For the first scale/material inspection, the preview
intentionally places his bind-pose mesh directly in front of the viewer instead
of claiming recovered gameplay placement.

## Developer-only mesh oracle and preview cache

The public UE Viewer repository was cloned outside this repository at pinned
commit `a0bfb468d42be831b126632fd8a0ae6b3614f981`. Its `LICENSE.txt` is MIT.
Its UE1 serializer successfully read `HarryPotter.skharryMesh` and the four
linked textures while animation decoding was deliberately disabled. It was
used as a format oracle and to generate a local comparison cache only:

```text
local/b10-harry/HarryPotter/SkeletalMesh/skharryMesh.psk
local/b10-harry/HarryPotter/Texture/skharryTex0.tga
local/b10-harry/HarryPotter/Texture/skharryTex1.tga
local/b10-harry/HarryPotter/Texture/skharryTex2.tga
local/b10-harry/HarryPotter/Texture/skharryTex3.tga
```

`local/` is gitignored. These derived proprietary assets are not committed,
packaged, or redistributed. The Quest runtime must ultimately replace this
developer bridge with direct loading from the user's owned packages.

The clean-room preview loader validates bounded ActorX chunks, cross-references
points/wedges/faces/materials, decodes only RLE 32-bit TGA, converts BGRA to
RGBA, and resamples four materials to the existing 256x256 GPU array. It
places the bind-pose mesh at a diagnostic location 2.5 m ahead with its feet
1.55 m below the local eye origin. The same explicit `0.02` model scale is used
for the initial castle/character comparison.

## Offline validation

The exact Release command included both the owned full map and the local NPC
cache, then exited before OpenXR initialization via `--validate-assets-only`.
It reported:

```text
[hp1.map.load] selected_triangles=20003 available_triangles=20003 omitted_triangles=0 vertices=60009
[hp1.npc.load] points=509 wedges=738 faces=988 vertices=2964 materials=4 texture_bytes=1048576 names=skharryTex0,skharryTex1,skharryTex2,skharryTex3
[hp1.assets.validate] PASS openxr_started=0
```

The feature-enabled Rust probe passes 42 tests, including synthetic PSK/TGA
framing, RLE orientation/BGRA conversion, texture resampling, the existing
external HP1 BSP FFI test, projection, tracked-wand, and gesture tests. The
native HP1 tests also pass with warnings treated as errors.

## Acceptance boundary and next run

The next headset run requires an explicit user launch instruction. Its purpose
is limited and visible: confirm that real textured Harry appears in front of
the camera, is upright, has coherent materials, and gives a useful human scale
reference against the already accepted castle. Animation, gameplay AI,
collision, actor placement, and direct in-package skeletal-mesh loading remain
future work and are not claimed by this gate.

The golden installation remains read-only. All generated controls and logs are
under ignored `local/` paths.

## Headset result

The explicitly authorized Quest Link run reached `FOCUSED` and submitted the
first stereo frame with 20,991 total triangles: 20,003 textured BSP triangles
and 988 Harry triangles. The GPU array contained the 88 existing BSP layers
plus four Harry material layers. The user supplied a headset screenshot and
confirmed that Harry appeared. The image shows an upright, front-facing,
complete diagnostic T-pose with coherent face, glasses, hair, robe, crest, and
hand materials against the textured entrance hall. This accepts the B10 static
mesh/material preview only.

The runtime later left `FOCUSED` and transitioned through `STOPPING`, `IDLE`,
and `EXITING`; the probe reported `session terminated before clean STOPPING:
EXITING` rather than its requested 10,000-frame aggregate. Therefore this is
visual acceptance, not a clean full-duration machine completion. The ignored
logs are `local/gate-b10-harry-live-01.log` and its `.stderr.log` companion.

T-pose, diagnostic placement, external developer cache, lack of animation,
and lack of gameplay remain explicit limitations. The next implementation
gate is direct `Engine.SkeletalMesh` loading from the owned package.
