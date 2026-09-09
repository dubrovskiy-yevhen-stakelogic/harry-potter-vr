# Gate B3: cross-package import/export linker

Date: 2026-09-03

## Claim

The independent runtime can now classify and link every UE1 import in the
complete `Lev_Tut1` package closure. Data-object imports resolve to a concrete
target export using both the normalized outer path and qualified class
identity. Package roots, package groups, native objects, and DLL-only modules
remain explicit non-export categories.

This is an identity linker only. It does not construct objects, decode general
export payloads, execute UnrealScript, implement the recorded native objects,
render BSP, decode audio, run gameplay, or prove Quest file access/runtime
behavior.

## Clean-room implementation

`inspect_hp1_package_link_table` exposes validated table metadata:

- signed local import/export reference;
- signed outer reference;
- qualified class identity;
- component-preserving object path;
- root-package classification;
- export flags and serialized byte count.

It shares the bounds-checked B1 parser. No payload content is returned or
decoded.

`link_hp1_package_graph` starts from the deterministic B2 closure and resolves
each import by recursively resolving its outer first. The target lookup key is
the case-insensitive pair:

```text
qualified class identity + normalized target object path
```

The outer traversal has explicit table bounds and cycle checks. Missing target
packages, missing exports in data-only packages, duplicate export identities,
invalid outers, and resource-limit violations fail closed and return no partial
graph or import list.

Two observed UE1 identity forms are retained rather than flattened:

- nested `Core.Package` imports are package-group namespaces, not ordinary
  object exports;
- a repeated same-name package-group alias resolves through its already linked
  outer identity, so grouped objects map to the concrete target export without
  suffix guessing.

A top-level export is allowed to have the same name as its containing package.
The name component is not removed merely because it matches the package name.

After exact export lookup, an absent object in a package with a confirmed DLL
companion is recorded as a `native_object` requirement. It is not reported as
an export link. This preserves the clean-room implementation backlog needed by
the future ARM64 runtime. A dependency represented only by a DLL is separately
classified as `native_module`; this particular closure contains none.

The CLI emits aggregate counts by source package and native target. It does not
dump the 2,993 proprietary object identities.

## Synthetic verification

Unit tests cover:

- exact package-root and object-export linking;
- preservation of signed references, outers, paths, and classes;
- a top-level object whose name equals its package name;
- virtual package groups;
- the observed same-name group-alias chain followed by an exact grouped object;
- native-object requirements in a package with a DLL companion;
- missing target exports;
- duplicate target export identities;
- fail-closed results without partial graphs.

## Lev_Tut1 result

Read-only inputs:

- data root: `C:\Program Files\HP`
- entry: `C:\Program Files\HP\Maps\Lev_Tut1.unr`

Command:

```powershell
& build\windows\src\wand\Release\hpvr_hp1_package_linker.exe `
  'C:\Program Files\HP' `
  'C:\Program Files\HP\Maps\Lev_Tut1.unr'
```

Complete closure:

| Category | Count |
| --- | ---: |
| Selected packages | 37 |
| Package dependency edges | 144 |
| All imports | 2,993 |
| Package roots | 144 |
| Package groups | 86 |
| Exact export links | 2,476 |
| Native-object requirements | 287 |
| DLL-only native modules | 0 |
| Missing or ambiguous links | 0 |

The native-object backlog is explicit:

| Native target package | Requirements |
| --- | ---: |
| Core | 226 |
| Engine | 60 |
| Editor | 1 |

These 287 entries are validated identities but are not implemented native
behavior.

The entry map itself contains 285 imports:

| Lev_Tut1 category | Count |
| --- | ---: |
| Package roots | 30 |
| Package groups | 37 |
| Exact export links | 215 |
| Native-object requirements | 3 |

## Build and regression evidence

- Windows Debug build with MSVC `/W4 /WX /permissive-`: passed.
- Windows Release build with MSVC `/W4 /WX /permissive-`: passed.
- Debug CTest: 2/2 passed.
- Release CTest: 2/2 passed.
- Default locked Rust all-target tests: 19 passed.
- Feature-enabled Rust/C++ bridge tests: 35 passed.
- Android NDK r27c ARM64 static-library build: passed.
- Post-change strict golden verifier: 7/7 recorded retail files `MATCH`.
- Existing PC runtime log retains the recorded Version 433, Startup,
  wand-spawn, and clean-exit markers.

No retail or user-state file was written. Neither the retail game nor a Quest
build was launched.

## Next gate

Gate B4 should use these stable identities to decode only the selected
`Engine.Level` root references and their actor/model handles into portable
indices. It should remain bounds-checked and construction-free before any BSP
geometry or UnrealScript execution is attempted.
