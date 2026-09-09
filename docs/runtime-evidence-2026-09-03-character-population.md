# Gate B13: automatic character population

Date: 2026-09-03

## Outcome

The preview runtime no longer needs one manually chosen character package,
mesh reference, and actor reference. A clean-room manifest pass now walks the
validated package graph, resolves each Level actor's exact UClass export,
follows visual defaults toward `HPBase.baseChar` / `HPBase.baseHarry`, resolves
the effective `Engine.SkeletalMesh`, and retains the actor transform and scale.
No UObject is constructed and no UnrealScript bytecode is executed.

The Rust population stage loads each distinct mesh once, concatenates its
256x256 material layers once, and expands bind-pose geometry for every actor
instance at its real level transform. The local Harry actor is explicitly
excluded because the target view is first person. Single-character preview
arguments remain available as a diagnostic path but are mutually exclusive
with the population path.

New opt-in runtime argument:

```text
--hp1-character-population <data-root> <excluded-actor-reference>
```

## Owned-level result

For `Lev_Tut1.unr`, the manifest inspected 2,011 non-null actors and selected
28 character instances after excluding Harry actor 603. The selected set
contains Filch, Fred, George, Ron, Hermione, Dumbledore, the tutorial ghost,
Peeves, McGonagall, Quirrell, Crabbe, Goyle, Malfoy, and generic students.

The runtime deduplicated these instances to 15 skeletal meshes:

```text
actors=28
distinct_meshes=15
distinct_mesh_source_faces=7693
expanded_vertices=41817
texture_layers=39
texture_bytes=10223616
```

Examples of exact resolved identities include:

```text
HarryPotter.FILCH       -> HarryPotter.u:560 skfilchMesh
Tut1.Tut1Ron            -> HarryPotter.u:494 skronMesh
Tut1.Tut1Hermione       -> HarryPotter.u:561 skhermioneMesh
Tut1.Tut1Dumbledore     -> HPModels.u:645 skdumbledoreMesh
Tut1.tut1Peeves         -> HarryPotter.u:1794 skpeevesMesh
Tut1.Tut1McGonagall     -> HarryPotter.u:1764 skmcgonagallMesh
Tut1.Tut1Quirrell       -> HPModels.u:656 skquirrellMesh
Tut3.Tut3Malfoy         -> HarryPotter.u:453 skdracoMesh
```

## Verification

- native MSVC Release build with warnings as errors: passed;
- CTest: 2/2 passed;
- Rust feature suite: 44/44 passed;
- Rust default suite: 26/26 passed;
- C/Rust manifest ABI layout: 940-byte actor, 300-byte report;
- complete owned-map asset pass: `PASS openxr_started=0`;
- all 15 distinct meshes and all 39 texture layers decoded successfully;
- no file in the golden installation was modified.

## Acceptance boundary

This is an offline population result. The whole populated scene has not been
started in OpenXR and no headset claim is made. All characters are still in
their bind pose. The next implementation block is shared skeletal animation,
not one headset test per model.
