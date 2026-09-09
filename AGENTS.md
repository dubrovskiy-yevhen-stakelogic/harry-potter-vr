# Harry Potter VR workspace rules

## Scope

This repository is the clean-room workspace for a VR port of the legally owned
US PC release of *Harry Potter and the Sorcerer's Stone* (2001). The long-term
target is native Meta Quest 3 standalone (Android ARM64 + OpenXR).

## Non-negotiable boundaries

- Treat `C:\Program Files\HP` as a read-only golden installation. Do not copy
  experimental DLLs, configs, packages, or saves into it.
- Treat `C:\Users\user\Documents\Harry Potter` as user-owned runtime state.
  Read it for diagnostics, but do not edit or delete it without an explicit
  request.
- Do not launch the retail game or install/launch a Quest build unless the user
  explicitly asks for that runtime action.
- Never commit proprietary game assets, packages, music, speech, executables,
  or derived asset dumps. The port must load data from a user-owned copy.
- Do not import engine code of uncertain or leaked provenance. Audit the source,
  license, redistribution terms, and AI/contribution policy before reuse.
- Keep third-party code and game-specific original work clearly separated.
- Build success, APK creation, installation, and runtime acceptance are separate
  claims. Record device/runtime evidence before claiming behavior on Quest.

## Development order

1. Preserve and verify the working 2D PC baseline.
2. Prove game-data and script compatibility without modifying the installation.
3. Establish a PC VR vertical slice: stereo view, head pose, wand pose, and one
   gesture-driven spell.
4. Port the proven runtime layer to Android ARM64/OpenXR.
5. Add Quest packaging, data import, performance telemetry, and comfort modes.

## Coordination

- Inspect before editing; this workspace may be shared by multiple agents.
- Use one writer for overlapping files and keep exploratory agents read-only.
- Preserve unrelated user work. Stop if unexpected files or concurrent edits
  appear.
- Prefer reversible, scoped changes and small commits with evidence in `docs/`.
