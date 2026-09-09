# Baseline inventory: HP1 PC

Captured read-only on 2026-09-02 (Europe/Kiev). The installed edition identifies
itself as `Harry Potter and the Sorcerer's Stone(TM) Version 1.0`. This document
identifies the working input installation; it does not authorize modifying it.

## Installation and runtime state

| Item | Evidence |
| --- | --- |
| Retail root | `C:\Program Files\HP` |
| Executable | `C:\Program Files\HP\system\HP.exe` |
| User state | `C:\Users\user\Documents\Harry Potter` |
| Current runtime log | `C:\Users\user\Documents\Harry Potter\HP.log` |
| Engine version | `433` |
| Engine compile stamp | `Oct 28 2001 14:55:11` |
| Executable ABI | PE32, machine `0x014c` (x86) |
| Executable link stamp | `Mon Oct 29 01:06:58 2001` |
| Renderer | `D3DDrv.D3DRenderDevice`, internal revision `1.9b` |
| Current display baseline | `640x480x16`, fullscreen |
| Runtime GPU detected | NVIDIA GeForce RTX 4090 |

The current log opened at `2026-09-02 06:31:28`, loaded `Entry` and
`Startup.unr`, possessed `Startup.Harry0`, spawned `Startup.baseWand0`, opened
`WindowsViewport0`, and initialized D3D and Galaxy audio. It closed normally at
`06:31:56` after `appRequestExit(0)`.

Warnings about missing `StoryBookTest.CommonRoom001` through `CommonRoom004`
appear during the menu sequence. They did not prevent this baseline launch and
are recorded as pre-existing behavior, not a VR regression.

## Key SHA-256 values

| Relative path | SHA-256 |
| --- | --- |
| `system\HP.exe` | `43B2D1471BC36E3290F4474BADF4D0FD84B674E4AA820FE4FCBD75DFBB2903BD` |
| `system\Core.dll` | `60F441EE152E13FA79DE481901645DDC65638B97142E2E3570C1E76E3DE8C788` |
| `system\Engine.dll` | `7756A2A3DF7198D72F4706952196BEE8ADB3B79EDFE7C8B3A5E4D2E3593D8EBC` |
| `system\Render.dll` | `41C0E9939CAC1833978C15BB10A13761B3559AD929F060EC88B6AAE8B96BC55F` |
| `system\D3DDrv.dll` | `7683B11647DAFE3926EFF7D0D055ABBE3D728648A19F5F8A613FD03EFD151599` |
| `system\HPBase.u` | `30B5EF44E9755AA9C020BE9D863E35335C26C2D6988FD0A00A347A98C44E105D` |
| `system\HarryPotter.u` | `5F18066AC7D6A64BA315A19753308613C0819B3944DA551A17BD0F710560CF60` |

## Data layout

The installation has the expected UE1-style roots: `Maps`, `Music`, `Sounds`,
`Textures`, `save`, and `system`. The active `HP.ini` resolves saves to
`C:\Users\user\Documents\Harry Potter\Save\*.usa`.

The `system` directory includes the native engine split (`Core.dll`,
`Engine.dll`, `Render.dll`, `Window.dll`, `WinDrv.dll`, `D3DDrv.dll`,
`Galaxy.dll`), editor components, and game packages including `HPBase.u`,
`HarryPotter.u`, `HPMenu.u`, `HPModels.u`, `HPParticle.u`, and `HPSounds.u`.

## Verification policy

- A matching hash proves file identity, not that the game currently runs.
- A clean PC log proves only the PC baseline.
- Quest behavior requires a current device log/capture and measured runtime
  evidence.
