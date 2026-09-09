# Gate B2: deterministic HP1 package dependency graph

Date: 2026-09-02

## Claim

The independent runtime can now resolve the complete package dependency graph
of the first tutorial map from an explicit, user-owned HP1 data root. Resolution
is case-insensitive and follows the shipped `Core.System` search order without
using the process working directory or modifying the installation.

This proves package selection and structural dependency closure only. It does
not yet prove cross-package object linking, object construction, general
property decoding, UnrealScript execution, BSP rendering, audio decoding,
gameplay, Android file access, or Quest runtime behavior.

## Search policy

Both the shipped `system\Default.ini` and the current user `HP.ini` declare
the same package order:

1. `System/*.u`
2. `Maps/*.unr`
3. `Textures/*.utx`
4. `Sounds/*.uax`
5. `Music/*.umx`

The resolver enumerates only those immediate directories and extensions. It
canonicalizes the explicit data root, entry package, and candidates; rejects
paths outside the root; and caps indexed files, packages, and edges.

Dependencies choose the first matching search path. Additional lower-priority
candidates remain in the result as shadowing diagnostics. Multiple
case-insensitive matches inside the same search rank fail as
`ambiguous_dependency`. A name with no data package but a matching
`System/*.dll` is retained as a terminal native-module requirement; the DLL
is recorded but never loaded or executed. A name with neither form fails as
`missing_dependency`, and failures expose no partial graph.

## Synthetic verification

Unit tests cover:

- `System/*.u` winning over a same-basename `Textures/*.utx` candidate;
- preservation of the shadowed candidate;
- a data package with a same-name native DLL companion;
- a DLL-only terminal dependency;
- finite resolution and back-edge counting for a two-package cycle;
- fail-closed behavior for a missing transitive dependency.

## Lev_Tut1 result

Read-only inputs:

- data root: `C:\Program Files\HP`
- entry: `C:\Program Files\HP\Maps\Lev_Tut1.unr`

Command:

```powershell
& build\windows\src\wand\Release\hpvr_hp1_package_graph.exe `
  'C:\Program Files\HP' `
  'C:\Program Files\HP\Maps\Lev_Tut1.unr'
```

Summary:

| Field | Value |
| --- | ---: |
| Resolved packages | 37 |
| Dependency edges | 144 |
| Cycle/back edges | 3 |
| Shadowed candidates | 1 |
| Missing dependencies | 0 |
| Ambiguous dependencies | 0 |

All 37 selected nodes are structurally valid data packages. Four selected
packages also have native companions: `Core`, `Editor`, `Engine`, and
`Fire`. This closure has no DLL-only terminal node.

The three back edges are the observed package self-dependencies
`Core -> Core`, `Editor -> Editor`, and `Engine -> Engine`. They are
reported and terminate normally rather than causing recursion.

The sole shadowed candidate is deterministic:

```text
selected: C:\Program Files\HP\system\Editor.u
shadowed: C:\Program Files\HP\Textures\Editor.utx
```

## Build and regression evidence

- Windows Debug build with MSVC `/W4 /WX /permissive-`: passed.
- Windows Release build with MSVC `/W4 /WX /permissive-`: passed.
- Debug CTest: 2/2 passed.
- Release CTest: 2/2 passed.
- Default locked Rust all-target tests: 19 passed.
- Feature-enabled Rust/C++ bridge tests: 35 passed.
- Android NDK r27c ARM64 static-library build: passed.
- Post-change strict golden verifier: 7/7 recorded retail files `MATCH`.
- Existing PC runtime log still contains the recorded Version 433, Startup,
  wand-spawn, and clean-exit markers.

No retail file was written, no proprietary package was copied into the
repository, and neither the retail game nor a Quest build was launched.

## Next gate

Gate B3 should resolve cross-package import references to concrete export
identities across this fixed graph, retaining object outer paths and class
identities while still avoiding object construction and proprietary payload
dumps. That creates the linker boundary needed before a narrow Level/BSP
vertical slice.
