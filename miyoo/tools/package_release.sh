#!/bin/sh
# Prepares a clean, 100% self-contained OnionOS release zip for Quake II & Mission Packs on Miyoo Mini Plus.
# Usage:
#   miyoo/tools/package_release.sh [version_tag] [build_dir]
set -e

REPO=$(cd "$(dirname "$0")/../.." && (pwd -W 2>/dev/null || pwd))
TAG="${1:-v1.0.0}"
BUILD_DIR="${2:-$REPO/build/miyoo_pgo}"
DIST_DIR="$REPO/dist"
STAGE_DIR="$REPO/build/release_stage"

for b in quake2 ref_soft.so baseq2/game.so xatrix/game.so rogue/game.so; do
	if [ ! -f "$BUILD_DIR/$b" ]; then
		echo "Error: Required binary $BUILD_DIR/$b not found!" >&2
		echo "Please build the project first: PGO=use miyoo/build/build_miyoo.sh build/miyoo_pgo" >&2
		exit 1
	fi
done

echo "=== Cleaning previous staging and preparing directories..."
rm -rf "$STAGE_DIR"
mkdir -p "$DIST_DIR"
mkdir -p "$STAGE_DIR/Roms/PORTS/Games/Quake2/baseq2"
mkdir -p "$STAGE_DIR/Roms/PORTS/Games/Quake2/xatrix"
mkdir -p "$STAGE_DIR/Roms/PORTS/Games/Quake2/rogue"
mkdir -p "$STAGE_DIR/Roms/PORTS/Shortcuts/FPS"
mkdir -p "$STAGE_DIR/Roms/PORTS/Imgs"

echo "=== Staging Quake II binaries and configs..."
cp "$BUILD_DIR/quake2" "$STAGE_DIR/Roms/PORTS/Games/Quake2/quake2"
cp "$BUILD_DIR/ref_soft.so" "$STAGE_DIR/Roms/PORTS/Games/Quake2/ref_soft.so"
cp "$BUILD_DIR/baseq2/game.so" "$STAGE_DIR/Roms/PORTS/Games/Quake2/baseq2/game.so"
cp "$BUILD_DIR/xatrix/game.so" "$STAGE_DIR/Roms/PORTS/Games/Quake2/xatrix/game.so"
cp "$BUILD_DIR/rogue/game.so" "$STAGE_DIR/Roms/PORTS/Games/Quake2/rogue/game.so"

cp "$REPO/miyoo/card/Roms/PORTS/Games/Quake2/launch.sh" "$STAGE_DIR/Roms/PORTS/Games/Quake2/launch.sh"
cp "$REPO/miyoo/card/Roms/PORTS/Games/Quake2/baseq2/yq2.cfg" "$STAGE_DIR/Roms/PORTS/Games/Quake2/baseq2/yq2.cfg"
cp "$REPO/miyoo/card/Roms/PORTS/Games/Quake2/xatrix/yq2.cfg" "$STAGE_DIR/Roms/PORTS/Games/Quake2/xatrix/yq2.cfg"
cp "$REPO/miyoo/card/Roms/PORTS/Games/Quake2/rogue/yq2.cfg" "$STAGE_DIR/Roms/PORTS/Games/Quake2/rogue/yq2.cfg"

chmod +x "$STAGE_DIR/Roms/PORTS/Games/Quake2/quake2" "$STAGE_DIR/Roms/PORTS/Games/Quake2/launch.sh"

echo "=== Staging Shortcuts and Box Art Images..."
cp "$REPO/miyoo/card/Roms/PORTS/Shortcuts/FPS/"* "$STAGE_DIR/Roms/PORTS/Shortcuts/FPS/"
cp "$REPO/miyoo/card/Roms/PORTS/Imgs/"* "$STAGE_DIR/Roms/PORTS/Imgs/"

cat << 'EOF' > "$STAGE_DIR/Roms/PORTS/Games/Quake2/README.txt"
Quake II for Miyoo Mini Plus (OnionOS)
======================================
Port based on Yamagi Quake II with software renderer tuned for the Miyoo Mini Plus.

Installation:
-------------
1. Extract this archive directly to the root of your SD card (/mnt/SDCARD/).
2. Copy your game data files (.pak) from your PC/GOG/Steam copy:
   - Base Quake II:
     Copy pak0.pak (and pak1.pak, pak2.pak) to:
     Roms/PORTS/Games/Quake2/baseq2/
   - Mission Pack 1: The Reckoning:
     Copy pak0.pak to:
     Roms/PORTS/Games/Quake2/xatrix/
   - Mission Pack 2: Ground Zero:
     Copy pak0.pak to:
     Roms/PORTS/Games/Quake2/rogue/
3. Optional CD Soundtrack (OGG music):
   Copy your 'music' folder (Track02.ogg ...) into:
   Roms/PORTS/Games/Quake2/music/ (or baseq2/music/)
4. Launch the games from the OnionOS Games -> Ports list:
   - Quake II
   - Quake II - The Reckoning
   - Quake II - Ground Zero

Controls:
---------
D-pad          : Move forward / back, Turn left / right
Y              : Fire
B              : Jump (Swim up)
X              : Crouch (Swim down)
A              : Next weapon
L1 / R1        : Strafe left / right
L2 / R2        : Look up / down
L2 + R2        : Center view (level horizon)
START / MENU   : Menu (Pause)

With SELECT held:
D-pad Left/Right : Previous / next inventory item
D-pad Up         : Show / hide inventory
D-pad Down       : Use item
A                : Previous weapon
B                : Help computer (Objectives)
X                : Toggle Run / Walk
EOF

ZIP_NAME="Quake2-Miyoo-${TAG}.zip"
echo "=== Creating $DIST_DIR/$ZIP_NAME..."
rm -f "$DIST_DIR/$ZIP_NAME"

(cd "$STAGE_DIR" && py -3 -m zipfile -c "$DIST_DIR/$ZIP_NAME" Roms)

SYM_ZIP="Quake2-Symbols-${TAG}.zip"
echo "=== Packaging unstripped debug symbols $DIST_DIR/$SYM_ZIP..."
rm -f "$DIST_DIR/$SYM_ZIP"
(cd "$BUILD_DIR" && py -3 -c "import zipfile, os; z = zipfile.ZipFile('$DIST_DIR/$SYM_ZIP', 'w', zipfile.ZIP_DEFLATED); [z.write(f) for f in ['quake2.unstripped', 'ref_soft.so.unstripped', 'baseq2/game.so.unstripped', 'xatrix/game.so.unstripped', 'rogue/game.so.unstripped'] if os.path.exists(f)]; z.close()")

echo "=== Release package created successfully:"
ls -lh "$DIST_DIR/$ZIP_NAME"
ls -lh "$DIST_DIR/$SYM_ZIP"
