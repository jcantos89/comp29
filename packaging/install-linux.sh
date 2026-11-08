#!/bin/sh
set -eu
package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
. "$package_dir/linux-common.sh"
comp29_paths "$@"
comp29_verify_package
for target in "$plugin_target" "$binary_target"; do
    if [ -e "$target" ] || [ -L "$target" ]; then
        comp29_error "Already exists: $target. Uninstall the matching older package or move it aside first."
    fi
done
if ! dependencies=$(LC_ALL=C ldd "$package_dir/Standalone/Comp29" 2>&1); then
    printf '%s\n' "$dependencies" >&2
    comp29_error "Runtime library check failed; see INSTALL.md and BUILD_INFO.txt"
fi
case "$dependencies" in *'not found'*) printf '%s\n' "$dependencies" >&2; comp29_error "Missing runtime libraries; see INSTALL.md" ;; esac
vst_stage=
bin_stage=
installed_vst=0
installed_bin=0
success=0
comp29_cleanup() {
    set +e
    if [ "$success" -eq 0 ]; then
        [ "$installed_bin" -eq 0 ] || rm -f -- "$binary_target"
        [ "$installed_vst" -eq 0 ] || rm -rf -- "$plugin_target"
    fi
    [ -z "$bin_stage" ] || rm -f -- "$bin_stage"
    [ -z "$vst_stage" ] || rm -rf -- "$vst_stage"
}
trap 'comp29_cleanup' 0
trap 'exit 1' HUP INT TERM
mkdir -p -- "$vst3_dir" "$bin_dir"
vst_stage=$(mktemp -d "$vst3_dir/.comp29-install.XXXXXX")
bin_stage=$(mktemp "$bin_dir/.comp29-install.XXXXXX")
cp -R -- "$package_dir/VST3/Comp29.vst3" "$vst_stage/Comp29.vst3"
find "$vst_stage/Comp29.vst3" -type d -exec chmod 755 {} +
find "$vst_stage/Comp29.vst3" -type f -exec chmod 644 {} +
cp -- "$package_dir/Standalone/Comp29" "$bin_stage"
chmod 755 "$bin_stage"
mv -T --no-clobber -- "$vst_stage/Comp29.vst3" "$plugin_target"
[ ! -e "$vst_stage/Comp29.vst3" ] || comp29_error "Destination appeared during installation; nothing overwritten"
installed_vst=1
mv -T --no-clobber -- "$bin_stage" "$binary_target"
[ ! -e "$bin_stage" ] || comp29_error "Destination appeared during installation; rolling back"
installed_bin=1
success=1
printf 'Installed VST3: %s\nInstalled Standalone: %s\n' "$plugin_target" "$binary_target"
printf '%s\n' 'Restart/rescan your DAW and insert Comp29 on a stereo bus.' \
    'Launch Standalone with ~/.local/bin/comp29 (or the custom binary path above).' \
    'Keep this extracted package for uninstall-linux.sh. See INSTALL.md for testing.'
