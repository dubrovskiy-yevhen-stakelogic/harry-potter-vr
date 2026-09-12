# Third-party runtime notices

HPVR is an independent community project. Original Harry Potter game data,
Warner Bros./EA logos, textures, models, music and speech are not included.
Game names describe the required user-owned data; no endorsement is implied.

This inventory describes dependencies used by the Quest build:

- Khronos OpenXR loader 1.1.43, distributed unmodified as
  org.khronos.openxr:openxr_loader_for_android:1.1.43. Copyright 2017-2024,
  The Khronos Group Inc. The resolved AAR provides Apache License 2.0 at
  META-INF/LICENSE; that exact text accompanies this release. The generated
  headers identify Apache-2.0 OR MIT; this distribution uses Apache-2.0.
  [Upstream release](https://github.com/KhronosGroup/OpenXR-SDK/tree/release-1.1.43).
- JsonCpp, included by the OpenXR loader. Its Public Domain/MIT license notice
  from the same official OpenXR release is reproduced below.
  [Upstream license](https://github.com/KhronosGroup/OpenXR-SDK/blob/release-1.1.43/src/external/jsoncpp/LICENSE).
- Android native_app_glue, from Android NDK 27.2.12479018. Copyright (C) 2010,
  The Android Open Source Project. Apache License 2.0. No source modification
  is made by this build.
- Statically linked C++ runtime and compiler support from Android NDK
  27.2.12479018 (libc++, libc++abi and compiler runtime). The installed
  NDK NOTICE and NOTICE.toolchain accompany the release verbatim, including
  Apache-2.0 WITH LLVM-exception, legacy LLVM/MIT and applicable component
  notices. These are aggregate toolchain notices, not a statement that every
  toolchain component is included in the application.

The release script retains these texts under APK META-INF/HPVR-NOTICES/ and
as readable THIRD-PARTY files beside the APK. It does not bundle Gradle, JDK,
ADB, FFmpeg, the NDK toolchain itself or proprietary game assets.

## Offline voice casting

Voice casting uses a CPU-only build of [sherpa-onnx 1.13.7](https://github.com/k2-fsa/sherpa-onnx/tree/917bed95c8e5c7c18aa4d69fea42e9ef8ef0a60e),
licensed under Apache-2.0, and [ONNX Runtime 1.27.1](https://github.com/microsoft/onnxruntime/tree/v1.27.1),
licensed under MIT. Microphone samples are processed locally; public builds
do not upload or save them. TTS, espeak-ng and JNI are disabled in the native
build. Generic upstream Android packages with TTS enabled are not used.

HPVR's native preparation script makes one documented compatibility change to
`sherpa-onnx/csrc/session.cc`: its two Android NNAPI guards additionally require
`HPVR_SHERPA_ENABLE_NNAPI`, which this CPU-only build leaves unset. This avoids
headers absent from the pinned ONNX Runtime Android package. The script pins
the source file's SHA-256 both before and after that patch; no NNAPI or GPU
inference provider is enabled.

The included neural weights are
`sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01`, by pkufool.
The README in the [official model archive](https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/sherpa-onnx-kws-zipformer-gigaspeech-3.3M-2024-01-01.tar.bz2)
explicitly licenses the weights under Apache-2.0 and identifies GigaSpeech XL
as the training corpus. Both that README and the complete Apache license are
included with the model. No training recordings, game speech or player
recordings are distributed. The custom `Flipendo` keyword token sequences are
HPVR configuration, not a separately trained model.

The static runtime also contains the following third-party components:

- kaldi-decoder 0.3.0, kaldi-native-fbank 1.22.3, kaldifst 1.8.0,
  OpenFst 1.8.5-2026-07-09 and simple-sentencepiece 0.7: Apache-2.0.
- nlohmann/json 3.12.0: MIT.
- Kiss FFT, commit `febd4caeed32e33ad8b2e0bb5ea77542c40f18ec`:
  BSD-3-Clause, with its copyright notice.
- Eigen 5.0.1: MPL-2.0 and the component-specific BSD, Apache and MINPACK
  notices included by upstream. Its six license files are retained. The
  unmodified covered source is available from the
  [exact Eigen release](https://gitlab.com/libeigen/eigen/-/tree/5.0.1);
  the native preparation script pins and downloads that source.
- ONNX Runtime's transitive dependencies: the complete, unmodified upstream
  `ThirdPartyNotices.txt` is included, rather than reducing these notices to
  ONNX Runtime's own MIT license.

`tools/voice/NEURAL-RUNTIME-NOTICES.psd1` records the exact upstream URL,
length and SHA-256 of each native notice. `VOICE-ASSETS.psd1` pins all 24
permitted model/configuration/notice files. The APK carries them in
`assets/hpvr-voice/`; release packaging also places readable copies of the
notices beside the APK. Native source and the build recipe are separate from
HPVR's original code. No source or binary from an unknown-provenance game
engine is used by this voice implementation.

## JsonCpp license (verbatim)

The JsonCpp library's source code, including accompanying documentation,
tests and demonstration applications, are licensed under the following
conditions...

Baptiste Lepilleur and The JsonCpp Authors explicitly disclaim copyright in all
jurisdictions which recognize such a disclaimer. In such jurisdictions,
this software is released into the Public Domain.

In jurisdictions which do not recognize Public Domain property (e.g. Germany as of
2010), this software is Copyright (c) 2007-2010 by Baptiste Lepilleur and
The JsonCpp Authors, and is released under the terms of the MIT License (see below).

In jurisdictions which recognize Public Domain property, the user of this
software may choose to accept it either as 1) Public Domain, 2) under the
conditions of the MIT License (see below), or 3) under the terms of dual
Public Domain/MIT License conditions described here, as they choose.

The MIT License is about as close to Public Domain as a license can get, and is
described in clear, concise terms at:

   http://en.wikipedia.org/wiki/MIT_License

The full text of the MIT License follows:

========================================================================
Copyright (c) 2007-2010 Baptiste Lepilleur and The JsonCpp Authors

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy,
modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
========================================================================
(END LICENSE TEXT)

The MIT license is compatible with both the GPL and commercial
software, affording one all of the rights of Public Domain with the
minor nuisance of being required to keep the above copyright notice
and license text in the source code. Note also that by accepting the
Public Domain "license" you can re-license your copy using whatever
license you like.
