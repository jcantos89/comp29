# Install and test Comp29 0.1.0

Close your DAW and any running Comp29 instance before installing, updating or
uninstalling. Use the native package for your operating system and CPU. The
current locally tested package is **Linux x86_64**. macOS and Windows packages
are produced by the native CI jobs; those jobs have not run in this session.

Each archive contains the complete VST3 bundle, Standalone application, install
instructions, BUILD_INFO.txt, SHA256SUMS, license notices and matching project/JUCE
source. A macOS archive additionally contains the AU component. This is a private
test build, without a notarized/signed public installer.

## Linux / Manjaro: install the prebuilt package

The current x86_64 Linux binary needs **glibc 2.38+** and a libstdc++ providing
**GLIBCXX_3.4.32** (GCC 13.2+ runtime). BUILD_INFO.txt records the actual runtime
symbols required by each native package. The installer uses `ldd` to detect missing
libraries before copying anything. A current Manjaro installation should provide
these runtimes. An older distribution may require building from source on that
machine instead.

On an up-to-date Manjaro system, the normal runtime libraries can be installed with:

```sh
sudo pacman -S --needed alsa-lib fontconfig freetype2 libx11 libxrandr libxinerama libxcursor mesa
```

The scripts use standard GNU coreutils, diffutils and `ldd`; no Python or compiler
is needed to install the binary package. The Standalone window needs a working
X11/XWayland desktop session.

1. Put the archive and its `.sha256` companion in the same directory. Verify it:

   ```sh
   sha256sum -c Comp29-0.1.0-Linux-x86_64.tar.gz.sha256
   ```

2. Extract the complete archive, then enter its folder:

   ```sh
   tar -xzf Comp29-0.1.0-Linux-x86_64.tar.gz
   cd Comp29-0.1.0-Linux-x86_64
   ```

3. Install as your regular user, **without sudo**:

   ```sh
   ./install-linux.sh
   ```

   Default destinations:

   - VST3: `~/.vst3/Comp29.vst3/` (the whole directory).
   - Standalone: `~/.local/bin/comp29`.

   The installer verifies payload checksums, stages both files, and rolls back its
   own copied files if a later step fails. It refuses to overwrite any existing
   Comp29 file or bundle. For an update, uninstall the matching older package or
   move its two files aside first.

   For a custom DAW path or isolated test directory:

   ```sh
   ./install-linux.sh --vst3-dir "$HOME/my-vst3" --bin-dir "$HOME/my-audio-bin"
   ```

   Add a custom VST3 directory to your DAW's plugin search paths.

4. Restart your DAW and rescan VST3 plugins. Look for **Comp29**, vendor
   **Comp29 Audio**, in the dynamics/effects category. In REAPER, for example,
   open Preferences > Plug-ins > VST, check the search path, and rescan. The
   package does not contain VST2, CLAP or LV2.

5. Launch Standalone separately with:

   ```sh
   "$HOME/.local/bin/comp29"
   ```

   Open Options > Audio/MIDI Settings and select the audio device, sample rate
   and buffer size. Standalone starts with input muted; its Settings button opens
   the controls for enabling input. A DAW plugin does not use that Standalone mute
   setting. `~/.local/bin` need not be on PATH when using the command above.

Keep the archive/extracted folder for uninstalling. To remove the installed
matching files, close the DAW/app, return to this package folder, and run:

```sh
./uninstall-linux.sh
```

Pass the same `--vst3-dir` and `--bin-dir` overrides if you used custom locations.
The uninstaller checks both destinations before removing either, and refuses to
remove changed files, another version, symlinks or a bundle containing extra
files. Sessions, presets and application preferences are retained. If a matching
package folder was deleted, re-extract its original archive first.

## First DAW test

1. Insert Comp29 on a stereo mix/drum bus. Start at Ratio 2:1 or 4:1, Attack
   10 ms, Release 0.3 s, SC Filter 95 Hz, Make-up 0 dB, Range 20 dB.
2. Play audio and lower Threshold until the GR meter shows about 1–3 dB. Check
   that L/R output meters move and that the stereo balance remains stable.
3. Adjust Make-up to compare similar perceived loudness. Toggle Bypass; it should
   transition smoothly to the original signal. Range zero disables attenuation,
   while Make-up still changes level.
4. Enable Auto Release. The manual Release control dims and retains its value;
   turn Auto off to restore manual operation.
5. Move controls, test wheel/double-click reset, and use Undo/Redo. Automate a
   parameter in the DAW, save the session, close it, and reopen to check recall.
6. To test external detection, route another track to Comp29's optional sidechain
   input and enable EXT SIDECHAIN. An enabled but silent bus should not trigger
   compression. With the bus disabled, detection falls back to the main input.

