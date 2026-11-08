# Comp29 validation

The implementation has been reviewed and refined through DSP, processor, editor,
realtime and actual VST3-host tests. Results below describe this Linux instance;
they do not claim native Windows/macOS or commercial-DAW certification.

## Automated coverage

| Suite | Checks | Coverage |
| --- | ---: | --- |
| DSP | 54 | Every ratio and timing, soft-knee boundaries, range, makeup, polarity-safe linking, HPF frequency behavior, program-dependent recovery, sample rates, invalid/extreme input |
| Processor | 49 | Bus negotiation and offsets, silent/disabled sidechain, automation, bypass, state, undo/redo, atomic availability, RMS/peak/clip ballistics |
| Editor | 73 | Attachments, host-to-UI changes, wheel, reset, normal/fine drag, switches, keyboard undo/redo, readable stepped labels, resizing and real-signal snapshots |
| Realtime | 27 | Bypass/unbypass transitions, host bypass, exact block-partition invariance with sample-aligned automation at 44.1/48/96/192 kHz, concurrent automation and meter polling, monitored heap/mutex calls |
| VST3 host | 25 | Scanning and loading the actual bundle, metadata, variable blocks within the negotiated maximum, stereo balance, state recall, external sidechain and bypass through the VST3 ABI |
| **Total** | **228** | Five completed suites |

All five suites passed with AddressSanitizer and UndefinedBehaviorSanitizer
configured to halt on errors. Project-owned source files and test files were
instrumented; third-party JUCE module objects were reused without instrumentation.
Leak checking remained enabled. The normal Release configuration was restored
and verified after sanitizer testing.

On Linux, the realtime suite interposes C++ allocation/deallocation and the
executable's references to malloc/calloc/realloc/free and pthread mutex
lock/try-lock. Across first callbacks, transitions and concurrent automation, it
observed **zero monitored heap calls and zero monitored mutex calls** inside
processing. Parameter edits and UI work occur outside the monitored audio scope.
This is a regression check on monitored calls, not a worst-case execution-time
proof or a guarantee about every shared-library, DAW or driver implementation.
The per-suite C++ counters are thread-local so unrelated JUCE timer initialization
cannot contaminate a callback measurement. Source review confirms no explicit
waits, system-clock calls, I/O or unbounded
retry loops in the processor path.

Processor tests deliberately exceed the prepare estimate to exercise fixed
scratch chunking. VST3 ABI tests respect `maxSamplesPerBlock`, which is a hard
contract between the VST3 host and plugin. A host test initially exceeded that
limit; its setup was corrected rather than treating invalid-host behavior as a
plugin processing failure.

## Refinements made during review

- Replaced abrupt bypass with a 5 ms linear dry/wet fade. Both host bypass and
  parameter bypass use the same implementation. The compressor stays warm while
  bypassed; meters measure the mixed output and display effective attenuation.
- Added a 10 ms detector-source transition and preserved external input buffers.
- Removed the hidden 20 Hz HPF clamp so positive displayed values match the DSP.
- Guarded finite output representation against overflow while retaining ordinary
  audio headroom above unity.
- Made stepped value labels read-only so partial numeric entries cannot select
  the wrong discrete setting.
- Retained lock-free status/meters, fixed storage and unchanged parameter IDs.

## Native Standalone smoke test

The normal Linux Standalone binary was launched on a local Xvfb display. It
remained running, painted the complete editor and displayed its hover tooltip.
The captured window was visually inspected:
`build/native-smoke/comp29-standalone.png`. The app and virtual display were then
stopped. Logs show the absent ALSA sequencer device; the default JUCE Standalone
input-mute guard remained active. This verifies native startup/rendering, not
hardware playback or a complete native-input interaction test.

## Still requiring native/manual validation

The saved CI workflow has not run in this session. It provides native GCC/Linux,
Apple Clang/macOS (including AU and `auval`) and MSVC/Windows builds and test jobs.
Signing, notarization, installer packaging and distribution identifiers also need
release review.

Before releasing, exercise:

1. Host scan, load, reopen and session recall in target DAWs. Route mono/stereo
   external sidechains, automate every control, and check host bypass.
2. Device sample-rate/buffer changes, transport stops/starts, offline rendering,
   repeated editor open/close, high-DPI displays and keyboard focus behavior.
3. Level-matched listening on full mixes, drums and bass-heavy material. Compare
   fixed/Auto release and HPF settings; check transient preservation and image.
4. Hardware/reference comparisons if a closer SSL circuit match is required. The
   present detector and auto-release curve are documented original models.

No audio hardware is exposed by this Cloud instance (`/dev/snd` is absent), so
real-device playback and subjective listening are not represented as completed.

## Test-install packaging

CPack creates a native archive with complete VST3/Standalone payloads (AU on
macOS), install instructions, runtime/build information, SHA-256 manifests and
matching Comp29/JUCE source. Only the Comp29Test install component is staged;
JUCE development installations, Git metadata and local build caches are excluded.

The Linux installer/uninstaller passed 30 checks using a disposable profile with
spaces in its path. Checks cover package integrity, destination conflicts,
modified/extra-file and symlink preservation, idempotent uninstall, and rollback
when the second commit fails. The copied VST3 also passed the 25-check host suite.
No normal user plugin directory was changed during these tests. The Linux
prebuilt requires GLIBC_2.38 and GLIBCXX_3.4.32; `ldd` is checked before installation.

The three-platform CI workflow now builds native test-install archives and runs
Linux package installation tests. Native macOS/Windows archive generation and
installation remain unexecuted in this session. See [INSTALL.md](INSTALL.md).

The sanitizer-enabled packaging rejection was also exercised: CPack failed with
an explicit diagnostic and emitted no test-install archive. The normal Release
configuration was restored afterwards.
