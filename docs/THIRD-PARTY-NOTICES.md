# Third-party runtime notices

HPVR is an independent community project. Original Harry Potter game data,
Warner Bros./EA logos, textures, models, music and speech are not included.
Game names describe the required user-owned data; no endorsement is implied.

This inventory describes dependencies linked by the C37 Quest build:

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
