# Offline voice casting

Voice mode recognizes the complete spoken **Flipendo** locally using neural
keyword spotting. It needs no account, online speech service or personal voice
enrollment. It is experimental and has only been tested by the author.

## Dependencies and builds

Run `FETCH-VOICE-DEPENDENCIES.ps1` to prepare the pinned model and Windows x64
test runtime. `-Offline` uses verified cached archives. Dependencies remain under
`local/neural-voice-dependencies`, outside the source distribution.

For Android, also run `BUILD-NEURAL-VOICE-RUNTIME.ps1`. It builds the pinned
sherpa-onnx C API with static ONNX Runtime for ARM64, CPU only and TTS disabled.
`-VerifyOnly` checks existing outputs without downloading or rebuilding.

The model is the Apache-2.0 licensed
[GigaSpeech 3.3M keyword model](https://k2-fsa.github.io/sherpa/onnx/kws/pretrained_models/index.html).
The runtime uses [sherpa-onnx 1.13.7](https://github.com/k2-fsa/sherpa-onnx/tree/917bed95c8e5c7c18aa4d69fea42e9ef8ef0a60e)
and ONNX Runtime 1.27.1. The encoder and joiner are INT8; the decoder is FP32.
`flipendo.keywords` contains complete canonical and alternate pronunciation
token paths, not a user-recorded template or a separately trained voice model.

`VOICE-ASSETS.psd1` pins the exact model/configuration/notice payload.
`NEURAL-RUNTIME-NOTICES.psd1` pins native dependency notices. Required notices
accompany distributed model files and binaries. APK validation rejects unlisted
voice assets and audio fixtures. Generic Android runtimes containing unrelated
TTS components are not used.

## Runtime behavior and privacy

Android stages the model to a versioned application-private directory and
verifies its files. Model loading is deferred until voice mode is enabled and
microphone permission is granted. Capture and recognition use one worker and
one inference thread, outside the renderer and GPU.

Input stays ready only during focused, tracked, playable challenge gameplay with
voice enabled and permission granted. Without an eligible locked target, input
is drained and discarded without neural decoding. A new target lock starts a
fresh decoder and excludes audio queued before that lock.

A result belongs to its target generation. Target changes invalidate in-flight
results. Disabling voice, losing tracking/focus, death, completion, game menus,
cutscenes or loading close input. Dialogue and Harry's own incantation temporarily
disarm recognition to avoid echo-triggered casts. A fresh generation allows
another cast while the trigger remains held.

Transient microphone errors use bounded retries. Six seconds without input
frames triggers an input retry; this is not a word deadline. After unsuccessful
recognition, 600 ms of quiet arms decoder renewal at the next onset, with a
bounded 320 ms RAM-only preroll. Every armed sample reaches the decoder; volume
alone cannot produce a spell.

Public builds do not save microphone audio, upload it, log transcripts or keep
permanent microphone history. **VOICE HINTS** is an independent saved setting,
off by default.

## Recognition and limitations

The decoder uses six complete pronunciation paths, beam size 16, keyword score 1,
one trailing blank and a default keyword threshold of 0.25. The PRE P END O
alternative has a stricter 0.35 threshold. These model scores are not calibrated
probabilities. Full keyword identity and bounded, monotonic word timing are
checked before dispatch.

Fast speech, accents and room noise can still cause misses. Near-sounding speech,
including “prepend a word”, can trigger while an eligible target is armed.
The supported paths are whole-word alternatives, not suffix wildcards. Testing
by one author does not establish reliability for every player or microphone.

## Tests

`BUILD-VOICE-CHECKS.ps1 -Offline` builds policy, worker-lifecycle and neural
decoder checks. Worker tests cover held-trigger repetition, long input, focus
and permission changes, stale events, retries and exceptions. Mocked worker
tests do not measure recognition accuracy.

Local PCM16 mono/16 kHz fixtures can be checked with:

```text
hpvr_voice_keyword_probe MODEL_DIRECTORY RECORDING.raw [KEYWORD_THRESHOLD]
hpvr_quest_voice_decoder_tests MODEL_DIRECTORY NEGATIVE.raw POSITIVE.raw [KEYWORD_THRESHOLD]
```

Include natural speech, quiet/fast variants, long waits, repeated commands and
unrelated words. The default check without a supplied negative recording is not
an unrelated-speech benchmark. The probes are not linked into the game; recordings
must stay outside source distributions and APKs.

## Developer recording diagnostics

Normal debug and all release APKs exclude recording code. An explicitly requested
`-VoiceDiagnostics` build uses a separate artifact and additionally requires
one-shot consent before capturing a bounded amount of armed input. It has no
automatic recording, upload or transcript feature.

Release tasks reject that option. APK verification and player packaging reject
diagnostic recording payloads. Never distribute a diagnostic APK or its captures.
