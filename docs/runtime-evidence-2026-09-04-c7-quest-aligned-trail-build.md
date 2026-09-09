# Gate C7: press-only guide and wand-aligned trail

## Scope and claim boundary

Gate C7 fixes the two user-reported C6 visual faults without changing the
accepted recognition policy. It is built, audited, and installed on Quest 3.
Following the user's updated workflow, the agent did not launch it; runtime
acceptance is pending a manual user launch.

## Behavior

- Idle state emits no template or trail geometry.
- Trigger press freezes the existing aim-facing plane and reveals the genuine
  20-point external `FlipPattern`. Its authored first point maps directly to
  the press-time wand tip.
- While held, the orange ribbon is generated from the actual world-space wand
  tip samples. It is neither projected onto the template plane nor shifted
  along the aim axis, so its endpoint equals the current physical tip.
- Release still fits a separate scoring copy to the authored bounds. That
  green/red result remains for 0.8 seconds; recognition and press-time target
  lock are unchanged.
- The guide renderer uses `LESS_OR_EQUAL` depth for the unshifted trail instead
  of introducing a spatial bias to avoid equal-depth overlap.

## Verification

The real-data regression now checks all of the following:

~~~text
idle guide                         = hidden
recording template                 = visible
recording trail                    >= 2 points
live trail endpoint                = exact current wand tip
rotated quarter-size Flipendo      = accepted once
accepted fitted result             = retained and visible
press-time aim                     = preserved
~~~

All 5/5 host tests passed. Android NDK r27c compiled the ARM64 renderer with
warnings treated as errors. APK packaging, v3 signature, alignment, native
symbol, ABI, and proprietary-asset audits passed.

~~~text
path   = android/app/build/outputs/apk/debug/app-debug.apk
sha256 = B45CDA108BFF63FEEA341B13FF2E68E97BEBF52CC14912CA795775E87156BAF9
install result = Success
launch by agent = not performed
~~~

The user subsequently launched this exact installed candidate and reported
that everything worked. C7 is therefore the accepted baseline for idle-guide
visibility and wand-tip trail alignment.
