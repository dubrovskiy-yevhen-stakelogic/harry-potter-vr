# Gate C11: visible Flipendo, contrast lighting, and wand tracing audio

## Runtime input

The user's C10 Quest run accepted NPC collision, looping fire ambience, and the
sound emitted after a successful gesture. It rejected three outcomes: there
was no visible Flipendo flight/impact, the wand made no sound during gesture
tracing, and the authored lighting pass was not visually apparent.

## Integrated change

C11 treats those as one gameplay/presentation block rather than three isolated
probes. No proprietary package or decoded asset is stored in the repository or
APK; all audio and level lighting continue to load read-only from the user's
imported owned data.

### Flipendo flight and impact

- The immutable ray captured on trigger press remains the aim source.
- Accepted release now creates a visible crossed-ribbon projectile from the
  locked wand-tip origin at `13 m/s`.
- The projectile has a `1.10 m` blue trail and a bright `0.22 m` core.
- A selected NPC receives its reaction only when the projectile reaches the
  target AABB, rather than immediately at gesture acceptance.
- Hit sound is delayed to the same impact frame and a `0.34 s` expanding
  three-axis flash is rendered.
- Misses remain visible for `18 m`, so an accepted gesture never disappears
  silently merely because the press-time ray did not intersect an NPC.
- Target tests now prove selection, duplicate rejection, delayed application,
  and finite return to the original position.

### Original wand tracing sounds

The AAudio mixer now loads two additional direct PCM exports from the user's
`Magic_sfx.uax`:

- `s_wand_wave` plays once when a valid trigger-held trace begins;
- `s_wand_wavehum` loops only while `GestureVisualState::Recording` is active.

The loop stops on release, cancellation, tracking loss, interaction-profile
change, local-space reset, or renderer teardown. Existing
`s_spell_throw1`/`s_spell_hit1` projectile sounds and `s_fire_loop` ambience
remain independent mixer voices.

### Lighting correction

C10 accumulated every overlapping light, flattening local contrast. C11 moves
the exact lighting function into the portable Quest runtime library and uses
only the two strongest authored lights at each point with a bounded secondary
contribution. The ambient floor remains deliberately dark.

The new read-only integration probe ran the exact function over all rendered
`Lev_Tut1` BSP vertices:

~~~text
lighting_probe_status=ok
vertices=60009
lights=431
luminance_min=0.171608
luminance_average=0.269266
luminance_max=0.956373
shadow_vertices=46105
highlight_vertices=196
~~~

This proves a non-flat authored distribution before device installation. It
does not substitute for the user's visual acceptance in the Quest headset.

## Verification and installation

~~~text
CTest = 7/7 PASS
Lev_Tut1 lighting probe = PASS
ARM64 NDK r27c compile/link = PASS
APK signature = v3 VALID
APK alignment = VALID
APK audit = PASS
proprietary assets in APK = 0
install result = Success
primaryCpuAbi = arm64-v8a
installed lastUpdateTime = 2026-09-04 19:49:18
launch by agent = not performed
path = android/app/build/outputs/apk/debug/app-debug.apk
sha256 = 2774427DDC4F64B853CDA5B707E10D3C7A05FF29A772B81D557B4F6AEE158F63
installed APK SHA256 match = YES
~~~

Device acceptance remains pending for the visible light balance, tracing-loop
volume, projectile readability, impact timing, and delayed NPC reaction.
