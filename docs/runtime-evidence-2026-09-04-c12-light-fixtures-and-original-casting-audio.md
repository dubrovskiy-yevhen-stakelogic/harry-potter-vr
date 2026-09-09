# Gate C12: light fixtures, flames, and original casting audio

## Runtime input

The user's C11 headset run confirmed that some new audio was audible, but the
scene still looked uniformly lit because the visible torch, lamp, and candle
actors were absent. The audible held-wand sample was also not the retail
tracing sound, and Harry did not speak the Flipendo incantation.

## Integrated change

C12 loads source objects as well as their illumination. The actor pass finds
exactly 4 `HProps.LampPost`, 16 `HProps.SingleCandleStick`, 20
`HProps.ThreeArmFloorCandleStick`, and 10 `HPParticle.TorchFire02` instances in
`Lev_Tut1`. The three fixture meshes and P8 textures are decoded read-only from
the user's `HProps.u`, validated against class defaults, transformed by each
actor's authored location, yaw, and draw scale, and appended to the scene's
texture array. Fifty source points render flickering crossed flame ribbons and
add bounded warm local illumination to the existing Engine.Light pass.

The sound parser now recognizes the shipped MPEG Layer II payload after the
UE1 Sound header. The exact `Magic_sfx.uax` exports are kept in memory and sent
to Android's platform decoder:

- `spell_tracing_loop` while a trigger-held gesture is recorded;
- `wand_ready_loop` as the recording onset;
- `spell_cast` when an accepted projectile launches;
- `flipendo_no`, mixed as Harry's incantation on that same launch;
- the existing PCM `s_spell_hit1` remains synchronized to physical impact.

No proprietary mesh, texture, or sound bytes are stored in the repository or
APK.

## Offline evidence

The host sound probe validates all four selected MPEG streams as mono 22050 Hz
Layer II data. Host tests pass 7/7 and the Android ARM64 library compiles and
links against `mediandk`. The connected Quest advertises its platform
`audio/mpeg` decoder without launching the application.

~~~text
CTest = 7/7 PASS
ARM64 NDK r27c compile/link = PASS
APK signature = v3 VALID
APK alignment = VALID
APK audit = PASS
proprietary assets in APK = 0
install result = Success
primaryCpuAbi = arm64-v8a
installed lastUpdateTime = 2026-09-04 21:10:36
launch by agent = not performed
sha256 = E54311AD21986928D76B5F0DEB1B404B87D302D4A13E4460CB56387FAB6B3CE4
installed APK SHA256 match = YES
~~~

Headset behavior is not claimed by these checks; fixture placement, visible
flicker, local light contrast, exact tracing audio, and the spoken incantation
remain pending user acceptance.
