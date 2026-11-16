#!/bin/sh
# SPDX-FileCopyrightText: 2026 shreyasajj
#
# SPDX-License-Identifier: BSD-2-Clause

# User-only development build reusing an installed Flimmer runtime and libraries.
# The YAML manifest remains the clean production build.
set -eu
app_id=io.github.jclsn.Flimmer
source_root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
stage_root="${FLIMMER_BRIDGE_BUILD:-$HOME/.cache/flimmer-bridge-build}"
keychain_root="$stage_root/qtkeychain-source"
mkdir -p "$stage_root"
flatpak info --user "$app_id" >/dev/null
flatpak info --user org.kde.Sdk//6.11 >/dev/null
if [ ! -f "$stage_root/app/metadata" ]; then
    flatpak info --show-commit "$app_id" > "$stage_root/base-commit"
    flatpak build-init --base="$app_id" --base-version=master "$stage_root/app" "$app_id" org.kde.Sdk org.kde.Platform 6.11
fi
if [ ! -d "$keychain_root/.git" ]; then
    git clone https://github.com/frankosterfeld/qtkeychain.git "$keychain_root"
fi
git -C "$keychain_root" checkout --detach e63da2868465db18eb35a312b2635c26fdc46923
flatpak build --filesystem="$source_root" --filesystem="$stage_root" "$stage_root/app" sh -c '
    set -eu
    cmake -S "$2/qtkeychain-source" -B "$2/keychain-bridge" -G Ninja -DCMAKE_INSTALL_PREFIX=/app \
        -DBUILD_WITH_QT6=ON -DBUILD_TRANSLATIONS=OFF -DBUILD_TEST_APPLICATION=OFF -DLIBSECRET_SUPPORT=OFF
    cmake --build "$2/keychain-bridge" -j4
    cmake --install "$2/keychain-bridge"
    cmake -S "$1" -B "$2/obj" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/app \
        -DCMAKE_PREFIX_PATH=/app -DUSE_SYSTEM_BIGSCREEN_UI=OFF -DBUILD_TESTING=ON
    cmake --build "$2/obj" -j4
    ctest --test-dir "$2/obj" --output-on-failure
    cmake --install "$2/obj"
' sh "$source_root" "$stage_root"
# CMake installs the native icon name; Flatpak exports only application-ID icons.
python3 - "$stage_root/app/files" <<'PYICON'
from pathlib import Path
import shutil
import sys
root = Path(sys.argv[1])
app_id = 'io.github.jclsn.Flimmer'
for icon in (root / 'share/icons/hicolor').glob('*/apps/flimmer.*'):
    shutil.copy2(icon, icon.with_name(app_id + icon.suffix))
desktop = root / 'share/applications' / (app_id + '.desktop')
desktop.write_text(desktop.read_text().replace('Icon=flimmer\n', 'Icon=' + app_id + '\n'))
PYICON
# Finalize a fresh export tree so incremental builds refresh launchers and icons.
export_root=$(mktemp -d "$stage_root/export-XXXXXX")
flatpak build-init "$export_root" "$app_id" org.kde.Sdk org.kde.Platform 6.11
cp -a "$stage_root/app/files/." "$export_root/files/"
flatpak build-finish "$export_root" --command=flimmer \
    --own-name=io.github.jclsn.Flimmer.Control --share=ipc --share=network \
    --socket=wayland --socket=fallback-x11 --socket=pulseaudio --device=dri \
    --talk-name=org.freedesktop.ScreenSaver --talk-name=org.kde.kwalletd6 --talk-name=org.kde.kwalletd5 \
    --filesystem=xdg-config/kdeglobals:ro --filesystem=xdg-data/color-schemes:ro --filesystem=xdg-videos
flatpak build-export "$stage_root/repo" "$export_root"
flatpak build-bundle "$stage_root/repo" "$stage_root/Flimmer-bridge.flatpak" "$app_id" master
printf '%s\n' "Built: $stage_root/Flimmer-bridge.flatpak" "Rollback commit: $stage_root/base-commit"
if [ "${1:-}" = --install ]; then
    flatpak install --user --reinstall -y --noninteractive "$stage_root/Flimmer-bridge.flatpak"
fi
