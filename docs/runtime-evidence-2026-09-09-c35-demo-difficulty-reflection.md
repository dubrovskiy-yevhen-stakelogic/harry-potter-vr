# C35: demo presentation, original lesson policy, clean source snapshot

## Scope and boundaries

User accepted C34 except the long dark bean reflection streak. This update also
restores original lesson difficulty with a VR option, adds first-step/end-demo
messages and Discord action, follows Harry during the final lesson route, and
exports source without game files. Retail installation and PC saves are read-only.
Standing authorization: install a verified build automatically, never launch it.
No retail/Quest game was launched in this turn. No owned data was imported again.

## Reflection correction

The old SSR accepted a front-to-behind history-depth discontinuity if the last
positive sample was within 23 cm. That can extrude a floating object's silhouette
without a real intersection. Both refined bracket endpoints now must converge
within a bounded 3.5-9 cm tolerance. A half-resolution color texel must match the
full-resolution hit depth; depth-blind neighboring blur samples are removed.

The shared GLSL/C++ policy is tested with 27 analytic brackets: 15 true surface
intersections and 12 false silhouette hits reproduced under the old policy.
Tracing remains 16 coarse + 4 refinement steps, shared left-eye history, no new
world render or fullscreen pass. Worst-case accepted-hit fetches fall from 24 to
22. This is a calculation/sample-budget result, not measured headset FPS or a
claim that every SSR screen-edge limitation has disappeared.

## Lesson difficulty

Fresh owned-data profile tests confirm accuracy radius .03, 12 seconds, and
round marks .50/.65/.80/.95. Original is now the default: no post-stroke shape
refitting and all four marks are used. The 12-second VR clock starts when the
trigger is pressed (rather than before the player has readied their hand).
Failure on the first tier retries; failure after earning a tier finishes the
lesson, following the original lesson flow. Existing praise/rejection audio is
retained; no new House Points UI is claimed.

VR settings -> LESSON DIFFICULTY -> ORIGINAL / RELAXED. Relaxed preserves the
earlier demo: 1.75x radius, position/scale assistance, .50 mark, unlimited time,
retry on every tier. The setting is saved, applied live, and cancels an active
stroke when changed. The menu states the timing/threshold policy.

VR settings now write HPVR_VR2 and read both VR1/VR2 with checksum/two-bank
recovery. Old scale/SSR values remain unchanged. The game-save format stays V7.

## Demo windows and final route

The welcome opens after 8 cm of actual stick movement in playable free movement,
not on loading, menu input, tracking jumps, head rotation or cutscene movement.
Dismissal is saved app-wide in VR settings, independent of game slots. The
message explains that this is a small demo through the first lesson, work is
ongoing, and news/feedback are on Discord.

Both notices use the same world-anchored floating-panel style as the VR menu.
The world remains visible, but notices hold quest progression until dismissed;
the ordinary VR settings/debugger remain live. Held confirmation cannot skip a
new notice. Text uses the existing English demo UI font.

OPEN DISCORD IN BROWSER is an explicit controller action for:
https://discord.com/channels/747967102895390741/1543691482861408276
The NativeActivity ACTION_VIEW handler checks JNI failures and absent handlers;
it never opens a browser automatically. A Discord account/server access may be
required. Successful device browser dispatch has not been tested by launching.

The owned CutScene60 has three Harry movement commands before CHANGELEVEL.
This final sequence now uses Harry's moving eye position with 6DOF; his own mesh
is hidden only for this first-person exit, other theatrical scenes keep Harry.
On return, the final actor position plus tracked lean and view yaw are saved and
restored in the same frame; it does not restore the old seated-body transform.
The thanks/feedback notice appears at the authored next-map boundary. The next
map is not loaded: this remains the end of the demo, not a complete game port.

## Verification

- Host and Android ARM64 builds PASS, warnings treated as errors in production.
- CTest 21/21 PASS, including C35 settings migration/checksum fallback, panel
  input/geometry bounds, first movement, exact Discord URL, actor eye movement,
  three-yaw 6DOF return continuity, and shared SSR policy regressions.
- Owned gesture tests PASS: visible template passes all four marks; a 25% sized
  trace fails Original and passes Relaxed; timeout produces one result and
  requires trigger release; policy changes cancel safely.
- Owned CutScene60 probe PASS: three Harry route movements and travel boundary.
- Full owned-scene CPU probe PASS: 32 actors, 5 doors, 303 grounded route samples,
  12,090,504 vertices; all 18 intro children complete, zero ground misses.
- Six SPIR-V modules pass Vulkan 1.0 validation.
- Welcome, end and difficulty windows rendered to local previews; bounds and
  readable layout checked. These are offline previews, not headset captures.
- APK ARM64/signature/alignment/no-proprietary-assets/C35 markers PASS.
- The first APK audit expected the old VR1 write literal. Updated it to VR2;
  backward reads remain covered by behavioral migration tests, not a string scan.

## Installed without launch

Quest 3 serial 2G0YC1ZF760BPC, package io.github.hpvr.quest.
adb install -r: Success. pidof after install: no process.
All six progress-save banks and both VR-settings banks are byte-identical by
SHA256 before/after installation. First runtime settings save will migrate to VR2.

- APK SHA256: BC169EF6B20865FB8741029DC0F97B4BAB5361D199D83A456239687A16264C14
- Native SHA256: 1EC630E7316A9CD3CFC7A3B2BFF03467E2D8886F0A5012B1B832DD620A4377EC
- Installed base: /data/app/~~ELSHswTzl9qSUo4VsTFhKg==/io.github.hpvr.quest-qLYBqiirAbm1D2h3k1nHyw==/base.apk
- C34 rollback: local/quest-c34-before-c35.apk
- Rollback SHA256: 161CEDD8CFB243D355FB694590B6441953FD7B08579FFEED9C28727AE6DAEDFB

Source-only export: EXPORT-SOURCE-KIT.ps1, target sibling
C:/Dev/harry-potter-vr-source-kit. Includes current modified/untracked production
text source and build metadata, excludes all local/build/data/assets/binaries and
signing keys, rejects junctions and disguised binary/package content, verifies
hashes and stable inventory, never overwrites a destination. Export boundaries
have 84 synthetic checks. See SOURCE-KIT-README.md for build/dependency/licensing
boundaries. This local snapshot is not assigned an invented open-source license.

Actual bean reflection appearance, controller difficulty feel, end-camera comfort
and browser opening remain fresh headset checks. Installation is not acceptance.
