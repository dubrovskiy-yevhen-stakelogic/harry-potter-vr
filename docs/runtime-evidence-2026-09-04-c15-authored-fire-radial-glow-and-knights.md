# Gate C15: authored fire, radial lamp glow, and knights

## Runtime correction

The user rejected C14 on Quest after comparing it directly with the retail PC
scene. Three visible mismatches were isolated:

- nested uniform-alpha quads exposed rectangular silhouettes instead of the
  retail scene's soft local haze;
- C14 generated a flame for every candlestick instead of respecting the map's
  authored particle actors;
- the non-character static-prop path did not include the level's suits of
  armour.

The owned `Lev_Tut1.unr` actor census provides the corrected source of truth:

- 10 `HPParticle.TorchFire02` actors;
- 4 `HProps.LampPost` actors;
- 16 `HProps.SingleCandleStick` actors;
- 20 `HProps.ThreeArmFloorCandleStick` actors;
- 6 `HProps.Knight` actors.

`HProps.Knight` resolves through the owned `HProps.u` package to class export
114 and `skknightMesh` export 152. The mesh contains 254 triangles and one
referenced 256x256 texture layer. No game asset is copied into the repository
or APK; the installed application resolves it from user-owned data at runtime.

## Renderer change

C15 keeps the headset-accepted C13 UE1 BSP lightmaps unchanged and replaces the
C14 local-effect approximation:

- flames are emitted only at the 10 serialized `TorchFire02` positions;
- the four lamp posts receive local haze, while candlestick geometry no longer
  manufactures extra fire or glow;
- glow uses a dedicated 24-segment, two-ring radial mesh: vertex alpha is 1 at
  the centre, 0.52 at radius 0.34, and 0 at the perimeter;
- three crossed radial planes avoid a single view-dependent disappearance,
  while zero-alpha circular edges remove the prior rectangular silhouette;
- the Vulkan effect vertex layout is RGBA and multiplies per-vertex alpha by
  the effect alpha in the shader;
- all six `HProps.Knight` instances are loaded by the static-prop path with
  their serialized position, yaw, scale, mesh, and texture.

The strict C15 scene invariant is 46 static props, 10 flames, 14 glows, and 216
vertices in the reusable radial glow mesh.

## Verification and deployment

- Android ARM64/OpenXR native build: PASS (`clang`/`clang++`, warnings as
  errors).
- Host suite: PASS, 7/7 tests.
- Signed APK audit: PASS, APK Signature Scheme v3.
- Proprietary assets in APK: 0.
- APK size: 14,189,205 bytes.
- Built APK SHA-256:
  `547C94209BA048E1094F8756555E2CC615F842AB1BBBFA2F6C3B15F9A52DB1AD`.
- `adb install -r`: SUCCESS on Quest serial `2G0YC1ZF760BPC`.
- Pulled installed base APK SHA-256:
  `547C94209BA048E1094F8756555E2CC615F842AB1BBBFA2F6C3B15F9A52DB1AD`.
- Source/installed hash comparison: PASS.
- Application process after installation: NOT RUNNING.

Installation and hash equality do not establish visual runtime acceptance.
The user must still confirm in-headset that the radial falloff, authored fire
placement, and restored knights match the retail reference.
