#!/bin/bash
# SPDX-FileCopyrightText: 2026 Pascal Klose
# SPDX-License-Identifier: GPL-3.0-or-later
#
# istar-wheelstar-ets2-plugin against a made-up Steam library: copies, not links (#27), and a build copied there by
# plugin.py stays untouched.
#
#   ets2plugintest.sh <path to istar-wheelstar-ets2-plugin>

set -euo pipefail

SCRIPT=$(realpath "$1")
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

export HOME=$WORK/home
export WHEELSTAR_PLUGIN_DLL=$WORK/package.dll
PLUGINS="$HOME/.local/share/Steam/steamapps/common/Euro Truck Simulator 2/bin/win_x64/plugins"
mkdir -p "${PLUGINS%/plugins}"
echo v1 > "$WHEELSTAR_PLUGIN_DLL"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

# An old link is replaced by a copy.
mkdir -p "$PLUGINS"
ln -s "$WHEELSTAR_PLUGIN_DLL" "$PLUGINS/wheelstar_telemetry.dll"
"$SCRIPT" link > /dev/null
[[ -f $PLUGINS/wheelstar_telemetry.dll && ! -L $PLUGINS/wheelstar_telemetry.dll ]] || fail "link left a link"
"$SCRIPT" status | grep -q '^copied ' || fail "status after link"

# An upgrade of the package: status notices, link refreshes.
echo v2 > "$WHEELSTAR_PLUGIN_DLL"
"$SCRIPT" status | grep -q '^outdated ' || fail "status did not see the outdated copy"
"$SCRIPT" link > /dev/null
[[ $(<"$PLUGINS/wheelstar_telemetry.dll") == v2 ]] || fail "link did not refresh the copy"

# unlink removes copy, marker and the then empty folder.
"$SCRIPT" unlink > /dev/null
[[ ! -e $PLUGINS ]] || fail "unlink left $PLUGINS behind"

# A build without the marker (plugin.py install) is kept by link and unlink.
mkdir -p "$PLUGINS"
echo dev > "$PLUGINS/wheelstar_telemetry.dll"
"$SCRIPT" link | grep -q '^kept ' || fail "link did not keep the build"
"$SCRIPT" unlink > /dev/null
[[ $(<"$PLUGINS/wheelstar_telemetry.dll") == dev ]] || fail "the build was touched"

echo "ok"
