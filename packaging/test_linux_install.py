#!/usr/bin/env python3
"""Exercise the real extracted installer in an isolated profile, including rollback."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--host-test', type=Path)
    args = parser.parse_args()
    checks = 0

    def check(condition, message):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(message)

    def run(script, destinations, *, expect_success=True, env=None):
        result = subprocess.run([str(script), *destinations], text=True, capture_output=True,
                                env=env, timeout=60)
        check((result.returncode == 0) == expect_success,
              f'{script.name}: exit {result.returncode}\n{result.stdout}\n{result.stderr}')
        return result

    with tempfile.TemporaryDirectory(prefix='comp29 install test ') as temporary:
        root = Path(temporary)
        with tarfile.open(args.archive.resolve(), 'r:gz') as archive:
            archive.extractall(root / 'extracted', filter='data')
        packages = list((root / 'extracted').iterdir())
        check(len(packages) == 1, 'archive has exactly one top-level package directory')
        package = packages[0]
        install, uninstall = package / 'install-linux.sh', package / 'uninstall-linux.sh'
        vst_dir, bin_dir = root / 'profile with spaces/vst3', root / 'profile with spaces/bin'
        destinations = ['--vst3-dir', str(vst_dir), '--bin-dir', str(bin_dir)]
        plugin, binary = vst_dir / 'Comp29.vst3', bin_dir / 'comp29'
        run(install, ['--help'])
        run(install, ['--vst3-dir'], expect_success=False)
        run(install, ['--unknown'], expect_success=False)
        run(install, ['--vst3-dir', str(package / 'VST3')], expect_success=False)
        payload = package / 'Standalone/Comp29'
        original = payload.read_bytes()
        payload.write_bytes(original + b'tampered')
        run(install, destinations, expect_success=False)
        check(not plugin.exists() and not binary.exists(), 'tampered package leaves destinations untouched')
        payload.write_bytes(original)
        run(install, destinations)
        check(plugin.is_dir() and binary.is_file(), 'both products installed')
        check(binary.read_bytes() == original, 'installed Standalone matches packaged executable')
        check(binary.stat().st_mode & 0o111 == 0o111, 'installed Standalone has executable permissions')
        check((plugin / 'Contents/Resources/moduleinfo.json').is_file(), 'complete VST3 manifest installed')
        if args.host_test:
            library = next((plugin / 'Contents').glob('*-linux/Comp29.so'))
            result = subprocess.run([str(args.host_test.resolve()), str(library)], text=True,
                                    capture_output=True, timeout=120)
            check(result.returncode == 0, f'installed VST3 host checks failed:\n{result.stdout}\n{result.stderr}')
            print(result.stdout.strip())
        before = hashlib.sha256(binary.read_bytes()).hexdigest()
        run(install, destinations, expect_success=False)
        check(hashlib.sha256(binary.read_bytes()).hexdigest() == before, 'reinstall refusal preserves existing binary')
        binary.write_bytes(original + b'user modification')
        run(uninstall, destinations, expect_success=False)
        check(plugin.is_dir() and binary.exists(), 'modified binary prevents partial uninstall')
        binary.write_bytes(original)
        unknown = plugin / 'user-notes.txt'
        unknown.write_text('Preserve user work\n')
        run(uninstall, destinations, expect_success=False)
        check(unknown.exists() and binary.exists(), 'unknown bundle contents preserved')
        unknown.unlink()
        run(uninstall, destinations)
        check(not plugin.exists() and not binary.exists(), 'matching installation removed')
        run(uninstall, destinations)
        sentinel = root / 'unrelated plugin'; sentinel.mkdir()
        (sentinel / 'keep.txt').write_text('unchanged\n')
        plugin.symlink_to(sentinel, target_is_directory=True)
        run(install, destinations, expect_success=False)
        run(uninstall, destinations, expect_success=False)
        check((sentinel / 'keep.txt').read_text() == 'unchanged\n', 'symlink target is preserved')
        plugin.unlink()
        # Fail the second atomic move after the VST3 has been committed. The
        # installer must remove only its own new products and staging files.
        fake_bin = root / 'injected command failure'; fake_bin.mkdir()
        real_mv = shutil.which('mv')
        check(real_mv is not None, 'GNU mv is available for rollback exercise')
        shim = fake_bin / 'mv'
        shim.write_text(f'#!{sys.executable}\nimport os, sys\n'
                        'if sys.argv[-1].endswith("/comp29"): sys.exit(1)\n'
                        f'os.execv({real_mv!r}, [{real_mv!r}, *sys.argv[1:]])\n')
        shim.chmod(0o755)
        failure_env = os.environ.copy()
        failure_env['PATH'] = str(fake_bin) + os.pathsep + failure_env['PATH']
        run(install, destinations, expect_success=False, env=failure_env)
        check(not plugin.exists() and not binary.exists(), 'late installation failure rolls back both products')
        check(not list(vst_dir.glob('.comp29-install.*')) and not list(bin_dir.glob('.comp29-install.*')),
              'rollback cleans staging files')
        check((sentinel / 'keep.txt').exists(), 'rollback leaves unrelated files alone')
    print(f'{checks} Linux package/install checks passed; real user installation paths untouched')


if __name__ == '__main__':
    main()
