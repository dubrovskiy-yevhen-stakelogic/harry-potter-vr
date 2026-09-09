# C21: capsule-supported stairs

User requested that Harry climb stairs. This change concerns first-person
stick locomotion; it does not replace the authored cinematic routes/animations.

## Cause and change

- Reproduced with the production collision resolver: a 0.16 m step blocks the
  capsule at Z=-0.28, before its center crosses the first riser. Ground detection
  sampled only the center, so it never raised the body before contacting the wall.
- Ground support now includes nearby tread edges inside the capsule footprint.
  The lower-hemisphere support envelope raises/lowers the body continuously as it
  crosses each edge. Existing 0.32 m maximum rise and 0.45 m drop windows remain.
- Overhead horizontal geometry now participates in clearance testing. Tall ledges
  and walls are not disabled to make steps traversable. Unsupported movement is
  rejected while gravity/jumping are not implemented.
- Corrected post-intro spawn height: eye offset is above capsule center, not feet;
  include the 0.84 m capsule half-height. Continue normalizes the saved location
  onto its local ground with the same standing height, including C20 saves.
- RestoreHead records the local tracking-Y reference for the body collider. A
  fresh headset tracking origin no longer changes collision-center height relative
  to the restored eye position. World geometry/scale and NPC placement unchanged.

## Verification

- New CTest stairs test: 12 synthetic steps, up/down/diagonal traversal PASS.
- Tall 0.7 m obstacle, low ceiling, and unsupported ledge remain blocked PASS.
- RestoreHead regression checks collision center, not just rendered camera PASS.
- Production resolver on owned Lev_Tut1 geometry traverses the full Dumbledore
  staircase route both ways (four route marks, 5.12 m height change) PASS.
  Evidence: ignored `local/c21-stairs-probe.log`; no game executable launched.
- Windows Release / Android ARM64 builds PASS; CTest 9/9 PASS; git diff check PASS.
- APK signature, alignment, assets exclusion and C21 marker audit PASS.
- C20 backup: `local/quest-c20-before-stairs.apk`, SHA-256
  ED59B3618CCA9663B13020B57038196AEB5D65852A1B61630F24C305D278D768.
- Installed with `adb -s 2G0YC1ZF760BPC install -r` under standing user permission.
  Success; data and save slots retained. No audio/data payload changes.
- C21 local and installed APK SHA-256 match:
  9B62F36F2EA198DDB4FF241467F468F28722E581AE4342174356432A46EBAB3A.
- No app PID after installation; agent did not launch the app or retail game.

Actual comfort and visual behavior while climbing in the headset remain pending
the user's next run. CPU route success and installation are not runtime acceptance.
