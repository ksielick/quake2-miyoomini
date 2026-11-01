#!/bin/sh
# Build Yamagi Quake II for the Miyoo Mini Plus (Cortex-A7, OnionOS, glibc 2.28) with the
# Windows-hosted Arm GNU toolchain, without make. Run from Git Bash:
#   miyoo/build/build_miyoo.sh [out_dir]
# Default out_dir: <repo>/build/miyoo (git-ignored). Objects go to <out_dir>/obj.
# Output: <out_dir>/quake2, ref_soft.so, baseq2/game.so (as Yamagi's Makefile lays them out).
# Only .c timestamps are checked: after a header change run with FORCE=1.
#
# A dynamic build, by tyrquake-miyoo's recipe (miyoo/standalone/build_tqrun.sh): Yamagi loads
# the renderer and the game with dlopen, and the SoC's 2D engine (MI_GFX) needs dlopen of the
# vendor libraries, so a static binary won't do.
#   - start.S + -nostartfiles: the toolchain's crt1.o needs __libc_start_main@GLIBC_2.34;
#   - glibc_compat.h (force-included) pins libm and pthread symbols to GLIBC_2.4, and
#     glibc_compat.c gives stat() as __xstat (-Wl,--wrap=stat);
#   - every binary is checked for symbols newer than glibc 2.28 at the end.
# SDL2 is not linked: the client is compiled against the real SDL2 headers and
# miyoo/platform/ implements the SDL functions it calls (after redriver2-miyoo's
# src_rebuild/platform/Miyoo/miyoo_sdl.cpp). The executable exports them (-rdynamic) to
# ref_soft.so.
# TC: the toolchain's bin directory. SDL2_INC: the SDL2 headers.
# Optimisation (tyrquake-miyoo's build_core.sh and pgo.sh; see miyoo/tools/pgo.sh):
#   OPT: the optimisation flags (default -O2);
#   LTO=1: link-time optimisation for ref_soft.so (where the frame's time goes);
#   PGO=gen: an instrumented build writing its profile to /tmp/pgo on the device;
#   PGO=use: a build with the profile in miyoo/pgo/<target>/*.gcda.
#   -dumpdir miyoo/pgo/<target>/ (relative, per target: the three link some of the same
#   sources) names each object's profile, so a profile made in one out_dir fits another.
set -e
REPO=$(cd "$(dirname "$0")/../.." && (pwd -W 2>/dev/null || pwd))	# (G:/... for the Windows compiler)
OUT=${1:-$REPO/build/miyoo}
TC=${TC:-G:/Emulacja/redriver2-miyoo/deps/arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-linux-gnueabihf/bin}
SDL2_INC=${SDL2_INC:-G:/Emulacja/redriver2-miyoo/deps/SDL2-2.30.9/include}
[ -x "$TC/arm-none-linux-gnueabihf-gcc.exe" ] || [ -x "$TC/arm-none-linux-gnueabihf-gcc" ] ||
	{ echo "no Arm toolchain in $TC: set TC" >&2; exit 1; }
CC="$TC/arm-none-linux-gnueabihf-gcc"
JOBS=${JOBS:-8}
cd "$REPO"

