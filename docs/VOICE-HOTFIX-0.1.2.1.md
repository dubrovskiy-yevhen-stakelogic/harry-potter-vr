# Voice pronunciation hotfix 0.1.2.1

## Scope

This hotfix extends the released 0.1.2 voice vocabulary. It adds no level and
does not import development-map work. The public build is 0.1.2.1-alpha, Android
versionCode 59. Existing casting, microphone lifecycle, model, timing checks and
confidence thresholds are unchanged.

Two full-word acoustic token paths supplement the six existing paths. They
cover pronunciation with different stress and more separated syllables. The
English keyword model can split an invented spell name into English-like
tokens; players are not expected to speak those token spellings.

## Local replay evidence

The second player supplied one recording containing exactly eight repetitions,
as confirmed by the sender. It was converted locally to mono PCM16 at 16 kHz.
No recording, sample-derived voice template, or transcript is shipped.

| Check | Released vocabulary | Hotfix vocabulary |
| --- | ---: | ---: |
| Events in the complete eight-repeat recording | 0 | 7 |
| Isolated repetitions recognized exactly once | 1/8 | 7/8 |
| Exactly-one events in 216 transformed clips | 35/216 | 93/216 |

The first isolated repetition remains missed. The 216 cases are synthetic tempo,
gain and leading-context variants of the same eight repetitions, not independent
speakers. They expose remaining sensitivity to signal conditions; the 7/8 result
must not be presented as general recognition accuracy.

Across 88 existing local fixtures, no previously accepted case lost hits. One
long author recording improved from 17 to 20 events; other counts were unchanged.
The existing near-word false positive on "prepend a word" remained at three
tested speaking rates. Of 15 added synthetic near-word negatives, one fast
"fleet been dormant" triggered; the other 14 did not. Raising new-path thresholds
enough to remove this event substantially reduced recognition of the supplied
spell, so the broader experimental vocabulary was retained.

No duplicate event was observed in any isolated repetition or transformed clip.
Recognition still requires an eligible locked target. Similar speech can
misfire while armed; unrelated speech outside the armed state cannot cast.

## Reproduction and boundaries

The production decoder and microphone worker are byte-identical between the
public 0.1.2 baseline and this hotfix. The replay harness links that decoder,
changes only the vocabulary path, starts a fresh stream for each independent
fixture and appends 500 ms of silence to flush the final word. Full-recording
tests use one continuous stream.

Final keyword SHA256:
`2A3B9CC97684B4838EEE3A0E4978095EFF14CA828F2D892B0B01ED139611848D`

`tools/voice/BUILD-VOICE-CHECKS.ps1 -Offline` passed four test suites and
81 APK-payload assertions. These checks cover lifecycle/policy and packaging;
they do not measure accents on a headset.

Testing on the author and one additional recording does not establish support
for every language, accent, room or headset microphone. No nationality-specific
claim is made. Recordings remain private local fixtures; normal player builds
exclude microphone-recording diagnostics, uploads and transcript logging.

## Public release verification

The release was built from public baseline
`9493f03e50d7e67918015cb3eb7be3275bf798e7`, not the newer development checkout.
All 65 host tests, four dedicated voice suites, 81 voice-payload assertions,
183 installer checks and 362 source-export checks passed. The only initial
host failure was a test expecting the previous welcome version and author-only
wording; both the labels and their test expectations were updated.

The signed ARM64 Release APK is non-debuggable, contains exactly 24 pinned
voice assets and has no game data or recording-diagnostic payload. Its signing
certificate matches the preceding public release. The player manifest lists
only maps 0, 1 and 2.

APK SHA256:
`B643075E01EA2C8ED92BD60E144D2AC11E13B448472B01324C005E29A52E893B`

This hotfix was not installed or launched during release preparation.
Offline replay and packaging checks do not establish live headset acceptance.
