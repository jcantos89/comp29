# Comp29

Comp29 is a JUCE 8 / C++17 stereo bus compressor with a VCA-style, log-domain
gain envelope, inspired by the SSL G-Series bus-compressor workflow. It is an
original feed-forward model, not a circuit-identical or hardware-certified clone.

Formats: VST3 and Standalone on Linux, macOS and Windows; AU additionally on
macOS. Linux builds and automated VST3 hosting have been validated. Native macOS
and Windows validation is pending execution of the included CI workflow.

## Test installation

Download native test packages from [GitHub Releases](https://github.com/jcantos89/comp29/releases).
Tagged builds publish a prerelease automatically after native build/test checks.
Choose the archive matching your OS/CPU and its `.sha256` companion. Download the
archive as a release asset; GitHub's automatic "Source code" archives do not
contain compiled plugins.

See [installation and first-test instructions](docs/INSTALL.md). The locally
validated package is Linux x86_64; it installs VST3 to `~/.vst3/Comp29.vst3` and
Standalone to `~/.local/bin/comp29`, without sudo. It includes install/uninstall
scripts, SHA-256 checksums, build/runtime details, matching source and notices.

After a normal Release build, generate the native archive:

```sh
cpack --config build/CPackConfig.cmake -C Release
```

The archive and its `.sha256` companion appear in `build/packages/`. CPack stages
files privately; generating a package does not install into your own plugin
folders. Linux packages use `.tar.gz`, macOS adds AU/app bundles in `.tar.gz`, and
Windows uses `.zip`. Native macOS/Windows packages still need their CI jobs to run.

## Build

Requirements: CMake 3.22+, Git and a C++17 compiler (GCC, Clang or MSVC).
JUCE 8.0.17-security is fetched at a pinned Git object. To use a source checkout
without fetching, pass `-DCOMP29_JUCE_SOURCE_DIR=/path/to/JUCE`.

- **Manjaro:** `base-devel cmake git pkgconf alsa-lib libx11 libxrandr libxinerama
  libxcursor freetype2 fontconfig mesa`. JACK is optional.
- **Debian/Ubuntu:** ALSA, X11/Xrandr/Xinerama/Xcursor, FreeType, Fontconfig and
  OpenGL development packages, plus pkg-config, CMake and a compiler. The CI
  workflow lists the exact package names used on Ubuntu.
- **macOS:** Xcode command-line tools and the macOS SDK. For universal binaries,
  add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` during configuration.
- **Windows:** Visual Studio 2022, Desktop development with C++, and a Windows SDK.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 3
```

Artifacts are under `build/Comp29_artefacts/Release/`. The `.vst3` directory is a
bundle: copy the whole bundle into the host's VST3 search path. Automatic copying
into system plugin folders is disabled. Signing, notarization and public installer
packaging remain separate release tasks; portable test archives are provided by
CPack. Finalize the manufacturer/bundle identifiers
before public distribution and retain them for session compatibility.

## Controls

| Parameter ID | Values | Default |
| --- | --- | --- |
| `threshold` | -60 to +12 dBFS | -12 dBFS |
| `ratio` | 1.2, 1.5, 2, 4, 10, INF | 4:1 |
| `attack` | 0.03, 0.1, 0.3, 1, 3, 10, 30 ms | 10 ms |
| `release` | 0.03, 0.1, 0.3, 0.4, 1, 2 s | 0.3 s |
| `autoRelease` | Off / On | Off |
| `makeup` | -12 to +24 dB | 0 dB |
| `range` | 0 to 30 dB maximum attenuation | 20 dB |
| `sidechainHpf` | Off (0), 1 to 300 Hz | 95 Hz |
| `externalSidechain` | Internal / External | Internal |
| `bypass` | Off / On | Off |

Choice parameters store indices into the ordered DSP tables. All ten parameters
use stable APVTS IDs and version hints for DAW automation. Saved state has
`schemaVersion=1`; malformed binary/XML, foreign roots and future schemas are
ignored. Undo/redo is backed by JUCE's UndoManager.

For a starting bus-compression setting, try 2:1 or 4:1, attack 10 or 30 ms, release
0.1/0.3 s or Auto, and a threshold giving a few dB of reduction. Match perceived
loudness with make-up gain when comparing bypass. Range zero disables attenuation
while still applying make-up gain. INF sets an infinite static ratio; finite
attack and lack of lookahead mean this is not a brickwall/true-peak limiter.

Main input/output layouts must match: mono or stereo. The optional second input
bus accepts mono/stereo sidechain audio. Enable and route that bus in the DAW,
then select External Sidechain. A disabled/unavailable bus falls back to internal
detection; an enabled silent bus remains silent. The Standalone wrapper does not
provide DAW-style external routing.

Stereo linking is hardwired and displayed as a read-only indicator. The detector
averages rectified, filtered L/R signals, avoiding opposite-polarity cancellation,
and applies identical gain to both audio channels. Mono and identical dual-mono
signals produce the same reduction. A hard-panned signal drives the detector
less than the same signal present at full amplitude on both channels.

## DSP and realtime design

- A fixed 6 dB quadratic soft knee joins the linear compression regions.
- Attack/release values are time constants (63.2% movement per constant), not
  full-settling times. Parameters use 10 ms exponential smoothing.
- Auto Release combines current reduction and a 1.5-second history, giving a
  recovery constant from 60 ms to 1.2 s. Lower reduction recovers faster; sustained
  events leave a slower release component.
- A unity-at-Nyquist one-pole HPF affects only the detector. Every displayed
  positive cutoff is used, including values below 20 Hz. Zero bypasses the HPF.
- Bypass crossfades linearly to dry unity gain over 5 ms, including host bypass.
  The envelope continues tracking while bypassed, avoiding stale recovery when
  re-enabled. Detector-source changes crossfade over 10 ms.
- Fixed 256-sample scratch arrays (4 KiB total) handle arbitrary processor block
  sizes in chunks. There is no lookahead or added latency. VST3 hosts must still
  respect their negotiated `maxSamplesPerBlock` contract.
- Processing only reads cached lock-free parameter atomics. It performs no
  allocation, deallocation, mutex locking, state serialization, UI callbacks,
  wall-clock calls or I/O. Float/32-bit atomics are checked for lock-free support
  at compile time. Undo, APVTS state copying and XML work stay off the audio path.
- Normal floating-point headroom above 0 dBFS is preserved. Non-finite input is
  sanitized; amplified values beyond float representation saturate at `FLT_MAX`
  rather than producing infinity. No audio-level clipping or saturation is added.

Call `prepareToPlay` before processing and serialize lifecycle/layout changes as
required by JUCE. DSP mutation belongs to the audio thread; UI/host parameter
changes pass through APVTS. State restore and undo are not audio-thread operations.
The plugin accepts float audio and calculates the compressor internally in double
precision. It reports zero latency and zero audible tail.

## Editor and meters

The vector editor defaults to 1000 x 720 and resizes proportionally from 850 x 612
to 1500 x 1080. No external image assets or fonts are required.

Drag/scroll knobs; Shift-drag uses finer velocity sensitivity. Double-click the
rotary area to reset. Continuous values can also be typed. Stepped value labels
are read-only, preventing partial text entries from selecting the wrong ratio or
time. Tab focuses controls and hover shows tooltips. Auto Release dims the manual
release knob while preserving its setting.

Undo/redo buttons and Ctrl/Cmd+Z / Ctrl/Cmd+Shift+Z operate while the editor is
focused. Completed UI gestures flush the APVTS mirror on the message thread to
keep rapid edits in separate undo transactions.

Output needles are VU-style RMS meters: **0 VU = -18 dBFS RMS**. A 65 ms exponential
power follower approaches a constant level in roughly 300 ms; this is not an
IEC-certified VU instrument. Digital readings show dBFS RMS and held sample peaks.
Peaks hold for 750 ms then fall at 12 dB/s. Clip lamps latch signals at or above
0 dBFS for 1.5 seconds of processed audio. The dedicated GR meter shows attenuation
with a negative dB readout. Output meters include make-up gain and the bypass fade.

Measurement uses fixed processor storage. Independent atomic fields transfer
meter values to a 30 Hz UI timer; readings are not a coherent multi-field snapshot.
An atomic block counter lets the UI decay frozen readings when processing stops.
Sidechain availability is also published atomically, avoiding UI reads of mutable
host layouts.

## Validation

Build all checks, including a separate host that loads the built VST3:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCOMP29_BUILD_TESTS=ON -DCOMP29_BUILD_HOST_TESTS=ON
cmake --build build --config Release --parallel 3
ctest --test-dir build -C Release --output-on-failure --timeout 120
```

The DSP-only suite also works without JUCE/CMake:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  Source/DSP/SSLCompressor.cpp Tests/SSLCompressorTests.cpp \
  -o /tmp/comp29-dsp-tests
/tmp/comp29-dsp-tests
```

For GCC/Clang sanitizer checks, configure with `-DCOMP29_ENABLE_SANITIZERS=ON`,
build and run CTest. ASan/UBSan instrument all project sources and tests while
reusing uninstrumented third-party JUCE objects. Sanitized VST3 builds skip JUCE's
unsanitized external manifest helper; the sanitizer-linked host loads the plugin
instead. Turn the option **OFF** and rebuild for distributable Release artifacts.

Current Linux/GCC 14.2 results: **228 checks pass**, both normally and with
ASan/UBSan. See [validation details](docs/VALIDATION.md) for coverage and limits.
Rendered editor images are in `build/ui-snapshots/`. Native Linux Standalone
startup and tooltip rendering were also checked on Xvfb; a captured window is in
`build/native-smoke/comp29-standalone.png`. Audio-device playback is untested.

[CI](.github/workflows/build.yml) builds/tests Linux, macOS and Windows, saves
native test-install archives, tests Linux install/uninstall, and runs `auval` on macOS. It is saved but has not run remotely in this
session. Hardware listening comparisons and commercial-DAW compatibility testing
remain manual release checks.

The Linux package passed 30 installer checks in isolated paths (including spaces,
checksum failure, overwrite refusal, rollback, modified-file preservation and
uninstall). The installed bundle also passed the existing 25-check VST3 host
suite. To repeat packaging validation locally (Python 3.12+):

```sh
python3 packaging/test_linux_install.py \
  --archive build/packages/Comp29-0.1.0-Linux-x86_64.tar.gz \
  --host-test build/Comp29HostTests_artefacts/Release/Comp29HostTests
```

## Source layout

```text
Source/
  DSP/SSLCompressor.h/.cpp   Compression, smoothing and detector HPF
  PluginProcessor.h/.cpp    APVTS, routing, crossfades, state and metering
  PluginEditor.h/.cpp       LookAndFeel, controls and animated meters
Tests/                      DSP, processor, editor, realtime and VST3 host checks
```

In this Cloud instance, source the ignored `build-tools/activate-comp29.sh` before
incremental builds. Dependencies live in ignored local directories because system
installation was unavailable. Signed Debian metadata and package checksums were
used for local libraries; no verification was disabled. Local ALSA-header warnings
are external to Comp29 source. These local tools are not needed on machines with
the normal prerequisites installed.

See LICENSE for repository licensing. JUCE has separate licensing requirements.
