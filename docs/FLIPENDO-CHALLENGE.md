# Flipendo Challenge

The alpha includes the original `Lev_Tut1b` challenge after the first Flipendo
lesson in `Lev_Tut1`. It can also be started through **LEVEL SELECT**.

## Supported gameplay

- Original map geometry, textures, lighting, props, music and dialogue.
- Fifteen cutscene definitions with Harry first-person and theatrical VR cameras.
- Spell switches, counters, delayed events, doors, moving platforms and pushable
  columns, including the route to the seventh challenge star.
- Rolling-barrel and gnome encounters. Active gnomes pursue the player; a hit uses
  the original knockback, dizzy and seated animations.
- Eight challenge stars and the original star-count-dependent ending branches.
- Tipping pots, breakable vases, collectible beans and animated save books.
- Three save slots with inventory, map-local mechanisms and encounter state.
- Challenge deaths return to the level entrance or last original save book,
  rather than a recently occupied position near a pit.

Moving-platform transport preserves walking and right-stick turning. Support is
measured from the locomotion capsule, independently of physical head height.
Pushable columns fall when unsupported and settle against the level geometry.

Fireplaces animate, candles use the owned flame/glow textures, and the abyss
combines dark lighting, height fog and blue particles. Rendering uses bounded
effects rather than the original engine's particle implementation.

The challenge ends before `Lev_Tut2`; later levels are not included. Gameplay
and cutscene behavior are implemented by this port, not a general UnrealScript VM.

## Preparation

The matching alpha player kit imports and prepares both maps by default.
See [player installation](../tools/release/PLAYER-INSTALL.md).

For source development, build the host tools from the
[source guide](../SOURCE-KIT-README.md), then validate the owned challenge data:

```powershell
.\PREPARE-QUEST-CHALLENGE.ps1 -BuildRoot .\build\host `
    -DataRoot 'C:\Games\Harry Potter' -Ffmpeg 'C:\Tools\ffmpeg\bin\ffmpeg.exe'
```

This reads the owned installation and writes private preparation outputs; it
does not install or launch either game. Do not distribute generated game data.

A replacement APK alone does not add missing second-map packages or audio to an
older one-map installation. Use the matching two-map importer. Public updates
must retain their signing identity; never uninstall to bypass a key mismatch.
