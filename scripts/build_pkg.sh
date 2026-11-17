#!/bin/zsh
set -e

cd "$(dirname "$0")/.."

BUILD_DIR="build"
PKGROOT="$BUILD_DIR/pkgroot"
DIST_DIR="dist"
COMPONENT_PKG="$BUILD_DIR/CME-UF5-Component.pkg"
FINAL_PKG="$DIST_DIR/CME-UF5-macOS-Installer.pkg"

rm -rf "$PKGROOT"
mkdir -p "$PKGROOT/Library/Audio/MIDI Drivers"
mkdir -p "$PKGROOT/Library/Audio/MIDI Device Scripts/CME"
mkdir -p "$PKGROOT/Library/Audio/MIDI Device Scripts/Central Music Co"
mkdir -p "$PKGROOT/Applications"
mkdir -p "$PKGROOT/usr/local/bin"
mkdir -p "$DIST_DIR"

# 1. CoreMIDI Driver Plug-in
cp -R "$BUILD_DIR/CMEUFMIDIDriver.plugin" "$PKGROOT/Library/Audio/MIDI Drivers/"

# 2. Native macOS Settings & Control Center Application
cp -R "$BUILD_DIR/CME UF5 Control.app" "$PKGROOT/Applications/"

# 3. Logic Pro Control Surface Profiles
cp -R "logic/UF5.device" "$PKGROOT/Library/Audio/MIDI Device Scripts/CME/UF5.device"
cp -R "logic/UF5.device" "$PKGROOT/Library/Audio/MIDI Device Scripts/CME/CME UF5.device"
cp -R "logic/UF5.device" "$PKGROOT/Library/Audio/MIDI Device Scripts/Central Music Co/CME UF MIDI Device.device"

# 4. Companion CLI Tool
cp "$BUILD_DIR/cme-uf5-ctl" "$PKGROOT/usr/local/bin/cme-uf5-ctl"
chmod 755 "$PKGROOT/usr/local/bin/cme-uf5-ctl"

chmod +x installer/scripts/postinstall

pkgbuild \
    --root "$PKGROOT" \
    --identifier "com.cme-pro.uf5.suite" \
    --version "2.0.0" \
    --scripts "installer/scripts" \
    --install-location "/" \
    "$COMPONENT_PKG"

productbuild \
    --distribution "installer/Distribution.xml" \
    --resources "installer" \
    --package-path "$BUILD_DIR" \
    "$FINAL_PKG"

echo "Built macOS Installer Package -> $FINAL_PKG"

