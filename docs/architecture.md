# Architecture constraints and gates

## One product, two proof layers

The fastest way to validate VR interaction is a PC OpenXR build of the same
portable runtime intended for Quest. A hook against the working x86 game may
still be useful as an observation tool, but it is not the primary product path.
That distinction avoids proving interactions against a renderer and ABI that
cannot move to Android.

It is not the Quest port. Quest 3 standalone requires an Android ARM64 runtime
capable of loading the original UE1 packages and implementing HP1's engine and
game-specific native behavior. The repository must keep those two claims
separate.

## Runtime foundation requirements

Any selected foundation must satisfy all of the following:

1. Auditable legal provenance and redistribution terms.
2. Original HP1 data remains external.
3. UE1 package version 433 and UnrealScript VM compatibility can be measured.
4. HP1-specific native classes/functions can be enumerated and implemented.
5. A renderer can submit stereo views through OpenXR on Windows and Android.
6. The core can build as 64-bit ARM code with Android NDK tooling.
7. File I/O, audio, save data, timing, and input have Android replacements.

## Vertical-slice gate

The first meaningful slice is intentionally narrow:

- load one original room/map from external data;
- render both eyes at correct scale;
- drive view orientation from the headset;
- render and aim a tracked wand independently of the head;
- recognize one explicit gesture;
- dispatch one original spell interaction to an original target;
- emit timing and compatibility diagnostics.

If a candidate foundation cannot load the startup packages and enumerate the
missing native surface without wholesale engine replacement, stop and reassess
before building more VR UI around it.

## Current candidate

OpenHP1 is the strongest technical candidate found so far. Its current Rust
workspace separates package, script, runtime, scene, render, game, audio, and
tool responsibilities; it loads an external original installation; and it
already implements HP1's native gesture calls and spell-lesson runtime path.

The candidate is not yet adopted. The audited checkout declares
`MIT OR Apache-2.0` in Cargo metadata but contains no tracked license text. It
also has no Android or OpenXR backend. A scoped Rust 1.97 toolchain is now
installed under the user's rustup directories without changing global `PATH`,
and the independent OpenXR capability probe passes `cargo check`. Those are
tooling facts, not evidence that the candidate renderer works in a headset.

The full decision record is in
[`runtime-foundation-decision.md`](runtime-foundation-decision.md).
