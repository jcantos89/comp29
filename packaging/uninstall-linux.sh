#!/bin/sh
set -eu
package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
. "$package_dir/linux-common.sh"
comp29_paths "$@"
comp29_verify_package
# Validate both destinations before deleting either one. Unknown versions,
# symlinks and user-added/modified files are preserved rather than removed.
if [ -e "$plugin_target" ] || [ -L "$plugin_target" ]; then
    [ ! -L "$plugin_target" ] && [ -d "$plugin_target" ] || comp29_error "Refusing unexpected plugin path: $plugin_target"
    diff -qr -- "$package_dir/VST3/Comp29.vst3" "$plugin_target" >/dev/null 2>&1 || comp29_error "Plugin differs from this package; preserved: $plugin_target"
fi
if [ -e "$binary_target" ] || [ -L "$binary_target" ]; then
    [ ! -L "$binary_target" ] && [ -f "$binary_target" ] || comp29_error "Refusing unexpected Standalone path: $binary_target"
    cmp -s -- "$package_dir/Standalone/Comp29" "$binary_target" || comp29_error "Standalone differs from this package; preserved: $binary_target"
fi
[ ! -d "$plugin_target" ] || rm -rf -- "$plugin_target"
[ ! -f "$binary_target" ] || rm -f -- "$binary_target"
printf 'Removed matching Comp29 files from %s and %s\n' "$vst3_dir" "$bin_dir"
printf '%s\n' 'DAW sessions, presets, application settings and unrelated files were left in place.'
