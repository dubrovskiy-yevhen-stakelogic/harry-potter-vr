# C26: Warner loading screen, movement, animated knights and Quirrell approach

## Scope

Preserved the existing C18-C25 dirty workspace. Retail HP and Documents runtime
state were read-only. Installed under the user's standing instruction; did NOT
launch. Offline checks and installation below do not establish headset acceptance.

## Changes

- Reassemble owned HPMenu FELegalTexture1-6 into the original 640x480 Warner
  startup image. A static OpenXR VIEW-space quad remains submitted while the
  asynchronous world build/GPU upload is pending. Proprietary artwork is loaded
  from the user's data, not embedded in the APK. Inspected the assembled PNG;
  compositor appearance remains untested. This masks loading, not a load-time fix.
- Fred/George become visible after the opening and on existing stage 1+ saves,
  rather than first appearing when their conversation begins.
- Jump launch speed 4.9 -> 6.0 m/s, gravity remains 19 m/s^2. Airborne horizontal
  movement gets 1.20x assistance. Predicted apex 0.947 m instead of 0.632 m;
  retain bounded descending landing-lip assistance. Ground speed unchanged.
- Run/walk vertex poses use a fixed rest-pose floor. Per-frame lowest-foot
  normalization previously moved the whole body inversely to foot lift.
- Six knights use the owned idle/look-left/look-right/return animation clips,
  380 samples each, staggered phases and stable pedestal placement. The two
  owned armor-head sounds play within 5 m with distance attenuation on a
  separate effect voice. Fixed pause cycle approximates the original random
  pauses; this is not the full KnightScript VM or HRTF positional audio.
- Restore CutScene56 (Filch), CutScene1 (Draco/Crabbe/Goyle), CutScene58
  (Hermione) and CutScene59 (Quirrell introduction) with six additional owned
  models and eleven spoken lines. SAY retains explicitly authored animation.
  Stages 12-20 and route triggers lead toward the classroom. Existing Flipendo
  practice unlocks after Quirrell's introduction; basic dud casting no longer
  runs simultaneously with gesture casting.
- Progress V4 stores eleven actor poses and stages through 20. Readers accept
  V1/V2/V3; existing slots are not rewritten by installation.

## Offline checks

- Release host build and Android ARM64 native build: PASS.
- CTest: 11/11 PASS, including explicit V1/V2/V3 migration and V4 Quirrell pose.
- Owned-data intro probe: PASS, 23 character models, 2 opening doors,
  10,668,618 vertices including knight frames and UI; atlas limits/ranges valid.
  Eighteen intro children spawned/destroyed, zero ground misses.
- Owned C25/C26 probe: PASS, 3000 Ron grounding samples, six animated knights
  with stable feet, all eight tested cutscene cue graphs resolve.
- First real platform gap: launch x=47.9, z=-59.23, speeds 3.2 and 4 m/s both
  land on the opposite platform (x=52.0158 / 52.8778, capsule y=12.9153).
- APK signature, alignment, ARM64 symbols, C26 markers, proprietary-assets
  exclusion audit: PASS. git diff --check: PASS.

## Installation evidence

Quest 3 serial: 2G0YC1ZF760BPC. Package: io.github.hpvr.quest.

- APK SHA256: 0E1565C715DCAEECADC7E8CAEFF2963DD1F057088DDB8807AE4210F42081401C
- Native SHA256: A7751DF666D07ADE77337381DD4F5ABB9F94A1FD98748C8A141E99A28ED021FB
- Installed base.apk hash matched the local APK at
  `/data/app/~~3CAoZ2A-N55HhDHuAgpTnA==/io.github.hpvr.quest-AMVayxXUjKNchCSnmOMrVw==/base.apk`.
- Added thirteen owned PCM cache files (two armor sounds, eleven dialogue
  lines); every device hash matched its local source. HPSounds.u already exists
  in the owned device data directory; no game package replacement.
- All six slot1/2/3 journal-bank SHA256 values unchanged across installation.
- pidof after installation: empty. No game launch performed.
- Previous APK preserved at ignored `local/quest-c25-before-c26.apk`, SHA256
  2F16A954FAE794F57E15F7862803210A2E0863DF8A14368523D3817D79296311.
  Do not downgrade after V4 saves are written without save compatibility work.

## Remaining boundaries

No C26 headset acceptance or end-to-end player route traversal is claimed.
The new chain reaches Quirrell's introduction and existing gesture practice,
not completion of the authored SpellLearnTrigger lesson. CutScene60 and the
Lev_tut1b transition, bean-for-card exchange, Peeves gameplay and the full
generic mover/script VM are not restored in this block. Only the two opening
doors currently have mover playback; later door triggers remain partial.