0 VU on the output needles equals -18 dBFS RMS. The digital RMS/held-peak readouts
use dBFS; the GR meter shows attenuation in dB. A lit clip indicator means output
samples reached 0 dBFS or higher. INF is an infinite compression ratio with finite
attack, not a true-peak limiter or an output ceiling.

## macOS native package

Use a native macOS archive from a successful CI build, matching Apple Silicon,
Intel or a universal build. Extract it and copy whole bundles:

| Payload | Per-user destination |
| --- | --- |
| `VST3/Comp29.vst3` | `~/Library/Audio/Plug-Ins/VST3/Comp29.vst3` |
| `AU/Comp29.component` | `~/Library/Audio/Plug-Ins/Components/Comp29.component` |
| `Standalone/Comp29.app` | `~/Applications/Comp29.app` |

Create the destination folders if needed. Restart/rescan the DAW. Logic uses the
AU component; other hosts may use VST3. For a Terminal AU check after copying:

```sh
auval -v aufx C029 Cp29
```

Test packages are not notarized public releases; macOS may require approval via
its normal security prompts. No global security settings need to be disabled.
To uninstall, quit the app/DAW and remove only the copied Comp29 bundles. Native
macOS installation and `auval` have not been validated in this Cloud instance.

## Windows native package

Use the x64 Windows ZIP from a successful native CI build. Extract the whole
archive; the Linux binary will not run on Windows.

- Copy the whole `VST3\Comp29.vst3` folder to
  `C:\Program Files\Common Files\VST3\Comp29.vst3`. Explorer may request administrator
  approval for that standard system-wide location. Hosts supporting the VST3
  per-user location can instead use `%LOCALAPPDATA%\Programs\Common\VST3`.
- Copy `Standalone\Comp29.exe` to a folder of your choice and run it.
- If Windows reports missing MSVC runtime DLLs, install Microsoft's official
  [Visual C++ x64 Redistributable](https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist).
- Restart/rescan the DAW and insert Comp29 as a VST3 effect. Windows test builds
  are unsigned and have not been validated on a native Windows machine here.

To uninstall, close the DAW/app and remove only the copied Comp29 folder and EXE.

## Checksums and troubleshooting

The archive's `.sha256` file checks the download. The extracted `SHA256SUMS` file
checks its payload; Linux can verify it with `sha256sum --quiet -c SHA256SUMS`.
On macOS use `shasum -a 256 -c SHA256SUMS`. In PowerShell, use
`Get-FileHash .\Comp29-0.1.0-Windows-x86_64.zip -Algorithm SHA256` and compare the
result with the archive's `.sha256` companion.

- **Plugin not found:** check CPU/OS compatibility and the VST3 search path. Copy
  the complete bundle, not just Comp29.so or the inner executable, then rescan.
- **Linux “GLIBC/GLIBCXX … not found”:** the runtime is older than this binary
  requires. Build natively from the included source or use a compatible native
  package. Do not copy libc/libstdc++ files out of this Cloud environment.
- **Linux missing libraries:** run `ldd Standalone/Comp29` in the extracted package
  and install the named runtime packages using your distribution's package manager.
- **No Standalone input:** choose a valid audio device and review the input-mute
  setting in Audio/MIDI Settings. This Cloud container has no audio hardware;
  real-device playback still needs local testing.
- **External sidechain does nothing:** enable the host's auxiliary bus and route a
  signal to it, then enable EXT SIDECHAIN. A silent enabled bus stays silent.

## Build a native package from the included source

Install the development prerequisites described in `source/Comp29/README.md`,
then run from this extracted package folder:

```sh
cmake -S source/Comp29 -B build-native -DCMAKE_BUILD_TYPE=Release \
  -DCOMP29_JUCE_SOURCE_DIR="$PWD/source/JUCE" \
  -DCOMP29_BUILD_TESTS=ON -DCOMP29_BUILD_HOST_TESTS=ON \
  -DCOMP29_ENABLE_SANITIZERS=OFF
cmake --build build-native --config Release --parallel 3
ctest --test-dir build-native -C Release --output-on-failure --timeout 120
cpack --config build-native/CPackConfig.cmake -C Release
```

CMake uses the included JUCE checkout, so no framework download is needed. Native
archives and SHA-256 companions appear under `build-native/packages/`. Packaging
rejects sanitizer configurations and, on Linux, sanitizer-linked artifacts.

See the included project LICENSE, licenses/JUCE-LICENSE.md and source/JUCE for
framework/dependency notices. The project's CC0 license does not replace JUCE's
separate licensing terms. Matching source is included for reproducibility.
