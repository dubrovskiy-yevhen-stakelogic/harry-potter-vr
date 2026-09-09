# C34: clean SSR history, emission, reduced shader work, live VR menu

## User feedback and boundaries

User confirms C33 reflections are visible, but fire appears black, the health
bolt cuts a hole in reflected scenery, SSR loses stable 72 FPS at 100%, and the
VR settings must not pause the world.

Changes are confined to this HP workspace and its installed Quest package.
Retail HP and Documents HP state were not modified. No assets, saves or audio
were replaced. No retail or Quest launch was performed. The old Quest process
was force-stopped only for the requested install; no log buffers were cleared.

## Rendering fixes

C33 captured the final left-eye color including HUD/menu/wand. Their exclusion
alpha rejected reflections beneath their silhouettes; changing only alpha
would have reflected the HUD itself, not restored the scene behind it.

C34 draws left-eye world/effects without overlays, draws the right eye normally,
then captures clean left color/depth after BOTH eyes finish sampling old history.
Only then it resumes the left target with LOAD to draw HUD/menu/wand/guide.
Thus there is one world geometry draw per eye, no additional reflected scene,
and no SSR fullscreen pass. With SSR enabled there IS one additional small
left-overlay render pass; its tile load/store cost is not claimed to be zero.
When SSR is disabled no overlay-resume pass or history copy occurs.

The compatible LOAD render pass has the same formats, references, subpass and
dependencies as the world pass; only attachment load/initial-layout settings
differ. Dependencies include color/depth reads/writes and late depth tests.
Timestamp scopes now include this deferred overlay in the total and left eye.

Fire's additive blend used to add 1 to the destination alpha, which the SSR
shader treated as excluded content. Fire/glows now add RGB while preserving
destination classification (source-alpha factor ZERO, destination ONE).
World effects also continue drawing with the live VR settings open.
As with other SSR, transparent fire uses opaque depth behind it; effects over
sky/no screen-space surface are not promised as complete geometric reflections.

## Optimization

- Project ray origin and direction once, then interpolate clip coordinates.
- Compare camera depth by recovering just homogeneous W from inverse-projection
  row 4, removing per-sample inverse mat4/world reconstruction/normalization.
- 16 coarse samples instead of 24; 4 refinements instead of 5. Quadratic step
  placement uses multiplication rather than pow. Range and strength unchanged.
- Enable glslc -O for packaged SPIR-V.
- Choose nearest-blit reduction separately for color and depth, with capability
  checks and an exact-copy fallback. Current Quest supports half-width,
  half-height color history; depth remains full resolution.
- On this device color history therefore has 25% of the old pixels. For the
  current 4-byte color + 4-byte D32 pair the combined history payload is 62.5%
  of the previous size (excluding alignment). This is NOT a measured FPS gain.
- Main eye render scale, 175% cap and selected SSR strength are preserved.

Device evidence from read-only cmd gpu vkjson:
Adreno (TM) 740; color formats 43/50 optimal features 130433 include BLIT_SRC and
BLIT_DST. D32 format 126 features 2147612163 include BLIT_SRC but NOT BLIT_DST.
We therefore do not issue an unsupported scaled depth blit on this Quest.
See [Vulkan blit requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBlitImage.html)
and [render-pass compatibility](https://docs.vulkan.org/spec/latest/chapters/renderpass.html).

## Live VR menu

World visibility/simulation pause and input capture are separate policies.
VR settings/debugger over gameplay leave animation, world effects, story,
audio and an already-active jump ticking. Controller inputs still select menu
rows rather than moving/casting accidentally.
The regular book pause remains a pause; opening VR settings over a paused book
does not silently unpause it. Main-menu music and story narration remain active.
Cinematic camera retrieval uses world-pause status rather than overlay visibility.

## Verification

- Host and Android ARM64 builds PASS.
- CTest 19/19 PASS. C34 covers live-menu/audio vs explicit book pause, deferred
  overlay policy, half-size/copy extents, 27 translated/rotated/asymmetric
  projection cases. Maximum camera-depth error vs full projection 0.000160217 m.
- All six generated SPIR-V modules pass spirv-val for Vulkan 1.0.
- Full owned-scene CPU probe PASS: 32 actors, 5 doors, 303 grounded route samples,
  12,069,096 vertices; 18 intro children complete, zero ground misses.
- Signature/alignment/ARM64/no-proprietary-assets/C34 marker audit PASS.
- git diff --check PASS (line-ending warning only).
- Current log buffers did not contain useful HPVR frame samples from the user's
  C33 run. No new run was launched to manufacture a before/after comparison.
  Stable 72 FPS, actual reduction in GPU ms, and headset visuals remain pending.

## Install without launch

Quest serial 2G0YC1ZF760BPC; io.github.hpvr.quest.
adb install -r: Success. Post-install pidof reports no process.
All six progress-save banks and both VR-settings banks have identical hashes
before and after installation.

- APK android/app/build/outputs/apk/debug/app-debug.apk SHA256:
  161CEDD8CFB243D355FB694590B6441953FD7B08579FFEED9C28727AE6DAEDFB
- Native build/quest-c3-android/libhpvr_quest.so SHA256:
  D85CD0956183B5765FE41C794A08F8AE70F3C26C8040C9EC723F1A2DAF21FC6B
- Installed base:
  /data/app/~~TAy-YfxP4adYFXff86IKZA==/io.github.hpvr.quest-XbzyKOfhrgrvPEytmuqlUg==/base.apk
- C33 rollback local/quest-c33-before-c34.apk SHA256:
  694F59A1F6978FE0E5D679F2508B4FECAEAB6EE98CC8093F16838403F52F3892

## Fresh user check

At the same red-floor viewpoint and 100% scale, compare SSR 0 and the previous
strength using the pinned debugger. Check actual FPS/frame GPU ms and fire color,
turn the head to check that the bolt no longer cuts a silhouette. Open VR
settings while NPCs/fire animate and confirm the world and sound continue.
Only after that compare 175%; no target-framerate guarantee is inferred from
the game's age or another port.
