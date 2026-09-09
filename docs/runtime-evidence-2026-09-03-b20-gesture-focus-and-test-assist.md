# B20 focused drawing and test-only accuracy assist

## Accepted prerequisite

The user-run B19 headset check accepted the aligned WandMesh/aim ray and
press-time target lock. The remaining runtime UX finding was that continuing
to show the cyan aim guide after the target had already been selected was
misleading during the physical Flipendo stroke.

## Behavior

- The cyan aim guide is visible while choosing a target.
- It disappears on the initial trigger press, stays hidden while held, and
  remains hidden on the release frame.
- The orange/green/red gesture trajectory remains visible, so the player sees
  the shape being drawn without a second moving line.
- The guide returns on the following neutral frame.

The user also requested a larger temporary error allowance. B20 adds the
explicit `--flipendo-test-assist` option used by `RUN-LATEST-VR.cmd`. It
multiplies the externally loaded HP1 `fAccuracy` radius by 1.75:

```text
authored accuracy radius = 0.0300
test effective radius    = 0.0525
authored pass threshold  = unchanged
```

The owned profile is not edited. Omitting the flag restores exact authored
scoring, which keeps the assist reversible and isolated to current playtesting.

## Verification

- A regression test checks all four guide states: neutral visible, press
  hidden, held hidden, and release hidden.
- A scorer-policy test checks `0.03 -> 0.0525` under assist and verifies that
  the non-assist path remains `0.03`.
- The complete feature-enabled suite passes 60/60 tests.
- The exact launcher arguments pass owned-data offline preflight with
  `[gesture.assist] enabled=1 authored_accuracy=0.030000 effective_accuracy=0.052500 multiplier=1.75 threshold=AUTHORED_UNCHANGED` and
  `[hp1.assets.validate] PASS openxr_started=0`.

No OpenXR session was started during B20 development. Headset acceptance of
the focused drawing UX and temporary tolerance is pending.
