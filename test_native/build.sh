#!/usr/bin/env bash
# Builds the PC check (parsers + face renders) with PlatformIO's MinGW compiler.
# Usage: test_native/build.sh <lvgl dir> <ArduinoJson src dir> <build dir>
set -e
LVGL="$1"; AJ="$2"; OUT="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"; FW="$HERE/.."
CXX="${CXX:-$HOME/.platformio/packages/toolchain-gccmingw32/bin/g++}"
CC="${CC:-$HOME/.platformio/packages/toolchain-gccmingw32/bin/gcc}"
export PATH="$(dirname "$CXX"):$PATH"   # MinGW fails silently without its own DLLs on PATH
mkdir -p "$OUT/obj"
INC=(-I"$HERE/shim" -I"$FW/include" -I"$FW/src" -I"$LVGL" -I"$LVGL/.." -I"$AJ")
DEF=(-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -D_POSIX_THREAD_SAFE_FUNCTIONS -O1)

# LVGL + fonts (C), compiled once
find "$LVGL/src" -name '*.c' > "$OUT/csrc.txt"
ls "$FW"/src/fonts/*.c >> "$OUT/csrc.txt"
while read -r f; do
  o="$OUT/obj/$(echo "$f" | md5sum | cut -c1-12).o"
  [ -f "$o" ] || echo "$CC -c ${INC[*]} ${DEF[*]} \"$f\" -o \"$o\""
done < "$OUT/csrc.txt" | xargs -P 8 -I{} sh -c "{}"

# object list goes in a response file (too many paths for one Windows command line)
(cd "$OUT/obj" && ls *.o) | sed "s|^|$OUT/obj/|" > "$OUT/objs.rsp"
"$CXX" -std=gnu++14 "${INC[@]}" "${DEF[@]}" \
  "$HERE/native_main.cpp" "$FW/src/parse.cpp" "$FW/src/ui.cpp" @"$OUT/objs.rsp" -o "$OUT/jm01_native.exe"
echo "built $OUT/jm01_native.exe"
