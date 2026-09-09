# C32 — damage display, staged facing, teacher table and VR graphics menu

## Scope and source boundary

Retail `C:\Program Files\HP` was read only. Documents saves were not touched.
No retail executable or Quest application was launched. The previously running
HPVR process was stopped only to replace its APK, under the user's standing
install-without-launch instruction. No game data or audio cache was replaced.

MiamiVR reference inspected read-only: `C:\Dev\miamivr-quest`, especially
`librw/src/vulkan/shaders/rw_world.frag` and the VR settings overlay. Its vehicle
reflection uses a shared previous left-eye image and a fixed-distance material
lookup, not a second scene render. Root and librw licenses were inspected.
No MiamiVR, reVC or third-party implementation was imported. HPVR reflection
math, settings, Vulkan resource management and shaders are original work here.

## Player-visible changes

- Peeves: owned baseHarry defaults encode LifePotions/MaxLifePotions=50;
  five raw damage now subtracts ten normalized percentage points. The previous
  five-percent conversion was wrong. Frog healing likewise converts ten raw
  points to twenty percent. First-hit idempotence and V7 saves are preserved;
  completed encounters are not retroactively damaged on loading old saves.
- More importantly, the owned lightning image has transparent padding. The
  old top-of-texture clip did not alter any opaque pixels even at 90% health.
  Filling now uses the actual alpha bounds. A fresh first hit removes 680
  visible texels; a numeric HP value below the bolt makes the loss explicit.
  The existing red damage flash remains. This explains a missing visual loss,
  not a claim that every earlier headset report was only cosmetic.
- Twins staged for the jumping/bean rooms face the approach immediately;
  their discarded retail clone yaw no longer points them at the wall. The
  stage-six save recovery also uses the waiting-facing policy. No cross-wall
  navigation was reintroduced.
- Owned TransTrestleTable0 (actor 3513, HProps class 418 / mesh 1590) loads at
  the teacher's authored position and yaw, with feet grounded on the BSP.
- Both grips (>75%) + left Menu toggles a world-anchored VR settings panel.
  Menu alone retains the original game book. The chord consumes the menu press
  through release, so releasing grips cannot accidentally open the game book.
- Left stick up/down selects, left/right changes a value; B or the chord closes.
  Return restores the previous game/menu state. Settings persist separately
  from game saves, in two checksummed `vr-settings.0/1` private-storage banks.
- RenderScale: 50–125%, step 5, default 100%, per dimension. Changes are live.
  One bounded 125% allocation is retained; viewport, render area and OpenXR
  submitted rectangles all use the current setting. Lower scales reduce
  rendered pixels but do not release the maximum-size attachments.
- SSR: 0–100%, step 5, default 35%. One shared previous left-eye color/depth
  history, 24 bounded depth-ray steps in the material shader, and edge/range
  fading. Only horizontal owned WoodRedFloor_3, 3rdWood1_B and woodflr_01_B
  surfaces qualify. 426 BSP vertices qualify in this level.
- No extra scene rendering and no full-screen SSR pass. Two left-eye image
  copies (color and depth) update history after both eyes finish. SSR=0 skips
  sampling and copies. UI, wand, sky, transparent effects and reflected floors
  are excluded as history hits using alpha classification. Reflection depth
  and color have separate sampled images, never active-attachment feedback.
- History invalidates on menus, lost focus, resize and large camera changes.
  Shared history memory relies on the renderer's existing end-of-frame fence
  wait; a future frames-in-flight refactor must bank these resources/UBO too.

## Offline checks

- Android ARM64 native build and both GLSL shaders: PASS.
- Host CTest: 17/17 PASS. C32 covers chord edges/focus, game-menu return,
  immediate settings, corrupt-bank fallback, bounded extents, inverse stereo
  projection/eye extraction, material gating and normalized damage.
- C32 owned-data check: PASS, one teacher table and 680 changed bolt texels.
- Complete owned scene load: PASS; 32 characters, 58 props, 5 doors,
  12,068,532 vertices, 999 prebuilt UI layouts / 279,834 UI vertices.
  253 texture layers in the probe plus runtime fire = 254, below the limit 256.
  Child choreography still spawns/destroys all 18 with zero ground misses.
- VR panel and damaged HUD raster previews inspected in ignored
  `local/quest-owned-audio/`; no overlap/cropping, damaged bolt visibly empties.
- APK audit: signature/alignment/AArch64/dependencies/C32 markers PASS;
  proprietary assets bundled: zero. No online assets downloaded.

## Build and install evidence

- Native `build/quest-c3-android/libhpvr_quest.so` SHA256:
  `3E8C6C3B2CAD892FCD812C84BB4DB48F2C204D461DD81C7AE7481D289210B282`
- APK `android/app/build/outputs/apk/debug/app-debug.apk` SHA256:
  `ABA1E0DA05159131EC70EBB9FC342E9947F17FEC62C3BDE2BF903911EB8032D8`
- C31 rollback APK `local/quest-c31-before-c32.apk` SHA256:
  `66344E64073F98EE7BB9DFF69385B3A410C3D6E042B8A19C0D237B732C7F9127`
- `adb -s 2G0YC1ZF760BPC install -r ...`: Success.
- Installed package: `io.github.hpvr.quest`, path:
  `/data/app/~~yWIjau50R-QqeAq4JxD4eg==/io.github.hpvr.quest-ZBR6NONTOrRI28rfmMZugw==/base.apk`
- All six save-bank SHA256 values unchanged across install; `pidof` empty
  afterward (exit 1 is the expected no-process result, not install failure).

## Still requires headset acceptance

This is not a GPU performance or visible-SSR acceptance claim. Check the fresh
Peeves hit/bolt, twins before their scene, teacher table, both-grips menu, live
scale changes and saved values after relaunch. Compare SSR=0 and 35 on the red
wood floor while moving the head. Screen-space reflections cannot show objects
outside the captured view; mono history trades stereo reflection depth for
lower cost and consistency. It is not a mirror or ray-traced reflection.
There are no Quest GPU timings for C32 yet; disabling extra passes does not
make fragment ray marching or history bandwidth free.
