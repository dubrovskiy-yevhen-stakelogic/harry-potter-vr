# Gate A6: Flipendo event and isolated test target

Date: 2026-09-02

## Claim

Gate A6 proves a narrow gameplay seam after the accepted HP1 Flipendo score:

1. one accepted trigger release emits one portable `SpellCastEvent`;
2. one isolated fixed test target consumes that serial exactly once;
3. the stereo scene renders a short green spell beam;
4. the non-cubic crystal target performs one deterministic knockback arc and
   returns to its authored test position.

This is a PC Quest Link vertical slice. It is not retail-game actor selection,
`baseWand.CastSpell`, damage, progression, original effects, or Quest 3
standalone execution.

## Implementation boundary

`gesture_projection.rs` owns the accepted-score producer. The event contains:

- monotonic serial and `Flipendo` identity;
- release predicted-display timestamp;
- wand tip, aim origin, and normalized aim direction;
- returned HP1 score and authored lesson threshold.

The producer refuses to overwrite an unconsumed event. Its accounting requires
accepted scores to equal emitted events, and emitted events to equal consumed,
explicitly discarded, and still-pending events.

`calibration_scene.rs` owns the test-only consumer. It rejects disabled,
non-finite, below-threshold, duplicate, and reordered events. Its target is
explicitly logged as `FIXED_TEST_TARGET`; aim miss is diagnostic and does not
pretend that retail target selection exists. The visual is an octahedral
magenta crystal with a gold ring, not another calibration cube.

No proprietary coordinates, package payloads, executables, music, speech, or
derived asset dumps are stored in the repository. The legally owned
`C:\Program Files\HP` installation remained a read-only data source.

## Machine verification

Before the live run:

- strict golden baseline: 7/7 recorded retail files `MATCH`;
- default Cargo locked all-target tests: 19 passed, 0 failed;
- `gesture-projection` Cargo locked all-target tests: 35 passed, 0 failed;
- feature-enabled release build: passed;
- `git diff --check`: passed.

After the live run, the same strict verifier again reported `MATCH` for all
seven recorded retail files.

New unit coverage proves that an accepted synthetic Flipendo emits one event,
the event can be taken only once, a repeated serial is rejected, the target
reaction stays finite and returns home, and maximum combined dynamic geometry
still fits the fixed vertex buffer.

## Live Quest Link evidence

Command shape:

```powershell
Push-Location .\tools\xr-runtime-probe
& C:\Users\user\.cargo\bin\cargo.exe run --quiet --locked --release `
  --features gesture-projection --bin wgpu_stereo_clear -- `
  --loader C:\Dev\gta5-vr\third_party\OpenXR-1.1.61\bin\x64\openxr_loader.dll `
  --frames 9000 --flipendo-data-root 'C:\Program Files\HP'
Pop-Location
```

The successful live process reached `READY -> SYNCHRONIZED -> VISIBLE ->
FOCUSED`, submitted the first Gate A6 stereo frame, and tracked the right
Touch Plus controller. Its first dynamic snapshot contained 744 vertices,
including the fixed crystal and ring.

Three completed attempts crossed projection and the original-semantic scorer:

| Attempt | Score | Threshold | Result | Dispatch |
| --- | ---: | ---: | --- | --- |
| 1 | 0.346154 | 0.500000 | rejected | none |
| 2 | 0.024876 | 0.500000 | rejected | none |
| 3 | 0.847352 | 0.500000 | accepted | `SPELL_EVENT` |

For attempt 3 the log contains the same serial on both sides:

```text
[spell.event] serial=1 spell=Flipendo status=EMITTED_ON_ACCEPTED_RELEASE ...
[spell.target] serial=1 spell=Flipendo selection=FIXED_TEST_TARGET outcome=IMPULSE_APPLIED ...
```

The target diagnostic recorded `aim_miss_m=0.7916`, which is retained to make
the fixed-target limitation visible. The user confirmed in the headset that
the magenta crystal with gold ring appeared and that the accepted interaction
behaved as described.

The evidence logs are local ignored artifacts:

- `local/gate-a6-live-03.log` (12,893 bytes at capture);
- `local/gate-a6-live-03.err.log` (804 bytes at capture).

## Acceptance and limitation

Functional Gate A6 result: **PASS**.

The Link session later transitioned through `STOPPING -> IDLE` before the
requested 9000-frame aggregate and clean-exit report. Therefore this run is not
claimed as a new clean renderer-duration pass. Gate A4 remains the accepted
9000-frame renderer/input baseline, while A6 establishes only the new
event-to-target behavior and its live visual acceptance.

The next gate must connect this portable event seam to a clean-room game-world
host with real target identity and auditable gameplay calls. It must not
silently promote the fixed test target into evidence of retail integration.
