# Gate B15: HP1 skeletal animation and populated idle playback

Date: 2026-09-03

## Outcome

The direct-package character path now decodes HP1's custom compressed
Engine.Animation payload, samples a looping sequence, skins the original mesh
points, expands them through the existing material wedges, and supplies one
animated vertex range for the whole 28-actor Lev_Tut1 population.

This replaces the B14 bind-pose boundary. No per-model headset acceptance was
used while implementing it.

## Clean-room format evidence

The 2017 HP1 public-header archive linked from the
[OldUnreal research thread](https://www.oldunreal.com/phpBB3/viewtopic.php?t=2233)
was kept under ignored local/research and used only to identify serialized
field boundaries. Its headers have no permissive license, so none of their
source code was imported.

The numeric behavior was independently confirmed by static, read-only
inspection of the user-owned golden
C:\Program Files\HP\system\Engine.dll:

- one compressed vector occupies three signed 16-bit components;
- tracks contain counts/references into three package-global arrays rather
  than inline stock-UE1 key arrays;
- position is component times track_scale divided by 32767;
- quaternion XYZ is the sine of component times (pi / 2) / 32767, with
  non-negative W reconstructed from unit length;
- time is an unsigned byte times the per-track scale and the first delta is
  zero.

The implementation contains only independently written parsing, decoding,
interpolation, hierarchy, and skinning code.

## Decoder and sampler contract

- all compact counts, references, storage spans, bone maps, and payload ends
  are bounds checked;
- summed track spans must exactly cover the three compressed global arrays;
- dynamic position tracks must share the orientation time stream;
- timestamps are accumulated from deltas and the final key loops back to key
  zero over the remaining TrackTime;
- rotations use shortest-arc spherical interpolation;
- bone names must match between mesh and animation;
- the mesh reference hierarchy remains authoritative for its weights and
  bone-local points;
- non-parent-first hierarchies are supported with explicit cycle detection;
- every skinned output point must remain finite.

The bind-pose oracle established the HP1 transform convention before animated
sampling: inverse quaternion application, local-times-parent quaternion
composition, and hierarchical parent translation. Dumbledore reconstructed
with RMS error 0.00053978 Unreal unit and maximum error 0.00259741.

## Population render bridge

The SkeletalMesh C ABI is version 2 and now retains the source point index for
each expanded triangle vertex. A separate versioned two-call ABI loads a
bounded 16-frame, frame-major idle animation directly from the same owned
package. It chooses Breathe, breath, Float, look, or walk in that order.

Rust deduplicates animation decoding with the existing 15 unique meshes,
expands the sampled points for all 28 actors at their serialized transforms,
and prepares 16 population-wide frames. The wgpu scene marks only the static
vertex buffer as COPY_DST when this animation is present and uploads at most
one population range per simulation frame. Both eyes render that same
immutable upload. The current preview advances at ten sampled frames per
second, producing a 1.6-second normalized idle loop.

## Offline evidence

Eight deliberately different character sets were individually decoded and
sampled before the full population pass:

| Character | Selected clip | points | half-cycle RMS displacement |
| --- | --- | ---: | ---: |
| Dumbledore | Breathe | 318 | 0.306492 |
| Quirrell | Breathe | 319 | 0.734577 |
| Filch | Breathe | 266 | 0.282753 |
| Ron | Breathe | 304 | 0.723053 |
| Hermione | Breathe | 322 | 0.308405 |
| Peeves | Breathe | 329 | 0.818226 |
| McGonagall | Breathe | 228 | 0.110648 |
| Draco | breath | 250 | 0.393541 |

The complete Release asset-only command then loaded:

    actors=28
    distinct_meshes=15
    source_faces=7693
    expanded_vertices=41817
    texture_layers=39
    animation_frames=16
    PASS openxr_started=0

It also selected a valid idle clip for every unique mesh, including
skghostMesh to Float, without opening OpenXR.

## Verification

- native MSVC Release build with warnings as errors: passed;
- pure-C ABI header/layout target: passed;
- CTest Release: 2/2 passed;
- Rust gesture-projection suite: 47/47 passed;
- Rust default suite: 29/29 passed;
- Android NDK 27.2 ARM64 static-library build: passed;
- complete Lev_Tut1 asset pass: passed with openxr_started=0;
- git diff --check: passed;
- the golden installation remained read only.

## Acceptance boundary

The decoder, sampler, complete population load, GPU-buffer update path, and
Android native library are build/offline validated. No OpenXR process was
started in this gate, so visible animation direction, cadence, seams, and
character placement are not yet headset accepted. Script-driven sequence
selection, event notifies, root motion, facial animation, and collision remain
future runtime work.

