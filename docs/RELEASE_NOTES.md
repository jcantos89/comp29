Comp29 test build: an SSL-inspired stereo bus compressor for local evaluation.

Download the native package for your operating system and CPU, plus its SHA-256
checksum file. Packages include VST3 and Standalone, installation instructions,
matching source and license notices. macOS packages additionally include AU.
Each platform's assets appear only after that platform's CI checks pass.

Linux quick install (x86_64):

```sh
sha256sum -c Comp29-0.1.0-Linux-x86_64.tar.gz.sha256
tar -xzf Comp29-0.1.0-Linux-x86_64.tar.gz
cd Comp29-0.1.0-Linux-x86_64
./install-linux.sh
```

Install as your normal user, without sudo. Restart/rescan the DAW for Comp29.
Standalone is installed as `~/.local/bin/comp29`. For uninstall, run the matching
package's `./uninstall-linux.sh`.

Full instructions are in INSTALL.md inside every package and in the repository:
https://github.com/jcantos89/comp29/blob/main/docs/INSTALL.md

The Linux runtime requirements are recorded in BUILD_INFO.txt inside the native
package; the locally validated build requires glibc 2.38 and GLIBCXX_3.4.32.
macOS/Windows packages are unsigned test builds. Commercial-DAW compatibility,
real-device playback and subjective listening still need local evaluation.

Do not use GitHub's automatic "Source code" downloads for binary installation.