# --- sources (Yamagi's Makefile, Linux, SDL2, no cURL, no OpenAL, bundled miniz)
CLIENT="src/backends/generic/misc.c
src/client/cl_cin.c src/client/cl_image.c src/client/cl_console.c src/client/cl_download.c
src/client/cl_effects.c src/client/cl_entities.c src/client/cl_input.c src/client/cl_inventory.c
src/client/cl_keyboard.c src/client/cl_lights.c src/client/cl_main.c src/client/cl_network.c
src/client/cl_parse.c src/client/cl_particles.c src/client/cl_prediction.c src/client/cl_screen.c
src/client/cl_tempentities.c src/client/cl_view.c src/client/curl/download.c src/client/curl/qcurl.c
src/client/input/gyro.c src/client/menu/menu.c src/client/menu/qmenu.c src/client/menu/videomenu.c
src/client/sound/ogg.c src/client/sound/openal.c src/client/sound/qal.c src/client/sound/sdl.c
src/client/sound/sound.c src/client/sound/wave.c src/client/vid/vid.c
src/common/argproc.c src/common/clientserver.c src/common/collision.c src/common/crc.c
src/common/cmdparser.c src/common/cvar.c src/common/filesystem.c src/common/glob.c src/common/md4.c
src/common/movemsg.c src/common/frame.c src/common/netchan.c src/common/pmove.c src/common/szone.c
src/common/zone.c src/common/shared/flash.c src/common/shared/rand.c src/common/shared/shared.c
src/server/sv_cmd.c src/server/sv_conless.c src/server/sv_entities.c src/server/sv_game.c
src/server/sv_init.c src/server/sv_main.c src/server/sv_save.c src/server/sv_send.c
src/server/sv_user.c src/server/sv_world.c
src/common/unzip/ioapi.c src/common/unzip/unzip.c src/common/unzip/miniz/miniz.c
src/common/unzip/miniz/miniz_tdef.c src/common/unzip/miniz/miniz_tinfl.c
src/client/input/sdl2.c src/client/vid/glimp_sdl2.c
src/backends/unix/main.c src/backends/unix/network.c src/backends/unix/signalhandler.c
src/backends/unix/system.c src/backends/unix/shared/hunk.c
miyoo/build/glibc_compat.c $(ls miyoo/platform/*.c 2>/dev/null | tr '\n' ' ')"
REFSOFT="$(ls src/client/refresh/soft/*.c | tr '\n' ' ')
src/client/refresh/files/surf.c src/client/refresh/files/common.c src/client/refresh/files/models.c
src/client/refresh/files/pcx.c src/client/refresh/files/stb.c src/client/refresh/files/wal.c
src/client/refresh/files/pvs.c src/common/shared/shared.c src/common/md4.c src/backends/unix/shared/hunk.c
miyoo/build/glibc_compat.c"
GAME="src/common/shared/flash.c src/common/shared/rand.c src/common/shared/shared.c
$(find src/game -name '*.c' | sort | tr '\n' ' ') miyoo/build/glibc_compat.c"

# --- flags (the Makefile's, for ARMv7 hard-float; -fsigned-char: the code is tested on x86)
OPT=${OPT:--O2}
case "$PGO" in
gen) PGOF="-fprofile-generate=/tmp/pgo -fprofile-update=atomic" ;;
use) PGOF="-fprofile-use -fprofile-correction -Wno-missing-profile" ;;
"") PGOF="" ;;
*) echo "PGO=gen or PGO=use" >&2; exit 1 ;;
esac
LTOF=""
LTOWRAP=""
# LTO loses the force-included .symver pins: glibc_compat.c (no LTO) binds them and the
# LTO units reach it through --wrap (tyrquake-miyoo's GLIBC_COMPAT_WRAP)
[ -n "$LTO" ] && LTOF="-flto -DGLIBC_COMPAT_WRAP" &&
	LTOWRAP="-Wl,--wrap=exp,--wrap=log,--wrap=pow,--wrap=exp2,--wrap=log2,--wrap=fmod,--wrap=pthread_create,--wrap=pthread_join"
CFLAGS="$OPT $PGOF -Wall -fomit-frame-pointer -fno-strict-aliasing -fwrapv -fvisibility=hidden
-ffp-contract=off -fsigned-char -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
-Wno-unused-result -Wno-format-truncation
-DYQ2OSTYPE=\\\"Linux\\\" -DYQ2ARCH=\\\"arm\\\" -DNOUNCRYPT -DMIYOO_MINI
-include miyoo/build/glibc_compat.h -I$OUT/include"

mkdir -p "$OUT/obj" "$OUT/baseq2"
# Yamagi includes <SDL2/SDL.h>: the headers as include/SDL2/
[ -f "$OUT/include/SDL2/SDL.h" ] || { mkdir -p "$OUT/include/SDL2"; cp "$SDL2_INC"/*.h "$OUT/include/SDL2/"; }
echo $CFLAGS > "$OUT/cflags.new"
echo "LTO=$LTO" >> "$OUT/cflags.new"	# (the stamp: any change rebuilds all)
cmp -s "$OUT/cflags.new" "$OUT/cflags.stamp" || { cp "$OUT/cflags.new" "$OUT/cflags.stamp"; FORCE=1; }
echo $CFLAGS > "$OUT/cflags.rsp"
rm -f "$OUT/cflags.new"
export CC RSP="$OUT/cflags.rsp"

# compile <target> <extra flags> <sources...>: objects in obj/<target>/
compile() {
	t=$1; x=$2; shift 2
	mkdir -p "$OUT/obj/$t"
	for s in "$@"; do
		o="$OUT/obj/$t/$(echo "$s" | tr '/' '_' | sed 's/\.c$/.o/')"
		if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ -n "$FORCE" ]; then
			xs=$x
			# glibc_compat.c's .symver pins must not go through LTO (they'd be lost)
			[ "$s" = miyoo/build/glibc_compat.c ] && xs="$xs -fno-lto"
			echo "$s $o -dumpdir miyoo/pgo/$t/ -frandom-seed=$s${xs:+ $xs}"	# (no trailing blank: xargs -L would join the next line)
		fi
	done | xargs -r -P "$JOBS" -L 1 sh -c 'echo "  CC $0"; "$CC" @"$RSP" $2 $3 $4 $5 $6 $7 $8 $9 -c "$0" -o "$1"'
}
compile client "" $CLIENT
compile ref_soft "-fPIC${LTOF:+ $LTOF}" $REFSOFT	# (no trailing blank, see above)
compile game "-fPIC" $GAME

CPUF="-marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard"
LIBS="-Wl,--wrap=stat -lm -Wl,--no-as-needed -l:libdl.so.2 -l:libpthread.so.0 -l:librt.so.1"
echo "  LD quake2"
"$CC" -no-pie -nostartfiles -rdynamic -static-libgcc $PGOF -o "$OUT/quake2" miyoo/build/start.S \
	$CPUF "$OUT"/obj/client/*.o $LIBS
echo "  LD ref_soft.so"
# (with LTO the code is generated here: the compile flags again)
"$CC" -shared -static-libgcc $PGOF ${LTOF:+$LTOF $OPT $CPUF -fPIC -fomit-frame-pointer -fno-strict-aliasing -fwrapv -ffp-contract=off -fsigned-char -fvisibility=hidden $LTOWRAP} \
	-o "$OUT/ref_soft.so" "$OUT"/obj/ref_soft/*.o -Wl,--wrap=stat -lm -l:libpthread.so.0	# (sw_mt.c)
echo "  LD baseq2/game.so"
"$CC" -shared -static-libgcc $PGOF -o "$OUT/baseq2/game.so" "$OUT"/obj/game/*.o -Wl,--wrap=stat -lm

# the Miyoo has glibc 2.28: no symbol may need a newer version
bad=0
for b in quake2 ref_soft.so baseq2/game.so; do
	cp "$OUT/$b" "$OUT/$b.unstripped"	# (for addr2line on crash addresses)
	"$TC/arm-none-linux-gnueabihf-strip" "$OUT/$b"
	NEW=$("$TC/arm-none-linux-gnueabihf-objdump" -T "$OUT/$b" | grep -o 'GLIBC_2\.[0-9]*' | sort -u -t. -k2,2n |
		awk -F. '$2 > 28')
	if [ -n "$NEW" ]; then
		echo "ERROR: $b needs newer glibc than the Miyoo's 2.28:"
		"$TC/arm-none-linux-gnueabihf-objdump" -T "$OUT/$b" | grep -E "$(echo $NEW | tr ' ' '|')"
		bad=1
	fi
done
# the modules' symbols outside glibc must come from the executable (the device's dynamic linker
# would stop at the first missing one: "symbol lookup error")
"$TC/arm-none-linux-gnueabihf-objdump" -T "$OUT/quake2" | awk '$2 != "*UND*" && $4 != "*UND*" {print $NF}' | sort -u > "$OUT/exports.txt"
for b in ref_soft.so baseq2/game.so; do
	MISSING=$("$TC/arm-none-linux-gnueabihf-objdump" -T "$OUT/$b" | awk '/\*UND\*/ && !/GLIBC/ && $NF != "" {print $NF}' |
		grep -v '^_\(_gmon_start__\|ITM_\|_cxa_finalize\)' | sort -u | comm -23 - "$OUT/exports.txt")
	if [ -n "$MISSING" ]; then
		echo "ERROR: $b needs symbols quake2 doesn't export:" $MISSING
		bad=1
	fi
done
ls -la "$OUT/quake2" "$OUT/ref_soft.so" "$OUT/baseq2/game.so"
exit $bad
